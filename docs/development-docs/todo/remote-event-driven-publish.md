# TODO: Event-Driven (Push) Publish From Services

Date: 2026-08-07

Status: deferred -- problem defined only, nothing designed or implemented.
Intentionally not started yet: `remote-output-stream-scheduler.md`'s
poll-based scheduler shipped first to unblock a separate, already-in-progress
implementation pass (`remote-arrays-and-chunked-streaming.md` Stage 6, chunked
`navigation.lidar` streaming). This document exists so that work can be picked
back up later without re-deriving the problem from scratch.

Depends on / related:

- `remote-output-stream-scheduler.md` (implemented -- the poll-based
  producer path this document proposes eventually complementing or replacing
  for latency-sensitive producers)
- `remote-arrays-and-chunked-streaming.md` §6.3 (the reliable-mode credit
  machinery this would reuse, same as the poll scheduler already does)

## Context hook

While implementing the poll-based output-stream scheduler
(`poll_output_streams`, `include/solar/remote/runtime.hpp:1732`), the user
asked directly: why is this polling, could it be event-driven instead, and
what would that change? The honest answer is that polling was not chosen for
efficiency or simplicity -- it was the *only reachable* option given a real
structural constraint in Solar's compile-time layering. That constraint,
and what solving it would take, is the subject of this document. The user
wants to address it, but explicitly after the currently-blocked session's
Stage 6 work resumes and lands.

## Problem statement

