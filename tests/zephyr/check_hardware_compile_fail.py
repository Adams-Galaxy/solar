#!/usr/bin/env python3

import argparse
import json
import pathlib
import shlex
import subprocess


CASES = {
    1: "SOLAR_DIAGNOSTIC_HARDWARE_ALIAS_NOT_GENERATED",
    2: "SOLAR_DIAGNOSTIC_HARDWARE_GPIO_DESCRIPTOR_REQUIRED",
    3: "SOLAR_DIAGNOSTIC_INVALID_HARDWARE_ENDPOINT",
    4: "SOLAR_DIAGNOSTIC_HARDWARE_UART_DESCRIPTOR_REQUIRED",
    5: "SOLAR_DIAGNOSTIC_HARDWARE_UART_INTERRUPT_DISABLED",
    6: "SOLAR_DIAGNOSTIC_HARDWARE_SPI_DESCRIPTOR_REQUIRED",
    7: "SOLAR_DIAGNOSTIC_HARDWARE_SPI_ASYNC_DISABLED",
    8: "SOLAR_DIAGNOSTIC_HARDWARE_PWM_CAPTURE_DISABLED",
    9: "SOLAR_DIAGNOSTIC_HARDWARE_ADC_SEQUENCE_CONTROLLER_MISMATCH",
    10: "SOLAR_DIAGNOSTIC_HARDWARE_I2C_ASYNC_DISABLED",
}


def source_entry(path: pathlib.Path) -> dict:
    database = json.loads(path.resolve().read_text())
    return next(item for item in database
                if item["file"].endswith("hardware_compile_fail/src/main.cpp"))


def command(entry: dict, case: int) -> list[str]:
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
        if argument.startswith("-DSOLAR_FAIL_CASE="):
            argument = f"-DSOLAR_FAIL_CASE={case}"
        filtered.append(argument)
    filtered.append("-fsyntax-only")
    return filtered


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compile-commands", required=True, type=pathlib.Path)
    args = parser.parse_args()
    entry = source_entry(args.compile_commands)
    failures = 0
    for case, token in CASES.items():
        completed = subprocess.run(command(entry, case), cwd=entry["directory"],
                                   capture_output=True, text=True)
        output = completed.stdout + completed.stderr
        passed = completed.returncode != 0 and token in output
        print(f"{'PASS' if passed else 'FAIL'} case {case}: {token}")
        if not passed:
            failures += 1
            print(output)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
