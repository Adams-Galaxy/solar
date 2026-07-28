#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile


def snapshot(directory: Path) -> dict[str, bytes]:
    return {
        path.name: path.read_bytes() for path in directory.iterdir() if path.is_file()
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="solar-remote-generation-") as temporary:
        root = Path(temporary)
        first, second = root / "first", root / "second"
        for output in (first, second):
            subprocess.run(
                [
                    sys.executable,
                    str(args.generator),
                    "--elf",
                    str(args.elf),
                    "--output",
                    str(output),
                ],
                check=True,
            )
        assert snapshot(first) == snapshot(second)
        sys.path.insert(0, str(args.generator.resolve().parents[2] / "sdk/python/src"))
        from solar_remote.codec import DynamicCodec
        from solar_remote.manifest import ManifestError, parse_manifest

        manifest = parse_manifest((first / "manifest.bin").read_bytes())
        assert manifest.digest.hex() == (first / "manifest.sha256").read_text().strip()
        assert any(schema["shape"] == "enumeration" for schema in manifest.schemas)
        sample = next(
            schema for schema in manifest.schemas if schema["name"] == "fixture.Sample"
        )
        assert sample["fields"][1]["description"] == "Measured value"
        assert sample["fields"][2]["unit"] == "ratio"
        assert sample["fields"][3]["deprecated"]
        assert not sample["fields"][4]["required"]
        codec = DynamicCodec(manifest)
        encoded = codec.encode(
            sample["id"],
            {
                "sequence": 1,
                "value": -2,
                "gain": 1.5,
                "valid": True,
            },
        )
        assert codec.decode(sample["id"], encoded) == {
            "sequence": 1,
            "value": -2,
            "gain": 1.5,
            "valid": True,
            "quality": None,
        }
        image = bytearray(manifest.image)
        count = int.from_bytes(image[8:10], "little") + 1
        image[8:10] = count.to_bytes(2, "little")
        image.extend(bytes((250, 0, 4, 0)))
        image[12:16] = len(image).to_bytes(4, "little")
        assert parse_manifest(bytes(image)).protocol == manifest.protocol
        image[-3] = 1
        try:
            parse_manifest(bytes(image))
        except ManifestError as error:
            assert "required" in str(error)
        else:
            raise AssertionError("unknown required record was accepted")
        spec = importlib.util.spec_from_file_location(
            "solar_generated", first / "constants.py"
        )
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        assert module.PROTOCOL == (1, 2)
        assert len(module.SCHEMA_SHA256) == 32
        sys.path.insert(0, str(first))
        generated_client = runpy.run_path(str(first / "client.py"))
        client = generated_client["FirmwareClient"]()
        assert client.protocol == (1, 2) and len(client.schema_sha256) == 32
        source = root / "generated.cpp"
        source.write_text(
            '#include "manifest.hpp"\nint main() { return solar::remote::generated::protocol_major == 1 ? 0 : 1; }\n'
        )
        subprocess.run(
            [
                args.compiler,
                "-std=c++23",
                "-I",
                str(first),
                str(source),
                "-o",
                str(root / "generated"),
            ],
            check=True,
        )
        subprocess.run([str(root / "generated")], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
