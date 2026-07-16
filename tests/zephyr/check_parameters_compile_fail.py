#!/usr/bin/env python3

import argparse
import json
import pathlib
import shlex
import subprocess
import sys


ENABLED_CASES = {
    1: "SOLAR_DIAGNOSTIC_PARAMETER_INVALID_DEFAULT",
    2: "SOLAR_DIAGNOSTIC_PARAMETER_SET_ACCESS",
    3: "SOLAR_DIAGNOSTIC_PARAMETER_SET_ACCESS",
    4: "SOLAR_DIAGNOSTIC_PARAMETER_ATOMIC_NOT_LOCK_FREE",
    6: "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_STABLE_ID",
    7: "SOLAR_DIAGNOSTIC_PARAMETER_CODEC_REQUIRED",
    8: "SOLAR_DIAGNOSTIC_PARAMETER_TRANSACTION_DUPLICATE",
    9: "SOLAR_DIAGNOSTIC_PARAMETER_CHANGE_UNREGISTERED",
    10: "SOLAR_DIAGNOSTIC_PARAMETER_CHANGE_HANDLER",
    11: "SOLAR_DIAGNOSTIC_DUPLICATE_PARAMETER_CHANGE",
    12: "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_NOT_REGISTERED",
    13: "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_STORE_MISMATCH",
    14: "SOLAR_DIAGNOSTIC_PARAMETER_TRANSACTION_IMMEDIATE_DURABILITY",
    15: "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_OPERATION",
    16: "SOLAR_DIAGNOSTIC_PARAMETER_CEILING",
    17: "SOLAR_DIAGNOSTIC_PARAMETER_CUSTOM_VALIDATOR",
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
        print(output, file=sys.stderr)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify focused Parameters compile-fail tokens.")
    parser.add_argument("--enabled", required=True, type=pathlib.Path)
    parser.add_argument("--persistence-disabled", required=True, type=pathlib.Path)
    parser.add_argument("--parameters-disabled", required=True, type=pathlib.Path)
    args = parser.parse_args()

    enabled = source_entry(args.enabled, "parameters_compile_fail/src/main.cpp")
    persistence_disabled = source_entry(
        args.persistence_disabled, "parameters_compile_fail/src/main.cpp"
    )
    parameters_disabled = source_entry(
        args.parameters_disabled, "parameters_disabled_compile_fail/src/main.cpp"
    )

    results = [expect_failure(enabled, token, case) for case, token in ENABLED_CASES.items()]
    results.append(
        expect_failure(
            persistence_disabled,
            "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_DISABLED",
            5,
        )
    )
    results.append(
        expect_failure(parameters_disabled, "SOLAR_DIAGNOSTIC_DISABLED_REQUIRED_BUILTIN")
    )
    failures = results.count(False)
    print(f"checked {len(results)} failure contracts; failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
