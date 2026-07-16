#!/usr/bin/env python3

import argparse
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Require a command to fail with a focused diagnostic token."
    )
    parser.add_argument("--token", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a command is required after --")

    completed = subprocess.run(command, capture_output=True, text=True)
    output = completed.stdout + completed.stderr

    if completed.returncode == 0:
        print("expected command failure, but the command succeeded", file=sys.stderr)
        return 1

    if args.token not in output:
        print(
            f"command failed without expected diagnostic token: {args.token}",
            file=sys.stderr,
        )
        print(output, file=sys.stderr)
        return 2

    print(f"observed expected diagnostic token: {args.token}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
