Those are both very natural Solar facilities, and they fit the architecture you have chosen unusually well.

I would treat them as two separate but related subsystems:

* **Bus**: distributes transient information and commands.
* **Parameters**: stores named runtime-adjustable state.

The bus says, “something happened.”

The parameter system says, “this value currently is.”

That distinction matters because it prevents the event system from becoming a strange database made of notifications, and prevents the parameter system from becoming an event stream with amnesia.

# 1. A typed Solar bus

I would strongly favour **types as the primary API**, with optional string names for remote inspection, logging, shell access, and serialization.

For example:

```cpp
namespace events {

struct ButtonAPressed {
    static constexpr std::string_view name =
        "button.a.pressed";
};

struct BatteryLow {
    static constexpr std::string_view name =
        "battery.low";

    float voltage;
};

struct PoseUpdated {
    static constexpr std::string_view name =
        "pose.updated";

    Pose pose;
};

}
```

Locally, users publish by type:

```cpp
solar::bus::emit(events::ButtonAPressed{});

solar::bus::emit(events::BatteryLow{
    .voltage = 10.4f
});
```

This gives you:

* compile-time payload checking
* no string typos
* easy discovery in code
* no casts
* no runtime event registry required for normal firmware use

The string name remains useful as external metadata:

```text
button.a.pressed
battery.low
pose.updated
```

That lets a shell, remote service, inspector, logger, or recorder identify the event in a human-readable form.

So I would not choose between strings and types. I would use:

> Types internally, names externally.

# 2. Separate event definition from delivery policy

An event should describe the data.

The subscription should describe how it is delivered.

For example:

```cpp
struct ButtonAPressed {
    static constexpr std::string_view name =
        "button.a.pressed";
};
```

The event itself should not decide whether it runs inline, through a work queue, or on a dedicated thread.

Instead:

```cpp
using Subscriptions = solar::Subscriptions<
    solar::Subscribe<
        events::ButtonAPressed,
        ToggleMode,
        solar::delivery::SystemWorkQueue>,

    solar::Subscribe<
        events::BatteryLow,
        EnterSafeMode,
        solar::delivery::Inline>,

    solar::Subscribe<
        events::PoseUpdated,
        PublishPose,
        solar::delivery::Coalesced<
            NavigationWorkQueue>>>;
```

That keeps event semantics and execution semantics cleanly separated.

# 3. Delivery policies

This is where the bus becomes genuinely useful rather than merely a glorified callback list.

I would expect Solar eventually to support several delivery styles.

## Inline

The subscriber executes immediately in the emitter’s context.

```cpp
solar::delivery::Inline
```

Equivalent conceptually to:

```cpp
handler(event);
```

Useful for:

* very small handlers
* deterministic synchronous propagation
* safety-critical immediate reactions
* internal state updates

Risks:

* the emitter inherits handler latency
* reentrancy becomes possible
* a slow subscriber slows every emitter
* unsafe from ISR context unless the handler is ISR-safe

Inline delivery should probably be the most explicit policy, not an invisible default.

## System work queue

The event is copied into work storage and posted to Zephyr’s system work queue.

```cpp
solar::delivery::SystemWorkQueue
```

Useful for:

* deferred response from interrupt context
* non-critical background work
* avoiding blocking the emitting component

Risks:

* shared queue congestion
* event ordering depends on queue behavior
* payload lifetime must be clear
* the system work queue may become an accidental bottleneck

## Named work queue

The handler is posted to a dedicated application work queue.

```cpp
solar::delivery::WorkQueue<ControlQueue>
solar::delivery::WorkQueue<TelemetryQueue>
solar::delivery::WorkQueue<StorageQueue>
```

This is probably the strongest general model.

For example:

```cpp
using Executors = solar::Executors<
    solar::WorkQueue<
        ControlQueue,
        solar::StackSize<2048>,
        solar::Priority<2>>,

    solar::WorkQueue<
        TelemetryQueue,
        solar::StackSize<2048>,
        solar::Priority<8>>>;
```

Then:

```cpp
solar::Subscribe<
    events::ButtonAPressed,
    ToggleDriveMode,
    solar::delivery::WorkQueue<ControlQueue>>
```

