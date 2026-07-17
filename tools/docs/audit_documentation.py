#!/usr/bin/env python3
"""Audit structural documentation coverage that Sphinx cannot infer."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

import kconfiglib


AGGREGATE_PAGES = {
    "bus": "reference/api/bus.md",
    "catalog": "reference/api/composition.md",
    "component": "reference/api/composition.md",
    "core": "reference/api/core.md",
    "events": "reference/api/events.md",
    "execution": "reference/api/execution.md",
    "hardware": "reference/api/hardware.md",
    "health": "reference/api/health.md",
    "inspection": "reference/api/inspection.md",
    "kernel": "reference/api/kernel.md",
    "lifecycle": "reference/api/lifecycle.md",
    "log": "reference/api/logging.md",
    "metrics": "reference/api/metrics.md",
    "parameters": "reference/api/parameters.md",
    "remote": "reference/api/remote.md",
    "solar": "reference/api/index.md",
    "supervisor": "reference/api/supervisor.md",
    "system": "reference/api/system.md",
    "version": "reference/api/core.md",
}

SUBSYSTEMS = {
    "bus", "events", "execution", "hardware", "health", "inspection", "kernel",
    "logging", "metrics", "parameters", "remote", "supervisor",
}

EXAMPLES = {
    "first-application", "system-composition", "data-pipeline", "remote-control",
    "supervised-device",
}


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    docs = root / "docs"
    failures: list[str] = []

    aggregates = {path.stem for path in (root / "include/solar").glob("*.hpp")}
    require(aggregates == set(AGGREGATE_PAGES),
            f"aggregate coverage map differs: headers={sorted(aggregates)}, "
            f"map={sorted(AGGREGATE_PAGES)}", failures)
    for header, page in AGGREGATE_PAGES.items():
        require((docs / page).is_file(), f"{header}.hpp has no API page {page}", failures)

    for subsystem in SUBSYSTEMS:
        require((docs / "subsystems" / f"{subsystem}.md").is_file(),
                f"missing subsystem page: {subsystem}", failures)

    for example in EXAMPLES:
        directory = root / "examples" / example
        for relative in ("CMakeLists.txt", "prj.conf", "sample.yaml", "src/main.cpp"):
            require((directory / relative).is_file(),
                    f"example {example} missing {relative}", failures)

    kconfig = kconfiglib.Kconfig(str(root / "zephyr/Kconfig"), warn=False)
    symbols = {
        symbol.name for symbol in kconfig.unique_defined_syms
        if symbol.name == "SOLAR" or symbol.name.startswith("SOLAR_")
    }
    generated = (docs / "reference/generated/kconfig.md").read_text()
    documented = set(re.findall(r"^## `CONFIG_(SOLAR(?:_[A-Z0-9_]+)?)`$", generated, re.M))
    require(symbols == documented,
            f"Kconfig reference mismatch: missing={sorted(symbols - documented)}, "
            f"extra={sorted(documented - symbols)}", failures)

    placeholder = re.compile(r"\b(?:TODO|TBD|FIXME)\b")
    for page in docs.rglob("*.md"):
        if "development-docs" in page.parts:
            continue
        match = placeholder.search(page.read_text())
        require(match is None, f"placeholder marker in {page.relative_to(root)}", failures)

    if failures:
        print("Documentation audit failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print(f"Documentation audit passed: {len(aggregates)} aggregate headers, "
          f"{len(SUBSYSTEMS)} subsystem pages, {len(symbols)} Kconfig symbols, "
          f"{len(EXAMPLES)} canonical examples")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
