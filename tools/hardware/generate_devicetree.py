#!/usr/bin/env python3

"""Generate Solar C++ endpoint selectors from one resolved Zephyr EDT."""

from __future__ import annotations

import argparse
import json
import pathlib
import pickle
import re
import sys
from dataclasses import dataclass


KIND_ENUM = {
    "unknown": "Unknown",
    "device": "Device",
    "gpio": "Gpio",
    "spi": "Spi",
    "i2c": "I2c",
    "uart": "Uart",
    "adc": "Adc",
    "pwm": "Pwm",
    "counter": "Counter",
    "watchdog": "Watchdog",
}

SELECTOR_ENUM = {"alias": "Alias", "chosen": "Chosen", "node_label": "NodeLabel"}

CAP_METADATA = 1 << 0
CAP_READY = 1 << 1
CAP_NATIVE = 1 << 2
CAP_READ = 1 << 3
CAP_WRITE = 1 << 4
CAP_INTERRUPT = 1 << 5


@dataclass(frozen=True)
class Selection:
    category: str
    name: str
    token: str
    path: str
    compatible: str
    kind: str
    shape: str
    capabilities: int
    okay: bool

    @property
    def stable_id(self) -> int:
        return fnv1a_32(self.path.encode("utf-8")) or 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument("--edt-pickle", required=True, type=pathlib.Path)
    parser.add_argument("--zephyr-base", required=True, type=pathlib.Path)
    parser.add_argument("--header", required=True, type=pathlib.Path)
    parser.add_argument("--summary", required=True, type=pathlib.Path)
    return parser.parse_args()


def fnv1a_32(value: bytes) -> int:
    result = 0x811C9DC5
    for byte in value:
        result ^= byte
        result = (result * 0x01000193) & 0xFFFFFFFF
    return result


def token(value: str) -> str:
    return re.sub(r"[-,.@/+]", "_", value.lower())


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def classify(node) -> tuple[str, str, int]:
    if "gpios" in node.props:
        return "gpio", "gpio", CAP_METADATA | CAP_READY | CAP_NATIVE | CAP_READ | CAP_WRITE | CAP_INTERRUPT

    if "io-channels" in node.props:
        return "adc", "adc", CAP_METADATA | CAP_READY | CAP_NATIVE | CAP_READ

    if "pwms" in node.props:
        return "pwm", "pwm", CAP_METADATA | CAP_READY | CAP_NATIVE | CAP_READ | CAP_WRITE

    # on_buses is inherited through nested children. Only an immediate child of
    # a controller is an addressed bus endpoint suitable for *_DT_SPEC_GET.
    buses = set(getattr(node.parent, "buses", [])) if node.parent is not None else set()
    if "spi" in buses:
        return "spi", "spi_endpoint", CAP_METADATA | CAP_READY | CAP_NATIVE | CAP_READ | CAP_WRITE
    if "i2c" in buses:
        return "i2c", "i2c_endpoint", CAP_METADATA | CAP_READY | CAP_NATIVE | CAP_READ | CAP_WRITE

    binding = pathlib.Path(node.binding_path).parent.name if node.binding_path else ""
    if binding == "spi":
        return "spi", "device", CAP_METADATA | CAP_READY | CAP_NATIVE
    if binding == "i2c":
        return "i2c", "device", CAP_METADATA | CAP_READY | CAP_NATIVE
    if binding == "serial":
        return "uart", "device", CAP_METADATA | CAP_READY | CAP_NATIVE | CAP_READ | CAP_WRITE
    if binding == "adc":
        return "adc", "device", CAP_METADATA | CAP_READY | CAP_NATIVE
    if binding == "pwm":
        return "pwm", "device", CAP_METADATA | CAP_READY | CAP_NATIVE
    if binding == "counter":
        return "counter", "device", CAP_METADATA | CAP_READY | CAP_NATIVE | CAP_READ | CAP_WRITE | CAP_INTERRUPT
    if binding == "watchdog":
        return "watchdog", "device", CAP_METADATA | CAP_READY | CAP_NATIVE | CAP_WRITE
    return "unknown", "node", CAP_METADATA


def selection(category: str, name: str, node) -> Selection:
    kind, shape, capabilities = classify(node)
    okay = node.status == "okay"
    if not okay:
        kind, shape, capabilities = "unknown", "node", CAP_METADATA
    return Selection(
        category=category,
        name=name,
        token=token(name),
        path=node.path,
        compatible=node.compats[0] if node.compats else "",
        kind=kind,
        shape=shape,
        capabilities=capabilities,
        okay=okay,
    )


def collect(edt) -> list[Selection]:
    values: list[Selection] = []
    for node in edt.nodes:
        values.extend(selection("alias", name, node) for name in sorted(node.aliases))
    values.extend(
        selection("chosen", name, node) for name, node in sorted(edt.chosen_nodes.items())
    )
    for node in edt.nodes:
        values.extend(selection("node_label", name, node) for name in sorted(node.labels))
    return sorted(values, key=lambda item: (item.category, item.name))


def selector_macro(item: Selection) -> str:
    if item.category == "alias":
        return f"DT_ALIAS({item.token})"
    if item.category == "chosen":
        return f"DT_CHOSEN({item.token})"
    return f"DT_NODELABEL({item.token})"


