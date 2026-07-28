"""Transport-independent Solar Remote protocol primitives."""

from __future__ import annotations

from dataclasses import dataclass, replace
import struct

import cbor2

PROTOCOL_MAJOR = 1
PROTOCOL_MINOR = 2
ENVELOPE_SIZE = 32
SUBSCRIPTION_POLICY_SIZE = 8
CREDIT_GRANT_SIZE = 8
IN_STREAM_OPEN_RESPONSE_SIZE = 12
IN_STREAM_CLOSE_REQUEST_SIZE = 4
IN_STREAM_CLOSED_SIZE = 8
BATCH_HEADER_SIZE = 4
BATCH_VERSION = 1
INTROSPECTION_SUMMARY_SIZE = 24
INTROSPECTION_PROTOCOL_SUMMARY = 0
INTROSPECTION_COLLECTIONS = 1
INTROSPECTION_COLLECTION_QUERY = 2
INTROSPECTION_SERVER_INFORMATION = 3
INTROSPECTION_MANIFEST = 4
COLLECTION_PAGE_HEADER_SIZE = 8
COLLECTION_DESCRIPTOR_HEADER_SIZE = 22

OPERATION_ACTION = 0
OPERATION_QUERY = 1
OPERATION_UPDATE = 2
OPERATION_IN_STREAM = 3

SUBSCRIPTION_DATA_STREAM = 0
SUBSCRIPTION_DATA_WATCH = 1
SUBSCRIPTION_TOPIC = 2
SUBSCRIPTION_STREAM = 3
SUBSCRIPTION_DATA_IN_STREAM = 4

KIND_CLIENT_HELLO = 1
KIND_SERVER_HELLO = 2
KIND_ERROR = 3
KIND_REQUEST = 4
KIND_RESPONSE = 5
KIND_CANCEL = 6
KIND_RESPONSE_ACK = 7
KIND_SUBSCRIBE = 8
KIND_UNSUBSCRIBE = 9
KIND_CREDIT = 10
KIND_DATA = 11
KIND_KEEPALIVE = 12
KIND_SESSION_RESET = 13
KIND_INTROSPECTION = 14
KIND_IN_STREAM_CLOSED = 15
KIND_PING = 16
KIND_PONG = 17

PING_REQUEST_SIZE = 16
PING_RESPONSE_SIZE = 32

IN_STREAM_CLOSED = 0
IN_STREAM_REPLACED = 1
IN_STREAM_DISCONNECT = 2
IN_STREAM_RESET = 3
IN_STREAM_FAULT = 4
IN_STREAM_CONFIGURATION_FAILED = 5

FLAG_FRAGMENTED = 1 << 0
FLAG_FINAL = 1 << 1
FLAG_PACKED_PAYLOAD = 1 << 2
FLAG_ERROR_PAYLOAD = 1 << 3


class FrameError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class PingRequest:
    nonce: int
    host_monotonic_ns: int

    def encode(self) -> bytes:
        return struct.pack("<QQ", self.nonce, self.host_monotonic_ns)


@dataclass(frozen=True, slots=True)
class PingResponse:
    nonce: int
    host_monotonic_ns: int
    remote_receive_us: int
    remote_send_us: int

    @classmethod
    def decode(cls, payload: bytes) -> "PingResponse":
        if len(payload) != PING_RESPONSE_SIZE:
            raise FrameError("invalid ping response")
        return cls(*struct.unpack("<QQQQ", payload))


@dataclass(frozen=True, slots=True)
class IntrospectionSummary:
    schemas: int
    data: int
    actions: int
    topics: int
    streams: int
    links: int
    maximum_frame_bytes: int
    maximum_message_bytes: int

    @classmethod
    def decode(cls, payload: bytes) -> "IntrospectionSummary":
        if (
            len(payload) != INTROSPECTION_SUMMARY_SIZE
            or payload[0] != 1
            or payload[1] != PROTOCOL_MAJOR
        ):
            raise FrameError("invalid introspection summary")
        counts = struct.unpack_from("<6H", payload, 4)
        maximum_frame, maximum_message = struct.unpack_from("<II", payload, 16)
        return cls(*counts, maximum_frame, maximum_message)


