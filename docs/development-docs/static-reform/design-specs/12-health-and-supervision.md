# Health, Safety, Supervision, And Expanded Kernel

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 12

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`
- `01-system-blueprint-and-binding.md`
- `02-identity-contributions-and-catalogs.md`
- `03-lifecycle-kernel-and-configuration.md`
- `04-bus.md`
- `05-parameters.md`
- `06-events.md`
- `07-metrics.md`
- `08-logging.md`
- `09-tasks-and-executors.md`
- `10-remote.md`
- `11-inspection.md`

## 1. Purpose

This specification defines three cooperating architectural layers:

1. an expanded `solar::kernel` providing typed C++23 access to the useful
   public Zephyr kernel surface;
2. a static `solar::health` facility that owns health-specific monitoring and
   assessment truth;
3. an optional `solar::supervisor` service that evaluates due monitors, applies
   explicit response policy, coordinates recovery, and gates watchdog feeding.

The design provides early warning and useful failure evidence without claiming
that software can prove every thread, device, or robot behavior safe.

It establishes:

- automatic Health records for every effective component;
- zero-ceremony generic lifecycle and execution monitoring;
- compact push-based component self-reporting;
- optional component-local aggregate assessment, named checks, and recovery;
- generic progress, stack-margin, execution, and thread monitors;
- independent readiness, liveness, safety, condition, and freshness facts;
- bounded assessment state and transition history;
- one dedicated Supervisor service with configurable monitoring cadence;
- explicit local containment, local recovery, and supervisory escalation;
- independent watchdog gating based on successful supervision cycles;
- honest separation of direct facts, reported evidence, and inference;
- panic-safe handling of fatal Zephyr failures;
- broad Kernel coverage without creating another scheduler or hardware HAL.

The common application path remains compact:

```cpp
solar::health::report<Imu>(solar::health::degraded(error));
solar::health::report<Imu>(solar::health::nominal());
```

Rich components opt into only the additional structure they need:

```cpp
struct Imu
{
    struct Health
    {
        static solar::Result<solar::health::Assessment> assess();
        static solar::Result<void> recover();
    };
};
```

## 2. Non-Goals

Health and Supervisor are not:

- proof of functional correctness;
- proof that a running thread performs useful work;
- proof that absence of reported errors means safety;
- a replacement for hardware interlocks or emergency-stop circuitry;
- a replacement for component-owned immediate containment;
- a universal application safety policy;
- automatic permission to restart, stop, reboot, or actuate hardware;
- duplicate storage for Events, Metrics, Logging, Lifecycle, Execution, or
  Remote records;
- a reason to invoke arbitrary application behavior from an event observer;
- a universal system snapshot;
- an unbounded incident database;
- a scheduler, tracing framework, or debugger;
- a guarantee that a corrupted kernel can safely recover;
- a requirement that every component define custom health code;
- a hidden periodic worker running on the system workqueue.

The expanded Kernel is not:

- a replacement Zephyr kernel;
- a second scheduler;
- an application execution registry;
- a lifecycle graph;
- a hardware driver abstraction;
- a devicetree wrapper;
- a supervisory policy engine;
- a mandate to wrap unstable internal Zephyr symbols;
- a reason to hide native semantics, context restrictions, or errno details.

## 3. Vocabulary

- **subject**: the component, executor, service, subsystem, or explicitly
  registered resource being assessed;
- **monitor**: one declared source of evidence about a subject;
- **observation**: one timestamped monitor result;
- **assessment**: Health's current interpretation of applicable observations;
- **condition**: the overall operational classification of a subject;
- **fault**: an abnormal condition or defect that may cause failure;
- **failure**: inability to perform a required function;
- **liveness**: whether required execution or progress continues;
- **readiness**: whether a subject can currently accept useful work;
- **availability**: whether a capability can currently be accessed;
- **safety**: whether continued operation satisfies declared application
  constraints;
- **progress marker**: an atomic generation or timestamp advanced after useful
  work;
- **check**: an optional bounded pull assessment provided by a component;
- **self-report**: a component-pushed assessment of its own domain state;
- **containment**: immediate action limiting the consequence of a fault;
- **recovery**: an explicit attempt to restore required operation;
- **response**: a Supervisor action selected by policy;
- **escalation**: transition from observation or local recovery to stronger
  system-level response;
- **watchdog gate**: a required condition that must pass before watchdog feed;
- **direct evidence**: a fact obtained from the canonical owner or platform;
- **reported evidence**: a fact asserted by the subject itself;
- **inferred evidence**: a conclusion such as stall derived from missing
  progress over time;
- **fatal path**: panic-safe processing after Zephyr declares the system or
  current execution irrecoverably failed.

## 4. Architectural Decision

### 4.1 Four responsibility layers

```text
                 Zephyr kernel and hardware facts
                              |
                              v
                    +------------------+
                    |  solar::kernel   |
                    | typed primitives |
                    | direct facts     |
                    +--------+---------+
                             |
       canonical subsystem records and component reports
                             |
                             v
                    +------------------+
                    |  solar::health   |
                    | monitors         |
                    | assessments      |
                    +--------+---------+
                             |
                             v
                    +------------------+
                    | solar::supervisor|
                    | evaluate / act   |
                    | watchdog gate    |
                    +--------+---------+
                             |
                  explicit application actions
```

Kernel reports direct platform facts. Execution reports Solar execution facts.
Health interprets evidence. Supervisor applies policy. Application components
own domain-specific containment and recovery mechanisms.

### 4.2 Separate Health and Supervisor

Health and Supervisor are separate cooperating subsystems.

Health is a static facility because:

- components must report health without an object reference;
- health records remain useful when active supervision is disabled;
- tests and diagnostics need deterministic direct access;
- health state has one canonical system-wide owner.

Supervisor is a service because:

- periodic checking requires independent execution;
- policy responses may block within declared bounds;
- watchdog feeding requires an active liveness domain;
- supervision must have lifecycle, stack, priority, and failure records.

Enabling Supervisor requires and automatically includes Health. Enabling Health
does not require Supervisor.

### 4.3 No circular self-assessment

Supervisor may publish its cycle and response records into Health as ordinary
reported evidence. It cannot authoritatively decide that it is live or healthy.

If Supervisor stalls:

- its progress record becomes stale;
- local inspection can reveal the last completed cycle;
- no further response policy runs;
- watchdog feeding stops;
- an independent hardware watchdog may reset the system.

## 5. Health Subject Model

### 5.1 Automatic component subjects

Every effective lifecycle component receives one Health subject when Health is
enabled. Users do not list components again in a Health catalog.

This includes:

- devices;
- facilities;
- services;
- lifecycle-owning executors;
- other future component categories accepted by the system graph.

Tasks remain execution leaves. A task may receive a focused Health subject when
it contributes a monitor or its execution registration requests one, but tasks
do not become lifecycle components merely for Health.

### 5.2 Additional subjects

Subsystems and applications may contribute non-component subjects where the
identity is meaningful, such as:

- one shared bus route;
- one storage region;
- one external communication link;
- one safety domain;
- one critical task registration.

Additional subjects use Phase 2 owner and origin metadata and must have bounded
static identity. They are not created dynamically at runtime.

### 5.3 Subject ownership

A subject has one semantic owner. Its Health record references the canonical
component or descriptor identity and does not copy names and ownership strings
into each runtime record.

## 6. Assessment Model

### 6.1 Orthogonal dimensions

Health does not collapse every fact into one enum. The baseline record is:

```cpp
struct HealthRecord
{
    SubjectRef subject{};
    Condition condition{Condition::Unknown};
    Liveness liveness{Liveness::Unknown};
    Readiness readiness{Readiness::Unknown};
    Safety safety{Safety::Unknown};
    Freshness freshness{Freshness::Unknown};

