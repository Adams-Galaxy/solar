#!/usr/bin/env python3
"""Create application-client shipment artifacts from a linked Solar ELF."""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import zipfile

from .compiler import generate_python_package
from .versions import SHIPMENT_FORMAT


def _catalog(items: list[dict]) -> dict[str, dict]:
    return {item["name"]: item for item in items}


def _wire_shape(collection: str, item: dict) -> dict:
    """Select and normalize every authored wire-relevant property."""
    ignored = {"cpp_name", "request", "response", "source"}
    result = {key: value for key, value in item.items() if key not in ignored}
    if collection == "schemas":
        result["fields"] = sorted(result.get("fields", []), key=lambda value: value["id"])
        result["values"] = sorted(
            result.get("values", []), key=lambda value: (value["value"], value["name"])
        )
    return result


def verify_contract(ir: dict, actual: dict) -> list[str]:
    differences: list[str] = []
    singular = {"schemas": "schema", "data": "data", "actions": "action", "streams": "stream"}
    for collection in ("schemas", "data", "actions", "streams"):
        expected = _catalog(ir["manifest"].get(collection, []))
        linked = _catalog(actual.get(collection, []))
        for name, declaration in expected.items():
            candidate = linked.get(name)
            if candidate is None:
                differences.append(f"linked ELF omits {singular[collection]} {name!r}")
                continue
            expected_shape = _wire_shape(collection, declaration)
            linked_shape = _wire_shape(collection, candidate)
            for key, value in expected_shape.items():
                if linked_shape.get(key) != value:
                    differences.append(f"{singular[collection]} {name!r} changed {key}")
        intrinsic_schemas = {"solar.Empty": 1, "solar.Status": 2, "solar.Error": 3}
        unexpected_names = set(linked) - set(expected)
        if collection == "schemas":
            unexpected_names = {
                name for name in unexpected_names
                if intrinsic_schemas.get(name) != int(linked[name].get("id", -1))
            }
        for name in sorted(unexpected_names):
            differences.append(f"linked ELF contains unexpected {singular[collection]} {name!r}")
    def capability_catalog(values: list[dict]) -> dict[tuple[str, int, str], dict]:
        return {
            (item["domain"], int(item["endpoint"]), item["kind"]): item
            for item in values
        }

    expected_capabilities = capability_catalog(ir["manifest"].get("capabilities", []))
    actual_capabilities = capability_catalog(actual.get("capabilities", []))
    for missing in sorted(set(expected_capabilities) - set(actual_capabilities)):
        differences.append(f"linked ELF omits capability {missing}")
    for unexpected in sorted(set(actual_capabilities) - set(expected_capabilities)):
        differences.append(f"linked ELF contains unexpected capability {unexpected}")
    for key in sorted(set(expected_capabilities) & set(actual_capabilities)):
        if expected_capabilities[key] != actual_capabilities[key]:
            differences.append(f"linked ELF capability {key} changed policy")
    return differences


def build_wheel(ir: dict, output: Path) -> Path:
    """Build the tiny pure-Python generated wheel without host build tooling."""
    distribution = ir["application"]["name"].replace("-", "_") + "_solar_client"
    package = ir["application"]["python_package"]
    version = "0.1.0"
    wheel = output / "wheel" / f"{distribution}-{version}-py3-none-any.whl"
    wheel.parent.mkdir(exist_ok=True)
    dist_info = f"{distribution}-{version}.dist-info"
    entries: dict[str, bytes] = {}
    for source in sorted((output / "python" / package).iterdir()):
        if not source.is_file() or source.suffix not in (".py", ".typed"):
            continue
        entries[f"{package}/{source.name}"] = source.read_bytes()
    entries[f"{dist_info}/METADATA"] = (
        "Metadata-Version: 2.3\n"
        f"Name: {ir['application']['name']}-solar-client\n"
        f"Version: {version}\n"
        "Requires-Python: >=3.11\n"
        "Requires-Dist: solar-remote>=0.1,<0.2\n\n"
    ).encode()
    entries[f"{dist_info}/WHEEL"] = (
        "Wheel-Version: 1.0\nGenerator: solar-codegen\n"
        "Root-Is-Purelib: true\nTag: py3-none-any\n"
    ).encode()
    rows: list[list[str]] = []
    for name, content in entries.items():
        digest = (
            base64.urlsafe_b64encode(hashlib.sha256(content).digest())
            .rstrip(b"=")
            .decode()
        )
        rows.append([name, f"sha256={digest}", str(len(content))])
    record_name = f"{dist_info}/RECORD"
    rows.append([record_name, "", ""])
    record = io.StringIO(newline="")
    csv.writer(record, lineterminator="\n").writerows(rows)
    entries[record_name] = record.getvalue().encode()
    with zipfile.ZipFile(wheel, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, content in sorted(entries.items()):
            # ZIP otherwise records wall-clock time, making identical shipments
            # differ byte-for-byte. 1980 is the earliest representable ZIP date.
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, content)
    return wheel


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--manifest-json", type=Path, required=True)
    parser.add_argument("--manifest-bin", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)

    ir = json.loads(args.ir.read_text())
    actual = json.loads(args.manifest_json.read_text())
    differences = verify_contract(ir, actual)
    if differences:
        for difference in differences:
            print(f"shipment verification failed: {difference}", file=sys.stderr)
        return 2

    image = args.manifest_bin.read_bytes()
    interface_digest = hashlib.sha256(image).hexdigest()
    build_digest = hashlib.sha256(args.elf.read_bytes()).digest()
    build_id = int.from_bytes(build_digest[:8], "little")
    effective_ir = dict(ir)
    effective_ir["manifest"] = actual

    args.output.mkdir(parents=True, exist_ok=True)
    generate_python_package(effective_ir, args.output, interface_digest, build_id)
    shutil.copy2(args.manifest_json, args.output / "manifest.json")
    shutil.copy2(args.manifest_bin, args.output / "manifest.bin")
    (args.output / "manifest.sha256").write_text(interface_digest + "\n")
    (args.output / "compatibility.json").write_text(
        json.dumps({"compatible": True, "differences": []}, indent=2) + "\n"
    )
    shipment_basis = {
        "format": SHIPMENT_FORMAT,
        "application": ir["application"]["name"],
        "protocol": actual["protocol"],
        "interface_sha256": interface_digest,
        "build_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        "build_id": build_id,
        "generator": ir["generator"],
    }
    shipment = dict(shipment_basis)
    shipment["shipment_sha256"] = hashlib.sha256(
        json.dumps(shipment_basis, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    (args.output / "shipment.json").write_text(
        json.dumps(shipment, indent=2, sort_keys=True) + "\n"
    )
    build_wheel(effective_ir, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