def identity_expression(item: Selection) -> str:
    return (
        f"hardware::identity(SelectorKind::{SELECTOR_ENUM[item.category]}, "
        f"{cpp_string(item.name)}, {cpp_string(item.path)}, {cpp_string(item.compatible)}, "
        f"0x{item.stable_id:08X}U, EndpointKind::{KIND_ENUM[item.kind]}, "
        f"static_cast<CapabilitySet>({item.capabilities}U), "
        f"{'true' if item.okay else 'false'})"
    )


def value_expression(item: Selection) -> str:
    identity = identity_expression(item)
    node = selector_macro(item)
    if item.kind == "gpio":
        return f"gpio(GPIO_DT_SPEC_GET({node}, gpios), {identity})"
    if item.shape == "spi_endpoint":
        return (f"spi(SPI_DT_SPEC_GET({node}, SPI_WORD_SET(8) | SPI_TRANSFER_MSB), "
                f"{identity})")
    if item.shape == "i2c_endpoint":
        return f"i2c(I2C_DT_SPEC_GET({node}), {identity})"
    if item.shape == "adc":
        return f"adc(ADC_DT_SPEC_GET({node}), {identity})"
    if item.shape == "pwm":
        return f"pwm(PWM_DT_SPEC_GET({node}), {identity})"
    if item.shape == "device":
        return f"device(DEVICE_DT_GET({node}), {identity})"
    return f"node({identity})"


def family_config(item: Selection) -> str | None:
    return {
        "gpio": "CONFIG_SOLAR_HARDWARE_GPIO",
        "spi": "CONFIG_SOLAR_HARDWARE_SPI",
        "i2c": "CONFIG_SOLAR_HARDWARE_I2C",
        "uart": "CONFIG_SOLAR_HARDWARE_UART",
        "adc": "CONFIG_SOLAR_HARDWARE_ADC",
        "pwm": "CONFIG_SOLAR_HARDWARE_PWM",
        "counter": "CONFIG_SOLAR_HARDWARE_COUNTER",
        "watchdog": "CONFIG_SOLAR_HARDWARE_WATCHDOG",
    }.get(item.kind)


def render_header(selections: list[Selection]) -> str:
    lines = [
        "#pragma once",
        "",
        "// Generated from the resolved Zephyr EDT. Do not edit.",
        "#include <array>",
        "#include <zephyr/devicetree.h>",
        "#include <zephyr/device.h>",
        "#include <zephyr/drivers/gpio.h>",
        "#include <solar/hardware/dt.hpp>",
        "",
        "namespace solar::hardware::dt::generated",
        "{",
        "",
    ]
    template_names = {"alias": "Alias", "chosen": "Chosen", "node_label": "NodeLabel"}
    for item in selections:
        template_name = template_names[item.category]
        lines.extend([
            f"template <> struct {template_name}<FixedString{{{cpp_string(item.name)}}}>",
            "{",
        ])
        config = family_config(item)
        if config is not None and item.shape != "node":
            lines.extend([
                f"#if defined({config})",
                f"    inline static constexpr auto value = {value_expression(item)};",
                "#else",
                f"    inline static constexpr auto value = node({identity_expression(item)});",
                "#endif",
            ])
        else:
            lines.append(f"    inline static constexpr auto value = {value_expression(item)};")
        lines.extend(["};", ""])
    lines.append(f"inline constexpr std::array<InventoryEntry, {len(selections)}> inventory{{{{")
    for item in selections:
        lines.append(
            "    InventoryEntry{"
            f".selector_kind = SelectorKind::{SELECTOR_ENUM[item.category]}, "
            f".selector = {cpp_string(item.name)}, .path = {cpp_string(item.path)}, "
            f".compatible = {cpp_string(item.compatible)}, .stable_id = 0x{item.stable_id:08X}U, "
            f".endpoint_kind = EndpointKind::{KIND_ENUM[item.kind]}, "
            f".capabilities = static_cast<CapabilitySet>({item.capabilities}U), "
            f".okay = {'true' if item.okay else 'false'}" "},"
        )
    lines.extend(["}};", "", "} // namespace solar::hardware::dt::generated", ""])
    return "\n".join(lines)


def render_summary(selections: list[Selection]) -> str:
    values = [
        {
            "selector_kind": item.category,
            "selector": item.name,
            "path": item.path,
            "compatible": item.compatible,
            "stable_id": item.stable_id,
            "endpoint_kind": item.kind,
            "capabilities": item.capabilities,
            "okay": item.okay,
        }
        for item in selections
    ]
    return json.dumps({"version": 1, "entries": values}, indent=2, sort_keys=True) + "\n"


def write_if_changed(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(content, encoding="utf-8", newline="\n")
    temporary.replace(path)


def main() -> int:
    args = parse_args()
    devicetree_python = args.zephyr_base / "scripts" / "dts" / "python-devicetree" / "src"
    sys.path.insert(0, str(devicetree_python))
    with args.edt_pickle.open("rb") as input_file:
        edt = pickle.load(input_file)
    selections = collect(edt)
    write_if_changed(args.header, render_header(selections))
    write_if_changed(args.summary, render_summary(selections))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
