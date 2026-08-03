"""Source-aware diagnostics shared by compiler stages."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class SourceLocation:
    path: Path
    line: int
    column: int

    def render(self) -> str:
        return f"{self.path}:{self.line}:{self.column}"


class CompileError(ValueError):
    def __init__(self, message: str, location: SourceLocation | None = None):
        prefix = f"{location.render()}: " if location else ""
        super().__init__(prefix + message)
        self.location = location
