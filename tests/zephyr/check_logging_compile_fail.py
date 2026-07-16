#!/usr/bin/env python3

import argparse
import json
import pathlib
import shlex
import subprocess


CASES = {
    1: "SOLAR_DIAGNOSTIC_LOG_SOURCE_UNREGISTERED",
    2: "SOLAR_DIAGNOSTIC_LOG_FORMAT",
    3: "SOLAR_DIAGNOSTIC_LOG_DOMAIN_UNREGISTERED",
    4: "SOLAR_DIAGNOSTIC_LOG_SINK",
}


def source_entry(path: pathlib.Path, suffix: str) -> dict:
    database = json.loads(path.resolve().read_text())
    entry = next((candidate for candidate in database if candidate["file"].endswith(suffix)), None)
    if entry is None:
        raise ValueError(f"no compile command ending in {suffix!r} in {path}")
    return entry


def syntax_command(entry: dict, fail_case: int | None = None) -> list[str]:
    arguments = shlex.split(entry["command"])
    filtered: list[str] = []
    skip_next = False
    for argument in arguments:
        if skip_next:
            skip_next = False
            continue
        if argument in {"-o", "-MF", "-MT", "-MQ"}:
            skip_next = True
            continue
        if argument in {"-c", "-MD", "-MMD", "-MP"}:
            continue
        if fail_case is not None and argument.startswith("-DSOLAR_FAIL_CASE="):
            argument = f"-DSOLAR_FAIL_CASE={fail_case}"
        filtered.append(argument)
    filtered.append("-fsyntax-only")
    return filtered


def expect_failure(entry: dict, token: str, fail_case: int | None = None) -> bool:
    completed = subprocess.run(
        syntax_command(entry, fail_case),
        cwd=entry["directory"],
        capture_output=True,
        text=True,
    )
    output = completed.stdout + completed.stderr
    passed = completed.returncode != 0 and token in output
    label = f"case {fail_case}" if fail_case is not None else pathlib.Path(entry["file"]).parent.name
    print(f"{'PASS' if passed else 'FAIL'} {label}: {token}")
    if not passed:
        print(output)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify focused Logging compile-fail tokens.")
    parser.add_argument("--enabled", required=True, type=pathlib.Path)
    parser.add_argument("--logging-disabled", required=True, type=pathlib.Path)
    args = parser.parse_args()

    suffix = "logging_compile_fail/src/main.cpp"
    enabled = source_entry(args.enabled, suffix)
    results = [expect_failure(enabled, token, case) for case, token in CASES.items()]
    results.append(
        expect_failure(
            source_entry(
                args.logging_disabled,
                "logging_disabled_compile_fail/src/main.cpp",
            ),
            "SOLAR_DIAGNOSTIC_DISABLED_REQUIRED_BUILTIN",
        )
    )
    failures = results.count(False)
    print(f"checked {len(results)} failure contracts; failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
