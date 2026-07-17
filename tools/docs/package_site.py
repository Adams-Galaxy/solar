#!/usr/bin/env python3
"""Package one Solar documentation build as latest and a versioned release."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil


def copy_tree(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--html", type=Path, required=True)
    parser.add_argument("--site", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    if args.site.exists():
        shutil.rmtree(args.site)
    args.site.mkdir(parents=True)
    copy_tree(args.html, args.site / args.version)
    copy_tree(args.html, args.site / "latest")

    (args.site / "versions.json").write_text(json.dumps([
        {"version": args.version, "url": f"{args.version}/"},
        {"version": "latest", "url": "latest/"},
    ], indent=2) + "\n")
    (args.site / "index.html").write_text(
        '<!doctype html><meta charset="utf-8">'
        '<meta http-equiv="refresh" content="0; url=latest/">'
        '<title>Solar documentation</title>'
        '<a href="latest/">Solar documentation</a>\n'
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
