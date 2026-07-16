#!/usr/bin/env python3
"""Generate deterministic Solar Remote host artifacts from a linked ELF."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import cbor2
from elftools.elf.elffile import ELFFile

KINDS = {1: "schema", 2: "field", 3: "data", 4: "action", 5: "topic", 6: "stream", 7: "link"}
COLLECTIONS = {3: "data", 5: "topics", 6: "streams", 7: "links"}
CODECS = {1: "cbor", 2: "packed"}
SHAPES = {0: "object", 1: "status-code"}
SCALARS = {1: "bool", 2: "unsigned", 3: "signed", 4: "float32", 5: "float64", 6: "enum", 7: "text", 8: "bytes"}


def read_image(path: Path) -> bytes:
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        section = elf.get_section_by_name(".solar.remote.manifest")
        if section is None:
            section = elf.get_section_by_name("solar_remote_manifest_area")
        if section is None:
            raise ValueError("ELF has no Solar Remote manifest section")
        data = section.data()
    start = data.find(b"SLRM")
    if start < 0 or len(data) < start + 16:
        raise ValueError("manifest section has no valid image")
    total = struct.unpack_from("<I", data, start + 12)[0]
    image = data[start:start + total]
    if len(image) != total:
        raise ValueError("truncated manifest image")
    return image


def parse_image(data: bytes) -> dict:
    if data[:4] != b"SLRM":
        raise ValueError("invalid manifest magic")
    format_version, major, minor, count, reserved, total = struct.unpack_from("<HBBHHI", data, 4)
    if format_version != 1 or reserved or total != len(data):
        raise ValueError("unsupported manifest header")
    result = {"format": format_version, "protocol": [major, minor], "schemas": [], "data": [],
              "actions": [], "topics": [], "streams": [], "links": []}
    schemas: dict[int, dict] = {}
    offset = 16
    parsed = 0
    while parsed < count:
        kind = data[offset]
        if kind == 2:
            _, scalar, field_id, schema_id, width, required, field_reserved = struct.unpack_from(
                "<BBHIHBB", data, offset)
            if field_reserved or scalar not in SCALARS or schema_id not in schemas:
                raise ValueError("malformed field record")
            schemas[schema_id]["fields"].append({"id": field_id, "kind": SCALARS[scalar],
                                                  "width": width, "required": bool(required)})
            offset += 12
        elif kind == 1:
            _, codec_and_shape, version, stable_id, maximum, name_len, description_len = struct.unpack_from(
                "<BBHIIHH", data, offset)
            codec = codec_and_shape & 0x0F
            shape = codec_and_shape >> 4
            offset += 16
            name = data[offset:offset + name_len].decode("utf-8")
            offset += name_len
            description = data[offset:offset + description_len].decode("utf-8")
            offset += description_len
            record = {"id": stable_id, "name": name, "description": description,
                      "version": version, "codec": CODECS[codec], "shape": SHAPES[shape],
                      "max_encoded_size": maximum,
                      "fields": []}
            result["schemas"].append(record)
            schemas[stable_id] = record
        elif kind == 4:
            (_, access, version, stable_id, request, response, error, name_len,
             description_len) = struct.unpack_from("<BBHIIIIHH", data, offset)
            offset += 24
            name = data[offset:offset + name_len].decode("utf-8"); offset += name_len
            description = data[offset:offset + description_len].decode("utf-8"); offset += description_len
            result["actions"].append({"id": stable_id, "name": name, "description": description,
                                      "version": version, "request_schema": request,
                                      "response_schema": response, "error_schema": error,
                                      "access_mask": access})
        elif kind in (3, 5, 6, 7):
            _, flags, version, stable_id, schema, name_len, description_len = struct.unpack_from(
                "<BBHIIHH", data, offset)
            offset += 16
            name = data[offset:offset + name_len].decode("utf-8"); offset += name_len
            description = data[offset:offset + description_len].decode("utf-8"); offset += description_len
            record = {"id": stable_id, "name": name, "description": description, "version": version}
            if kind != 7:
                record["schema"] = schema
            if kind == 3:
                record["capability_mask"] = flags
            elif kind in (5, 6):
                record["codec"] = CODECS[flags]
            result[COLLECTIONS[kind]].append(record)
        else:
            raise ValueError(f"unknown manifest record kind {kind}")
        parsed += 1
    if offset != len(data):
        raise ValueError("manifest has trailing bytes")
    for collection in ("schemas", "data", "actions", "topics", "streams", "links"):
        result[collection].sort(key=lambda item: item["id"])
    return result


def generate(elf: Path, output: Path) -> None:
    manifest = parse_image(read_image(elf))
    canonical = cbor2.dumps(manifest, canonical=True)
    digest = hashlib.sha256(canonical).hexdigest()
    output.mkdir(parents=True, exist_ok=True)
    (output / "manifest.cbor").write_bytes(canonical)
    review = {**manifest, "schema_sha256": digest}
    (output / "manifest.json").write_text(json.dumps(review, indent=2, sort_keys=True) + "\n")
    (output / "manifest.sha256").write_text(digest + "\n")
    python = ["# Generated by Solar Remote; do not edit.", f"PROTOCOL = {tuple(manifest['protocol'])!r}",
              f"SCHEMA_SHA256 = bytes.fromhex({digest!r})"]
    for collection, prefix in (("schemas", "SCHEMA"), ("data", "DATA"), ("actions", "ACTION"),
                               ("topics", "TOPIC"), ("streams", "STREAM"), ("links", "LINK")):
        for item in manifest[collection]:
            name = "".join(character if character.isalnum() else "_" for character in item["name"]).upper()
            python.append(f"{prefix}_{name} = 0x{item['id']:08X}")
    (output / "constants.py").write_text("\n".join(python) + "\n")
    client = ["# Generated by Solar Remote; do not edit.",
              "from solar_remote import Client", "", "class FirmwareClient(Client):",
              f"    protocol = {tuple(manifest['protocol'])!r}",
              f"    schema_sha256 = bytes.fromhex({digest!r})"]
    for collection, prefix in (("data", "DATA"), ("actions", "ACTION"),
                               ("topics", "TOPIC"), ("streams", "STREAM")):
        for item in manifest[collection]:
            name = "".join(character if character.isalnum() else "_" for character in item["name"]).upper()
            client.append(f"    {prefix}_{name} = 0x{item['id']:08X}")
    (output / "client.py").write_text("\n".join(client) + "\n")
    cpp = ["#pragma once", "", "#include <array>", "#include <cstdint>", "",
           "namespace solar::remote::generated {",
           f"inline constexpr std::uint8_t protocol_major = {manifest['protocol'][0]};",
           f"inline constexpr std::uint8_t protocol_minor = {manifest['protocol'][1]};",
           "inline constexpr std::array<std::uint8_t, 32> schema_sha256{" +
           ", ".join(f"0x{digest[index:index + 2]}" for index in range(0, 64, 2)) + "};"]
    cpp.append("} // namespace solar::remote::generated\n")
    (output / "manifest.hpp").write_text("\n".join(cpp))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        generate(args.elf, args.output)
    except (OSError, ValueError, KeyError, struct.error) as error:
        print(f"solar-remote-generate: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
