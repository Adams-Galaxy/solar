"""Solar Station error types and stable protocol error codes."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(slots=True)
class StationError(Exception):
    code: str
    message: str
    details: Any = None

    def __str__(self) -> str:
        return self.message

    def to_wire(self) -> dict[str, Any]:
        output: dict[str, Any] = {"code": self.code, "message": self.message}
        if self.details is not None:
            output["details"] = self.details
        return output

    @classmethod
    def from_wire(cls, value: object) -> StationError:
        if not isinstance(value, dict):
            return cls("invalid_response", "Station returned a malformed error")
        return cls(
            str(value.get("code", "station_error")),
            str(value.get("message", "Station operation failed")),
            value.get("details"),
        )


class ProtocolError(StationError):
    def __init__(self, message: str, details: Any = None):
        super().__init__("invalid_request", message, details)


def wrap_error(error: BaseException) -> StationError:
    if isinstance(error, StationError):
        return error
    if isinstance(error, TimeoutError):
        return StationError("request_timeout", str(error) or "Operation timed out")
    return StationError(
        "internal_error",
        str(error) or type(error).__name__,
        {"type": type(error).__name__},
    )