    kernel::TimePoint assessed_at{};
    kernel::TimePoint last_transition{};
    std::uint32_t transition_count{};
    std::optional<Error> last_error{};
    std::optional<EvidenceRef> primary_evidence{};
};
```

Initial dimension values are:

```cpp
enum class Condition {
    Unknown,
    Nominal,
    Degraded,
    Faulted,
};

enum class Liveness {
    Unknown,
    Live,
    Late,
    Stalled,
    Exited,
};

enum class Readiness {
    Unknown,
    Ready,
    NotReady,
};

enum class Safety {
    Unknown,
    Acceptable,
    AtRisk,
    Unsafe,
};
```

Availability and support remain source-result properties rather than believable
health values. A disabled or unsupported monitor does not report Nominal.

### 6.2 Convenience assessments

The common constructors are compact:

```cpp
solar::health::nominal();
solar::health::degraded(error);
solar::health::faulted(error);
solar::health::recovering(error);
```

`recovering()` is an assessment helper, not a distinct permanent condition. It
normally represents Degraded, NotReady, and direct reported recovery state.

Rich callers may construct all dimensions explicitly.

### 6.3 Evidence quality

Observations use deterministic categories:

```cpp
enum class EvidenceQuality {
    Direct,
    Reported,
    Inferred,
};
```

Solar does not assign arbitrary numeric confidence scores. Freshness,
availability, source identity, and evidence quality communicate what is known.

### 6.4 Successful fault versus failed assessment

A component assessment hook returns `Result<Assessment>`:

```cpp
return solar::health::faulted(connection_error);
```

means the assessment succeeded and found a fault.

```cpp
return std::unexpected(check_error);
```

means Solar could not complete the assessment. These states remain distinct.

## 7. Zero-Ceremony Component Participation

### 7.1 No custom declaration

A component with no nested Health declaration still receives:

- lifecycle state evidence;
- service or executor execution evidence where applicable;
- unexpected-exit evidence;
- optional stack and runtime diagnostics for Solar-owned threads;
- an ordinary Health record and focused query API.

No component is required to provide `Health`, `Checks`, descriptors, or policy.

### 7.2 Push-based self-reporting

Any registered component may publish its current domain assessment:

```cpp
solar::health::report<Imu>(solar::health::degraded(error));
```

Recovery or return to nominal state is explicit:

```cpp
solar::health::report<Imu>(solar::health::nominal());
```

Absence of another fault report never implies recovery.

### 7.3 Catalog registration modes

`report<Component>()` requires `Component` to be an effective subject in the
bound system. An unknown type is a strict compile-time error or relaxed
`NotRegistered` result. Relaxed use before Health frontend binding returns
`NotReady`; a Kconfig-disabled Health facility returns `Disabled`.

Explicit test systems use:

```cpp
solar::health::Of<TestApplication>::report<Imu>(assessment);
```

### 7.4 Report semantics

`report`:

- attributes evidence to the reporting subject;
- records `EvidenceQuality::Reported`;
- coherently updates current self-assessment state;
- updates transition counters and timestamps;
- schedules or wakes Supervisor when configured and appropriate;
- does not invoke recovery or response behavior inline;
- returns a typed receipt or structured error.

Repeated equivalent reports update freshness and occurrence count without
creating an unbounded transition history.

## 8. Optional Component Health Contract

### 8.1 Nested contract

Components needing richer participation provide one nested `Health` type:

```cpp
struct Imu
{
    struct Health
    {
        static solar::Result<solar::health::Assessment> assess();
        static solar::Result<void> recover();
    };
};
```

The nested shape avoids a separate signal type naming `Imu` while `Imu` names
the signal. Solar infers the owner while normalizing the component catalog.

### 8.2 Optional hooks

All nested hooks are optional:

- `assess()` provides one component aggregate pull assessment;
- `Checks` contributes named focused checks;
- `recover()` advertises one bounded recovery capability.

Presence of nested `Health` does not require all three.

### 8.3 No Solar template parameter

Health hooks are ordinary static functions. They receive no System, Use,
Context, or runtime object:

```cpp
solar::Result<solar::health::Assessment>
Imu::Health::assess()
{
    if (!Imu::connected()) {
        return solar::health::faulted(Imu::last_error());
    }

    return solar::health::nominal();
}
```

The out-of-line implementation may include direct dependency headers and the
composition root according to Phase 1 include-direction rules.

### 8.4 Assessment cadence

`assess()` uses the Supervisor default check cadence unless the nested contract
declares a static override:

```cpp
static constexpr auto period = 2s;
```

An assessment hook must be bounded and thread-context safe. Solar cannot inspect
the function body to prove this contract.

## 9. Named Checks

### 9.1 Optional detailed checks

A component may expose separately queryable checks:

```cpp
struct Imu
{
    struct Health
    {
        struct Connection
        {
            static solar::Result<solar::health::Observation> check();
        };

        struct DataValidity
        {
            static constexpr auto period = 2s;
            static constexpr bool required = true;

            static solar::Result<solar::health::Observation> check();
        };