This lets the architecture express that different kinds of work have different scheduling importance.

## Dedicated thread mailbox

A service may already own a thread. Events can be delivered to its mailbox or queue.

```cpp
solar::delivery::ServiceMailbox<NavigationService>
```

The service thread then consumes:

```cpp
while (running) {
    auto event = Mailbox::receive();
    handle(event);
}
```

Useful where a service should serialize all its own state changes on one thread.

This avoids locking because only the service thread touches service state.

## Coalesced delivery

Only one pending work item exists for a subscription. Multiple events collapse into one pending invocation.

```cpp
solar::delivery::Coalesced<
    solar::delivery::WorkQueue<TelemetryQueue>>
```

Useful for events such as:

```text
metrics.changed
telemetry.dirty
display.refresh.requested
configuration.updated
```

If twenty updates arrive before the work runs, one refresh may be enough.

## Latest-value delivery

The newest payload replaces any older pending payload.

```cpp
solar::delivery::Latest<
    solar::delivery::WorkQueue<NavigationQueue>>
```

This is useful for high-frequency state-like data:

```cpp
PoseUpdated
VelocityCommandUpdated
JoystickUpdated
```

The consumer often wants the newest value, not an archaeological expedition through stale samples.

## Queued delivery

Every event is retained up to a fixed capacity.

```cpp
solar::delivery::Queued<
    Capacity<16>,
    WorkQueue<EventQueue>,
    OverflowPolicy<DropOldest>>
```

Useful for:

* discrete commands
* alarms
* button transitions
* protocol messages
* audit records

# 4. Overflow policy should be explicit

Once delivery is asynchronous, event storage becomes finite.

Solar should force the architecture to decide what happens when the queue is full.

For example:

```cpp
solar::overflow::DropNewest
solar::overflow::DropOldest
solar::overflow::Coalesce
solar::overflow::Reject
solar::overflow::Block
solar::overflow::Fault
```

Not every policy is valid in every context.

Blocking from an ISR is impossible.

Blocking a high-priority control thread on a telemetry subscriber would be an architectural booby trap.

Solar can catch some invalid combinations at compile time:

```cpp
static_assert(
    !(is_isr_emittable<Event> &&
      is_blocking_policy<Policy>),
    "ISR-emittable events cannot use blocking delivery");
```

# 5. Subscriber shape

A subscriber can be a static function object:

```cpp
struct ToggleMode {
    static void handle(
        const events::ButtonAPressed&) {

        ModeManager::toggle();
    }
};
```

Then:

```cpp
solar::Subscribe<
    events::ButtonAPressed,
    ToggleMode,
    solar::delivery::WorkQueue<ControlQueue>>
```

Or support a member-like convention:

```cpp
struct NavigationService {
    static void on(
        const events::PoseUpdated& event) {
        // ...
    }
};
```

Then:

```cpp
solar::Subscribe<
    events::PoseUpdated,
    NavigationService,
    solar::delivery::ServiceMailbox<
        NavigationService>>
```

Solar can detect one of several allowed handlers:

```cpp
Handler::handle(event);
Handler::on(event);
Handler{}(event);
```

I would choose one canonical form, probably:

```cpp
static void handle(const Event&);
```

Predictability is worth more than clever handler detection.

# 6. Where subscriptions live

There are two strong options.

## Central declaration

```cpp
using Subscriptions = solar::Subscriptions<
    solar::Subscribe<
        ButtonAPressed,
        ToggleMode,
        WorkQueue<ControlQueue>>,

    solar::Subscribe<
        BatteryLow,
        SafetyService,
        Inline>>;
```

Advantages:

* complete event topology visible in one place
* easy graph inspection
* easy duplicate detection
* easy policy review
* excellent for documentation

Disadvantages:

* subscription definition is separated from handler code

## Subscriber-local declaration

```cpp
struct SafetyService {
    using Subscriptions = solar::List<
        solar::On<
            events::BatteryLow,
            solar::delivery::Inline>,
        solar::On<
            events::Overcurrent,
            solar::delivery::Inline>>;

    static void handle(
        const events::BatteryLow&);
};
```