**No code running as part of a service (a device driver ISR, a work-queue
callback, any service method) can push a value onto the wire the instant it
becomes ready.** Every device-to-station data flow a service can actually
reach today is framework-*pull*: the runtime calls into the service on a
schedule or in response to a station request (`Data::Read`, `Watch`, `Topic`,
and now `poll_output_streams`'s `Output<Endpoint,Publisher>`). There is no
working reverse path, and the poll scheduler does not create one -- it is
explicitly scoped to a *scheduled pull*, never a service-initiated push (see
`remote-output-stream-scheduler.md` §2.1 and its open question 3, and the
"Design" section of the poll scheduler's own implementation notes).

**Why this is structural, not a missing function call:**
`include/solar/application.hpp`'s `RemoteSynthesis` (~line 397-420) builds
`Dispatch = system::Dispatch<Contract, Components>` (line 409) *before*
`Architecture`/`Runtime` exist (line 412 onward) -- `Dispatch` only needs the
already-known `Contract` (generated from yaml) and `Components` (the concrete
service list). `RuntimeContext<ArchitectureT, PollDispatchT>`
(`include/solar/remote/runtime_context.hpp:53`) and the functions that can
actually write to the wire (`write_data<System, DataT>`,
`include/solar/remote/runtime.hpp:590`; `publish_stream<System, StreamT>`;
`enqueue_reliable_stream<System, StreamT>`) all require naming a concrete
`System` = `RuntimeContext<Architecture, ...>` -- and `Architecture` is not
nameable until *after* every service header (`navigation.hpp`, etc.) has
already been fully parsed and type-checked as an ordinary, non-template
struct, further down in the same `Application` composition. A service simply
never has, at the point its own code runs, a name for the thing it would need
to call to push a value immediately.

The poll scheduler works around this cleanly for the *pull* direction because
the pull always originates from *runtime* code (`Service::run()`'s
maintenance tick, `include/solar/remote/service.hpp:1636`), which by
construction only ever runs after `Architecture`/`RuntimeContext` exist. The
service itself never has to name anything -- it just implements an ordinary
function that the generated `Output<Endpoint,Publisher>` binding calls when
asked. This is *why* poll was reachable and push was not: pull needs the
*runtime* to name the service's function (easy, the function is a plain,
already-known type); push needs the *service* to name the runtime's function
(hard, the runtime's type doesn't exist yet).

## What exists today (state definition)

- **Working, pull-based, poll-scheduled:** `poll_output_streams`
  (`runtime.hpp:1698-1735`), driven by `Service::run()`'s ~50ms maintenance
  tick. Handles both best-effort (`Queue<N,Overflow>`) and reliable
  (`ReliableWindow<Count>`) `direction: out` streams. This is real,
  implemented, and tested (`tests/zephyr/remote_output_stream_scheduler`).
- **Working, but only reachable from a hand-built test harness, never a real
  service:** `write_data<System, DataT>` (`runtime.hpp:590`) and
  `ByteRuntime::publish<StreamT>` (`runtime_context.hpp`). Both require
  naming a concrete `System`. The only place in the codebase that does this
  today is `tests/zephyr/remote_outbound_credit/src/main.cpp`, which defines
  `Architecture` and calls these functions directly in the same translation
  unit -- a shape no real Solar service (compiled as an ordinary,
  Architecture-independent struct) can replicate.
- **Not implemented at all:** any path for a service (or, more importantly,
  an ISR/work-queue callback a service's `Dependencies` own, e.g. a UART RX
  interrupt handler for an LD06-class sensor) to call into the runtime the
  instant a value is ready, for either `Data` or `Stream` capabilities.

## Cost of staying poll-only

- **Latency floor**: a value that becomes ready at time T is not observed
  until the next maintenance tick that is also due per the stream's
  `maximum_rate_hz` -- up to ~50ms (the maintenance interval) added on top of
  whatever the rate limit itself already implies. For a driver that produces
  data in bursts tied to real hardware timing (e.g. a lidar revolution, a
  UART frame), this is added, uncontrolled jitter, not a designed property.
- **Wasted work on idle streams**: every `direction: out` stream is asked
  "do you have something new?" every tick, whether or not anything changed.
  Currently cheap (an `if constexpr`-guarded function call plus a tick
  comparison) but scales linearly with declared output streams regardless of
  how often they actually produce.
- **No ISR-to-wire path, at all, for anything.** This is the more important
  gap long-term: today, and even after this document's problem is solved for
  the common case, an ISR still cannot safely call arbitrary Remote publish
  machinery directly (locking, allocation, and reentrancy constraints on
  interrupt context are real -- see `write_data`'s existing `FromIsr`
  template parameter and its `is_trivially_copyable`/`is_trivially_destructible`
  static assertions, `runtime.hpp:590-606`, which already models this
  constraint for the one path that has any ISR awareness at all). A future
  design here needs an explicit ISR-safety story, not just a reachability
  fix.
- **A cheap, orthogonal mitigation that does *not* solve this problem** and
  should not be mistaken for solving it: `Service::run()`'s wait between
  maintenance ticks is already dynamically shortened for pending reliable-
  stream work (`process_poll_releases`, referenced at `service.hpp:1638-1639`
  in the surrounding maintenance loop). The same technique could shorten the
  wait to the earliest `PollScheduleState.next_poll` across all output
  streams (`runtime.hpp`'s `PollScheduleState`/`poll_schedule_state`), so the
  loop wakes closer to when a stream is actually due rather than a flat
  ~50ms. That's a real, low-risk latency/efficiency improvement worth doing
  independently, but it is still polling -- it does not let a service push,
  and does not touch the ISR problem above.

## Non-goals of this document

This document defines the problem, not a solution. In particular it does
*not*:

- propose a concrete mechanism (deferred-instantiation handle, type-erased
  callback, registration table, or otherwise);
- decide whether `Data` and `Stream` capabilities should share one push
  mechanism or two;
- decide the ISR-safety contract for a push path;
- estimate implementation size.

Those are exactly the open questions the TODO below is for.

## TODO

1. **Survey solution shapes** for letting code that runs before `Architecture`
   exists (a service method, an ISR/work-queue callback) eventually reach a
   concrete `RuntimeContext<Architecture>`-shaped publish call once
   `Architecture` exists. Candidates worth evaluating, not yet vetted:
   - a deferred-registration handle, mirroring how `PollDispatchT` is
     threaded into `RuntimeContext`/`ByteRuntime` after the fact
     (`runtime_context.hpp:53`, `application.hpp:418`), but for the *reverse*
     direction -- something the service can hold and call, not something the
     runtime calls into the service with.
   - a type-erased function-pointer/vtable "publish token" resolved once at
     `Application` composition time and handed back to the owning service
     (would need a place to store it -- service structs are stateless type
     collections today, not instances).
   - a global, stable (non-Architecture-dependent) queue/mailbox a service or
     ISR can always name and push raw bytes/values into, drained by the
     runtime's own thread on a semaphore/event wake rather than a timer --
     this sidesteps the naming problem by never requiring the producer to
     name `RuntimeContext` at all, at the cost of a new intermediate buffer
     and its own overflow/backpressure policy.
2. **Decide the ISR-safety contract** for whichever shape is chosen: what's
   legal to call from interrupt context, what data has to be
   trivially-copyable/trivially-destructible (mirroring `write_data`'s
   existing `FromIsr` constraint), and how a value gets from an ISR to the
   runtime's own thread without doing real work (locking, encoding) inside
   the interrupt.
3. **Decide scope**: does this unify `Data`'s `OutStream<Push,...>` push path
   and `Stream`'s poll-scheduled path into one mechanism, or do they stay
   separate (poll for scheduled/rate-limited producers, push for
   event-driven ones, both able to select `ReliableWindow`/`Queue` delivery
   the same way)?
4. **Implement**, with the same verification bar the scheduler and Stage 5
   used: solar host ctest, the relevant Zephyr native-sim suites, and a real
   fixture exercising a genuine ISR-to-wire path end to end (not just a
   hand-rolled call from `main()`).
5. Once done, revisit whether `poll_output_streams` should stay as the
   default for scheduled/rate-limited producers (it should, most producers
   are genuinely periodic) with push reserved for producers where latency
   from real hardware timing actually matters.
