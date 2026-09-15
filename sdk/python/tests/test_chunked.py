from __future__ import annotations

from dataclasses import dataclass

import pytest

from solar_remote.chunked import ChunkGap, ReassembledScan, reassemble_chunks


@dataclass(frozen=True, slots=True)
class Chunk:
    generation: int
    chunk_index: int
    final: bool
    points: list[int]


async def _source(chunks: list[Chunk]):
    for chunk in chunks:
        yield chunk


@pytest.mark.asyncio
async def test_single_generation_reassembles_in_order() -> None:
    chunks = [
        Chunk(generation=1, chunk_index=0, final=False, points=[1, 2]),
        Chunk(generation=1, chunk_index=1, final=False, points=[3, 4]),
        Chunk(generation=1, chunk_index=2, final=True, points=[5]),
    ]
    results = [item async for item in reassemble_chunks(_source(chunks))]
    assert results == [ReassembledScan(generation=1, points=[1, 2, 3, 4, 5])]


@pytest.mark.asyncio
async def test_empty_final_only_scan_reassembles_to_empty_points() -> None:
    chunks = [Chunk(generation=1, chunk_index=0, final=True, points=[])]
    results = [item async for item in reassemble_chunks(_source(chunks))]
    assert results == [ReassembledScan(generation=1, points=[])]


@pytest.mark.asyncio
async def test_back_to_back_generations_each_reassemble_independently() -> None:
    chunks = [
        Chunk(generation=1, chunk_index=0, final=True, points=[1]),
        Chunk(generation=2, chunk_index=0, final=True, points=[2]),
    ]
    results = [item async for item in reassemble_chunks(_source(chunks))]
    assert results == [
        ReassembledScan(generation=1, points=[1]),
        ReassembledScan(generation=2, points=[2]),
    ]


@pytest.mark.asyncio
async def test_new_generation_before_final_surfaces_a_gap_and_discards_the_partial_scan() -> (
    None
):
    chunks = [
        Chunk(generation=1, chunk_index=0, final=False, points=[1]),
        Chunk(generation=1, chunk_index=1, final=False, points=[2]),
        # generation 2 starts before generation 1 ever reached `final` --
        # best-effort DropOldest overwrote the rest of generation 1.
        Chunk(generation=2, chunk_index=0, final=True, points=[9]),
    ]
    results = [item async for item in reassemble_chunks(_source(chunks))]
    assert results == [
        ChunkGap(generation=1, received_chunks=2, received_points=2),
        ReassembledScan(generation=2, points=[9]),
    ]


@pytest.mark.asyncio
async def test_dropped_middle_chunk_surfaces_a_gap_and_waits_for_the_next_generation() -> (
    None
):
    chunks = [
        Chunk(generation=1, chunk_index=0, final=False, points=[1]),
        # chunk_index 1 was dropped -- jumps straight to 2.
        Chunk(generation=1, chunk_index=2, final=True, points=[3]),
        Chunk(generation=2, chunk_index=0, final=True, points=[9]),
    ]
    results = [item async for item in reassemble_chunks(_source(chunks))]
    assert results == [
        ChunkGap(generation=1, received_chunks=1, received_points=1),
        ReassembledScan(generation=2, points=[9]),
    ]


@pytest.mark.asyncio
async def test_join_mid_scan_waits_silently_for_a_clean_start() -> None:
    # The very first chunk this subscriber ever sees is mid-scan (it
    # subscribed partway through a generation already in flight) -- there is
    # nothing collected yet, so this must not report a spurious gap.
    chunks = [
        Chunk(generation=1, chunk_index=3, final=False, points=[7]),
        Chunk(generation=1, chunk_index=4, final=True, points=[8]),
        Chunk(generation=2, chunk_index=0, final=True, points=[9]),
    ]
    results = [item async for item in reassemble_chunks(_source(chunks))]
    assert results == [ReassembledScan(generation=2, points=[9])]


@pytest.mark.asyncio
async def test_unwrap_extracts_the_chunk_from_a_wrapper_object() -> None:
    @dataclass(frozen=True, slots=True)
    class Frame:
        value: Chunk

    chunks = [Frame(Chunk(generation=1, chunk_index=0, final=True, points=[1, 2]))]
    results = [
        item
        async for item in reassemble_chunks(
            _source(chunks), unwrap=lambda frame: frame.value
        )
    ]
    assert results == [ReassembledScan(generation=1, points=[1, 2])]
