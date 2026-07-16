#!/usr/bin/env python3
from __future__ import annotations

import argparse
import filecmp
import importlib.util
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile


def snapshot(directory: Path) -> dict[str, bytes]:
    return {path.name: path.read_bytes() for path in directory.iterdir() if path.is_file()}


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
            subprocess.run([sys.executable, str(args.generator), "--elf", str(args.elf),
                            "--output", str(output)], check=True)
        assert snapshot(first) == snapshot(second)
        spec = importlib.util.spec_from_file_location("solar_generated", first / "constants.py")
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        assert module.PROTOCOL == (1, 0)
        assert len(module.SCHEMA_SHA256) == 32
        sys.path.insert(0, str(args.generator.parent))
        generated_client = runpy.run_path(str(first / "client.py"))
        client = generated_client["FirmwareClient"]()
        assert client.protocol == (1, 0) and len(client.schema_sha256) == 32
        source = root / "generated.cpp"
        source.write_text('#include "manifest.hpp"\nint main() { return solar::remote::generated::protocol_major == 1 ? 0 : 1; }\n')
        subprocess.run([args.compiler, "-std=c++23", "-I", str(first), str(source), "-o",
                        str(root / "generated")], check=True)
        subprocess.run([str(root / "generated")], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
