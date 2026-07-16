# Typed Application Bus

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 4

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`
- `01-system-blueprint-and-binding.md`
- `02-identity-contributions-and-catalogs.md`
- `03-lifecycle-kernel-and-configuration.md`

## 1. Purpose

This specification defines Solar's typed application bus.

The bus coordinates application behavior by distributing statically declared
messages to zero, one, or many statically declared subscriptions. It separates
what occurred from where and when subscriber behavior executes.

It establishes:

- message and payload vocabulary;
- compact message declarations and global emission APIs;
- message and subscription contribution forms;
- subscriber handler contracts;
- subscription identity and duplicate rules;
- inline, queued, latest-value, and coalesced delivery;
- system-work-queue and named-executor targets;
- bounded overflow and shutdown policies;
- deterministic ordering and reentrant emission behavior;
- asynchronous payload constraints;
- focused records and unavailable-feature behavior;
- lifecycle and executor integration;
- migration away from graph-owned channels.

The normal path is compact:

```cpp
struct ButtonPressed
{
    Button button;

    static constexpr solar::bus::Descriptor descriptor{
        .name = "button.pressed",
    };
};

solar::bus::emit<ButtonPressed>({.button = Button::A});
```

Types provide local identity and payload checking. Names and explicit stable
IDs remain metadata for inspection and deliberate external exposure.

## 2. Non-Goals

The bus does not provide:

- current-value storage;
- parameter persistence;
- diagnostic event history;
- logging;
- request-response calls;
- return values from multiple subscribers;
- point-to-point stream ownership;
- a replacement for direct typed component calls;
- a universal application queue;
- runtime subscription registration;
- runtime message type registration;
- dynamic allocation;
- automatic Remote exposure;
- automatic conversion to observability events;
- service mailbox semantics in the initial design;
- executor implementation details owned by Phase 9.

The bus is not an object broker or runtime callback registry. Its complete
topology is derived from the bound effective system.

## 3. Semantic Boundary

### 3.1 Bus message

A **bus message** is a transient typed occurrence intended to coordinate
application behavior.

Examples include:

- a button was pressed;
- emergency stop was requested;
- a mode transition was requested;
- a new command arrived;
- a component should refresh derived work.

A message says that something occurred. It does not promise retained state or
a response.

### 3.2 Direct typed call

Use a direct typed call when:

- the caller knows the required recipient;
- the recipient is an intentional dependency;
- an immediate return value matters;
- the operation is a capability request or query;
- failure must be attributed directly to the called component.

```cpp
LeftMotor::set_speed(speed);
auto pose = Odometry::pose();
```

A bus emission must not disguise a required direct dependency merely to avoid
including its type.

### 3.3 Typed queue

Use a component-owned or kernel typed queue when:

- data has one receiving owner;
- the receiver explicitly consumes a stream;
- queue occupancy and backpressure belong to that owner;
- receive timing is part of the API;
- request-response correlation is required.

### 3.4 Parameters

Use `solar::parameters` for named runtime-adjustable current state. A parameter
change may later cause a bus message, but the notification is not the canonical
value store.

### 3.5 Observability events

Bus messages coordinate behavior. Observability events record facts.

```cpp
solar::bus::emit<ButtonPressed>({.button = Button::A});
solar::events::observe<FrameDropped>({.bytes = frame_size});
```

Observability events may carry timestamps, severity, source, persistence, and
sink routing. Bus messages carry none of that automatically.

Application behavior must not subscribe to `solar::events` as a hidden control
bus. Conversely, bus messages are not automatically retained as diagnostics.

## 4. Vocabulary

The public vocabulary is:

- **message**: a typed transient occurrence and its payload schema;
- **descriptor**: immutable authored metadata for a message;
- **subscriber**: the semantic component that receives a message;
- **handler**: the static function invoked for one subscription;
- **subscription**: one message-to-subscriber route;
- **delivery**: where and how a subscription invokes its handler;
- **executor**: execution infrastructure used by deferred delivery;
- **pending message**: an accepted occurrence not yet handled;
- **overflow policy**: behavior when bounded pending storage is full;
- **stop policy**: drain or cancellation behavior during shutdown.

The subsystem uses **message**, not **event**, in its public declaration
vocabulary. The verb `emit` remains concise and natural.

## 5. Message Declaration

### 5.1 The message is its payload

The message declaration type is also the payload type delivered to handlers.

```cpp
struct JoystickUpdated
{
    float x;
    float y;

    static constexpr solar::bus::Descriptor descriptor{
        .name = "controls.joystick.updated",
    };
};
```

Solar does not require a separate nested `Payload`, envelope, or occurrence
wrapper for the common path.

### 5.2 Empty signals

An empty message represents a signal with no payload fields:

```cpp
struct EmergencyStop
{
    static constexpr solar::bus::Descriptor descriptor{
        .name = "safety.emergency_stop",
    };
};

solar::bus::emit<EmergencyStop>();
```

The handler still receives `const EmergencyStop&`, preserving one handler
contract for empty and payload-bearing messages.

### 5.3 Descriptor customization

The normal authored metadata is:

```cpp
static constexpr solar::bus::Descriptor descriptor{...};
```

The generic customization point is the Phase 2 trait:

```cpp
solar::descriptor_traits<solar::bus::message_tag, Message>
```

Trait specialization supports third-party message types when they cannot carry
a static member.

### 5.4 Minimum descriptor

The initial descriptor contains at least:

- a stable human-readable name;
- optional authored metadata needed by inspection;
- optional external stable identity under the Phase 2 rules.

Delivery policy, subscriber count, executor, capacity, and overflow behavior do
not belong in the message descriptor. They are route architecture.

### 5.5 No automatic envelope

Emission does not automatically add:

- timestamp;
- source component;
- severity;
- thread identity;
- sequence number;
- history metadata.

A message may explicitly contain a domain field when application behavior
requires it. Infrastructure diagnostic metadata remains the responsibility of
observability events and focused bus records.

## 6. Message Contributions

### 6.1 Conventional component alias

The compact conventional alias is `Messages`:

```cpp
struct Controls
{
    using Messages = solar::bus::Messages<
        ButtonPressed,
        JoystickUpdated>;
};
```

The bus subsystem owns the corresponding
`contribution_source<bus::message_tag, Component>` adapter. The generic Phase 2
collector normalizes the alias and preserves semantic owner and registration
origin.

`BusMessages` is not required.

### 6.2 Root declarations

Application-owned message declarations may be registered in the blueprint:

```cpp
solar::Bus<
    solar::bus::Messages<
        EmergencyStop,
        SystemModeChanged>>
