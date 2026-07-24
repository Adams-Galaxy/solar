#!/usr/bin/env python3
"""Generate Solar's complete Kconfig reference with Kconfiglib."""

from __future__ import annotations

import argparse
from pathlib import Path

import kconfiglib


TYPE_NAMES = {
    kconfiglib.BOOL: "bool",
    kconfiglib.TRISTATE: "tristate",
    kconfiglib.STRING: "string",
    kconfiglib.INT: "int",
    kconfiglib.HEX: "hex",
    kconfiglib.UNKNOWN: "unknown",
}


def expression(value) -> str:
    return kconfiglib.expr_str(value).replace("&&", "and").replace("||", "or")


def menu_path(node: kconfiglib.MenuNode) -> str:
    names: list[str] = []
    parent = node.parent
    while parent is not None:
        if parent.prompt and parent.item is kconfiglib.MENU:
            names.append(parent.prompt[0])
        parent = parent.parent
    return " / ".join(reversed(names)) or "Solar"


def render_symbol(symbol: kconfiglib.Symbol) -> list[str]:
    node = symbol.nodes[0]
    prompt = node.prompt[0] if node.prompt else symbol.name
    lines = [f"## `CONFIG_{symbol.name}`", "", prompt, ""]
    lines += [f"- Type: `{TYPE_NAMES[symbol.type]}`", f"- Menu: {menu_path(node)}"]

    if symbol.defaults:
        values = []
        for entry in symbol.defaults:
            value, condition = entry[:2]
            item = f"`{expression(value)}`"
            if expression(condition) != "y":
                item += f" if `{expression(condition)}`"
            values.append(item)
        lines.append(f"- Defaults: {', '.join(values)}")
    direct = expression(symbol.direct_dep)
    if direct != "y":
        lines.append(f"- Depends on: `{direct}`")
    if symbol.selects:
        values = []
        for entry in symbol.selects:
            target, condition = entry[:2]
            item = f"`{target.name}`"
            if expression(condition) != "y":
                item += f" if `{expression(condition)}`"
            values.append(item)
        lines.append(f"- Selects: {', '.join(values)}")
    if symbol.ranges:
        ranges = [
            f"`{expression(entry[0])}` to `{expression(entry[1])}`"
            for entry in symbol.ranges
        ]
        lines.append(f"- Range: {', '.join(ranges)}")

    help_text = (node.help or "No additional help text.").strip()
    lines += ["", help_text, ""]
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kconfig", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    kconfig = kconfiglib.Kconfig(str(args.kconfig), warn=False)
    symbols = sorted(
        (
            symbol
            for symbol in kconfig.unique_defined_syms
            if symbol.name == "SOLAR" or symbol.name.startswith("SOLAR_")
        ),
        key=lambda symbol: symbol.name,
    )

    lines = [
        "# Complete Kconfig Reference",
        "",
        "This page is generated from `zephyr/Kconfig`. Change the Kconfig source or",
        "help text, then rebuild the documentation; do not edit this page manually.",
        "",
        f"Solar currently defines {len(symbols)} configuration symbols.",
        "",
    ]
    for symbol in symbols:
        lines.extend(render_symbol(symbol))

    output = "\n".join(lines).rstrip() + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != output:
        args.output.write_text(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