Advantages:

* subscriptions live near behavior
* easier component reuse
* less central boilerplate

Disadvantages:

* topology is more distributed
* execution policy becomes partly hidden inside components

I would use a hybrid:

* A component may declare what it consumes.
* The system may override delivery policy.

For example:

```cpp
struct SafetyService {
    using Consumes = solar::Events<
        events::BatteryLow,
        events::Overcurrent>;
};
```

Then:

```cpp
using Subscriptions = solar::Subscriptions<
    solar::Deliver<
        events::BatteryLow,
        SafetyService,
        solar::delivery::Inline>,

    solar::Deliver<
        events::Overcurrent,
        SafetyService,
        solar::delivery::Inline>>;
```

That is stronger, but perhaps more ceremony than you need initially.

For a first version, central subscriptions are probably simplest.

# 7. Bus runtime shape

The bus can remain entirely static.

Conceptually:

```cpp
template<typename Event>
struct EventRoute {
    using Subscribers =
        typename ActiveSystem::template
            subscribers_for<Event>;
};
```

Then:

```cpp
template<typename Event>
Status emit(const Event& event) {
    return dispatch<
        Event,
        typename EventRoute<Event>::
            Subscribers>(event);
}
```

A simplified dispatch:

```cpp
template<typename Event, typename... Subscriptions>
Status dispatch(
    const Event& event,
    TypeList<Subscriptions...>) {

    return combine(
        dispatchOne<Subscriptions>(event)...
    );
}
```

Each subscription policy handles its own delivery:

```cpp
template<
    typename Event,
    typename Handler>
struct InlineDelivery {

    static Status dispatch(
        const Event& event) {

        Handler::handle(event);
        return solar::ok();
    }
};
```

For work queue delivery, the subscription needs statically allocated storage.

Conceptually:

```cpp
template<
    typename Event,
    typename Handler,
    typename Queue>
struct WorkQueueDelivery {
    static inline PendingEvent<Event> pending;
    static inline k_work work;

    static Status dispatch(
        const Event& event) {

        pending.store(event);

        return Queue::submit(work);
    }
};
```

The exact design depends on whether you allow multiple queued copies, coalescing, and variable-size events.

# 8. Event payload constraints

For embedded predictability, I would place restrictions on asynchronously delivered events.

For example:

```cpp
template<typename Event>
concept AsyncEvent =
    std::is_trivially_copyable_v<Event> &&
    std::is_destructible_v<Event> &&
    sizeof(Event) <= max_event_size;
```

Or allow movable nontrivial values if you are comfortable managing them.

For Zephyr firmware, a strong first version could require:

* no dynamic allocation
* fixed payload size
* trivially copyable asynchronous events
* references forbidden for deferred delivery
* pointers allowed only by explicit policy

That catches this kind of bug:

```cpp
struct BadEvent {
    std::string_view message;
};
```

The pointed-to data may disappear before deferred handling.

Instead:

```cpp
struct LogMessage {
    FixedString<64> message;
};
```

Or make the event synchronous only.

You could provide traits:

```cpp
struct ButtonAPressed {
    using DeliverySafety =
        solar::event::Copyable;
};
```

But much of this can be inferred.

# 9. Events versus channels

You already mentioned channels in your broader architecture. It is useful to distinguish them clearly.

An event bus is good for:

```text
button pressed
fault occurred
parameter changed
service started
connection lost
```

A channel is good for:

```text
latest pose
stream of LiDAR scans
motor command queue
sensor samples
request-response exchange
```

A useful rule:

> Events represent occurrences. Channels represent data flow.

You can implement both over shared lower-level primitives, but the user-facing semantics should remain distinct.

# 10. The runtime parameter system

The second idea is extremely valuable.

I would call it one of:

* `Parameters`
* `Settings`
* `Properties`
* `Tuning`
* `Variables`

For Solar, I think **Parameters** is the strongest general term.

“Configuration” often implies compile-time or startup-only values.

“Settings” sounds user-interface-oriented.

“Properties” is broad but somewhat vague.

