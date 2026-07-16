#!/usr/bin/env python3

import argparse
import json
import pathlib
import shlex
import subprocess


def syntax_command(command: str, header: pathlib.Path) -> list[str]:
    arguments = shlex.split(command)
    filtered: list[str] = []
    skip_next = False
    for index, argument in enumerate(arguments):
        if skip_next:
            skip_next = False
            continue
        if argument in {"-o", "-MF", "-MT", "-MQ"}:
            skip_next = True
            continue
        if argument in {"-c", "-MD", "-MMD", "-MP"}:
            continue
        if argument.endswith((".c", ".cc", ".cpp", ".cxx")) and index > 0:
            continue
        filtered.append(argument)
    filtered.extend(["-Werror", "-fsyntax-only", "-include", str(header), "-x", "c++", "/dev/null"])
    return filtered


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compile-commands", required=True, type=pathlib.Path)
    parser.add_argument("--include-root", required=True, type=pathlib.Path)
    args = parser.parse_args()
    database = json.loads(args.compile_commands.resolve().read_text())
    entry = next(item for item in database if item["file"].endswith("health/src/main.cpp"))
    root = args.include_root.resolve()
    headers = sorted((root / "solar" / "health").glob("*.hpp")) + [root / "solar" / "health.hpp"]
    failures = 0
    for header in headers:
        completed = subprocess.run(syntax_command(entry["command"], header),
                                   cwd=entry["directory"], capture_output=True, text=True)
        label = header.relative_to(root)
        if completed.returncode == 0:
            print(f"PASS {label}")
        else:
            failures += 1
            print(f"FAIL {label}\n{completed.stdout}{completed.stderr}")
    print(f"checked {len(headers)} headers; failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