@dataclass(frozen=True, slots=True)
class ServerInformation:
    maximum_frame_bytes: int
    maximum_message_bytes: int
    build_id: int
    manifest_digest: bytes
    manifest_size: int
    feature_flags: int

    @classmethod
    def decode(cls, payload: bytes) -> "ServerInformation":
        if len(payload) != 56 or payload[0] != 1 or payload[1] != PROTOCOL_MAJOR:
            raise FrameError("invalid server information")
        maximum_frame, maximum_message = struct.unpack_from("<II", payload, 4)
        build_id = struct.unpack_from("<Q", payload, 12)[0]
        manifest_size = struct.unpack_from("<I", payload, 52)[0]
        return cls(
            maximum_frame,
            maximum_message,
            build_id,
            payload[20:52],
            manifest_size,
            payload[3],
        )


@dataclass(frozen=True, slots=True)
class ManifestChunk:
    offset: int
    total: int
    data: bytes

    @classmethod
    def decode(cls, payload: bytes) -> "ManifestChunk":
        if len(payload) < 8:
            raise FrameError("invalid manifest chunk")
        offset, total = struct.unpack_from("<II", payload)
        if offset > total or offset + len(payload) - 8 > total:
            raise FrameError("manifest chunk exceeds advertised image")
        return cls(offset, total, payload[8:])


def encode_manifest_request(offset: int, limit: int) -> bytes:
    if not 0 <= offset <= 0xFFFFFFFF or not 0 < limit <= 0xFFFF:
        raise ValueError("manifest request is out of range")
    return struct.pack("<IHH", offset, limit, 0)


@dataclass(frozen=True, slots=True)
class CollectionDescriptor:
    local_id: int
    stable_id: int
    version: int
    subsystem: int
    capabilities: int
    consistency_modes: int
    synchronization: int
    context: int
    cost: int
    maximum_page: int
    record_size: int
    query_size: int
    may_block: bool
    expensive: bool
    values_may_be_stale: bool
    name: str


@dataclass(frozen=True, slots=True)
class CollectionPage:
    total: int
    next: int
    has_more: bool
    collections: tuple[CollectionDescriptor, ...]

    @classmethod
    def decode(cls, payload: bytes) -> "CollectionPage":
        if len(payload) < COLLECTION_PAGE_HEADER_SIZE or payload[0] != 1:
            raise FrameError("invalid collection page")
        count = payload[1]
        total, next_offset = struct.unpack_from("<HH", payload, 2)
        offset = COLLECTION_PAGE_HEADER_SIZE
        records: list[CollectionDescriptor] = []
        for _ in range(count):
            if offset + COLLECTION_DESCRIPTOR_HEADER_SIZE > len(payload):
                raise FrameError("truncated collection descriptor")
            local_id, stable_id, version = struct.unpack_from("<HIH", payload, offset)
            subsystem, capabilities, consistency, synchronization, context, cost = (
                payload[offset + index] for index in range(8, 14)
            )
            maximum_page, record_size, query_size = struct.unpack_from(
                "<HHH", payload, offset + 14
            )
            flags = payload[offset + 20]
            name_size = payload[offset + 21]
            name_begin = offset + COLLECTION_DESCRIPTOR_HEADER_SIZE
            name_end = name_begin + name_size
            if name_end > len(payload):
                raise FrameError("truncated collection name")
            try:
                name = payload[name_begin:name_end].decode("utf-8")
            except UnicodeDecodeError as error:
                raise FrameError("invalid collection name") from error
            records.append(
                CollectionDescriptor(
                    local_id,
                    stable_id,
                    version,
                    subsystem,
                    capabilities,
                    consistency,
                    synchronization,
                    context,
                    cost,
                    maximum_page,
                    record_size,
                    query_size,
                    bool(flags & 1),
                    bool(flags & 2),
                    bool(flags & 4),
                    name,
                )
            )
            offset = name_end
        if offset != len(payload):
            raise FrameError("trailing collection page data")
        return cls(total, next_offset, bool(payload[6]), tuple(records))