```

The exact normalized section representation may flatten nested message and
subscription packs internally. Public declarations remain tagged and must not
mix bus configuration into the catalog contents.

### 6.3 Catalog registration

Every intentionally emitted message must belong to the effective bus message
catalog.

```cpp
solar::bus::emit<UnregisteredMessage>({}); // strict error; relaxed NotRegistered
```

A subscription is blueprint architecture, so referencing an unregistered
message remains a compile-time error in both modes. A call-site use follows the
configured strict or relaxed catalog-binding mode. Subscriptions do not
silently claim semantic ownership of message declarations.

### 6.4 Duplicate declarations

Repeated registration of one message type or conflicting ownership is rejected
according to Phase 2. Solar does not silently deduplicate a root declaration
and a component contribution.

## 7. Subscription Declaration

### 7.1 Component-local form

Component-local subscriptions are the normal form:

```cpp
struct DriveController
{
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<
            EmergencyStop,
            solar::bus::delivery::Inline>,

        solar::bus::On<
            JoystickUpdated,
            solar::bus::delivery::Latest<ControlExecutor>>>;

    static void handle(const EmergencyStop& message);
    static solar::Result<void> handle(const JoystickUpdated& message);
};
```

The contribution owner supplies the subscriber component. The common form does
not repeat `DriveController` inside every route.

### 7.2 Conventional alias normalization

`Subscriptions` is recognized through the bus subsystem's
`contribution_source<bus::subscription_tag, Component>` adapter.

The alias normalizes to ordinary Phase 2 contributions. Subscription entries
retain:

- semantic subscriber owner;
- registration origin;
- message reference;
- route tag;
- handler type;
- delivery policy;
- local subscription ID.

### 7.3 Central form

Central routes are an escape hatch for application wiring and third-party
types:

```cpp
solar::bus::Subscriptions<
    solar::bus::To<
        BatteryLow,
        SafetyController,
        solar::bus::delivery::Inline>>
```

The target component is the semantic subscriber. The application root is the
registration origin.

Central declaration is not required merely to make the topology inspectable.
The generated subscription catalog already provides a complete system view.

### 7.4 No runtime subscription

Subscribers cannot be added or removed at runtime. Product variation occurs
through blueprint composition and compile-time policy.

Runtime enable or mute state may later suppress an existing route, but it does
not mutate catalog membership or identity.

## 8. Handler Contract

### 8.1 Canonical handler

The normal handler is a public static overload on the subscriber component:

```cpp
static Return handle(const Message& message);
```

Overloading `handle` by message type supports several subscriptions without
inventing several naming conventions.

### 8.2 Accepted return forms

Handlers permanently accept exactly:

```cpp
void
solar::Status
solar::Result<void>
```

Normalization is:

- `void` means successful completion;
- `Status::Ok` means successful completion;
- another `Status` means handler failure;
- successful `Result<void>` means successful completion;
- failed `Result<void>` preserves its status.

Arbitrary integers, booleans, unrelated expected error types, and generic
result-like objects are rejected.

Supporting `void` is intentional. Many notifications cannot fail meaningfully,
and requiring `return Status::Ok` would add ceremony without information.

### 8.3 Rich errors

A handler that needs a rich typed domain error retains it in the subscriber's
focused state and maps deliberately to `Status` before crossing the bus handler
boundary.

### 8.4 Explicit handler route

When one component needs several handlers for the same message, or when a
separate static handler type is clearer, it uses an explicitly tagged route:

```cpp
solar::bus::Route<
    AuditRoute,
    ButtonPressed,
    AuditHandler,
    solar::bus::delivery::Queued<AuditExecutor, 8>>
