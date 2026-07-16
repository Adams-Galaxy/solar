#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import replace
import json
from pathlib import Path
import random
import struct

import cbor2

from solar_remote.protocol import (
    FLAG_FINAL,
    FLAG_FRAGMENTED,
    KIND_CREDIT,
    KIND_DATA,
    KIND_REQUEST,
    KIND_INTROSPECTION,
    KIND_SERVER_HELLO,
    OPERATION_QUERY,
    SUBSCRIPTION_TOPIC,
    CreditGrant,
    CollectionPage,
    CollectionQueryPage,
    Envelope,
    FrameDecoder,
    SubscriptionPolicy,
    SubscriptionRequest,
    IntrospectionSummary,
    crc32c,
    decode_batch,
    decode_frame,
    encode_batch,
    encode_collection_request,
    encode_collection_query_request,
    encode_frame,
)
from solar_remote.client import Client, Hello, Reassembler


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vectors", type=Path, required=True)
    args = parser.parse_args()
    vector = json.loads(args.vectors.read_text())
    envelope = Envelope(**vector["envelope"])
    payload = bytes.fromhex(vector["payload_hex"])
    assert envelope.encode().hex() == vector["envelope_hex"]
    frame = encode_frame(envelope, payload)
    assert frame.hex() == vector["frame_hex"]
    decoded, decoded_payload = decode_frame(frame)
    assert decoded_payload == payload and decoded.payload_size == len(payload)
    query = envelope.with_operation(OPERATION_QUERY)
    assert query.operation == OPERATION_QUERY and Envelope.decode(query.encode()) == query
    topic = envelope.with_subscription(SUBSCRIPTION_TOPIC)
    assert topic.subscription == SUBSCRIPTION_TOPIC and Envelope.decode(topic.encode()) == topic
    request = SubscriptionRequest(minimum_interval_us=20_000, batch_size=4)
    assert SubscriptionRequest.decode(request.encode()) == request
    policy = SubscriptionPolicy(minimum_interval_us=20_000, batch_size=1, codec=1)
    assert SubscriptionPolicy.decode(policy.encode()) == policy
    credit = CreditGrant(credits=3, window=8)
    assert CreditGrant.decode(credit.encode()) == credit
    batch = [b"one", b"two", b""]
    assert decode_batch(encode_batch(batch, codec=1)) == (1, batch)
    crc_vector = vector["crc32c"]
    assert crc32c(bytes.fromhex(crc_vector["input_hex"])) == crc_vector["value"]
    cbor_value = {int(key): value for key, value in vector["cbor"]["value"].items()}
    assert cbor2.dumps(cbor_value, canonical=True).hex() == vector["cbor"]["canonical_hex"]
    corrupt = bytearray(frame)
    corrupt[8] ^= 0x10
    try:
        decode_frame(bytes(corrupt))
    except ValueError:
        pass
    else:
        raise AssertionError("corrupt frame was accepted")
    parser = FrameDecoder(128)
    assert parser.feed(frame[:7]) == []
    assert parser.feed(frame[7:]) == [(decoded, payload)]
    recovered = []
    for byte in bytes(corrupt) + frame:
        recovered.extend(parser.feed(bytes([byte])))
    assert recovered == [(decoded, payload)] and parser.rejected == 1
    small_parser = FrameDecoder(8)
    assert small_parser.feed(b"x" * 20 + b"\0") == []
    assert small_parser.overflowed == 1

    now = [0.0]
    reassembler = Reassembler(32, slots=1, timeout=1.0, clock=lambda: now[0])
    fragmented = Envelope(
        kind=KIND_DATA, flags=FLAG_FRAGMENTED, fragment_id=7, fragment_count=2,
        target=4, session_epoch=2,
    )
    assert reassembler.push(fragmented, b"abc") is None
    complete = reassembler.push(
        replace(fragmented, flags=FLAG_FRAGMENTED | FLAG_FINAL, fragment_index=1), b"def"
    )
    assert complete is not None and complete.payload == b"abcdef"
    expiring = Envelope(
        kind=KIND_DATA, flags=FLAG_FRAGMENTED, fragment_id=8, fragment_count=2,
    )
    assert reassembler.push(expiring, b"old") is None
    now[0] = 2.0
    reassembler.expire()
    assert reassembler.expired == 1

    client = Client(maximum_frame_size=64, maximum_message_size=256)
    hello = Hello(1, 0, 64, 256, 0x0F).encode()
    server = encode_frame(
        Envelope(kind=KIND_SERVER_HELLO, session_epoch=3, frame_sequence=1), hello
    )
    assert len(client.feed(server[:5])) == 0
    assert len(client.feed(server[5:])) == 1 and not client.active
    client_hello = client.take_outgoing()
    assert client_hello is not None
    client_envelope, client_payload = decode_frame(client_hello)
    assert client_envelope.kind == 1 and Hello.decode(client_payload).maximum_frame_bytes == 64
    client.feed(encode_frame(
        Envelope(kind=KIND_SERVER_HELLO, session_epoch=3, frame_sequence=2), hello
    ))
    assert client.active and client.session_epoch == 3
    request_id = client.request(0x1234, b"query", OPERATION_QUERY)
    request_frames = []
    while (queued := client.take_outgoing()) is not None:
        request_frames.append(decode_frame(queued))
    assert request_id == 1 and request_frames[0][0].kind == KIND_REQUEST
    assert request_frames[0][0].operation == OPERATION_QUERY
    credit_frame = encode_frame(
        Envelope(kind=KIND_CREDIT, session_epoch=3, target=0x44),
        CreditGrant(credits=2, window=4).encode(),
    )
    client.feed(credit_frame)
    client.send_stream(0x44, bytes(range(100)), sequence=1)
    stream_fragments = []
    while (queued := client.take_outgoing()) is not None:
        stream_fragments.append(decode_frame(queued))
    assert len(stream_fragments) > 1 and client.credits[0x44].credits == 1
    assert all(item[0].flags & FLAG_FRAGMENTED for item in stream_fragments)
    assert stream_fragments[-1][0].flags & FLAG_FINAL
    introspection_id = client.introspect()
    introspection_frame = client.take_outgoing()
    assert introspection_frame is not None
    introspection_envelope, introspection_payload = decode_frame(introspection_frame)
    assert introspection_id == 2 and introspection_envelope.kind == KIND_INTROSPECTION
    assert introspection_payload == b""
    summary = IntrospectionSummary.decode(
        bytes((1, 1, 0, 1))
        + (3).to_bytes(2, "little") + (4).to_bytes(2, "little")
        + (5).to_bytes(2, "little") + (6).to_bytes(2, "little")
        + (7).to_bytes(2, "little") + (8).to_bytes(2, "little")
        + (1024).to_bytes(4, "little") + (8192).to_bytes(4, "little")
    )
    assert summary.schemas == 3 and summary.links == 8
    assert summary.maximum_message_bytes == 8192
    collection_payload = (
        bytes((1, 1)) + (1).to_bytes(2, "little") + (1).to_bytes(2, "little")
        + bytes((0, 0))
        + (0).to_bytes(2, "little") + (0x6D9A0001).to_bytes(4, "little")
        + (1).to_bytes(2, "little") + bytes((1, 17, 2, 0, 0, 1))
        + (8).to_bytes(2, "little") + (32).to_bytes(2, "little")
        + (16).to_bytes(2, "little") + bytes((0, 10)) + b"components"
    )
    collection_page = CollectionPage.decode(collection_payload)
    assert collection_page.total == 1
    assert collection_page.collections[0].name == "components"
    assert collection_page.collections[0].stable_id == 0x6D9A0001
    query_page = CollectionQueryPage.decode(cbor2.dumps({
        0: 0x6D9A0002, 1: 1, 2: 4, 3: True, 4: 7, 5: 1, 6: 0,
        7: 0, 8: [{0: 1, 1: 2}], 9: True,
    }, canonical=True))
    assert query_page.stable_id == 0x6D9A0002 and query_page.loss_known
    assert query_page.records == ({0: 1, 1: 2},)
    collections_id = client.introspect(1, encode_collection_request(limit=4))
    collections_frame = client.take_outgoing()
    assert collections_frame is not None and collections_id == 3
    collections_envelope, collections_request = decode_frame(collections_frame)
    assert collections_envelope.target == 1 and collections_request == b"\0\0\4\0"
    query_id = client.introspect(
        2, encode_collection_query_request(0x6D9A0002, offset=3, revision=4, limit=2)
    )
    query_frame = client.take_outgoing()
    assert query_frame is not None and query_id == 4
    query_envelope, query_request = decode_frame(query_frame)
    assert query_envelope.target == 2
    assert query_request == struct.pack("<IIIH", 0x6D9A0002, 3, 4, 2)
    fuzz = random.Random(301)
    fuzz_parser = FrameDecoder(128)
    for _ in range(500):
        candidate = bytearray(frame)
        for _ in range(fuzz.randrange(1, 5)):
            index = fuzz.randrange(len(candidate))
            candidate[index] ^= 1 << fuzz.randrange(8)
        fuzz_parser.feed(bytes(candidate))
    assert fuzz_parser.rejected + fuzz_parser.overflowed > 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
