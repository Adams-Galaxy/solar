#!/usr/bin/env python3

import argparse
import json
import pathlib
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", required=True, type=pathlib.Path)
    parser.add_argument("--edt-pickle", required=True, type=pathlib.Path)
    parser.add_argument("--zephyr-base", required=True, type=pathlib.Path)
    parser.add_argument("--header", required=True, type=pathlib.Path)
    parser.add_argument("--summary", required=True, type=pathlib.Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        generated_header = root / "devicetree.hpp"
        generated_summary = root / "devicetree.json"
        subprocess.run([
            "python3", str(args.generator),
            "--edt-pickle", str(args.edt_pickle),
            "--zephyr-base", str(args.zephyr_base),
            "--header", str(generated_header),
            "--summary", str(generated_summary),
        ], check=True)
        if generated_header.read_bytes() != args.header.read_bytes():
            raise SystemExit("generated Hardware header is not deterministic")
        if generated_summary.read_bytes() != args.summary.read_bytes():
            raise SystemExit("generated Hardware summary is not deterministic")

    summary = json.loads(args.summary.read_text())
    aliases = {entry["selector"]: entry for entry in summary["entries"]
               if entry["selector_kind"] == "alias"}
    if "led0" not in aliases or aliases["led0"]["endpoint_kind"] != "gpio":
        raise SystemExit("resolved led0 alias was not classified as GPIO")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