```

The exact template parameter order may be refined for diagnostics, but the
semantic fields are route tag, message, handler, and delivery.

### 8.5 No handler context object

Solar does not pass a bus context, system object, runtime object, subscription
record, or executor object to the handler.

The subscriber calls its direct typed dependencies and global Solar subsystems
normally.

## 9. Subscription Identity

### 9.1 Logical key

The normalized logical subscription key is:

```text
message type + subscriber component type + route tag type
```

The common `On<Message, Delivery>` form uses one implicit default route tag.

### 9.2 Delivery is not identity

Delivery policy, executor, capacity, overflow policy, and stop policy are not
part of logical identity.

Changing a route from one executor to another changes architecture metadata. It
does not create an unrelated logical subscription or invalidate references that
identify the route by its explicit tag.

### 9.3 Duplicate rules

Solar rejects:

- duplicate concrete registration types;
- duplicate logical subscription keys;
- two default routes for one subscriber and message;
- one explicit tag reused for the same subscriber and message;
- conflicting semantic subscriber ownership.

### 9.4 Multiple intentional routes

A subscriber may intentionally register multiple routes for one message by
giving each a distinct route tag.

This is uncommon and should remain explicit because each route owns independent
storage, diagnostics, delivery, and failure behavior.

### 9.5 Stable identity

Subscription IDs are normally dense typed build-local IDs. A stable external
subscription ID is added only when a later protocol deliberately exposes the
subscription itself.

Message stable identity follows the Phase 2 bus schema identity domain.
Compiler type-name hashes and unmanifested name hashes are forbidden as wire
identity.

## 10. Emission API

### 10.1 Normal emission

The canonical global API is:

```cpp
template<solar::bus::Message M>
solar::Result<void, solar::bus::Error>
emit(const M& message);
```

The intended calls are:

```cpp
solar::bus::emit<ButtonPressed>({.button = Button::A});
solar::bus::emit<EmergencyStop>();
```

An inferred overload is also supported where normal argument deduction works:

```cpp
solar::bus::emit(ButtonPressed{.button = Button::A});
```

The explicit form remains useful for braced initialization and empty signals.

### 10.2 Non-blocking emission

```cpp
template<solar::bus::Message M>
solar::Result<void, solar::bus::Error>
try_emit(const M& message);
```

`try_emit` never waits for asynchronous route capacity. A route whose effective
policy would wait is attempted with no-wait semantics for that call.

Inline handlers still execute synchronously because inline delivery is the
declared behavior, not queue-capacity blocking.

### 10.3 ISR emission

```cpp
template<solar::bus::IsrMessage M>
solar::Result<void, solar::bus::Error>
try_emit_isr(const M& message);
```

`try_emit_isr` is always non-blocking. Its complete effective route topology
must be ISR-compatible.

If a registered message has one route that cannot be safely accepted from ISR
context, use of `try_emit_isr<M>` is a compile-time error in both modes. An
unregistered call is a strict compile-time error or relaxed `NotRegistered`.
Solar does not silently skip the incompatible route.

### 10.4 No standalone frontend

All emission APIs resolve through the one application binding. They cannot
operate against an unbound fallback bus or create an implicit runtime registry.

Explicit-system forms may exist for test systems:

```cpp
solar::bus::Of<TestApplication>::try_emit<Message>(message);
```

## 11. Emission Result

### 11.1 Error structure

`solar::bus::Error` is a compact structured error containing at least:

- broad reason;
- message local ID;
- first failing subscription local ID, when applicable;
- attempted route count;
- accepted route count;
- rejected route count;
- dropped route count.

The broad reasons include:

- bus not ready;
- route rejected due to capacity;
- route wait timed out;
- executor unavailable;
- inline handler failed;
- internal invariant failed.

### 11.2 Attempt every route

Emission attempts every matching route in deterministic catalog order even if
one route fails.

The first failure is retained in the returned error while aggregate counts
describe partial acceptance. One failing route must not starve unrelated
subscribers.

### 11.3 Meaning of async acceptance

Successful asynchronous emission means the route accepted the message under
its delivery policy. It does not mean the handler has run successfully.

Later asynchronous handler failure belongs to that subscription's focused
runtime record.

### 11.4 Intentional dropping

`DropNewest` and `DropOldest` are intentional overload policies. Applying one
successfully does not make the entire emission fail, but canonical route
records expose the drop. If another route fails, the returned aggregate error
also includes the number of dropped routes considered by that emission.

`Reject` and timeout are failures because the route explicitly refused the
message.

### 11.5 No subscriber

Emitting a registered message with zero effective subscribers succeeds and has
no runtime effect.

This supports product variants in which an optional consumer is absent. A
message requiring at least one subscriber uses compile-time topology validation
instead of turning a static architectural fact into a runtime surprise.

## 12. Required Subscribers

The bus configuration may expose a policy such as:

```cpp
solar::bus::Configuration<
    solar::bus::RequireSubscriber<EmergencyStop>>
```

Effective-blueprint validation then requires at least one route for that
message.

The policy is separate from message descriptor metadata and delivery policy.
It expresses a system composition invariant.

Future forms may require a minimum count or a particular subscriber type. The
initial requirement is zero allowed by default and at least one when explicitly
required.

## 13. Delivery Model

### 13.1 Message meaning is independent

The message type describes what occurred. Each subscription independently
selects where, when, and with what bounded storage its handler executes.

One message may therefore have an inline safety handler and a deferred
background handler without changing the message declaration.

### 13.2 Initial delivery modes

The initial modes are:

```cpp
solar::bus::delivery::Inline
solar::bus::delivery::InlineIsr

solar::bus::delivery::Queued<
    Executor,
    Capacity,
    OverflowPolicy,
    StopPolicy>

solar::bus::delivery::Latest<
    Executor,
    StopPolicy>

solar::bus::delivery::Coalesced<
    Executor,
    StopPolicy>
