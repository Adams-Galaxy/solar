from __future__ import annotations

import io
import logging

from solar_station.service import _StationLogFormatter, _supports_color


class _TerminalBuffer(io.StringIO):
    def isatty(self) -> bool:
        return True


def _record(level: int = logging.WARNING) -> logging.LogRecord:
    return logging.LogRecord(
        name="solar_station.remote",
        level=level,
        pathname=__file__,
        lineno=1,
        msg="connection unavailable",
        args=(),
        exc_info=None,
    )


def test_station_formatter_colors_structured_fields() -> None:
    rendered = _StationLogFormatter(color=True).format(_record())

    assert "\033[2m" in rendered
    assert "\033[1;33mWARNING" in rendered
    assert "\033[36msolar_station.remote\033[0m" in rendered
    assert rendered.endswith("connection unavailable")


def test_station_formatter_remains_plain_when_color_is_disabled() -> None:
    rendered = _StationLogFormatter(color=False).format(_record())

    assert "\033[" not in rendered
    assert "WARNING solar_station.remote: connection unavailable" in rendered


def test_no_color_overrides_terminal_detection(monkeypatch) -> None:
    stream = _TerminalBuffer()
    monkeypatch.setenv("NO_COLOR", "")
    monkeypatch.setenv("FORCE_COLOR", "1")

    assert _supports_color(stream) is False
