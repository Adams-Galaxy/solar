"""Runtime primitives shared by generated Solar application clients."""

from __future__ import annotations

from collections.abc import AsyncIterator, Mapping
from contextlib import AbstractAsyncContextManager
from dataclasses import asdict, is_dataclass
from enum import Enum
import itertools
import time
from typing import Any, Generic, TypeVar

from .frames import Frame


class InterfacePolicy(Enum):
    EXACT = "exact"
    COMPATIBLE = "compatible"


class BuildPolicy(Enum):
    ANY = "any"
    EXACT = "exact"


class InterfaceMismatch(RuntimeError):
    """A generated application contract cannot bind to connected firmware."""

    def __init__(self, differences: list[str]):
        self.differences = tuple(differences)
        super().__init__(
            "generated client cannot bind to firmware:\n  - "
            + "\n  - ".join(differences)
        )


def _requirements_by_name(requirements: Mapping[str, Any], collection: str):
    return {item["name"]: item for item in requirements.get(collection, ())}


def validate_generated_interface(
    session: Any,
    *,
    expected_digest: bytes,
    requirements: Mapping[str, Any],
    interface_policy: InterfacePolicy = InterfacePolicy.EXACT,
    expected_build: int | None = None,
    build_policy: BuildPolicy = BuildPolicy.ANY,
) -> None:
    """Validate protocol, effective interface, and optional build identity."""

    manifest = getattr(session, "manifest", None)
    if manifest is None:
        raise InterfaceMismatch(["the active session has no Remote manifest"])
    differences: list[str] = []
    protocol = tuple(requirements.get("protocol", ()))
    if protocol and tuple(manifest.protocol) != protocol:
        differences.append(
            f"protocol {tuple(manifest.protocol)} does not match required {protocol}"
        )
    if interface_policy is InterfacePolicy.EXACT:
        if manifest.digest != expected_digest:
            differences.append(
                f"interface digest {manifest.digest.hex()} does not match "
                f"{expected_digest.hex()}"
            )
    else:
        for collection in ("schemas", "data", "actions", "streams"):
            actual = {item["name"]: item for item in getattr(manifest, collection)}
            for name, required in _requirements_by_name(
                requirements, collection
            ).items():
                item = actual.get(name)
                if item is None:
                    differences.append(f"required {collection[:-1]} {name!r} is absent")
                    continue
                if int(item["id"]) != int(required["id"]):
                    differences.append(f"{collection[:-1]} {name!r} changed stable ID")
                for key in (
                    "schema",
                    "request_schema",
                    "response_schema",
                    "error_schema",
                    "shape",
                ):
                    if key in required and item.get(key) != required.get(key):
                        differences.append(f"{collection[:-1]} {name!r} changed {key}")
        actual_capabilities = {
            (item["domain"], int(item["endpoint"]), item["kind"])
            for item in manifest.capabilities
        }
        for capability in requirements.get("capabilities", ()):
            key = (
                capability["domain"],
                int(capability["endpoint"]),
                capability["kind"],
            )
            if key not in actual_capabilities:
                differences.append(
                    f"required capability {key[0]}.{key[2]} for 0x{key[1]:08X} is absent"
                )
    if build_policy is BuildPolicy.EXACT:
        information = getattr(session, "server_information", None)
        actual_build = getattr(information, "build_id", None)
        if expected_build is None:
            differences.append("generated client has no exact build identity")
        elif actual_build != expected_build:
            differences.append(
                f"build identity {actual_build!r} does not match {expected_build!r}"
            )
    if differences:
        raise InterfaceMismatch(differences)


def coerce_model(model: type[Any], value: Any) -> Any:
    if isinstance(value, model):
        return value
    if is_dataclass(value):
        value = asdict(value)
    if isinstance(value, Mapping):
        return model(**value)
    return model(value)


T = TypeVar("T")