“Parameters” naturally covers:

* PID gains
* thresholds
* timeouts
* logging level
* calibration offsets
* feature flags
* runtime behavior tuning

You could name the facility:

```cpp
solar::facilities::Parameters
```

and individual definitions:

```cpp
params::DriveKp
params::DriveKi
params::LogLevel
```

# 11. A parameter is a type

Like your components and events, parameter identity can be represented by type.

```cpp
namespace params {

struct DriveKp {
    using Value = float;

    static constexpr std::string_view name =
        "drive.pid.kp";

    static constexpr Value default_value =
        1.2f;
};

struct DriveKi {
    using Value = float;

    static constexpr std::string_view name =
        "drive.pid.ki";

    static constexpr Value default_value =
        0.1f;
};

struct LoggingVerbosity {
    using Value = LogLevel;

    static constexpr std::string_view name =
        "logging.verbosity";

    static constexpr Value default_value =
        LogLevel::Info;
};

}
```

Usage:

```cpp
const float kp =
    solar::params::get<params::DriveKp>();

solar::params::set<params::DriveKp>(1.4f);
```

Or shorter:

```cpp
params::DriveKp::get();
params::DriveKp::set(1.4f);
```

I would probably keep the storage facility explicit:

```cpp
solar::parameters::get<DriveKp>()
```

because it avoids making parameter definition types themselves stateful.

But either is coherent.

# 12. Parameter metadata

Parameters become much more useful when they carry metadata.

```cpp
struct DriveKp {
    using Value = float;

    static constexpr std::string_view name =
        "drive.pid.kp";

    static constexpr std::string_view description =
        "Proportional gain for drive velocity control";

    static constexpr Value default_value = 1.2f;
    static constexpr Value minimum = 0.0f;
    static constexpr Value maximum = 10.0f;

    static constexpr std::string_view units = "";
};
```

Then inspection or a remote UI can automatically produce:

```text
drive.pid.kp
type: float
value: 1.2
range: 0.0 to 10.0
persistent: yes
description: Proportional gain for drive velocity control
```

That is where type-level definitions become especially powerful. You get runtime control without surrendering compile-time structure.

# 13. Validation policy

A parameter should validate before accepting a new value.

For simple ranges:

```cpp
using Validation =
    solar::validation::Range<0.0f, 10.0f>;
```

For enums:

```cpp
using Validation =
    solar::validation::OneOf<
        LogLevel::Trace,
        LogLevel::Debug,
        LogLevel::Info,
        LogLevel::Warn,
        LogLevel::Error>;
```

For custom validation:

```cpp
struct DriveKp {
    using Value = float;

    static bool validate(Value value) {
        return std::isfinite(value) &&
               value >= 0.0f &&
               value <= 10.0f;
    }
};
```

Then:

```cpp
auto result =
    solar::parameters::set<DriveKp>(12.0f);
```

returns:

```cpp
ParameterError::ValidationFailed
```

The current value remains unchanged.

# 14. Persistence policy

Persistence should absolutely be policy-driven.

For example:

```cpp
struct DriveKp {
    using Value = float;

    static constexpr Value default_value = 1.2f;

    using Persistence =
        solar::persistence::Stored<
            solar::storage::SettingsPartition>;
};
```

For volatile values:

```cpp
using Persistence =
    solar::persistence::Volatile;
```

For delayed persistence:

```cpp
using Persistence =
    solar::persistence::Deferred<
        2s,
        solar::storage::SettingsPartition>;
```

For explicit-save values:

```cpp
using Persistence =
    solar::persistence::Manual;
```

This distinction is important because writing flash on every change is dangerous for both performance and endurance.

A PID slider dragged through fifty values should not produce fifty immediate flash writes.

# 15. Useful persistence modes

I would consider these.

## Volatile

Value resets to default at boot.

```cpp
solar::persistence::Volatile
```

Useful for:

* temporary debug levels
* transient operating modes
* runtime experiments

## Load-on-boot, write-immediately

```cpp
solar::persistence::Immediate<Storage>
```

Useful for rarely changed parameters.

Dangerous for rapidly updated values.

## Deferred write

