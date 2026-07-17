#!/usr/bin/env python3
"""Inspect generated Remote artifacts and exercise the host protocol runtime."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "remote"))


def load_module(path: Path):
    spec = importlib.util.spec_from_file_location("solar_generated_client", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generated", type=Path, required=True,
                        help="build/solar/remote output directory")
    args = parser.parse_args()

    manifest = json.loads((args.generated / "manifest.json").read_text())
    generated = load_module(args.generated / "client.py")
    client = generated.FirmwareClient()

    print(f"manifest: {manifest['schema_sha256']}")
    print(f"schemas: {len(manifest['schemas'])}")
    print(f"data: {len(manifest['data'])}")
    print(f"actions: {len(manifest['actions'])}")
    print(f"host client: {type(client).__name__}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
