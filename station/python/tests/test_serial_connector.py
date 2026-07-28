from __future__ import annotations

from solar_station.connectors.serial import _read_available


class FakeSerial:
    def __init__(self, waiting: int, payload: bytes):
        self.in_waiting = waiting
        self.payload = payload
        self.read_sizes: list[int] = []

    def read(self, size: int) -> bytes:
        self.read_sizes.append(size)
        return self.payload[:size]


def test_serial_read_blocks_for_only_the_first_byte_when_idle() -> None:
    device = FakeSerial(0, b"response")

    assert _read_available(device, 4096) == b"r"
    assert device.read_sizes == [1]


def test_serial_read_immediately_drains_the_available_burst() -> None:
    device = FakeSerial(37, b"x" * 37)

    assert _read_available(device, 4096) == b"x" * 37
    assert device.read_sizes == [37]


def test_serial_read_caps_a_large_available_burst() -> None:
    device = FakeSerial(8192, b"x" * 8192)

    assert len(_read_available(device, 4096)) == 4096
    assert device.read_sizes == [4096]