Changes update RAM immediately, then persist after a quiet period.

```cpp
solar::persistence::Deferred<
    2s,
    Storage>
```

This is excellent for UI-driven tuning.

Every change restarts the timer. Only the final settled value is written.

## Batched transaction

Several parameters are committed together:

```cpp
solar::parameters::beginTransaction();

set<DriveKp>(1.4f);
set<DriveKi>(0.2f);
set<DriveKd>(0.01f);

solar::parameters::commit();
```

Useful where related values should be atomically consistent.

## Manual save

The runtime value changes, but persistence occurs only when explicitly requested:

```cpp
solar::parameters::save<DriveKp>();
```

Useful for experimentation where the user chooses whether to retain tuning.

# 16. Parameter declarations in the system

You might add a section:

```cpp
using Parameters = solar::Parameters<
    params::DriveKp,
    params::DriveKi,
    params::DriveKd,
    params::LoggingVerbosity,
    params::BatteryLowThreshold>;
```

Then:

```cpp
using System = solar::System<
    Board,
    Peripherals,
    Devices,
    Facilities,
    Services,
    Tasks,
    Channels,
    Parameters,
    Policies>;
```

Or treat parameters as a facility contribution.

For example:

```cpp
struct DriveController {
    using Parameters = solar::Parameters<
        params::DriveKp,
        params::DriveKi,
        params::DriveKd>;
};
```

Solar could collect parameter contributions from components.

That is an appealing long-term model:

```cpp
struct NavigationService {
    using Parameters = solar::Parameters<
        params::ObstacleThreshold,
        params::SearchSpeed>;
};
```

Then the top-level system does not need one enormous parameter list.

Solar flattens all parameter contributions into a single registry and validates uniqueness.

You could support both:

* local component contributions
* explicit global parameters

# 17. Parameter storage

Internally:

```cpp
template<typename Parameter>
struct ParameterSlot {
    using Value = typename Parameter::Value;

    static inline Value value =
        Parameter::default_value;

    static inline bool dirty = false;
    static inline std::uint32_t version = 0;
};
```

Then:

```cpp
template<typename Parameter>
const typename Parameter::Value& get() {
    return ParameterSlot<Parameter>::value;
}
```

And:

```cpp
template<typename Parameter>
Status set(
    typename Parameter::Value value) {

    if (!validate<Parameter>(value)) {
        return error(
            ParameterError::ValidationFailed);
    }

    auto& slot = ParameterSlot<Parameter>{};

    if (slot.value == value) {
        return ok();
    }

    const auto old_value = slot.value;
    slot.value = value;
    slot.version++;

    applyPersistencePolicy<Parameter>();
    notifyChanged<Parameter>(
        old_value,
        value);

    return ok();
}
```

Again, all static, deterministic, and allocation-free.

# 18. Parameter change notifications

This is where the bus and parameter systems meet naturally.

When:

```cpp
solar::parameters::set<DriveKp>(1.4f);
```

succeeds, Solar can emit:

```cpp
solar::events::ParameterChanged<DriveKp>
```

with:

```cpp
template<typename Parameter>
struct ParameterChanged {
    using Value = typename Parameter::Value;

    Value old_value;
    Value new_value;
};
```

Then a controller may subscribe:

```cpp
using Subscriptions = solar::Subscriptions<
    solar::Subscribe<
        solar::events::ParameterChanged<
            params::DriveKp>,
        DriveController,
        solar::delivery::Inline>>;
```

Handler:

```cpp
struct DriveController {
    static void handle(
        const ParameterChanged<
            params::DriveKp>& event) {

        pid_.setKp(event.new_value);
    }
};
```

Or the control loop simply reads the parameter each iteration:

```cpp
pid_.setKp(
    solar::parameters::get<
        params::DriveKp>());
```

Which approach is better depends on frequency.

For values used every millisecond, copying into local controller state on change may be cleaner.

For rarely read values, direct lookup is fine.

# 19. Parameter access policy

Some parameters should be externally writable. Others should not.

You may want:

```cpp
using Access =
    solar::access::ReadWrite;
```

Other possibilities:

```cpp
solar::access::ReadOnly
solar::access::InternalOnly
solar::access::RemoteReadOnly
solar::access::RemoteReadWrite
solar::access::Privileged
```

For example:

```cpp
struct DeviceSerialNumber {
    using Value = FixedString<24>;

    using Access =
        solar::access::ReadOnly;

    using Persistence =
        solar::persistence::Stored<
            FactoryPartition>;
};
```

Or:

```cpp
struct EmergencyStopActive {
    using Value = bool;

    using Access =
        solar::access::InternalOnly;

    using Persistence =
        solar::persistence::Volatile;
};
```

The remote service can inspect access metadata before permitting a change.

# 20. Startup behavior

A parameter system needs a deterministic boot flow.

Conceptually:

```text
1. Register parameter definitions
2. Initialize persistence backend
3. Load persisted records
4. Validate loaded values
5. Use defaults for missing or invalid values
6. Report migrations or corruption
7. Mark parameter facility ready
8. Initialize dependent components
```

This implies that a component depending on runtime parameters should depend on the parameter facility:

```cpp
using Dependencies =
    solar::Requires<
        solar::facilities::Parameters>;
```

More specifically, Solar could derive dependency on individual parameters, but that may be too fine-grained.

# 21. Persistence versioning and migration

This becomes important surprisingly quickly.

Suppose a parameter changes from:

```cpp
float
```

to:

```cpp
std::uint16_t
```

or its key is renamed.

Persisted data from old firmware may no longer match.

Each parameter can have a stable ID and version:

```cpp
struct DriveKp {
    static constexpr std::uint32_t id =
        0x81A41E2B;

    static constexpr std::uint16_t version = 1;
};
```

Or derive an ID from the name, though explicit stable IDs are safer across renames.

Storage records might contain:

```cpp
struct ParameterRecordHeader {
    std::uint32_t id;
    std::uint16_t version;
    std::uint16_t size;
    std::uint32_t checksum;
};
```

On mismatch:

* migrate
* reject and use default
* preserve as unknown
* signal configuration incompatibility

For an initial version, defaulting on mismatch is acceptable. But leave room in the storage format for versioning.

# 22. Atomic snapshots

Some parameter groups should update together.

Consider PID gains:

```cpp
Kp
Ki
Kd
```

Changing them one at a time may briefly create an undesirable intermediate controller state.

You could define a grouped parameter:

```cpp
struct DrivePid {
    struct Value {
        float kp;
        float ki;
        float kd;
    };

    static constexpr Value default_value{
        .kp = 1.2f,
        .ki = 0.1f,
        .kd = 0.02f
    };
};
```

Then:

```cpp
solar::parameters::set<DrivePid>({
    .kp = 1.4f,
    .ki = 0.2f,
    .kd = 0.01f
});
```

This is atomically coherent.

That may be better than three independent parameters unless external tooling strongly benefits from separate values.

Another option is transactional updates.

# 23. Concurrency model

Parameter reads and writes may occur across threads.

You need to decide whether:

```cpp
get<DriveKp>()
```

is lock-free, atomic, or mutex-protected.

For scalar types:

```cpp
float
int
bool
enum
```

you may use atomic storage if supported appropriately.

For larger structures, use:

* a mutex
* sequence lock
* double buffer
* copy-under-lock
* immutable snapshot replacement

A simple first model:

```cpp
template<typename Parameter>
struct ParameterSlot {
    static inline k_mutex mutex;
    static inline Value value;
};
```

Reads:

```cpp
Value get() {
    Lock lock{mutex};
    return value;
}
```

Returning by value is important because returning a reference after unlocking would expose races.

For control-loop parameters, locking every cycle may be undesirable. There, cache-on-change is cleaner.

# 24. Remote integration

These two facilities become especially powerful together with your remote service.

A remote inspector could expose:

```text
events
parameters
components
metrics
logs
```

Parameter protocol operations:

```text
parameter.list
parameter.get
parameter.set
parameter.save
parameter.reset
parameter.describe
```

Event protocol operations:

```text
event.subscribe
event.unsubscribe
event.emit
event.list
```