        using Checks = solar::health::Checks<Connection, DataValidity>;
    };
};
```

Check types do not reference their owner. Owner, origin, and local identity are
assigned while collecting `Imu::Health::Checks`.

### 9.2 Check result

An Observation contains:

- the affected dimensions;
- evidence quality;
- source time where meaningful;
- optional structured error;
- optional bounded detail code;
- freshness policy;
- whether the observation represents an explicit recovery.

Human-readable prose belongs in descriptors and formatting, not unbounded
runtime strings.

### 9.3 Pull checks are optional

Push reporting is preferred when a component already knows its state. Pull
checks are appropriate for:

- inexpensive self-tests;
- canonical state that changes without an event;
- periodic validation of invariants;
- platform diagnostics requiring active sampling.

A pull check must not perform an unbounded bus transaction, wait forever, or
depend on the same stalled execution context it is intended to diagnose.

## 10. Generic Monitor Catalog

### 10.1 Monitor categories

Initial generic monitor families are:

```cpp
solar::health::Progress<Period>
solar::health::StackMargin<Bytes>
solar::health::Execution
solar::health::Signal
solar::health::Check<Checker>
```

Internal adapters may add focused monitors for lifecycle, events, metrics,
logging loss, Remote links, and future hardware records.

### 10.2 Automatic execution monitors

Solar-owned service and executor threads automatically contribute:

- created, started, running, stop-requested, and exited facts;
- expected versus unexpected exit;
- configured priority and stack size;
- deadline, overrun, backlog, and missed-release facts where available;
- optional native runtime diagnostics.

Users do not declare `Execution` merely to obtain these basic facts.

### 10.3 Explicit semantic progress

A running or scheduled thread is not proof of useful progress. Components that
require semantic liveness declare a progress monitor and advance it only after
meaningful work:

```cpp
struct ControlService
{
    struct Health
    {
        using Checks = solar::health::Checks<
            solar::health::Progress<500ms>,
            solar::health::StackMargin<256>>;
    };
};
```

```cpp
void ControlService::run(solar::kernel::StopToken stop)
{
    while (!stop.requested()) {
        update_control();
        solar::health::progress<ControlService>();
    }
}
```

`progress<Component>()` is atomic, allocation free, non-blocking, and ISR-safe.
It advances a generation and records monotonic time. It does not perform
assessment or response work inline.

### 10.4 Heartbeat versus progress

Solar uses progress vocabulary by default. A heartbeat proves only that code
reached the heartbeat call. Applications may call that useful liveness, but the
descriptor should state what the marker semantically follows.

### 10.5 Direct, reported, and inferred results

Examples:

- thread exited: direct;
- component says connection lost: reported;
- progress marker did not advance before deadline: inferred;
- hardware stack guard fired: direct fatal evidence;
- runtime cycles stayed constant: direct scheduler evidence but only inferred
  semantic stall.

## 11. Thread And Stack Monitoring

### 11.1 Known threads first

Health monitors statically known Solar-owned threads directly through their
Kernel handles and Execution records.

It does not normally enumerate all Zephyr threads. `k_thread_foreach()` holds a
global kernel spinlock while callbacks run, while the unlocked form permits
thread-lifetime races. Global enumeration remains an optional diagnostic
capability with explicit cost and lifetime constraints.

### 11.2 Monitoring cadence

The default Supervisor base cadence is approximately two cycles per second:

```text
CONFIG_SOLAR_SUPERVISOR_PERIOD_MS=500
```

This does not mean every monitor runs every cycle.

Recommended defaults:

- event-driven reports: immediate admission;
- progress and required deadline checks: each due cycle;
- execution exit and lifecycle facts: each cycle;
- runtime-cycle deltas: each cycle or every few cycles;
- stack high-water scan: rotated across threads over several seconds;
- expensive platform checks: explicit slower cadence.

### 11.3 Rotating stack scans

Stack high-water measurement scans initialized stack memory and can be
comparatively expensive. Supervisor maintains a cursor over known threads and
checks only a bounded number per cycle.

The record states:

- measurement time;
- configured stack bytes;
- unused and used bytes;
- configured warning margin;
- whether the value is available;
- whether the platform forbids current-thread inspection;
- whether the measurement is stale.

### 11.4 Stack warning versus overflow

Stack margin is early-warning evidence. It is not overflow protection.

Actual overflow detection uses applicable Zephyr features:

- hardware stack protection;
- userspace memory protection;
- stack sentinel;
- compiler stack canaries;
- fatal stack-check reason.

An actual stack overflow may already have corrupted memory. It enters the fatal
path and is not treated as an ordinary recoverable Health transition.

### 11.5 Runtime statistics

Thread runtime cycles and utilization indicate scheduler activity. They do not
prove useful progress. Health may combine runtime deltas with explicit progress
markers, but preserves their separate evidence identities.

### 11.6 Stall and starvation honesty

Solar can infer a stall when a required progress deadline expires. It cannot in
general prove whether the cause is:

- deadlock;
- livelock;
- starvation;
- blocked I/O;
- priority inversion;
- missing external input;
- application logic failure.

The assessment reports the observed and inferred facts without inventing a
root cause.

## 12. Health Aggregation

### 12.1 Required and advisory monitors

Each monitor is required or advisory.

- a failing required monitor can make its subject Faulted;
- a warning required monitor normally makes it Degraded;
- an advisory monitor may warn without changing overall condition;
- an unavailable required monitor produces Unknown or Degraded according to
  explicit policy, never Nominal by omission.

### 12.2 Component evidence does not override generic evidence

A component self-report contributes evidence but cannot erase other required
monitors:

```text
Imu self-assessment = Nominal
thread progress      = Stalled
stack margin         = Warning
```

The final result cannot be Nominal.

### 12.3 Hysteresis and confirmation

Monitor policy may declare:

- failures required before transition;
- successes required before recovery;
- warning and fault thresholds;
- minimum active duration;
- cooldown;
- latching;
- stale timeout.

Defaults come from Kconfig and Health policy. Components override only where
their semantics require it.

### 12.4 Recovery is explicit

Recovery requires one of:

- explicit component nominal report;
- successful component assessment;
- successful named check;
- declared resolving canonical event;
- successful Supervisor recovery followed by confirmed evidence.

Elapsed time without another fault is not recovery.

### 12.5 Whole-system state

```cpp
solar::health::state();
```

returns the focused whole-system Health summary. This is not a universal system
snapshot. It derives only from Health subjects and required safety policy.

The system summary preserves:

- worst required condition;
- readiness of required domains;
- system safety state;
- count of degraded and faulted subjects;
- stale or unknown required evidence;
- primary evidence reference;
- assessment generation and time.

## 13. Health Runtime Storage

Health owns distributed type-derived static storage for:

- one current record per effective subject;
- one current record per monitor;
- atomic progress generations and times;
- self-report state;
- miss, failure, recovery, and transition counters;
- one bounded transition history when enabled;
- one bounded ISR report ingress when enabled;
- one system summary record.

Health does not own copies of source event, log, metric, lifecycle, execution,
or Remote histories. Evidence references identify those sources where stable
references are available.

All capacities are compile-time bounded. Core Health allocates no dynamic memory
after boot.

## 14. Health Concurrency

### 14.1 Thread reporting

Ordinary `report<Component>()` is thread-safe and performs one short coherent
copy under the Health mutex. It never invokes component hooks, formatters,
events, logs, Remote, or Supervisor actions while holding that mutex.

### 14.2 ISR reporting

Rich assessment commits are not generally ISR-safe. ISR sources use an explicit
bounded non-blocking form:

```cpp
solar::health::try_report_isr_from<Component>(compact_observation);
```

It admits a trivially copyable compact record into reserved ingress storage.
Full assessment occurs later in thread context. Overflow is counted and
reported; ISR calls never wait.

### 14.3 Progress markers

`progress<Component>()` is the fast ISR-safe path and uses atomic generation and
time storage appropriate to the platform.

### 14.4 Check invocation

Supervisor invokes `assess()` and named `check()` outside every Health mutex.
It then commits the owned result coherently. Formatting and subsystem adapters
also run after locks are released.

## 15. Health Query API

The focused global API is:

```cpp
solar::health::state();
solar::health::subjects();
solar::health::record<Imu>();
solar::health::monitors<Imu>();
solar::health::conditions();
solar::health::history::read(cursor, destination);
```

Explicit-system access is:

```cpp
solar::health::Of<TestApplication>::record<Imu>();
```

Typed direct queries return coherent bounded copies. Enumeration uses immutable
descriptors or caller-owned pages according to Phase 11.

There is no `health::snapshot()`.

## 16. Supervisor Service

### 16.1 Dedicated execution

Supervisor is one optional built-in service with a dedicated Solar-owned
thread. It does not run as periodic work on the system workqueue because that
would prevent it from independently detecting a blocked or starved shared
queue.

Its effective component, stack, priority, and lifecycle are visible through
Graph, Lifecycle, Execution, Health, and Inspection.

### 16.2 Cycle

Each cycle performs bounded phases:

```text
wake
  -> drain admitted reports
  -> collect due direct evidence
  -> invoke due component checks
  -> evaluate monitor transitions
  -> commit Health assessments
  -> emit declared adapters
  -> execute due response policy
  -> evaluate watchdog gates
  -> feed watchdog if every required gate passes
  -> record cycle outcome
  -> wait until next due deadline
