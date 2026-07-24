"""USB CDC discovery without application-specific device assumptions."""

from __future__ import annotations

from dataclasses import dataclass

from serial.tools import list_ports


@dataclass(frozen=True, slots=True)
class UsbTarget:
    device: str
    vid: int | None
    pid: int | None
    serial_number: str | None
    interface: str | None

    @property
    def target(self) -> str:
        return f"serial://{self.device}"


def discover_usb(*, vid: int | None = None, pid: int | None = None) -> list[UsbTarget]:
    output = []
    for port in list_ports.comports():
        if vid is not None and port.vid != vid or pid is not None and port.pid != pid:
            continue
        output.append(
            UsbTarget(
                port.device, port.vid, port.pid, port.serial_number, port.interface
            )
        )
    return sorted(output, key=lambda item: item.device)