class GeneratedSubscription(
    AbstractAsyncContextManager["GeneratedSubscription[T]"],
    AsyncIterator[Frame[T]],
    Generic[T],
):
    def __init__(self, opener: Any, model: type[T], descriptor: Any):
        self._opener = opener
        self._model = model
        self._descriptor = descriptor
        self._subscription: Any = None
        self._sequence = itertools.count(1)

    async def __aenter__(self) -> "GeneratedSubscription[T]":
        self._subscription = await self._opener
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.aclose()

    def __aiter__(self) -> "GeneratedSubscription[T]":
        return self

    async def __anext__(self) -> Frame[T]:
        if self._subscription is None:
            raise RuntimeError("subscription must be entered before iteration")
        value = await self._subscription.__anext__()
        if isinstance(value, Frame):
            return Frame(
                value=coerce_model(self._model, value.value),
                endpoint=value.endpoint,
                sequence=value.sequence,
                received_monotonic_ns=value.received_monotonic_ns,
                received_wall_ns=value.received_wall_ns,
                session_id=value.session_id,
                loss_count=value.loss_count,
            )
        return Frame(
            value=coerce_model(self._model, value),
            endpoint=self._descriptor,
            sequence=next(self._sequence),
            received_monotonic_ns=time.monotonic_ns(),
            received_wall_ns=time.time_ns(),
            session_id=str(
                getattr(
                    getattr(self._subscription, "session", None), "session_epoch", 0
                )
            ),
        )

    async def aclose(self) -> None:
        if self._subscription is not None:
            await self._subscription.aclose()
            self._subscription = None


class GeneratedParameter(Generic[T]):
    def __init__(self, session: Any, endpoint: str, wrapper: type[Any]):
        self.session = session
        self.endpoint = endpoint
        self.wrapper = wrapper
        self.descriptor = session.manifest.require_catalog().data[endpoint]

    @property
    def id(self) -> int:
        return self.descriptor.id

    @property
    def supported(self) -> bool:
        return self.descriptor.supports("query")

    async def get(self) -> T:
        value = await self.session.get(self.descriptor.id)
        return getattr(value, "value")

    async def set(self, value: T) -> None:
        await self.session.set(self.descriptor.id, self.wrapper(value=value))


class GeneratedData(Generic[T]):
    def __init__(self, session: Any, endpoint: str, model: type[T]):
        self.session = session
        self.endpoint = endpoint
        self.model = model
        self.descriptor = session.manifest.require_catalog().data[endpoint]

    @property
    def id(self) -> int:
        return self.descriptor.id

    @property
    def readable(self) -> bool:
        return self.descriptor.supports("query")

    @property
    def writable(self) -> bool:
        return self.descriptor.supports("update")

    async def get(self) -> T:
        if not self.readable:
            raise ValueError(f"{self.endpoint!r} is not readable")
        return coerce_model(self.model, await self.session.get(self.descriptor.id))

    async def set(self, value: T | Mapping[str, Any]) -> None:
        if not self.writable:
            raise ValueError(f"{self.endpoint!r} is not writable")
        await self.session.set(self.descriptor.id, coerce_model(self.model, value))


RequestT = TypeVar("RequestT")
ResponseT = TypeVar("ResponseT")


class GeneratedAction(Generic[RequestT, ResponseT]):
    def __init__(
        self,
        session: Any,
        endpoint: str,
        request_model: type[RequestT],
        response_model: type[ResponseT],
    ):
        self.session = session
        self.endpoint = endpoint
        self.request_model = request_model
        self.response_model = response_model
        self.descriptor = session.manifest.require_catalog().actions[endpoint]

    @property
    def id(self) -> int:
        return self.descriptor.id

    async def __call__(
        self, request: RequestT | Mapping[str, Any] | None = None
    ) -> ResponseT:
        request_value = (
            self.request_model()
            if request is None
            else coerce_model(self.request_model, request)
        )
        response = await self.session.call(self.descriptor.id, request_value)
        return coerce_model(self.response_model, response)


class GeneratedOutputStream(Generic[T]):
    def __init__(self, session: Any, endpoint: str, model: type[T]):
        self.session = session
        self.endpoint = endpoint
        self.model = model
        catalog = session.manifest.require_catalog()
        self.descriptor = catalog.streams[endpoint]

    @property
    def id(self) -> int:
        return self.descriptor.id

    def subscribe(
        self,
        *,
        frequency: float | None = None,
        batch: int = 1,
        queue_depth: int = 32,
    ) -> GeneratedSubscription[T]:
        return GeneratedSubscription(
            self.session.stream(
                self.descriptor.id,
                frequency=frequency,
                batch=batch,
                queue_depth=queue_depth,
            ),
            self.model,
            self.descriptor,
        )


class GeneratedInputStream(Generic[T]):
    def __init__(self, session: Any, endpoint: str, model: type[T]):
        self.session = session
        self.endpoint = endpoint
        self.model = model
        self.descriptor = session.manifest.require_catalog().streams[endpoint]

    @property
    def id(self) -> int:
        return self.descriptor.id

    def open(
        self,
        *,
        frequency: float | None = None,
        credit_timeout: float | None = None,
    ) -> Any:
        return self.session.in_stream(
            self.descriptor.id,
            frequency=frequency,
            credit_timeout=credit_timeout,
        )
