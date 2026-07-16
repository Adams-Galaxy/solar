#!/usr/bin/env python3

import argparse
import json
import pathlib
import shlex
import subprocess


ENABLED_CASES = {
    1: "SOLAR_DIAGNOSTIC_EVENT_PAYLOAD",
    2: "SOLAR_DIAGNOSTIC_EVENT_PAYLOAD_SIZE",
    3: "SOLAR_DIAGNOSTIC_EVENT_PAYLOAD_ALIGNMENT",
    4: "SOLAR_DIAGNOSTIC_EVENT_AGGREGATION_KEY",
    5: "SOLAR_DIAGNOSTIC_EVENT_AGGREGATION_CEILING",
    7: "SOLAR_DIAGNOSTIC_EVENT_PERSISTENCE_STABLE_ID",
    8: "SOLAR_DIAGNOSTIC_EVENT_PERSISTENCE_STORE",
    9: "SOLAR_DIAGNOSTIC_EVENT_CRITICAL_CEILING",
    10: "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_OWNER",
    11: "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_HANDLER",
    12: "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_UNREGISTERED",
    13: "SOLAR_DIAGNOSTIC_EVENT_SOURCE_UNREGISTERED",
    14: "SOLAR_DIAGNOSTIC_EVENT_ISR_POLICY",
    15: "SOLAR_DIAGNOSTIC_EVENT_RECOVERY_UNREGISTERED",
    16: "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_OPERATION",
    17: "SOLAR_DIAGNOSTIC_EVENT_CEILING",
    18: "SOLAR_DIAGNOSTIC_EVENT_PAYLOAD_BORROWED",
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
    parser = argparse.ArgumentParser(description="Verify focused Events compile-fail tokens.")
    parser.add_argument("--enabled", required=True, type=pathlib.Path)
    parser.add_argument("--persistence-disabled", required=True, type=pathlib.Path)
    parser.add_argument("--executor-default-disabled", required=True, type=pathlib.Path)
    parser.add_argument("--events-disabled", required=True, type=pathlib.Path)
    args = parser.parse_args()

    enabled = source_entry(args.enabled, "events_compile_fail/src/main.cpp")
    persistence_disabled = source_entry(
        args.persistence_disabled, "events_compile_fail/src/main.cpp"
    )
    executor_disabled = source_entry(
        args.executor_default_disabled, "events_compile_fail/src/main.cpp"
    )
    events_disabled = source_entry(
        args.events_disabled, "events_disabled_compile_fail/src/main.cpp"
    )

    results = [expect_failure(enabled, token, case) for case, token in ENABLED_CASES.items()]
    results.append(
        expect_failure(
            persistence_disabled,
            "SOLAR_DIAGNOSTIC_EVENT_PERSISTENCE_DISABLED",
            6,
        )
    )
    results.append(
        expect_failure(
            executor_disabled,
            "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_EXECUTOR_REQUIRED",
            19,
        )
    )
    results.append(
        expect_failure(events_disabled, "SOLAR_DIAGNOSTIC_DISABLED_REQUIRED_BUILTIN")
    )
    failures = results.count(False)
    print(f"checked {len(results)} failure contracts; failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