```

### 16.3 Deadline-driven wake-up

The base period is a default scheduling quantum, not mandatory polling of every
monitor. Supervisor calculates the next due monitor, response retry, recovery
deadline, and watchdog feed deadline and sleeps until the earliest.

Push reports may wake Supervisor early through an event or semaphore.

### 16.4 Budget

Supervisor declares and records:

- configured base period;
- maximum checks per cycle;
- maximum stack scans per cycle;
- maximum response attempts per cycle;
- cycle deadline and grace;
- last and maximum cycle duration;
- missed and overrun count;
- last completed phase;
- current assessment generation;
- watchdog feed outcome.

Optional work is deferred when the cycle budget is exhausted. Required monitor
or watchdog deadlines are not silently skipped.

### 16.5 Priority

Supervisor priority must allow it to observe ordinary application starvation
without preempting critical real-time work for long periods. The Kconfig default
is platform tuned and may be overridden by explicit service policy.

Checks and stack scans remain bounded so elevated priority cannot create long
latency spikes.

## 17. Supervisor Records And API

Supervisor canonically owns:

- service cycle state;
- monitor scheduling cursor;
- response attempts and outcomes;
- retry, cooldown, and anti-flapping state;
- watchdog gate and feed state;
- last safe-state request;
- last requested stop, reboot, or panic action;
- bounded response history;
- its own cycle progress generation.

Focused queries are:

```cpp
solar::supervisor::state();
solar::supervisor::responses();
solar::supervisor::watchdog();
solar::supervisor::record<Imu>();
```

Supervisor records describe policy execution. Health records describe subject
assessment. Neither substitutes for the other.

## 18. Component Agency And Recovery

### 18.1 No agency taxonomy required

Solar does not require every condition to declare an agency enum. Responsibility
is expressed by actual component behavior and explicit Supervisor policy.

### 18.2 Immediate local containment

A component acts immediately when waiting for Supervisor would be unsafe:

```cpp
void MotorController::feedback_invalid(Error error)
{
    MotorOutput::disable();
    solar::health::report<MotorController>(
        solar::health::faulted(error, solar::health::Safety::Unsafe));
}
```

Supervisor may later escalate, latch, or request broader safe state. It does not
replace the immediate device-domain action.

### 18.3 Local recovery

A component may begin its own bounded asynchronous recovery and report progress:

```cpp
solar::health::report<Imu>(solar::health::recovering(error));
ImuRecoveryWork::submit();
```

It explicitly reports Nominal or Faulted when the attempt completes.

### 18.4 Recovery hook

An optional nested hook advertises a recovery capability:

```cpp
static solar::Result<void> Imu::Health::recover();
```

Mere presence does not authorize automatic invocation. Supervisor policy must
select `TryRecover<Imu>` explicitly.

The hook must:

- be bounded or start bounded asynchronous recovery;
- be idempotent or return `Already`/`InProgress` appropriately;
- avoid waiting forever;
- preserve component synchronization;
- report subsequent assessment explicitly;
- not assume Supervisor owns the component's domain state.

### 18.5 Local and supervised concurrency

Supervisor serializes its own recovery attempts. A component that may also
start local recovery owns one canonical recovery state machine and rejects or
coalesces duplicate entry. Health does not create a second recovery lock that
competes with device state.

## 19. Supervisor Response Policy

### 19.1 Policy is explicit

Initial response primitives are:

```cpp
solar::supervisor::Observe
solar::supervisor::Warn
solar::supervisor::Latch
solar::supervisor::TryRecover<Component>
solar::supervisor::EnterSafeState<Action>
solar::supervisor::RequestStop<Component>
solar::supervisor::RequestSystemStop
solar::supervisor::RequestReboot
solar::supervisor::StopFeedingWatchdog
solar::supervisor::Panic
```

Support for a response depends on Lifecycle, Hardware, and platform capability.
Unavailable actions fail validation or return a structured unsupported result.

### 19.2 Compact application policy

Illustrative policy:

```cpp
using RobotSupervision = solar::supervisor::Policy<
    solar::supervisor::OnFault<
        Imu,
        solar::supervisor::TryRecover<Imu>>,
    solar::supervisor::OnRecoveryFailure<
        Imu,
        solar::supervisor::EnterSafeState<NavigationSafe>>,
    solar::supervisor::OnStall<
        ControlService,
        solar::supervisor::EnterSafeState<DriveSafe>>>;