You should probably not allow arbitrary remote event emission by default.

A remote user should not necessarily be able to fabricate:

```text
battery.low
motor.overcurrent
safety.trip
```

You may need event access policy:

```cpp
using RemoteAccess =
    solar::remote::ObserveOnly;
```

Or:

```cpp
solar::remote::NotExposed
solar::remote::Observable
solar::remote::Emittable
```

The same type metadata can drive the remote interface automatically.

# 25. A combined example

```cpp
namespace app::events {

struct ButtonAPressed {
    static constexpr std::string_view name =
        "button.a.pressed";

    using RemoteAccess =
        solar::remote::Observable;
};

struct DriveModeChanged {
    static constexpr std::string_view name =
        "drive.mode.changed";

    DriveMode mode;
};

}
```

Parameters:

```cpp
namespace app::params {

struct DriveSpeed {
    using Value = float;

    static constexpr std::string_view name =
        "drive.speed";

    static constexpr Value default_value = 0.5f;

    using Validation =
        solar::validation::Range<0.0f, 1.0f>;

    using Persistence =
        solar::persistence::Deferred<
            2s,
            solar::storage::Settings>;

    using Access =
        solar::access::RemoteReadWrite;
};

struct LoggingVerbosity {
    using Value = LogLevel;

    static constexpr std::string_view name =
        "logging.verbosity";

    static constexpr Value default_value =
        LogLevel::Info;

    using Persistence =
        solar::persistence::Stored<
            solar::storage::Settings>;

    using Access =
        solar::access::RemoteReadWrite;
};

}
```

Handler:

```cpp
struct ToggleDriveMode {
    static void handle(
        const events::ButtonAPressed&) {

        const auto current =
            DriveModeManager::mode();

        const auto next =
            current == DriveMode::Manual
                ? DriveMode::Autonomous
                : DriveMode::Manual;

        DriveModeManager::setMode(next);

        solar::bus::emit(
            events::DriveModeChanged{
                .mode = next
            });
    }
};
```

Subscriptions:

```cpp
using Subscriptions =
    solar::Subscriptions<
        solar::Subscribe<
            events::ButtonAPressed,
            ToggleDriveMode,
            solar::delivery::WorkQueue<
                ControlQueue>>>;

```

Parameters:

```cpp
using Parameters =
    solar::Parameters<
        params::DriveSpeed,
        params::LoggingVerbosity>;
```

System:

```cpp
using System = solar::System<
    Board,
    Peripherals,
    Devices,
    Facilities,
    Services,
    Tasks,
    Channels,
    Parameters,
    Subscriptions,
    Executors,
    Policies>;
```

# 26. What I would implement first

For the bus, begin with only:

```cpp
emit<Event>(event)
Subscribe<Event, Handler, Delivery>
Inline
WorkQueue<Queue>
```

Require asynchronously delivered events to be trivially copyable and bounded in size.

Then add:

```cpp
Coalesced
Latest
Queued
OverflowPolicy
ISR-safe emission
```

only after the basic architecture is stable.

For parameters, begin with:

```cpp
Parameter type
get<Parameter>()
set<Parameter>()
default value
validation
volatile storage
persistent storage
parameter-changed event
```

Then add:

```cpp
deferred writes
transactions
versioning
remote access
reset-to-default
grouped updates
```

# 27. The key boundaries

The bus should not become a universal replacement for direct calls.

This is still perfectly good:

```cpp
LeftMotor::setSpeed(50);
```

You do not need:

```cpp
emit(SetLeftMotorSpeed{50});
```

unless asynchronous decoupling is actually useful.

Likewise, parameters should not replace ordinary component state.

This belongs inside a component:

```cpp
static inline int retry_count_;
```

This belongs in parameters:

```cpp
communications.max_retry_count
```

because it is meaningful to inspect, tune, persist, or modify.

A useful rule:

> Use the bus when producers should not know who reacts.
> Use direct calls when the dependency is intentional and immediate.
> Use parameters when a value should be globally identifiable, inspectable, and potentially adjustable.

With those boundaries, both systems add real leverage without turning Solar into a framework labyrinth.
