"""Transport-independent Solar Remote host session runtime."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, replace
import struct
import time
from typing import Callable

from .protocol import (
    CreditGrant,
    Envelope,
    FLAG_FINAL,
    FLAG_FRAGMENTED,
    FrameDecoder,
    FrameError,
    KIND_CANCEL,
    KIND_CLIENT_HELLO,
    KIND_CREDIT,
    KIND_DATA,
    KIND_ERROR,
    KIND_KEEPALIVE,
    KIND_INTROSPECTION,
    KIND_REQUEST,
    KIND_RESPONSE,
    KIND_RESPONSE_ACK,
    KIND_SERVER_HELLO,
    KIND_SESSION_RESET,
    KIND_SUBSCRIBE,
    KIND_UNSUBSCRIBE,
    OPERATION_ACTION,
    PROTOCOL_MAJOR,
    PROTOCOL_MINOR,
    SubscriptionRequest,
    encode_frame,
)


@dataclass(frozen=True, slots=True)
class Message:
    envelope: Envelope
    payload: bytes


@dataclass(frozen=True, slots=True)
class Hello:
    major: int
    minor: int
    maximum_frame_bytes: int
    maximum_message_bytes: int
    capabilities: int

    @classmethod
    def decode(cls, payload: bytes) -> "Hello":
        if len(payload) != 16:
            raise FrameError("invalid hello size")
        major, minor = payload[0], payload[1]
        if major != PROTOCOL_MAJOR:
            raise FrameError("incompatible protocol major")
        maximum_frame, maximum_message, capabilities = struct.unpack_from("<III", payload, 4)
        return cls(major, minor, maximum_frame, maximum_message, capabilities)

    def encode(self) -> bytes:
        output = bytearray(16)
        output[0] = self.major
        output[1] = self.minor
        struct.pack_into(
            "<III", output, 4, self.maximum_frame_bytes,
            self.maximum_message_bytes, self.capabilities,
        )
        return bytes(output)


@dataclass(slots=True)
class _FragmentSlot:
    envelope: Envelope
    payload: bytearray
    next_index: int
    deadline: float


class Reassembler:
    """Bounded ordered logical-message reassembly for ordered Remote links."""

    def __init__(
        self,
        maximum_message_size: int,
        slots: int = 2,
        timeout: float = 1.0,
        clock: Callable[[], float] = time.monotonic,
    ):
        if maximum_message_size <= 0 or slots <= 0 or timeout <= 0:
            raise ValueError("reassembly bounds must be positive")
        self.maximum_message_size = maximum_message_size
        self.maximum_slots = slots
        self.timeout = timeout
        self.clock = clock
        self._slots: dict[int, _FragmentSlot] = {}
        self.rejected = 0
        self.expired = 0

    def reset(self) -> None:
        self._slots.clear()

    def expire(self) -> None:
        now = self.clock()
        expired = [key for key, slot in self._slots.items() if slot.deadline <= now]
        for key in expired:
            del self._slots[key]
            self.expired += 1

    def push(self, envelope: Envelope, payload: bytes) -> Message | None:
        self.expire()
        fragmented = bool(envelope.flags & FLAG_FRAGMENTED)
        if not fragmented and envelope.fragment_count == 1:
            return Message(envelope, payload)
        if (
            not fragmented or envelope.fragment_count <= 1 or not envelope.fragment_id
            or envelope.fragment_index >= envelope.fragment_count
        ):
            self.rejected += 1
            raise FrameError("invalid fragmented message")

        slot = self._slots.get(envelope.fragment_id)
        if envelope.fragment_index == 0:
            if slot is not None or len(self._slots) == self.maximum_slots:
                self.rejected += 1
                raise FrameError("reassembly capacity exhausted")
            slot = _FragmentSlot(envelope, bytearray(), 0, self.clock() + self.timeout)
            self._slots[envelope.fragment_id] = slot
        if slot is None:
            self.rejected += 1
            raise FrameError("fragment has no active message")

        first = slot.envelope
        stable = (
            envelope.kind == first.kind and envelope.session_epoch == first.session_epoch
            and envelope.target == first.target and envelope.request_id == first.request_id
            and envelope.reserved == first.reserved
            and envelope.fragment_count == first.fragment_count
        )
        is_last = envelope.fragment_index + 1 == envelope.fragment_count
        if (
            not stable or envelope.fragment_index != slot.next_index
            or bool(envelope.flags & FLAG_FINAL) != is_last
            or len(slot.payload) + len(payload) > self.maximum_message_size
        ):
            del self._slots[envelope.fragment_id]
            self.rejected += 1
            raise FrameError("fragment sequence rejected")

        slot.payload.extend(payload)
        slot.next_index += 1
        slot.deadline = self.clock() + self.timeout
        if not is_last:
            return None

        del self._slots[envelope.fragment_id]
        complete = replace(
            first,
            flags=(first.flags & ~FLAG_FRAGMENTED) | FLAG_FINAL,
            payload_size=len(slot.payload),
            fragment_id=0,
            fragment_index=0,
            fragment_count=1,
        )
        return Message(complete, bytes(slot.payload))


class Client:
    """One bounded Remote session with caller-owned transport I/O."""

    def __init__(
        self,
        maximum_frame_size: int = 1024,
        maximum_message_size: int = 4096,
        reassembly_slots: int = 2,
    ):
        self.decoder = FrameDecoder(maximum_frame_size + 16)
        self.reassembler = Reassembler(maximum_message_size, reassembly_slots)
        self.maximum_frame_size = maximum_frame_size
        self.maximum_message_size = maximum_message_size
        self.session_epoch = 0
        self.frame_sequence = 0
        self.request_id = 0
        self.fragment_id = 0
        self.active = False
        self._hello_sent = False
        self.server_hello: Hello | None = None
        self.credits: dict[int, CreditGrant] = {}
        self._outgoing: deque[bytes] = deque()

    def _next_frame(self) -> int:
        self.frame_sequence = (self.frame_sequence + 1) & 0xFFFFFFFF
        return self.frame_sequence

    def _next_request(self) -> int:
        self.request_id = (self.request_id + 1) & 0xFFFFFFFF
        if self.request_id == 0:
            raise FrameError("request ID space exhausted; reset the session")
        return self.request_id

    def _next_fragment(self) -> int:
        self.fragment_id = (self.fragment_id + 1) & 0xFFFF
        if self.fragment_id == 0:
            self.fragment_id = 1
        return self.fragment_id

    def _queue(self, envelope: Envelope, payload: bytes = b"") -> None:
        if len(payload) > self.maximum_message_size:
            raise FrameError("logical message exceeds negotiated maximum")
        maximum_payload = self.maximum_frame_size - 32 - 4
        if maximum_payload <= 0:
            raise FrameError("negotiated frame size cannot carry a payload")
        if len(payload) <= maximum_payload:
            self._outgoing.append(encode_frame(envelope, payload))
            return
        count = (len(payload) + maximum_payload - 1) // maximum_payload
        if count > 0xFF:
            raise FrameError("logical message requires too many fragments")
        fragment = self._next_fragment()
        for index in range(count):
            start = index * maximum_payload
            part = payload[start:start + maximum_payload]
            flags = envelope.flags | FLAG_FRAGMENTED
            if index + 1 == count:
                flags |= FLAG_FINAL
            physical = replace(
                envelope,
                flags=flags,
                frame_sequence=envelope.frame_sequence if index == 0 else self._next_frame(),
                fragment_id=fragment,
                fragment_index=index,
                fragment_count=count,
            )
            self._outgoing.append(encode_frame(physical, part))

    def take_outgoing(self) -> bytes | None:
        return self._outgoing.popleft() if self._outgoing else None

    def feed(self, data: bytes) -> list[Message]:
        accepted: list[Message] = []
        for envelope, payload in self.decoder.feed(data):
            message = self.reassembler.push(envelope, payload)
            if message is None:
                continue
            if message.envelope.kind == KIND_SERVER_HELLO:
                hello = Hello.decode(message.payload)
                self.server_hello = hello
                self.maximum_frame_size = min(self.maximum_frame_size, hello.maximum_frame_bytes)
                self.maximum_message_size = min(
                    self.maximum_message_size, hello.maximum_message_bytes
                )
                self.session_epoch = message.envelope.session_epoch
                if not self._hello_sent:
                    client_hello = Hello(
                        PROTOCOL_MAJOR, min(PROTOCOL_MINOR, hello.minor),
                        self.maximum_frame_size, self.maximum_message_size,
                        hello.capabilities,
                    )
                    self._queue(
                        Envelope(kind=KIND_CLIENT_HELLO, frame_sequence=self._next_frame()),
                        client_hello.encode(),
                    )
                    self._hello_sent = True
                else:
                    self.active = True
                accepted.append(message)
            elif message.envelope.kind == KIND_SESSION_RESET:
                self.active = False
                self._hello_sent = False
                self.session_epoch = message.envelope.session_epoch
                self.request_id = 0
                self.credits.clear()
                self.reassembler.reset()
                accepted.append(message)
            elif message.envelope.kind == KIND_CREDIT:
                grant = CreditGrant.decode(message.payload)
                current = self.credits.get(message.envelope.target, CreditGrant(0, grant.window))
                self.credits[message.envelope.target] = CreditGrant(
                    min(0xFFFF, current.credits + grant.credits), grant.window
                )
                accepted.append(message)
            else:
                accepted.append(message)
        return accepted

    def request(
        self,
        target: int,
        payload: bytes = b"",
        operation: int = OPERATION_ACTION,
    ) -> int:
        if not self.active:
            raise FrameError("session is not active")
        request = self._next_request()
        envelope = Envelope(
            kind=KIND_REQUEST,
            session_epoch=self.session_epoch,
            frame_sequence=self._next_frame(),
            target=target,
            request_id=request,
        ).with_operation(operation)
        self._queue(envelope, payload)
        return request

    def subscribe(
        self,
        target: int,
        subscription: int,
        policy: SubscriptionRequest = SubscriptionRequest(),
    ) -> int:
        request = self._next_request()
        envelope = Envelope(
            kind=KIND_SUBSCRIBE,
            session_epoch=self.session_epoch,
            frame_sequence=self._next_frame(),
            target=target,
            request_id=request,
        ).with_subscription(subscription)
        self._queue(envelope, policy.encode())
        return request

    def unsubscribe(self, target: int, subscription: int) -> int:
        request = self._next_request()
        envelope = Envelope(
            kind=KIND_UNSUBSCRIBE,
            session_epoch=self.session_epoch,
            frame_sequence=self._next_frame(),
            target=target,
            request_id=request,
        ).with_subscription(subscription)
        self._queue(envelope)
        return request

    def acknowledge(self, response: Message | int) -> None:
        request = response if isinstance(response, int) else response.envelope.request_id
        self._queue(Envelope(
            kind=KIND_RESPONSE_ACK,
            session_epoch=self.session_epoch,
            frame_sequence=self._next_frame(),
            request_id=request,
        ))

    def cancel(self, request: int) -> None:
        self._queue(Envelope(
            kind=KIND_CANCEL,
            session_epoch=self.session_epoch,
            frame_sequence=self._next_frame(),
            request_id=request,
        ))

    def keepalive(self, payload: bytes = b"") -> None:
        self._queue(Envelope(
            kind=KIND_KEEPALIVE,
            session_epoch=self.session_epoch,
            frame_sequence=self._next_frame(),
        ), payload)

    def introspect(self, target: int = 0, payload: bytes = b"") -> int:
        if not self.active:
            raise FrameError("session is not active")
        request = self._next_request()
        self._queue(Envelope(
            kind=KIND_INTROSPECTION,
            session_epoch=self.session_epoch,
            frame_sequence=self._next_frame(),
            target=target,
            request_id=request,
        ), payload)
        return request

    def send_stream(self, target: int, payload: bytes, sequence: int) -> None:
        grant = self.credits.get(target)
        if grant is None or grant.credits == 0:
            raise FrameError("inbound stream has no credit")
        self.credits[target] = CreditGrant(grant.credits - 1, grant.window)
        self._queue(Envelope(
            kind=KIND_DATA,
            session_epoch=self.session_epoch,
            frame_sequence=self._next_frame(),
            target=target,
            request_id=sequence,
        ), payload)