@dataclass(frozen=True, slots=True)
class CollectionQueryPage:
    stable_id: int
    written: int
    next: int
    has_more: bool
    revision: int
    consistency: int
    freshness: int
    loss_count: int
    loss_known: bool
    records: tuple[object, ...]

    @classmethod
    def decode(cls, payload: bytes) -> "CollectionQueryPage":
        try:
            value = cbor2.loads(payload)
        except (cbor2.CBORDecodeError, ValueError) as error:
            raise FrameError("invalid collection query page") from error
        if not isinstance(value, dict) or any(key not in value for key in range(10)):
            raise FrameError("invalid collection query page")
        records = value[8]
        if not isinstance(records, list) or value[1] != len(records):
            raise FrameError("invalid collection query records")
        scalar_fields = (
            value[0],
            value[1],
            value[2],
            value[4],
            value[5],
            value[6],
            value[7],
        )
        if any(
            not isinstance(field, int) or isinstance(field, bool)
            for field in scalar_fields
        ):
            raise FrameError("invalid collection query metadata")
        if not isinstance(value[3], bool) or not isinstance(value[9], bool):
            raise FrameError("invalid collection query flags")
        return cls(
            value[0],
            value[1],
            value[2],
            value[3],
            value[4],
            value[5],
            value[6],
            value[7],
            value[9],
            tuple(records),
        )


def encode_collection_request(offset: int = 0, limit: int = 8) -> bytes:
    if not 0 <= offset <= 0xFFFF or not 1 <= limit <= 0xFFFF:
        raise FrameError("invalid collection page request")
    return struct.pack("<HH", offset, limit)


def encode_collection_query_request(
    stable_id: int,
    offset: int = 0,
    revision: int = 0,
    limit: int = 8,
) -> bytes:
    if not 1 <= stable_id <= 0xFFFFFFFF:
        raise FrameError("invalid collection stable ID")
    if not 0 <= offset <= 0xFFFFFFFF or not 0 <= revision <= 0xFFFFFFFF:
        raise FrameError("invalid collection cursor")
    if not 1 <= limit <= 0xFFFF:
        raise FrameError("invalid collection query limit")
    return struct.pack("<IIIH", stable_id, offset, revision, limit)


@dataclass(frozen=True, slots=True)
class Envelope:
    kind: int
    flags: int = 0
    session_epoch: int = 0
    frame_sequence: int = 0
    target: int = 0
    request_id: int = 0
    payload_size: int = 0
    fragment_id: int = 0
    fragment_index: int = 0
    fragment_count: int = 1
    reserved: int = 0
    major: int = PROTOCOL_MAJOR
    minor: int = PROTOCOL_MINOR

    @property
    def operation(self) -> int:
        return self.reserved & 0xFF

    def with_operation(self, operation: int) -> "Envelope":
        if not 0 <= operation <= 0xFF:
            raise FrameError("invalid operation")
        return replace(self, reserved=(self.reserved & 0xFFFFFF00) | operation)

    @property
    def subscription(self) -> int:
        return self.reserved & 0xFF

    def with_subscription(self, subscription: int) -> "Envelope":
        if not 0 <= subscription <= 0xFF:
            raise FrameError("invalid subscription kind")
        return replace(self, reserved=(self.reserved & 0xFFFFFF00) | subscription)

    def encode(self) -> bytes:
        if self.major != PROTOCOL_MAJOR or not self.fragment_count:
            raise FrameError("unsupported envelope")
        if self.fragment_index >= self.fragment_count:
            raise FrameError("invalid fragment coordinates")
        return struct.pack(
            "<BBBBHIIIIHHBBI",
            self.major,
            self.minor,
            self.kind,
            self.flags,
            ENVELOPE_SIZE,
            self.session_epoch,
            self.frame_sequence,
            self.target,
            self.request_id,
            self.payload_size,
            self.fragment_id,
            self.fragment_index,
            self.fragment_count,
            self.reserved,
        )

    @classmethod
    def decode(cls, data: bytes) -> "Envelope":
        if len(data) < ENVELOPE_SIZE:
            raise FrameError("short envelope")
        values = struct.unpack("<BBBBHIIIIHHBBI", data[:ENVELOPE_SIZE])
        if values[0] != PROTOCOL_MAJOR or values[4] != ENVELOPE_SIZE:
            raise FrameError("unsupported protocol version")
        return cls(
            major=values[0],
            minor=values[1],
            kind=values[2],
            flags=values[3],
            session_epoch=values[5],
            frame_sequence=values[6],
            target=values[7],
            request_id=values[8],
            payload_size=values[9],
            fragment_id=values[10],
            fragment_index=values[11],
            fragment_count=values[12],
            reserved=values[13],
        )


