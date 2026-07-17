# Future Idea: Deterministic Coroutine Runtime

Status: deferred exploration

This document preserves the initial direction for a possible Solar coroutine
runtime. It is not an accepted design specification, implementation plan, or
commitment to add coroutines to Solar.

Solar has only just completed its first coherent static-system implementation.
A coroutine runtime would add a substantial new execution model, resource
model, and body of safety work. It should be reconsidered only after the
existing kernel, execution, lifecycle, hardware, and Remote systems have seen
meaningful use on physical targets.

## Motivation

C++20 coroutines could make some naturally asynchronous embedded workflows
considerably easier to express:

- waiting for hardware initialization without occupying a thread;
- sequencing asynchronous UART, SPI, I2C, or Remote operations;
- applying bounded timeouts to protocol exchanges;
- composing delays, signals, and cancellation;
- expressing state machines as direct control flow;
- awaiting completion without exposing callbacks throughout application code.

Illustrative, non-normative usage might eventually resemble:

```cpp
solar::async::Task<solar::Result<Reading, ImuError>> initialize_imu()
{
    co_await solar::async::sleep_for(20ms);

    auto identity = co_await ImuBus::read_register(WhoAmI);
    if (!identity) {
        co_return solar::fail(identity.error());
    }

    co_return co_await Imu::configure(*identity);
}
```

The value is structured asynchronous composition, not merely making ordinary
functions syntactically asynchronous.

## Proposed Architectural Position

The likely public subsystem is `solar::async`.

It should be a sibling of `solar::kernel`, with integration into
`solar::execution`:

- `solar::kernel` continues to provide direct C++ wrappers over Zephyr kernel
  primitives;
- `solar::async` owns coroutine tasks, awaiters, cancellation, frame storage,
  and continuation scheduling semantics;
- `solar::execution` supplies or selects executors on which resumptions run;
- hardware and Remote expose awaitable operations without owning a general
  coroutine scheduler;
- lifecycle may launch an explicitly declared root task, but lifecycle hooks
  should remain synchronous in the first coroutine iteration.

The runtime must not become a hidden second system, implicit global thread, or
replacement for Zephyr scheduling.

## Non-Negotiable Embedded Constraints

Any accepted design should provide:

- no unbounded dynamic allocation;
- statically sized or explicitly supplied coroutine-frame storage;
- deterministic admission failure when frame capacity is exhausted;
- explicit executor ownership;
- bounded queues and continuation counts;
- cooperative cancellation and structured task ownership;
- defined destruction behavior for suspended tasks;
- timeout and stop-token integration;
- ISR-safe completion ingress that schedules, rather than directly performs,
  arbitrary coroutine resumption;
- observable resource use and failure;
- no dependence on C++ exceptions for ordinary error propagation;
- compatibility with Solar's `Result<T, E>` convention.

The opaque size and allocation behavior of compiler-generated coroutine frames
is the principal technical risk. Solar must prove how frames are allocated,
bounded, aligned, rejected, and reclaimed on every supported toolchain.

## Potential Surface

Names here are exploratory:

```cpp
solar::async::Task<T>
solar::async::DetachedTask
solar::async::FramePool<Bytes, Count>
solar::async::sleep_for(duration)
solar::async::sleep_until(deadline)
solar::async::schedule_on<Executor>()
solar::async::with_timeout(operation, timeout)
solar::async::when_all(...)
solar::async::when_any(...)
solar::async::yield()
```

Likely integrations include:

```cpp
co_await Uart::receive(buffer, timeout);
co_await RemoteLink::request<Request>(payload, timeout);
co_await signal.wait(stop_token);
```

Every awaitable must document:

- where completion originates;
- where continuation resumption occurs;
- whether it is cancellable;
- what storage it owns or borrows;
- what happens on timeout, shutdown, and executor stop;
- whether completion can race cancellation;
- its ISR contract.

## Initial Scope If Revisited

The first experiment should be intentionally narrow:

1. one statically bounded frame pool;
2. one executor adapter over an existing Solar executor or Zephyr work queue;
3. `Task<Result<T, E>>` with move-only structured ownership;
4. sleep, signal, cancellation, and timeout awaiters;
5. one fake asynchronous device operation;
6. one Remote request-response operation;
7. native and physical-target resource measurements.

It should not initially add asynchronous lifecycle hooks, a hidden default
executor, detached fire-and-forget work as the normal path, or broad combinator
machinery.

## Questions To Resolve Later

- Can supported compilers expose or reliably constrain frame size before
  allocation?
- Should pools be selected per task type, executor, subsystem, or application?
- How are child tasks cancelled and joined during component stop?
- Which resumption operations are legal from ISR context?
- How does priority flow through a suspended operation?
- How are dead executor, stale completion, and double completion represented?
- Can task ownership remain obvious when tasks are contributed through the
  system graph?
- What diagnostics are needed for leaked, blocked, or pool-starved tasks?

The design should proceed only when those questions can be answered with
toolchain evidence and measured target behavior.