```

Template defaults may shorten nonessential policies, but normalization always
produces one complete effective route policy.

### 13.3 Explicit delivery

Every subscription explicitly selects its delivery mode. Solar does not hide
inline execution behind an omitted policy.

Nested policy defaults such as overflow and stop behavior may come from the
configuration precedence chain, but whether delivery is inline, queued,
latest, or coalesced remains visible at the subscription.

## 14. Inline Delivery

### 14.1 Behavior

`delivery::Inline` invokes the handler immediately in the emitter's thread
context before moving to the next subscription.

It owns no pending payload storage and uses no executor.

### 14.2 Consequences

The emitter inherits:

- handler latency;
- handler lock acquisition;
- handler stack use;
- handler priority context;
- handler failure;
- reentrant behavior.

An inline handler must remain short and must not pretend it was deferred.

### 14.3 ISR-safe inline

Ordinary `Inline` is thread-context delivery.

`InlineIsr` is the explicit ISR-safe form. It invokes the same canonical
handler contract but asserts an architectural promise that the handler and
everything it calls are ISR-safe and non-blocking.

Solar can validate the declared shape but cannot prove arbitrary handler-body
behavior. Misdeclaring an unsafe handler as `InlineIsr` is an application defect.

## 15. Queued Delivery

### 15.1 Per-subscription FIFO

`Queued` owns a fixed-capacity FIFO for one subscription. Every accepted message
is copied into that route's exact typed storage.

The queue is not shared with other subscribers merely because they receive the
same message. A slow route cannot consume another route's capacity.

### 15.2 Capacity

Capacity is a positive compile-time value. The route's storage requirement is
visible in the effective architecture and build map.

### 15.3 Handler execution

The selected executor removes messages from the route FIFO and invokes the
handler one at a time in FIFO order.

The bus does not promise FIFO ordering relative to another subscription or
another executor.

### 15.4 Submission coalescing

The route may use one pending executor work item to drain several FIFO entries.
This implementation optimization does not alter the promise that each accepted
queued message is delivered once unless stop policy cancels it.

## 16. Latest-Value Delivery

`Latest<Executor>` owns one payload slot.

When no invocation is pending, emission stores the payload and schedules work.
When work is pending, a new emission replaces the stored pending payload.

The route guarantees:

- bounded one-payload storage;
- the handler eventually observes the newest accepted payload after emission
  settles, subject to shutdown and executor failure;
- replacement counts are observable;
- no promise of one invocation per occurrence.

If emission occurs while the handler is running, the route schedules another
invocation when necessary so the final accepted value is not lost behind the
in-flight handler.

Latest-value delivery is a transient coordination mechanism. It is not a
queryable canonical state store. Consumers requiring current state use a
component API or parameter.

## 17. Coalesced Delivery

`Coalesced<Executor>` ensures that at most one invocation is pending for a
subscription.

Repeated emission while pending records coalescing and does not schedule
another independent invocation.

Initial coalesced delivery is valid only for empty signal messages. A
payload-bearing message must use `Latest` to define which payload survives.

This restriction prevents an ambiguous policy in which a handler receives an
unspecified first, last, or arbitrary payload.

## 18. Executor Targets

### 18.1 Executor ownership

The bus owns:

- route payload storage;
- route queue indices and synchronization;
- executor registration or work item;
- subscription counters and last failure;
- route admission and quiescence state.

The executor owns:

- worker thread or Zephyr execution context;
- scheduling priority;
- stack;
- worker lifecycle and containment;
- generic submission mechanics;
- execution diagnostics.

### 18.2 System work queue

The Zephyr system work queue is exposed as an execution target such as:

```cpp
solar::execution::SystemWorkQueue
```

Solar owns only its route work items and typed pending storage. It does not own,
stop, drain, or globally inspect the Zephyr system work queue.

During shutdown Solar may synchronously cancel or wait for its own work item.
It must not claim that unrelated system work has quiesced.

### 18.3 Named shared executor

A route may target an effective executor component:

```cpp
solar::bus::delivery::Latest<ControlExecutor>
```

Phase 9 defines the final executor registration API. That API must allow the bus
to answer:

- whether submission was accepted;
- whether submission is ISR-safe;
- whether pending bus work can drain or cancel;
- whether a handler is currently in flight;
- whether the executor is contained during stop.

### 18.4 No private route thread

The initial bus does not create a dedicated thread for each subscription.
Applications needing dedicated scheduling declare an executor and make that
cost visible in the blueprint.

## 19. Service Mailbox Decision

`delivery::ServiceMailbox<Service>` is deferred to Phase 9.

A real service mailbox requires decisions about:

- heterogeneous message representation;
- service-owned receive and select behavior;
- mailbox capacity and fairness;
- interaction with service stop tokens;
- whether handling occurs through `handle` or the service's run loop;
- executor and service ownership of records;
- containment of messages after service exit.

These are execution semantics, not a small bus delivery adapter. Named shared
executors and component-owned typed queues cover the initial use cases without
prejudging the mailbox design.

## 20. Overflow Policies

### 20.1 Initial policies

Queued asynchronous delivery supports:

```cpp
solar::bus::overflow::Reject
solar::bus::overflow::DropNewest
solar::bus::overflow::DropOldest
solar::bus::overflow::WaitFor<Duration>
```

Latest and coalesced replacement are delivery semantics rather than overflow
policies.

### 20.2 Reject

`Reject` leaves pending storage unchanged and reports route rejection to the
emitter.

### 20.3 Drop newest

`DropNewest` discards the new occurrence, increments dropped-newest facts, and
leaves previously accepted entries unchanged.

### 20.4 Drop oldest

`DropOldest` removes the oldest pending entry, records that loss, and accepts
the new occurrence at the end of the FIFO.

### 20.5 Bounded wait

`WaitFor<Duration>` permits normal `emit` to wait for route capacity for a
finite configured duration.

It is invalid for ISR emission. `try_emit` always overrides it with no-wait
admission for that call.

Waiting forever is not part of the initial bus policy. A fan-out emission that
waits indefinitely on one low-priority subscriber can deadlock unrelated
application behavior.

### 20.6 Fan-out wait cost

Normal emission visits routes in deterministic order. Several waiting routes
may therefore contribute cumulative latency.

Architectures requiring strict call latency use `try_emit`, non-wait overflow,
or inline handlers with bounded execution.

## 21. Stop Policies

Initial asynchronous stop policies are:

```cpp
solar::bus::stop::Drain
solar::bus::stop::CancelPending
```

### 21.1 Drain

`Drain` stops accepting new messages and attempts to handle every previously
accepted pending message before the effective shutdown timeout.

### 21.2 Cancel pending

`CancelPending` stops accepting messages, removes work not yet started, records
the cancelled count, and waits only for an already in-flight handler.

### 21.3 In-flight behavior

Solar does not asynchronously abort an arbitrary handler function. An in-flight
handler must finish or be contained by its owning executor under the Phase 3
rules.

### 21.4 Timeout

Failure to drain, cancel, or contain a route is a bus facility stop failure. The
subscriber owner and executor are dependencies that may need preservation
under Phase 3.

## 22. Ordering

### 22.1 Subscription traversal

Matching subscriptions are visited in deterministic subscription catalog order.
This order is stable within one effective build.

It is not an external protocol contract. A stable external ordering requires
an explicit domain design rather than relying on build-local IDs.

### 22.2 Inline ordering

Inline handlers execute synchronously in catalog order.

If an earlier handler mutates application state, a later inline handler may
observe that mutation. Applications should not create hidden order dependencies
between otherwise independent subscribers.

### 22.3 Asynchronous ordering

Queued delivery preserves FIFO order for one subscription.

Solar promises no relative execution order between:

- two subscriptions;
- two executors;
- inline and deferred routes after the deferred route accepts work;
- system work queue and named executor routes.

### 22.4 Latest and coalesced

Latest and coalesced modes intentionally do not preserve occurrence order or
count. Their replacement and coalescing facts remain observable.

## 23. Reentrant Emission

Inline handlers may emit other bus messages, including the same message type.

Reentrant inline emission follows ordinary depth-first C++ call behavior. For
example, if subscriber A emits the same message before outer subscriber B has
run, the nested dispatch traverses its routes before outer dispatch resumes at
B.

Solar does not use one global recursion guard because that would incorrectly
reject legitimate concurrent emission from another thread. It also cannot
derive arbitrary handler-body call cycles at compile time.

Applications must avoid unbounded recursive behavior. Asynchronous delivery is
the appropriate policy where recursive inline propagation would be unsafe.

## 24. Payload Constraints

### 24.1 Inline payload

Inline delivery receives a `const Message&` valid for the duration of the
emission call. The message need not be trivially copyable when every route is
inline.

### 24.2 Asynchronous payload

A message used by any asynchronous route must initially be:

- complete;
- destructible;
- trivially copyable;
- within the configured asynchronous payload size ceiling;
- within the configured alignment ceiling;
- safe to retain independently of the emitter's stack and object lifetime.

Each route stores the concrete message type directly. There is no universal
type-erased payload buffer in the normal path.

### 24.3 Borrowed data

References, views, spans, and pointers to emitter-owned storage are not safe for
deferred delivery unless the application can prove a longer stable lifetime
through an explicitly designed policy.

C++23 cannot reflect over every aggregate member and detect every raw pointer.
Solar rejects known borrowed wrapper types where practical and documents the
remaining author contract. Trivially copyable alone does not make pointed-to
data owned.

### 24.4 No dynamic allocation

Emission, route storage, dispatch, and diagnostics require no dynamic
allocation. Every asynchronous byte is accounted for by the effective route
types and capacity policies.

### 24.5 Large data

Large frames, images, scans, and byte streams should not be copied once per bus
subscriber by default. Use an explicitly owned pool, stable handle, stream, or
point-to-point queue with a separately designed lifetime contract.

## 25. Concurrency

### 25.1 Concurrent emitters

Thread-context emission is safe from multiple threads.

Inline handlers may therefore execute concurrently when different threads emit
the same message. A subscriber requiring serialized state access selects one
serial executor or performs its own synchronization.

### 25.2 Route-local synchronization

Asynchronous route storage uses route-local synchronization. Solar must not
hold one global bus mutex while:

- invoking a handler;
- waiting for queue capacity;
- submitting executor work;
- draining during stop;
- querying unrelated routes.

### 25.3 Handler serialization

One queued, latest, or coalesced subscription invokes its handler serially.
Different subscriptions targeting the same executor follow that executor's
worker and ordering guarantees.

### 25.4 Query consistency

Each focused record copy is internally coherent. Separate query calls may
observe progress between them.

## 26. ISR Behavior

### 26.1 Explicit API only

ISR code uses `try_emit_isr`. It must not call `emit` and rely on runtime context
detection to avoid blocking.

### 26.2 Route requirements

Every route for an ISR-emitted message must satisfy one of:

- explicit `InlineIsr` delivery;
- asynchronous storage with ISR-safe no-wait admission and ISR-safe executor
  signaling.

The route must not use bounded wait, thread mutexes, formatting, allocation, or
an executor lacking ISR-safe submission.

### 26.3 Payload requirements

ISR-emitted messages satisfy the asynchronous storage constraints even when a
particular route is inline. This keeps one predictable ISR-safe concept and
avoids context-dependent object lifetime rules.

### 26.4 Failure behavior

ISR emission returns a compact expected result and updates ISR-safe counters.
It does not log, emit an observability event, format text, or invoke Remote as a
side effect.

## 27. Runtime Ownership And Storage

### 27.1 Immutable catalogs

The message catalog and subscription catalog are immutable compile-time
architecture. Their descriptor views are safe under the Phase 2 rules.

### 27.2 Bus facility

The built-in bus facility owns mutable canonical route state, including:

- admission state;
- pending payload storage;
- FIFO indices;
- route synchronization;
- executor work registration;
- per-route counters;
- first and last handler failures;
- high-water mark;
- stop and cancellation facts.

### 27.3 Per-subscription storage

Asynchronous state is allocated per normalized subscription. The bus does not
place unrelated message types into one runtime-erased central queue.

This preserves exact size, alignment, policy, and ownership in the type system.

### 27.4 No subscription objects

Subscriptions are static catalog declarations with type-owned route state.
They are not heap-allocated callback objects or runtime polymorphic interfaces.

## 28. Lifecycle

### 28.1 Facility inclusion

The built-in bus facility is included when:

- `CONFIG_SOLAR_BUS` enables its implementation; and
- the effective blueprint contains a message, subscription, or explicit bus
  requirement.

Intentional bus registration while the implementation is disabled is a
compile-time error.

### 28.2 Generated dependencies

The bus facility depends on:

- every semantic subscriber owner;
- every named executor referenced by its routes;
- any other explicit facility needed by a route adapter.

This makes subscriber state and executor infrastructure available before the
bus is activated, and places bus quiescence before their destructive teardown.

The Zephyr system work queue is a platform capability rather than an effective
component dependency.

### 28.3 Availability window

Emission is valid only while the complete system state is `Running`.

Before boot, during initialization or start, once stopping begins, after stop,
and after failure, emission returns `bus::Reason::NotReady` or its final named
equivalent.

This intentionally forbids bus-based boot coordination. Components use direct
dependencies in lifecycle hooks. Services that begin execution during the
system start sweep must wait until the system is running before emitting.

### 28.4 Initialization

Facility init prepares route state, validates native adapters, initializes
system work items, and clears canonical runtime records.

Failure is a normal bus facility lifecycle failure and enters the Phase 3 boot
report with an optional subscription leaf reference.

### 28.5 Start

Facility start confirms referenced executors are available and prepares route
activation. The global API admits emission only after the overall system enters
`Running`.

### 28.6 Stop admission

The transition away from system `Running` closes bus admission immediately.
Concurrent calls that have not committed a route are rejected as not ready.
Already committed calls and in-flight handlers follow their route stop policy.

### 28.7 Executor containment

Queued bus routes are executor registrations. During Phase 3 executor
containment, the execution subsystem drains or cancels those registrations
according to route stop policy before stopping the worker.

The bus does not introduce a second executor shutdown model.

### 28.8 Facility stop

The bus facility stop operation:

1. confirms admission is closed;
2. waits for current inline dispatch to leave;
3. synchronizes its system-work-queue items;
4. verifies named-executor route registrations are quiescent;
5. records cancelled, drained, and failed routes;
6. reports uncontained work through the Phase 3 failure model.

### 28.9 Dependency preservation

If a handler or route remains uncontained, the subscriber owner, executor, and
their transitive dependencies remain potentially in use. Phase 3 dependency
preservation applies.

## 29. Focused Runtime Records

### 29.1 Message descriptor view

The message query surface exposes immutable metadata:

```cpp
solar::bus::messages();
solar::bus::message<ButtonPressed>();
```

Descriptor views are available before boot because they contain compile-time
facts.

### 29.2 Subscription descriptor view

```cpp
solar::bus::subscriptions();
```

Each descriptor identifies message, subscriber owner, origin, route tag,
handler, delivery mode, executor, capacity, overflow, stop policy, and local ID.

### 29.3 Runtime route record

Focused records include at least:

- emitted or considered count;
- accepted count;
- delivered count;
- pending count;
- high-water mark;
- replacement count;
- coalesced count;
- dropped-newest and dropped-oldest counts;
- rejected and timed-out counts;
- handler-failed count;
- last normalized handler status;
- in-flight fact;
- drain and cancellation facts;
- executor availability or failure.

### 29.4 Typed record query

The intended query shape is:

```cpp
solar::bus::record<DriveController, JoystickUpdated>();
```

An explicit route tag disambiguates multiple routes:

```cpp
solar::bus::record<DriveController, ButtonPressed, AuditRoute>();
```

An explicit-system form supports tests.

### 29.5 No history

Bus records count and diagnose delivery. They do not retain a generic history
of message payloads.

Applications needing history use observability events, explicit component
state, or a domain-specific bounded store.

## 30. Metrics, Events, And Logging Interaction

Bus counters are canonical bus records. The metrics subsystem may later expose
selected counters as instruments without becoming their owner.

The bus does not automatically:

- emit an observability event for every drop;
- log every handler failure;
- increment an unregistered application metric;
- recursively emit a bus fault message.

Such automatic fan-out risks recursion and makes disabled sibling subsystems
change core bus behavior.

Explicit adapters may bridge selected operational facts after the relevant
subsystem specifications define bounded behavior.

## 31. Remote Boundary

Remote does not automatically expose internal bus messages.

Explicit bridge declarations may later:

- publish selected bus messages as Remote topics;
- translate authenticated incoming Remote operations into selected bus
  messages;
- inspect message and subscription descriptors;
- expose selected bus delivery counters.

The bridge owns schema conversion, authorization, rate limiting, and external
stable identity.

A C++ message type or local message ID is never serialized as an accidental
wire contract. Payloads are not assumed remotely encodable merely because they
are asynchronously copyable.

## 32. Configuration

### 32.1 Kconfig ownership

Kconfig owns build capabilities and hard ceilings such as:

- `CONFIG_SOLAR_BUS` implementation inclusion;
- system-work-queue adapter support;
- focused runtime record support where optional;
- maximum asynchronous payload bytes;
- maximum asynchronous payload alignment;
- maximum per-route queued capacity;
- default bounded wait and stop timeout;
- default overflow and stop behavior where a route omits them;
- required Zephyr work and ISR capabilities.

Exact symbol names are finalized during implementation. There is no C++
fallback configuration header.

### 32.2 C++ policy ownership

Typed architecture owns:

- message and subscription membership;
- delivery mode;
- executor selection;
- route capacity;
- explicit overflow policy;
- explicit stop policy;
- route tag;
- required-subscriber invariants;
- explicit Remote bridge declarations.

### 32.3 Precedence

The Phase 3 precedence rule applies:

```text
explicit route policy
    > bus blueprint configuration
    > Kconfig default