@dataclass(frozen=True, slots=True)
class SubscriptionRequest:
    minimum_interval_us: int = 0
    batch_size: int = 0
    codec: int = 0
    flags: int = 0

    def encode(self) -> bytes:
        return struct.pack(
            "<IHBB", self.minimum_interval_us, self.batch_size, self.codec, self.flags
        )

    @classmethod
    def decode(cls, data: bytes) -> "SubscriptionRequest":
        if len(data) != SUBSCRIPTION_POLICY_SIZE:
            raise FrameError("invalid subscription request size")
        return cls(*struct.unpack("<IHBB", data))


@dataclass(frozen=True, slots=True)
class SubscriptionPolicy:
    minimum_interval_us: int
    batch_size: int
    codec: int
    flags: int = 0

    def encode(self) -> bytes:
        return struct.pack(
            "<IHBB", self.minimum_interval_us, self.batch_size, self.codec, self.flags
        )

    @classmethod
    def decode(cls, data: bytes) -> "SubscriptionPolicy":
        if len(data) != SUBSCRIPTION_POLICY_SIZE:
            raise FrameError("invalid subscription policy size")
        policy = cls(*struct.unpack("<IHBB", data))
        if policy.codec not in (1, 2):
            raise FrameError("invalid subscription codec")
        return policy


@dataclass(frozen=True, slots=True)
class CreditGrant:
    token: int
    credits: int
    window: int

    def encode(self) -> bytes:
        if (
            not 1 <= self.token <= 0xFFFFFFFF
            or not 0 <= self.credits <= 0xFFFF
            or not 0 <= self.window <= 0xFFFF
        ):
            raise FrameError("invalid credit grant")
        return struct.pack("<IHH", self.token, self.credits, self.window)

    @classmethod
    def decode(cls, data: bytes) -> "CreditGrant":
        if len(data) != CREDIT_GRANT_SIZE:
            raise FrameError("invalid credit grant size")
        grant = cls(*struct.unpack("<IHH", data))
        if not grant.token:
            raise FrameError("invalid credit token")
        return grant


@dataclass(frozen=True, slots=True)
class InStreamOpenResponse:
    policy: SubscriptionPolicy
    token: int

    def encode(self) -> bytes:
        if not 1 <= self.token <= 0xFFFFFFFF:
            raise FrameError("invalid inbound-stream token")
        return self.policy.encode() + struct.pack("<I", self.token)

    @classmethod
    def decode(cls, data: bytes) -> "InStreamOpenResponse":
        if len(data) != IN_STREAM_OPEN_RESPONSE_SIZE:
            raise FrameError("invalid inbound-stream open response size")
        token = struct.unpack_from("<I", data, SUBSCRIPTION_POLICY_SIZE)[0]
        if not token:
            raise FrameError("invalid inbound-stream token")
        return cls(
            SubscriptionPolicy.decode(data[:SUBSCRIPTION_POLICY_SIZE]),
            token,
        )


@dataclass(frozen=True, slots=True)
class InStreamCloseRequest:
    token: int

    def encode(self) -> bytes:
        if not 1 <= self.token <= 0xFFFFFFFF:
            raise FrameError("invalid inbound-stream token")
        return struct.pack("<I", self.token)

    @classmethod
    def decode(cls, data: bytes) -> "InStreamCloseRequest":
        if len(data) != IN_STREAM_CLOSE_REQUEST_SIZE:
            raise FrameError("invalid inbound-stream close request size")
        request = cls(struct.unpack("<I", data)[0])
        if not request.token:
            raise FrameError("invalid inbound-stream token")
        return request


@dataclass(frozen=True, slots=True)
class InStreamClosed:
    token: int
    reason: int

    def encode(self) -> bytes:
        if not 1 <= self.token <= 0xFFFFFFFF or not 0 <= self.reason <= 5:
            raise FrameError("invalid inbound-stream closure")
        return struct.pack("<IB3x", self.token, self.reason)

    @classmethod
    def decode(cls, data: bytes) -> "InStreamClosed":
        if len(data) != IN_STREAM_CLOSED_SIZE or data[5:] != b"\0\0\0":
            raise FrameError("invalid inbound-stream closure size")
        closed = cls(*struct.unpack_from("<IB", data))
        if not closed.token or not 0 <= closed.reason <= 5:
            raise FrameError("invalid inbound-stream closure")
        return closed


