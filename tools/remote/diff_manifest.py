#!/usr/bin/env python3
"""Classify compatibility between two exact Solar Remote manifest images."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk/python/src"))
from solar_remote.manifest import compatibility, parse_manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("previous", type=Path)
    parser.add_argument("current", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    result = compatibility(
        parse_manifest(args.previous.read_bytes()),
        parse_manifest(args.current.read_bytes()),
    )
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        for category, entries in result.items():
            for entry in entries:
                print(f"{category}: {entry}")
    return 2 if result["breaking"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