```

No policy may re-enable a Kconfig-disabled capability or exceed a hard ceiling.

### 32.4 No hidden dynamic fallback

An oversized payload or queue is a compile-time diagnostic. Solar does not
silently allocate from a heap, shrink a route, change delivery mode, or drop
messages to make an invalid architecture compile.

## 33. Capacity And Resource Accounting

The effective system can compute exact bus-owned storage from normalized
subscriptions:

- inline route: no payload queue;
- queued route: `sizeof(Message) * Capacity` plus bounded metadata;
- latest route: one payload slot plus synchronization;
- coalesced route: no payload slot for empty signals;
- each deferred route: one bounded executor work registration.

The build should make this information inspectable through generated metadata,
compile-time constants, or a resource report.

No global capacity is consumed merely because a message exists without an
asynchronous subscriber.

## 34. Channel Replacement

### 34.1 Current channel capability

The existing `solar::services::Channel<Name, Payload, Depth>` provides:

- one typed payload;
- one statically allocated fixed-depth FIFO;
- non-blocking publish;
- non-blocking receive;
- current size;
- compile-time capacity;
- graph membership and lifecycle treatment inherited from the old system.

It does not provide fan-out, subscriber routing, delivery policy, or dynamic
registration.

### 34.2 Point-to-point FIFO migration

A channel used as one producer-to-consumer storage becomes a type-owned kernel
queue:

```cpp
struct NavigationMailbox
{
    static inline solar::kernel::Queue<NavigationCommand, 8> queue{};

