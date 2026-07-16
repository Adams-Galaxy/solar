#!/usr/bin/env python3

import argparse
import json
import pathlib
import shlex
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compile-commands", required=True, type=pathlib.Path)
    parser.add_argument("--include-root", required=True, type=pathlib.Path)
    args = parser.parse_args()
    database = json.loads(args.compile_commands.resolve().read_text())
    entry = next(item for item in database
                 if item["file"].endswith("hardware_compile_fail/src/main.cpp"))
    arguments = shlex.split(entry["command"])
    base: list[str] = []
    skip_next = False
    for argument in arguments:
        if skip_next:
            skip_next = False
            continue
        if argument in {"-o", "-MF", "-MT", "-MQ"}:
            skip_next = True
            continue
        if argument in {"-c", "-MD", "-MMD", "-MP"} or argument.startswith("-DSOLAR_FAIL_CASE="):
            continue
        base.append(argument)

    headers = sorted((args.include_root / "solar" / "hardware").glob("*.hpp"))
    headers.append(args.include_root / "solar" / "hardware.hpp")
    failures = 0
    with tempfile.TemporaryDirectory() as directory:
        source = pathlib.Path(directory) / "header.cpp"
        for header in headers:
            relative = header.relative_to(args.include_root).as_posix()
            source.write_text(f"#include <{relative}>\nint main() {{ return 0; }}\n")
            completed = subprocess.run(base + [str(source), "-fsyntax-only"],
                                       cwd=entry["directory"], capture_output=True, text=True)
            passed = completed.returncode == 0
            print(f"{'PASS' if passed else 'FAIL'} {relative}")
            if not passed:
                failures += 1
                print(completed.stdout + completed.stderr)
    print(f"checked {len(headers)} headers; failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
