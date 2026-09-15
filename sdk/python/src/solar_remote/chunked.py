"""Reassembly facade for chunked `OutStream` delivery.

See `docs/development-docs/remote-arrays-and-chunked-streaming.md` §6 for the
wire-level design: a chunked stream's `Value` is an ordinary object carrying
`generation` / `chunk_index` / `final` plus one bounded array field (e.g.
`LidarScanChunk.points`). This module reassembles that chunk sequence into
complete values for the application, the same way the firmware side never
buffers more than one chunk at a time -- the station buffers at most one
in-progress generation.

Best-effort streams (`Queue<N, DropOldest>`, the only delivery kind this
supports today -- reliable-mode reassembly has no gaps to surface and can
just collect until `final`) can lose chunks under backpressure. This never
guesses at a partial reconstruction: any break in the expected sequence --
a new `generation` starting before the previous one reached `final`, or a
`chunk_index` that isn't exactly the next one expected -- discards whatever
was collected and reports it as a `ChunkGap`, matching how a dropped/
overwritten `Latest` value is already invisible-by-default one layer up.
"""

from __future__ import annotations

from collections.abc import AsyncIterator, Callable
from dataclasses import dataclass
from typing import Any, Generic, TypeVar

T = TypeVar("T")
C = TypeVar("C")

__all__ = ["ChunkGap", "ChunkReassembler", "ReassembledScan", "reassemble_chunks"]


@dataclass(frozen=True, slots=True)
class ReassembledScan(Generic[T]):
    """A complete value reassembled from a `generation`'s chunks, in order."""

    generation: int
    points: list[T]


@dataclass(frozen=True, slots=True)
class ChunkGap:
    """A `generation` that never reached `final` before reassembly gave up on it.

    `received_chunks`/`received_points` describe what *was* collected before
    the gap, purely for diagnostics -- the reassembled data itself is
    discarded, since a partial scan is not a meaningful value to hand the
    application (matching how the firmware's own decimation has no notion of
    "half a revolution").
    """

    generation: int
    received_chunks: int
    received_points: int


class ChunkReassembler(Generic[T]):
    """Wraps an async iterator of chunk objects, yields complete values or gaps.

    `chunks` must yield objects with `.generation` (int), `.chunk_index`
    (int, 0-based), `.final` (bool), and an attribute named `field` holding
    the chunk's slice of the payload (a list-like of the element type).
    """

    def __init__(
        self,
        chunks: AsyncIterator[C],
        *,
        field: str = "points",
        unwrap: Callable[[C], Any] | None = None,
    ):
        self._chunks = chunks
        self._field = field
        self._unwrap = unwrap

    def __aiter__(self) -> AsyncIterator[ReassembledScan[T] | ChunkGap]:
        return self._iterate()

    async def _iterate(self) -> AsyncIterator[ReassembledScan[T] | ChunkGap]:
        generation: int | None = None
        expected_index = 0
        collected: list[T] = []

        async for raw in self._chunks:
            chunk = self._unwrap(raw) if self._unwrap is not None else raw

            if generation is not None and chunk.generation != generation:
                # A new generation started before the previous one reached
                # `final` -- the old one is unrecoverable.
                yield ChunkGap(
                    generation=generation,
                    received_chunks=expected_index,
                    received_points=len(collected),
                )
                generation, expected_index, collected = None, 0, []

            if chunk.chunk_index != expected_index:
                if expected_index != 0:
                    yield ChunkGap(
                        generation=chunk.generation,
                        received_chunks=expected_index,
                        received_points=len(collected),
                    )
                generation, expected_index, collected = None, 0, []
                if chunk.chunk_index != 0:
                    # Mid-scan desync with no way to tell where we are in a
                    # generation we never saw the start of -- wait for the
                    # next one entirely rather than guess.
                    continue

            generation = chunk.generation
            collected.extend(getattr(chunk, self._field))
            expected_index += 1

            if chunk.final:
                yield ReassembledScan(generation=generation, points=collected)
                generation, expected_index, collected = None, 0, []


def reassemble_chunks(
    chunks: AsyncIterator[C],
    *,
    field: str = "points",
    unwrap: Callable[[C], Any] | None = None,
) -> ChunkReassembler[Any]:
    return ChunkReassembler(chunks, field=field, unwrap=unwrap)