    static solar::Status send(const NavigationCommand& command)
    {
        return queue.try_send(command);
    }
};
```

The owning component determines lifecycle and access policy. The queue itself
is a kernel primitive, not a component graph entry.

### 34.3 Fan-out migration

A channel used to notify several behaviors becomes one bus message with several
subscriptions.

Each asynchronous subscription receives its own bounded route storage and
overflow policy.

### 34.4 Latest state migration

A channel used only so a consumer can obtain the newest update becomes:

- component-owned current state when it must be queryable;
- a parameter when it is runtime configuration;
- `delivery::Latest` when only newest pending behavior matters.

### 34.5 Direct operation migration

A channel used to request one known component operation becomes a direct typed
call unless decoupled one-way fan-out is genuinely required.

### 34.6 Request-response migration

Request-response does not become two loosely related bus messages by default.
Use a direct typed result, a dedicated protocol abstraction, Remote Action, or
explicit correlated queues.

### 34.7 Removed architecture

The target system removes:

- `Channels<...>` blueprint sections;
- `ComponentKind::Channel`;
- channel init and start boot phases;
- channel lifecycle records;
- channel participation in generic component contributions;
- positional channel offsets in `System`.

## 35. Complete Example

```cpp
struct ButtonPressed
{
    Button button;

    static constexpr solar::bus::Descriptor descriptor{
        .name = "controls.button.pressed",
    };
};

struct EmergencyStop
{
    static constexpr solar::bus::Descriptor descriptor{
        .name = "safety.emergency_stop",
    };
};