def encode_batch(entries: list[bytes], codec: int) -> bytes:
    if codec not in (1, 2) or len(entries) > 0xFFFF:
        raise FrameError("invalid batch metadata")
    output = bytearray(struct.pack("<HBB", len(entries), codec, BATCH_VERSION))
    for entry in entries:
        if len(entry) > 0xFFFF:
            raise FrameError("batch entry too large")
        output.extend(struct.pack("<H", len(entry)))
        output.extend(entry)
    return bytes(output)


def decode_batch(data: bytes) -> tuple[int, list[bytes]]:
    if len(data) < BATCH_HEADER_SIZE:
        raise FrameError("short batch")
    count, codec, version = struct.unpack("<HBB", data[:BATCH_HEADER_SIZE])
    if codec not in (1, 2) or version != BATCH_VERSION:
        raise FrameError("unsupported batch")
    entries: list[bytes] = []
    offset = BATCH_HEADER_SIZE
    for _ in range(count):
        if offset + 2 > len(data):
            raise FrameError("truncated batch length")
        size = struct.unpack("<H", data[offset : offset + 2])[0]
        offset += 2
        if offset + size > len(data):
            raise FrameError("truncated batch entry")
        entries.append(data[offset : offset + size])
        offset += size
    if offset != len(data):
        raise FrameError("trailing batch data")
    return codec, entries


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1))
    return (~crc) & 0xFFFFFFFF


def cobs_encode(data: bytes) -> bytes:
    output = bytearray(b"\0")
    code_index = 0
    code = 1
    for value in data:
        if value == 0:
            output[code_index] = code
            code_index = len(output)
            output.append(0)
            code = 1
        else:
            output.append(value)
            code += 1
            if code == 0xFF:
                output[code_index] = code
                code_index = len(output)
                output.append(0)
                code = 1
    output[code_index] = code
    output.append(0)
    return bytes(output)


def cobs_decode(data: bytes) -> bytes:
    if data.endswith(b"\0"):
        data = data[:-1]
    output = bytearray()
    index = 0
    while index < len(data):
        code = data[index]
        index += 1
        if not code or index + code - 1 > len(data):
            raise FrameError("malformed COBS frame")
        output.extend(data[index : index + code - 1])
        index += code - 1
        if code != 0xFF and index < len(data):
            output.append(0)
    return bytes(output)


def encode_frame(envelope: Envelope, payload: bytes) -> bytes:
    if len(payload) > 0xFFFF:
        raise FrameError("payload too large")
    envelope = replace(envelope, payload_size=len(payload))
    content = envelope.encode() + payload
    return cobs_encode(content + struct.pack("<I", crc32c(content)))


def decode_frame(frame: bytes) -> tuple[Envelope, bytes]:
    decoded = cobs_decode(frame)
    if len(decoded) < ENVELOPE_SIZE + 4:
        raise FrameError("short frame")
    content, checksum = decoded[:-4], struct.unpack("<I", decoded[-4:])[0]
    if crc32c(content) != checksum:
        raise FrameError("CRC32C integrity failure")
    envelope = Envelope.decode(content)
    payload = content[ENVELOPE_SIZE:]
    if len(payload) != envelope.payload_size:
        raise FrameError("payload length mismatch")
    return envelope, payload


class FrameDecoder:
    """Caller-owned zero-delimited stream parser with bounded resynchronization."""

    def __init__(self, maximum_encoded_size: int):
        if maximum_encoded_size <= 0:
            raise ValueError("maximum_encoded_size must be positive")
        self.maximum_encoded_size = maximum_encoded_size
        self._pending = bytearray()
        self._dropping = False
        self.rejected = 0
        self.overflowed = 0

    def feed(self, data: bytes) -> list[tuple[Envelope, bytes]]:
        accepted: list[tuple[Envelope, bytes]] = []
        for value in data:
            if value == 0:
                if self._dropping:
                    self._dropping = False
                    self._pending.clear()
                    self.overflowed += 1
                elif self._pending:
                    try:
                        accepted.append(decode_frame(bytes(self._pending)))
                    except FrameError:
                        self.rejected += 1
                    self._pending.clear()
                continue
            if self._dropping:
                continue
            if len(self._pending) == self.maximum_encoded_size:
                self._pending.clear()
                self._dropping = True
            else:
                self._pending.append(value)
        return accepted