```

Exact helper parameter ordering may be refined, but policy remains centralized,
typed, and compile-time validated.

### 19.3 Safe-state action

An application safe-state action is an ordinary static type:

```cpp
struct DriveSafe
{
    static solar::Result<void> enter();
};
```

It must be:

- explicit;
- idempotent;
- bounded;
- callable independently of the failed component where practical;
- tested under partial subsystem failure;
- authorized by the application policy.

### 19.4 No event-observer behavior

Events may provide evidence and Health transitions may emit events. Application
recovery does not run in an Events infrastructure observer. Supervisor consumes
an admitted transition and invokes policy in its own thread context.

### 19.5 Initial default

The default Supervisor policy observes, records, and emits configured warning
adapters. It does not invent recovery, stop components, reboot, or panic.

Production watchdog and safe-state policy must be selected explicitly.

### 19.6 Restart limitation

Component restart is not an initial generic response because accepted Lifecycle
does not yet define truthful in-process component restart and state reset.
`TryRecover<Component>` invokes a component-domain recovery capability without
claiming to recreate its lifecycle.

## 20. Watchdog Architecture

### 20.1 Two layers

Watchdog integration separates:

- `solar::kernel::TaskWatchdog`: typed wrapper over Zephyr's task watchdog
  service;
- Phase 13 `solar::hardware` watchdog device: typed wrapper over the physical
  watchdog driver and devicetree node.

Supervisor integrates both but owns neither implementation.

### 20.2 Feed gating

Components do not feed the final system watchdog merely because their thread is
running. They update progress and health evidence.

Supervisor feeds only after:

- the supervision cycle completed;
- every required watchdog gate was evaluated;
- required progress markers are current;
- required safety state permits continued operation;
- no selected response requires withholding feed;
- the watchdog feed window permits feeding.

### 20.3 Supervisor failure

The final watchdog feed occurs only in the Supervisor execution domain. If
Supervisor stalls, deadlocks, overruns indefinitely, or cannot complete its
cycle, feed stops.

### 20.4 Task watchdog role

Zephyr Task Watchdog may provide software channels and a hardware fallback. It
is an enforcement backend, not Health's canonical assessment state machine.

Task watchdog timeout callbacks may execute in timer/ISR context. They perform
only panic-safe compact capture or immediate platform action and never ordinary
Health locking, formatting, or arbitrary recovery.

### 20.5 Hardware limitations

Hardware watchdogs differ in:

- supported reset modes;
- callback support;
- feed windows;
- pause-in-sleep and pause-under-debug support;
- ability to disable or restart;
- multistage timeout support;
- reset-reason retention.

Phase 13 provides the typed device capability. Supervisor policy must not assume
unsupported behavior.

## 21. Fatal Error Boundary

### 21.1 Fatal reasons

Kernel exposes a normalized fatal reason covering Zephyr reasons such as:

- CPU exception;
- spurious interrupt;
- stack check failure;
- kernel oops;
- kernel panic;
- allocation failure where fatal policy applies;
- architecture-specific unknown fatal reason.

The native reason remains available in the structured Error.

### 21.2 Panic-safe bridge

Solar may install one application fatal-handler bridge when configured. It may:

- atomically latch the fatal reason;
- copy a tiny panic-safe retained record;
- notify Solar Logging panic mode;
- invoke configured coredump or retained-storage integration;
- halt or reset according to fatal policy.

It must not:

- acquire ordinary mutexes;
- allocate;
- invoke component checks;
- run Supervisor responses;
- perform ordinary Remote transmission;
- claim recovery from kernel panic;
- return after a supervisor-mode fault unless a platform-specific policy has
  explicitly established that behavior as safe.

### 21.3 Previous-boot evidence

Hardware reset cause and retained fatal evidence are integrated after Phase 13.
Health may expose them as previous-boot evidence but does not fabricate them
when the board cannot retain the data.

## 22. Expanded Kernel Mandate

### 22.1 Coverage goal

`solar::kernel` should provide coherent typed coverage for the vast majority of
stable, useful public Zephyr kernel object families.

This supersedes the narrower wording in Phase 3 that wrappers are added only
for immediate demonstrated framework needs. The principle against empty
renaming remains: wrappers must add C++ ownership, static storage, type safety,
RAII, chrono integration, concepts, structured results, or context clarity.

Full coverage means broad practical coverage, not wrapping every internal
symbol, architecture hook, user-mode permission call, or diagnostic experiment.

### 22.2 Kernel boundary

Kernel owns typed wrappers and direct facts for:

- threads and thread stack storage;
- scheduling and current-context operations;
- time, clocks, timeout, deadline, and cycle facts;
- synchronization;
- kernel data passing;
- timers;
- work and workqueues;
- polling and signals;
- fixed and optional dynamic kernel memory primitives;
- thread and scheduler diagnostics;
- task watchdog;
- fatal and panic primitives;
- optional SMP and userspace primitives.

Kernel does not own:

- service, executor, and task registration records;
- lifecycle transitions;
- system graph identity;
- health assessment;
- supervisor response policy;
- hardware drivers or devices;
- devicetree interpretation;
- application message semantics.

## 23. Kernel Wrapper Principles

Every public Kernel wrapper must:

- preserve deterministic static storage where Zephyr supports it;
- use C++23 concepts and types to reject invalid usage early;
- use `std::chrono` at public duration boundaries where natural;
- use `std::span` for borrowed contiguous buffers;
- use `Result<T>` and structured Error for fallible operations;
- preserve relevant native errno and platform detail;
- document thread, ISR, and non-blocking context contracts;
- expose native handles deliberately;
- avoid hidden dynamic allocation;
- avoid hidden threads and workqueues;
- preserve Zephyr coalescing, cancellation, timeout, and scheduling semantics;
- avoid exceptions in core firmware paths;
- make Kconfig-disabled capability explicit;
- remain usable without a bound Solar system;
- avoid implicit graph or execution registration.

RAII is used only when destruction semantics are truthful. A destructor must not
silently wait forever, abort a thread, flush a queue, or discard pending work.

## 24. Kernel Thread Surface

Kernel provides a complete typed thread family covering:

- static thread stack and control-block storage;
- create, start, delayed start, and naming;
- priority get/set;
- suspend and resume;
- cooperative stop source and token;
- no-wait exit predicate;
- bounded join;
- explicit abort;
- current thread identity;
- sleep, yield, busy wait, and deadline wait;
- optional CPU affinity and CPU-mask operations;
- optional time-slice and scheduler-lock operations;
- native handle access;
- thread state and diagnostics;
- optional userspace options and resource-pool assignment where selected.

Destruction of a live `Thread` never silently aborts or joins. Ownership and
containment remain explicit.

Thread state uses a typed record rather than parsing Zephyr's display string.
Where Zephyr exposes only limited public state, unavailable detail remains
optional rather than reading private kernel fields.

## 25. Kernel Synchronization Surface

Kernel provides typed wrappers for:

- mutex;
- recursive mutex where Zephyr semantics genuinely support it;
- lock guard and unique lock;
- condition variable;
- semaphore;
- binary semaphore;
- event flags;
- spinlock;
- interrupt lock / critical section;
- scheduler lock;
- optional futex and user-mode mutex where userspace is enabled;
- atomic wait-free helpers only where `std::atomic` is insufficient for the
  Zephyr interface.

Timed operations use `Timeout`, `Deadline`, or `std::chrono` consistently.
ISR-safe operations expose explicit non-blocking forms.

The current `RecursiveMutex = Mutex` alias must be validated against actual
Zephyr recursive mutex behavior and renamed or separated if its semantics are
not exact.

## 26. Kernel Data-Passing Surface

Kernel provides typed, capacity-aware wrappers for:

- message queue;
- intrusive queue;
- FIFO;
- LIFO;
- bounded stack;
- mailbox;
- pipe;
- poll signal;
- fixed packet or byte transport adapters where based on stable Zephyr kernel
  primitives.

Wrappers preserve the underlying ownership model:

- message queues copy fixed-size values;
- intrusive queues/FIFOs/LIFOs require stable node lifetime;
- mailboxes and pipes preserve transfer and timeout semantics;
- no wrapper accepts a temporary whose storage may outlive the call;
- ISR admission is explicitly limited to the native non-blocking forms.

Solar Bus remains the typed application message fabric. Kernel data-passing
objects are local execution primitives and are never automatically registered
as Bus routes.

## 27. Kernel Work, Timer, And Poll Surface

Kernel provides complete wrappers for:

- timer start, stop, status, and user data;
- ordinary work;
- delayable work;
- triggered/poll work where supported;
- work submission and busy-state outcomes;
- synchronous and asynchronous cancellation;
- work flush;
- workqueue start, drain, plug, unplug, and stop;
- system workqueue reference;
- poll events, poll signals, and bounded poll sets;
- deadline and timeout composition.

Phase 9 remains authoritative for semantic Execution registration, periodic
task policy, counted admission, executor records, and lifecycle ownership.

## 28. Kernel Memory Surface

Kernel provides:

- typed fixed-size memory slab;
- bounded memory-block allocator where stable Zephyr support fits;
- explicit heap wrapper when dynamic allocation is enabled;
- system-heap access only through explicit policy;
- allocation statistics where supported;
- native storage alignment and size validation.

Core Solar does not require heap allocation. `CONFIG_SOLAR_ALLOW_DYNAMIC_ALLOCATION`
remains the hard capability boundary for wrappers that allocate dynamically.

Fixed slabs and block pools are valuable even when dynamic allocation is
disabled because they provide deterministic ownership for buffers, protocol
frames, and asynchronous transfers.

## 29. Kernel Time, Scheduler, And SMP Surface

Kernel provides typed access to:

- monotonic uptime;
- ticks and cycle counter;
- tick/cycle/duration conversion;
- absolute and relative timeout;
- deadlines and grace;
- scheduler lock state where publicly available;
- current CPU identity;
- configured CPU count;
- optional thread affinity and CPU masks;
- per-thread, per-CPU, and aggregate runtime statistics;
- optional CPU idle/load facts;
- optional floating-point thread options where selected.

Kernel does not provide a shadow scheduling policy. Native Zephyr priority,
preemption, cooperative scheduling, SMP, and timeslicing semantics remain
authoritative.

## 30. Kernel Diagnostics Surface

### 30.1 Focused records

The old `ThreadSnapshot` is replaced by:

```cpp
struct ThreadDiagnostics
{
    kernel::ThreadId id{};
    std::optional<std::string_view> name{};
    ThreadExecutionState state{};
    std::optional<Priority> priority{};
    std::optional<std::size_t> stack_size{};
    std::optional<std::size_t> stack_used{};
    std::optional<std::size_t> stack_unused{};
    std::optional<std::size_t> stack_warning_margin{};
    std::optional<ThreadRuntimeStats> runtime{};
    kernel::TimePoint observed_at{};
};
```

The API remains focused:

```cpp
solar::kernel::thread_diagnostics(thread);
solar::kernel::stack_usage(thread);
solar::kernel::runtime_stats(thread);
solar::kernel::thread_exited(thread);
```

There is no broad Kernel system snapshot.

### 30.2 Stack safety

Kernel wraps:

- configured stack size and native stack information;
- high-water unused-space query;
- runtime unused-threshold configuration;
- full stack safety check;
- abbreviated threshold check;
- explicit unsupported result on platforms forbidding inspection.

### 30.3 Thread enumeration

Optional global enumeration exposes two explicitly named policies:

- locked enumeration, documenting global spinlock duration;
- unlocked enumeration, documenting thread-lifetime restrictions.

Health and Supervisor do not use either for statically known Solar threads.

### 30.4 Thread analyzer

Solar does not enable Zephyr's automatic text-printing thread-analyzer thread as
its production monitor. It uses underlying public diagnostic APIs with its own
typed records, selected threads, bounded cadence, and existing Supervisor
service.

The Zephyr analyzer remains available directly for development builds.

## 31. Kernel Task Watchdog And Fatal Surface

### 31.1 Task watchdog wrapper

`solar::kernel::TaskWatchdog<Capacity>` provides:

- explicit initialization;
- statically bounded channel registration;
- typed move-only channel handles;
- feed;
- delete/release;
- suspend and resume;
- optional physical watchdog fallback handle;
- callback context documentation;
- structured errors for invalid channel, capacity exhaustion, and unsupported
  fallback.

The wrapper does not conceal Zephyr's global task-watchdog implementation or
pretend that channels are independent hardware timers.

### 31.2 Fatal primitives

Kernel provides:

```cpp
solar::kernel::panic(error);       // does not return
solar::kernel::fatal_reason();     // valid in documented fatal context
solar::kernel::fatal_halt(reason); // does not return
```

Ordinary application code should use structured errors, Events, Health, and
Supervisor policy. Panic is reserved for unrecoverable conditions.

## 32. Native Escape Hatches

Every owning wrapper exposes an intentionally named native handle where needed:

```cpp
thread.native_handle();
mutex.native_handle();
queue.native_handle();
work.native_handle();
```

Native escape does not transfer ownership unless explicitly documented. Code
using native APIs must preserve wrapper invariants.

Solar does not wrap unstable private kernel structures merely to avoid one
native call.

## 33. Lifecycle Behavior

### 33.1 Health lifecycle

Health is automatically initialized after graph/catalog construction and before
services requiring reporting start.

Initialization:

- creates normalized subject and monitor descriptors;
- initializes records to Unknown;
- installs progress storage;
- validates component hooks;
- configures optional ISR ingress;
- records disabled and unsupported native capabilities.

Health stop prevents new ordinary reports according to policy, drains admitted
compact reports where bounded, and preserves final records for stop report and
inspection.

### 33.2 Supervisor lifecycle

Supervisor starts only after Health and required Kernel primitives are ready.
It performs an initial assessment before enabling watchdog feed.

During stop:

- new recovery attempts cease;
- safe-state or shutdown policy may run explicitly;
- watchdog handling follows configured shutdown policy;
- the service exits cooperatively within its timeout;
- final cycle and response records remain queryable.

### 33.3 Boot behavior

Pre-Supervisor component reports are accepted after Health initialization.
Required subjects remain Unknown until their first valid evidence. Boot policy
may require an initial nominal/readiness assessment before the system enters its
fully operational state, but Health does not silently rewrite Lifecycle state.

## 34. Kconfig And Type Policy

Kconfig owns build capability, hard ceilings, and platform defaults including:

- Health inclusion;
- Supervisor inclusion;
- Supervisor base period;
- Supervisor stack and priority;
- maximum subjects and monitors;
- transition and response history capacities;
- ISR report ingress capacity;
- checks and stack scans per cycle;
- default progress grace and confirmation counts;
- Kernel diagnostic capability;
- Zephyr stack information and initialization requirements;
- runtime stack safety;
- runtime statistics;
- thread monitor and optional global enumeration;
- task watchdog integration and channel ceiling;
- fatal bridge and coredump/retention capability;
- default watchdog gating capability;
- dynamic Kernel memory-wrapper availability.

Blueprint policy owns:

- system-wide Health defaults;
- required subject domains;
- Supervisor response policy;
- safe-state actions;
- effective watchdog gates;
- optional expensive diagnostic policy;
- application recovery escalation.

Component declarations own:

- nested assessment and checks;
- progress semantics;
- component-specific periods and thresholds;
- recovery capability;
- direct domain self-reporting.

Precedence remains:

```text
component/response declaration > blueprint policy > Kconfig default
```

Compiled capability is a hard boundary and cannot be enabled by C++ policy.

## 35. Catalog And Contribution Integration

Health normalization derives:

- component subjects from the effective component catalog;
- automatic execution monitors from effective services and executors;
- optional nested `Component::Health` contracts;
- named checks and progress monitors;
- additional subsystem and application subjects;
- response rules from effective Supervisor policy;
- external adapters from explicitly enabled subsystem integration.

The resulting immutable catalogs include:

- subject descriptors;
- monitor descriptors;
- check descriptors;
- recovery capability descriptors;
- response rule descriptors;
- watchdog gate descriptors.

Runtime records reference these immutable descriptors. Health does not require
components to repeat names or owners.

## 36. Events, Metrics, And Logging Integration

### 36.1 Events as evidence

Selected active Event conditions may feed Health through explicit adapters.
Health references Events' canonical condition state and does not reproduce its
occurrence history.

### 36.2 Health transitions as events

Health may emit declared events such as:

- subject degraded;
- subject faulted;
- subject recovered;
- progress late or stalled;
- stack margin warning;
- Supervisor response failed;
- watchdog gate withheld.

Adapters prevent a Health-generated event from recursively re-entering the same
Health condition.

### 36.3 Metrics

Declared adapters may expose:

- degraded/faulted subject counts;
- transition counters;
- progress lateness;
- stack high-water values;
- Supervisor cycle duration and overruns;
- response and recovery outcomes;
- watchdog feed and withholding counts.

Metrics remains canonical owner of numeric instruments.

### 36.4 Logging

Health and Supervisor may emit concise logs for transitions and responses.
Logging failure cannot prevent canonical Health update, local containment, or
watchdog policy.

Fatal processing uses only the panic-safe Logging path.

## 37. Inspection And Remote

Inspection collections may expose:

- Health subjects;
- monitor descriptors and records;
- active conditions;
- bounded transition history;
- Supervisor cycle and response records;
- watchdog gate state;
- Kernel thread diagnostics for explicitly selected threads.

Inspection remains read-only and source-owning APIs remain canonical.

Remote exposure is explicit and separately authorized. Read-only Health facts
do not imply permission to invoke recovery, safe state, stop, reboot, watchdog,
or panic actions. Control operations require explicit Remote Actions and grants.

A disconnected Remote session cannot change local Health assessment or block
Supervisor.

## 38. Errors And Availability

Health and Supervisor distinguish:

- `Disabled`: feature excluded by Kconfig or effective policy;
- `Unsupported`: platform cannot provide a requested native fact;
- `Unavailable`: capability exists but is not currently ready;
- `Busy`: source could not provide a bounded coherent observation;
- `Stale`: previously valid evidence exceeded its freshness policy;
- `NotFound`: subject or monitor identity is unknown;
- `Failed`: check, recovery, response, or canonical source failed;
- `Rejected`: policy or lifecycle does not permit the operation;
- `Already`/`InProgress`: recovery or response is already active.

A stale observation may remain useful and is represented as a value with
freshness metadata. A failed assessment uses `Result` error state.

## 39. Resource Bounds

The design is statically bounded by:

- effective component and subject count;
- normalized monitor and response count;
- one current record per subject and monitor;
- fixed progress storage;
- fixed ISR ingress;
- fixed transition and response histories;
- checks and scans per Supervisor cycle;
- fixed task-watchdog channel capacity;
- statically allocated Supervisor stack;
- caller-owned query and formatting buffers.

Supervisor records deferral and missed budgets. It does not allocate more
storage or loop without bound to catch up.

## 40. Validation And Diagnostics

Solar must diagnose at least:

- malformed nested `Health` contract;
- invalid `assess()` return type;
- malformed or duplicate named check;
- unbounded or zero check period;
- progress monitor without a valid subject;
- duplicate Health subject identity;
- invalid stack margin larger than configured stack;
- required native monitor without matching Kconfig capability;
- recovery policy for a component without `Health::recover()`;
- response action with invalid signature;
- unsupported lifecycle restart response;
- safe-state action depending exclusively on the failed subject;
- response policy cycle;
- watchdog feed policy without an enforcement backend;
- watchdog timeout incompatible with Supervisor deadline and grace;
- more subjects, monitors, responses, or channels than configured capacity;
- ISR report payload that is not safely copyable;
- dynamic Kernel wrapper used while dynamic allocation is disabled;
- Kernel wrapper requested without required Zephyr Kconfig support;
- use of an ISR-unsafe blocking Kernel operation from an explicitly ISR path.

Diagnostics should identify the subject, monitor, component, response, Kernel
primitive, and missing capability.

## 41. Verification Requirements

### 41.1 Health compile-time tests

- component with no custom Health declaration;
- push-only component;
- aggregate `assess()` component;
- named-check component;
- recoverable and non-recoverable components;
- malformed hooks and duplicate checks;
- automatic owner and origin inference;
- strict bound-system subject registration;
- disabled Health behavior.

### 41.2 Health runtime tests

- nominal, degraded, faulted, and recovering reports;
- successful fault assessment versus assessment failure;
- repeated equivalent reports and explicit recovery;
- required and advisory aggregation;
- stale and unavailable evidence;
- hysteresis, confirmation, cooldown, and latching;
- component Nominal report not overriding stalled progress;
- bounded history and overflow accounting;
- ISR ingress admission and overflow;
- concurrent reports and coherent queries.

### 41.3 Supervisor tests

- 500 ms default cadence and alternate policy cadence;
- event-driven early wake;
- due-monitor scheduling;
- rotating stack scans;
- cycle budget and deferral;
- cycle overrun and missed-cycle records;
- local recovery followed by nominal confirmation;
- Supervisor-invoked recovery;
- duplicate recovery rejection;
- escalation after recovery failure;
- safe-state idempotence;
- response retries, cooldown, and anti-flapping;
- Supervisor stop and restart across test system instances;
- Supervisor stall causing watchdog feed cessation.

### 41.4 Thread and stack tests

- known Solar thread diagnostics;
- stack usage available and unsupported paths;
- warning threshold crossing;
- stale stack measurement;
- runtime-cycle delta without progress;
- progress without measurable runtime statistics;
- expected and unexpected thread exit;
- locked and unlocked enumeration policy tests where enabled;
- hardware/sentinel/canary fatal integration on supported targets.

### 41.5 Watchdog and fatal tests

- task-watchdog channel capacity;
- feed, delete, suspend, and resume;
- successful full-cycle feed;
- gate failure withholding feed;
- Supervisor overrun withholding feed;
- callback context restrictions;
- hardware fallback integration with a fake device;
- fatal reason normalization;
- panic-safe latch and Logging notification;
- no ordinary lock or dynamic allocation in fatal path;
- previous-boot retained evidence where supported.

### 41.6 Expanded Kernel tests

Every wrapper family requires:

- compile-time storage and concept tests;
- native success and errno mapping;
- no-wait, finite timeout, deadline, and forever behavior where supported;
- thread versus ISR context tests;
- native handle interoperability;
- disabled Kconfig capability tests;
- cancellation and teardown semantics;
- no hidden allocation or hidden execution;
- tests against each supported Zephyr upgrade.

## 42. Migration From Current Solar

### 42.1 Kernel

Current Kernel wrappers are retained where their semantics match this contract,
then completed and normalized.

Required early changes include:

- rename `ThreadSnapshot` to `ThreadDiagnostics`;
- replace availability booleans and believable zeroes with optional fields and
  structured results;
- complete thread join, state, diagnostics, and stack-safety surfaces;
- add task watchdog and fatal primitives;
- add missing synchronization, data-passing, poll, memory, scheduler, and SMP
  families;
- audit current aliases such as `RecursiveMutex`;
- unify timeout and errno mapping;
- add context contracts and native handles consistently.

### 42.2 Health and Supervisor

No current Solar Health or Supervisor implementation is retained as canonical
architecture. New storage derives from the effective component and execution
catalogs.

Existing ad hoc lifecycle failures, service thread facts, event conditions,
metrics, and Remote diagnostics become explicit adapters rather than copied
state.

### 42.3 Firmware migration

Firmware components migrate incrementally:

1. gain automatic generic Health records with no source change;
2. add `health::report<Component>()` at meaningful domain transitions;
3. add progress markers to critical loops;
4. optionally add nested assessment/check/recovery contracts;
5. introduce explicit Supervisor safe-state and escalation policy;
6. enable watchdog enforcement only after deterministic fault-injection tests.

## 43. Deferred Capabilities

Deferred while preserving extension points:

- generic in-process component lifecycle restart;
- distributed or multi-controller supervision;
- persisted incident journals beyond bounded retained boot evidence;
- predictive statistical anomaly detection;
- dynamic monitor registration;
- arbitrary runtime policy scripting;
- full deadlock graph detection;
- automatic root-cause inference;
- cryptographically attested health reports;
- standard C++ reflection-generated checks;
- hardware-watchdog device details owned by Phase 13;
- broad Zephyr userspace object-permission wrappers not required by the first
  implementation;
- architecture-specific tracing and performance-monitor units.

## 44. Rejected Alternatives

### 44.1 One combined Health/Supervisor object

Rejected because passive assessment remains useful without active supervision,
and watchdog liveness requires a distinct execution owner.

### 44.2 Separate signal types referencing their owner

Rejected for the common path because `Imu` naming `ImuSignal` while
`ImuSignal` names `Imu` creates include and declaration friction. Ownership is
inferred from nested contracts or the reporting component template argument.

### 44.3 Mandatory descriptors and agency policy

Rejected because most components need either generic monitoring or one compact
self-report. Rich policy remains optional.

### 44.4 One `Component::healthy()` boolean

Rejected because it cannot distinguish failed assessment, stale evidence,
liveness, readiness, degradation, or safety.

### 44.5 Supervisor owns all recovery

Rejected because components understand domain recovery and immediate
containment, and many responses cannot wait for a supervision cycle.

### 44.6 Components own all response

Rejected because system-level dependency, safe-state, watchdog, and escalation
policy exceed one component's authority.

### 44.7 Automatically invoke `Health::recover()`

Rejected because capability does not imply authorization. Explicit Supervisor
policy selects recovery.

### 44.8 Thread-running means healthy

Rejected because a thread can deadlock, livelock, process invalid data, or run
the wrong behavior while remaining scheduled.

### 44.9 Runtime cycles mean progress

Rejected because scheduler activity is not semantic application progress.

### 44.10 Enumerate every Zephyr thread each cycle

Rejected because Solar already knows its owned threads and Zephyr enumeration
has global locking or lifetime-race costs.

### 44.11 Zephyr automatic thread analyzer as Supervisor

Rejected because it creates separate periodic execution and text output rather
than typed source-owned records and policy integration.

### 44.12 Supervisor attempts recovery after stack overflow

Rejected because memory or kernel state may already be corrupted.

### 44.13 Every task feeds the hardware watchdog

Rejected because independent feeding can conceal a failed required domain and
does not verify a complete supervision cycle.

### 44.14 Wrap every Zephyr symbol

Rejected because private, architecture-specific, unstable, or semantically
empty wrappers increase maintenance without improving type safety or ownership.
Kernel instead covers the vast majority of stable useful public object families
and retains explicit native escape hatches.

## 45. Final Decisions

1. Health and Supervisor are separate cooperating subsystems.
2. Health is a static facility and Supervisor is an optional dedicated service.
3. Enabling Supervisor automatically requires Health.
4. Every effective component receives a Health subject when Health is enabled.
5. Components need no custom Health declaration for generic monitoring.
6. `health::report<Component>(assessment)` is the compact self-report path.
7. Recovery must be reported explicitly; silence is not recovery.
8. Components may optionally define nested `Component::Health`.
9. `Health::assess()` is an optional aggregate pull assessment.
10. `Health::Checks` is an optional set of nested named checks.
11. Check ownership is inferred from the enclosing component.
12. `Health::recover()` is an optional recovery capability.
13. Recovery capability is never automatically authorized.
14. Push reporting is preferred when the component already knows its state.
15. Pull checks must be bounded and run outside Health locks.
16. Generic execution monitoring is automatic for Solar-owned threads.
17. Semantic progress requires an explicit progress marker.
18. Progress markers are atomic, non-blocking, allocation-free, and ISR-safe.
19. Component self-assessment cannot erase failing required generic monitors.
20. Condition, liveness, readiness, safety, and freshness remain distinct.
21. Evidence quality is Direct, Reported, or Inferred rather than numeric.
22. Successful fault assessment and failed assessment remain distinct.
23. Health owns assessments and monitor state, not source subsystem histories.
24. Ordinary reporting is mutex-protected and ISR reporting uses bounded
    explicit ingress.
25. Supervisor runs on one dedicated thread, not the system workqueue.
26. The default Supervisor base period is 500 ms.
27. Monitors have individual cadences and expensive checks are rotated.
28. Known Solar threads are inspected directly.
29. Global Zephyr thread enumeration is optional and not the Health default.
30. Stack high-water scans are warnings, not overflow protection.
31. Actual stack overflow follows the fatal path.
32. Runtime cycles are scheduler evidence, not proof of useful progress.
33. Supervisor owns evaluation scheduling, response state, and watchdog gating.
34. Health owns canonical assessment state.
35. Components own immediate domain containment and local recovery mechanisms.
36. Supervisor policy owns system-level escalation.
37. No mandatory agency taxonomy is introduced.
38. Safe-state actions are explicit, idempotent, bounded application types.
39. Default Supervisor behavior observes and reports without inventing recovery.
40. Generic component restart remains unavailable until Lifecycle supports it.
41. Task watchdog is a Kernel primitive and physical watchdog is Hardware.
42. Final watchdog feed occurs only after a complete successful supervision
    cycle and all required gates pass.
43. Supervisor failure therefore stops final watchdog feeding.
44. Fatal processing is panic-safe and does not run ordinary supervision.
45. `solar::kernel` expands to cover the vast majority of stable useful public
    Zephyr kernel object families.
46. Kernel remains usable without system binding or component registration.
47. Kernel owns primitives and direct facts, never health or execution policy.
48. Kernel wrappers preserve native semantics, errors, context, and handles.
49. Kernel adds no hidden allocation, thread, workqueue, or scheduler.
50. Thread, synchronization, data passing, work, timer, poll, fixed memory,
    time, scheduler, SMP, diagnostics, task-watchdog, and fatal families are in
    scope.
51. Hardware devices and devicetree remain outside Kernel.
52. Execution registration remains outside Kernel.
53. Unstable and niche native APIs retain explicit escape hatches.
54. There is no universal Health or Kernel snapshot.

## 46. Open Questions

There are no blocking architectural questions for Phase 13.

Implementation may refine without changing this contract:

- exact Assessment field ordering and compact storage widths;
- exact report receipt type;
- exact nested-check descriptor conveniences;
- exact default confirmation and hysteresis counts;
- exact Supervisor priority and stack defaults per board;
- exact checks and stack scans per cycle;
- exact wake primitive used for push reports;
- exact ISR compact-observation representation;
- exact task-watchdog wrapper template shape;
- exact fatal retained-record width;
- exact names for locked and unlocked thread enumeration;
- exact staged implementation order of expanded Kernel wrapper families;
- optional wrappers for less commonly used Zephyr userspace and object-core
  diagnostics;
- compatibility aliases retained temporarily during Kernel migration.