struct JoystickUpdated
{
    float x;
    float y;

    static constexpr solar::bus::Descriptor descriptor{
        .name = "controls.joystick.updated",
    };
};

struct InputService
{
    using Messages = solar::bus::Messages<
        ButtonPressed,
        JoystickUpdated>;

    static solar::Result<void> run(solar::StopToken stop);
};

struct DriveController
{
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<
            EmergencyStop,
            solar::bus::delivery::Inline>,

        solar::bus::On<
            JoystickUpdated,
            solar::bus::delivery::Latest<ControlExecutor>>>;

    static void handle(const EmergencyStop&);
    static solar::Status handle(const JoystickUpdated&);
};

using RobotBlueprint = solar::Blueprint<
    solar::Services<InputService>,
    solar::Facilities<DriveController>,
    solar::Executors<ControlExecutor>,
    solar::Bus<
        solar::bus::Messages<EmergencyStop>>,
    solar::bus::Configuration<
        solar::bus::RequireSubscriber<EmergencyStop>>>;

using RobotSystem = solar::System<RobotBlueprint>;

SOLAR_BIND_SYSTEM(RobotSystem);
```

Runtime use remains direct:

```cpp
auto result = solar::bus::try_emit<JoystickUpdated>({
    .x = x,
    .y = y,
});

if (!result)
{
    // Domain-specific response to rejected coordination.
}
```

## 36. Include Direction

Message declarations include only the bus descriptor declaration header and
their domain value types.

Subscriber component headers include:

- message declarations they consume;
- direct typed dependency headers;
- bus subscription declaration headers.

They do not include the application composition root.

Definitions that call bound global bus APIs normally live in source files that
include the completed root, following Phase 1:

```cpp
// services/input_service.cpp
#include "app/system.hpp"

solar::Result<void> InputService::poll_once()
{
    return solar::bus::emit<ButtonPressed>({.button = Button::A});
}
```

The root includes message and component declarations. This preserves one-way
header composition and avoids circular inclusion.

## 37. Compile-Time Validation

Effective-system validation rejects:

- blueprint routes or subscriptions referencing an unregistered message;
- subscriptions referencing an unregistered message;
- missing or invalid message descriptors;
- duplicate message registration;
- conflicting message ownership;
- duplicate subscription keys;
- missing canonical handlers;
- unsupported handler return types;
- asynchronous messages that are not trivially copyable;
- payload size or alignment beyond Kconfig ceilings;
- zero or oversized queued capacity;
- wait overflow on ISR topology;
- ordinary inline delivery on ISR topology;
- executor targets absent from the effective component graph;
- executors lacking the required submission contract;
- coalesced payload-bearing messages;
- required messages with zero subscribers;
- bus registration while the bus implementation is disabled;
- C++ policies requesting disabled Zephyr capabilities.

Diagnostics should identify the message, subscriber, route tag, and invalid
policy rather than failing only inside final dispatch instantiation.

## 38. Runtime Errors

Runtime failure remains possible after valid compilation:

- relaxed frontend use before binding, while disabled, or with an unregistered
  message;
- the bus is not in its running availability window;
- bounded route storage rejects a message;
- bounded wait times out;
- a native executor submission fails;
- an inline handler reports failure;
- an asynchronous handler later reports failure;
- shutdown cannot drain or contain accepted work;
- a native system work item cannot synchronize as expected.

These failures update focused records. The bus does not throw exceptions or
silently redirect work to another delivery mode.

## 39. Verification Requirements

The implementation must eventually cover:

- compact `using Messages` contribution collection;
- component-local and central subscription collection;
- owner and origin preservation;
- strict unregistered message rejection and relaxed `NotRegistered` behavior;
- duplicate message and subscription diagnostics;
- overloaded canonical handlers;
- `void`, `Status`, and `Result<void>` handler normalization;
- zero-subscriber success;
- required-subscriber compile failure;
- deterministic inline traversal;
- all-route attempts after one route fails;
- recursive inline emission ordering;
- concurrent thread emitters;
- queued FIFO delivery and high-water accounting;
- latest-value replacement during pending and in-flight work;
- signal-only coalescing;
- reject, drop-newest, drop-oldest, and bounded-wait overflow;
- non-blocking `try_emit` override;
- valid and invalid ISR topologies;
- exact static storage accounting;
- system-work-queue item cancellation without global queue ownership;
- named-executor route registration;
- drain and cancel-pending stop behavior;
- handler failure records;
- emission rejection outside system `Running`;
- dependency preservation for uncontained route execution;
- message and subscription descriptor queries;
- Kconfig default and typed override precedence;
- migration examples for every old channel use category.

Tests use explicit test-system bindings and fixed deterministic executors where
timing-sensitive behavior would otherwise be nondeterministic.

## 40. Deferred Capabilities

The following remain deliberate later work:

- service mailbox delivery;
- runtime route mute or enable policy;
- large-payload pools and stable shared handles;
- schema generation for explicit Remote bridges;
- generated bus topology diagrams;
- compile-time subscription-cycle analysis after language reflection matures;
- richer executor quality-of-service policy;
- route deadlines and latency distributions;
- controlled fault-policy integration;
- externally stable subscription IDs where a protocol genuinely needs them.

Deferred features must preserve static membership, bounded storage, focused
ownership, and the lifecycle contract.

## 41. Rejected Alternatives

### 41.1 Runtime callback registry

Rejected because the complete firmware topology is known at compile time and
runtime registration adds ordering, allocation, synchronization, and failure
without benefit to the normal application.

### 41.2 Strings as the primary API

Rejected because strings lose compile-time payload and registration validation.
Names remain metadata and external lookup keys.

### 41.3 Separate payload type required for every message

Rejected because making the message type itself the payload keeps declarations,
emission, and handlers significantly smaller without losing type safety.

### 41.4 Call bus messages events throughout the API

Rejected because Solar already uses observability events for durable diagnostic
facts. Distinct vocabulary protects the semantic boundary.

### 41.5 Message-owned delivery policy

Rejected because one message may need several subscribers with different
latency, storage, executor, and overflow requirements.

### 41.6 Central subscriptions only

Rejected because it separates reusable component behavior from its consumption
declarations and creates a large application wiring table.

### 41.7 Component-local subscriptions only

Rejected because third-party types, generated architecture, and explicit
application-level wiring need a central escape hatch.

### 41.8 Infer every handler spelling

Rejected because probing `handle`, `on`, call operators, member functions, and
free functions creates ambiguous diagnostics. Static `handle(const Message&)`
is canonical.

### 41.9 Require status return from every handler

Rejected because `void` is precise for handlers with no meaningful failure and
keeps ordinary notification code compact.

### 41.10 Delivery policy as subscription identity

Rejected because changing execution architecture should not silently create a
different logical route.

### 41.11 Shared queue per message

Rejected because subscribers would steal occurrences from one another and one
subscriber's capacity would affect unrelated routes.

### 41.12 One erased central payload queue

Rejected because it imposes a global maximum, alignment, dispatch, and overflow
policy while hiding exact per-route memory cost.

### 41.13 Coalesced payload with unspecified survivor

Rejected because the handler would receive semantically ambiguous data. Use
`Latest` for payload-bearing messages.

### 41.14 Infinite blocking overflow

Rejected because one subscriber could indefinitely stall fan-out and create
priority inversion or shutdown deadlock.

### 41.15 Silent skipping in ISR

Rejected because emitting from ISR must not change which application behavior
receives the message without a compile-time error.

### 41.16 Automatic bus history

Rejected because behavior coordination and diagnostic retention have different
storage, schema, and policy.

### 41.17 Automatic Remote export

Rejected because internal behavior messages are not automatically stable,
authorized, rate-limited external protocols.

### 41.18 Initial service mailbox adapter

Rejected for the initial bus because its ownership and receive-loop semantics
belong with the Phase 9 task and executor design.

### 41.19 Preserve channels as graph components

Rejected because a bounded FIFO is a kernel or component-owned primitive, not
an independently orchestrated component category.

### 41.20 Emit during initialization and start

Rejected because subscribers may be initialized but not behaviorally ready.
Lifecycle coordination uses direct dependencies; bus behavior begins only when
the complete system is running.

## 42. Accepted Decisions

1. The bus coordinates application behavior through typed transient messages.
2. Direct typed calls remain normal for known required dependencies.
3. Point-to-point streams use typed queues rather than bus fan-out.
4. Bus messages and observability events remain semantically distinct.
5. Public bus vocabulary uses message rather than event.
6. A message declaration type is also its payload type.
7. Empty message types represent payload-free signals.
8. Authored metadata uses `solar::bus::Descriptor`.
9. Delivery policy is not part of message metadata.
10. The conventional component message alias is `Messages`.
11. Message contributions preserve semantic owner and registration origin.
12. All emitted messages require effective catalog registration.
13. Subscriptions do not implicitly take ownership of message declarations.
14. Component-local `Subscriptions` is the primary route declaration form.
15. Central `To` routes remain an explicit escape hatch.
16. The canonical handler is static `handle(const Message&)`.
17. Handlers accept `void`, `Status`, or `Result<void>`.
18. Handlers receive no system or context object.
19. Subscription identity is message, subscriber, and route tag.
20. Delivery policy is metadata rather than subscription identity.
21. Duplicate logical routes are compile-time errors.
22. Multiple routes for one subscriber and message require explicit tags.
23. `emit`, `try_emit`, and `try_emit_isr` are distinct APIs.
24. Emission returns a typed expected result.
25. Every route is attempted after an earlier route failure.
26. Async acceptance does not promise later handler success.
27. Zero subscribers is successful by default.
28. Required-subscriber topology is an explicit compile-time policy.
29. Every subscription explicitly selects a delivery mode.
30. Initial modes are inline, queued, latest, and coalesced.
31. `InlineIsr` makes ISR execution an explicit architectural promise.
32. Queued storage is independent per subscription.
33. Latest delivery retains the newest pending payload.
34. Coalesced delivery is restricted to empty signals initially.
35. Deferred routes target the system work queue or named executors.
36. The bus owns route state while executors own worker execution.
37. Solar does not own or drain the whole Zephyr system work queue.
38. Service mailbox delivery is deferred to Phase 9.
39. Initial overflow policies are reject, drop newest, drop oldest, and finite
    wait.
40. Intentional drop updates records without failing the complete emission.
41. Infinite wait is not an initial overflow policy.
42. Initial stop policies are drain and cancel pending.
43. In-flight handlers are contained through their executor, not individually
    aborted by the bus.
44. Inline ordering follows deterministic subscription catalog order.
45. Queued ordering is guaranteed only within one subscription.
46. Inline reentrant emission follows ordinary depth-first call behavior.
47. Concurrent emitters are supported without a global handler lock.
48. Asynchronously stored payloads are initially trivially copyable and owned.
49. Every asynchronous route uses exact typed static storage.
50. The bus requires no dynamic allocation.
51. ISR topology is compile-time validated and never silently filtered.
52. The built-in bus facility is present only when enabled and required.
53. Emission is available only while the complete system is `Running`.
54. Bus route quiescence integrates with Phase 3 executor containment.
55. Subscriber owners and route executors are facility dependencies.
56. Runtime records are focused per subscription and retain bounded counters.
57. The bus retains no generic payload history.
58. Metrics, events, logging, and Remote integrations are explicit adapters.
59. Kconfig owns build capabilities, defaults, and hard ceilings.
60. C++ types own message, route, executor, capacity, and delivery architecture.
61. Explicit route policy overrides blueprint policy, then Kconfig default.
62. Channels are removed from the blueprint and component lifecycle.
63. Point-to-point channel use migrates to a type-owned kernel queue.
64. There is no runtime bus registry or universal bus object.

## 43. Open Questions

There are no blocking open questions for Phase 5.

Later specifications must refine without changing this bus contract:

- final executor adapter concepts and names in Phase 9;
- exact Kconfig symbol names and default numeric ceilings;
- the eventual service mailbox model;
- explicit Remote bridge schema and authorization;
- optional metrics and observability adapters;
- stable shared-handle policy for large payloads;
- whether future C++ reflection can strengthen borrowed-member validation.

These are extensions of the accepted static routing, ownership, delivery,
boundedness, and lifecycle model.
