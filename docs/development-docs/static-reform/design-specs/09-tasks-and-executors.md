# Tasks, Work, And Executors

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 9

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

## 1. Purpose

This specification defines Solar's model for repeatable task behavior, Zephyr
work, execution registrations, workqueue executors, scheduling, cancellation,
containment, and focused execution diagnostics.

It establishes:

- the boundary between `solar::kernel` mechanisms and `solar::execution`
  system integration;
- task behavior independently from where and when it runs;
- work, delayable-work, periodic, and poll-triggered registrations;
- Zephyr's system workqueue as a first-class execution target;
- explicitly declared application workqueues as executor components;
- optional Kconfig selection of the system workqueue as the omitted-target
  default;
- native Zephyr work submission, coalescing, scheduling, rescheduling, flush,
  and cancellation semantics;
- task and infrastructure registration identity, ownership, and dependencies;
- bounded admission extensions when native coalescing is insufficient;
- fixed-rate and fixed-delay periodic behavior;
- explicit dedicated execution without a hidden private task thread;
- lifecycle activation, quiescence, executor stop, and dependency preservation;
- subsystem reuse by bus, parameters, events, logging, metrics, and Remote;
- bounded task, registration, target, and executor records;
- migration from Solar's current task implementation.

The ordinary application path remains compact:

```cpp
struct RecalculateControl
{
    static solar::Result<void> execute();
};

using Recalculate = solar::execution::OnDemand<
    "recalculate-control",
    RecalculateControl,
    ControlWorkQueue>;

solar::execution::submit<Recalculate>();
```

No executor object, system object, runtime context, work item, or native queue
pointer appears at the call site.

## 2. Non-Goals

The execution subsystem is not:

- a dynamic thread pool;
- a heap-allocated callback scheduler;
- a general future, coroutine, or promise runtime;
- an unbounded job queue;
- a replacement for Zephyr's scheduler;
- a replacement for direct kernel primitives;
- a second workqueue state machine layered over Zephyr;
- a payload bus;
- a runtime task registry;
- a reason to turn every component into a task;
- a reason to give every task a private stack;
- a guarantee that system-workqueue latency is isolated from ecosystem work;
- permission to stop or globally drain Zephyr's system workqueue;
- a universal execution snapshot;
- a health or supervisory policy engine.

Services remain sustained component-owned execution loops. Bus messages remain
typed application communication. Events remain structured operational facts.
Execution registrations describe where and when bounded work runs.

## 3. Canonical Vocabulary

- **task behavior**: reusable static bounded behavior independent of its
  trigger and execution target;
- **execution registration**: a typed leaf binding behavior to identity,
  target, trigger or schedule, admission, dependencies, and stop policy;
- **task registration**: an application-authored execution registration;
- **infrastructure registration**: a registration owned by a Solar subsystem,
  such as an event processor or bus-route drain;
- **work item**: one statically owned Zephyr-compatible work mechanism used to
  signal that a registration has runnable work;
- **release**: a trigger or schedule occurrence that requests runnable work;
- **job occurrence**: one accepted unit represented by a registration's
  admission semantics;
- **execution target**: a workqueue context to which work is submitted;
- **executor**: a lifecycle-owning component that creates and controls an
  execution target;
- **system workqueue**: Zephyr's shared essential platform workqueue;
- **native coalescing**: Zephyr behavior in which submitting an already queued
  work item does not append another occurrence;
- **delayable work**: work associated with a future submission deadline;
- **periodic work**: a Solar scheduling policy repeatedly releasing work;
- **triggered work**: work armed against a fixed Zephyr poll-event set;
- **pending**: scheduled, queued, running, cancelling, or otherwise still
  referenced according to the relevant work contract;
- **in flight**: behavior currently executing;
- **quiescent**: no future release is armed, no job is queued, and no behavior
  is in flight;
- **containment**: proof that owned execution can no longer access its
  dependencies.

The word **job** describes a runtime occurrence. It is not the canonical name
for the static registration catalog.

## 4. Architectural Decision

### 4.1 Zephyr work is the foundation

Solar adopts Zephyr's workqueue implementation as the canonical mechanism for
shared deferred execution.

Solar does not reimplement:

- work-item queueing;
- queue-thread wakeup;
- native work flags;
- cancellation synchronization;
- delayable timeout insertion;
- workqueue drain and plug behavior;
- system-workqueue scheduling.

Solar adds static type identity, architecture validation, target selection,
lifecycle ownership, policy, accounting, and focused diagnostics around those
mechanisms.

### 4.2 No hidden Solar executor

Solar never automatically creates an application workqueue merely because a
registration omitted its target.

An omitted target resolves only when Kconfig deliberately selects Zephyr's
system workqueue as the project-wide default. Otherwise omission is a
compile-time error.

No implicit Solar thread, stack, priority, or queue exists.

### 4.3 Additional queues are explicit

An application declares a named workqueue only when shared system-workqueue
execution is unsuitable because of latency, blocking, stack, priority,
isolation, or containment requirements.

Every application queue is visible as an executor component in the effective
blueprint.

## 5. Kernel And Execution Boundary

### 5.1 Boundary rule

`solar::kernel` answers:

> How can C++ code use this Zephyr kernel mechanism directly and safely?

`solar::execution` answers:

> Which registered system work exists, where does it run, and how is it
> activated, inspected, stopped, and contained?

### 5.2 Kernel owns mechanisms

The target Kernel vocabulary is:

```cpp
solar::kernel::Work
solar::kernel::DelayableWork
solar::kernel::TriggeredWork
solar::kernel::WorkQueue<StackSize>
solar::kernel::SystemWorkQueue
solar::kernel::system_work_queue
```

Kernel wrappers provide typed C++ ergonomics over:

- `k_work`;
- `k_work_delayable`;
- `k_work_poll`;
- `k_work_q`;
- `k_work_sync`;
- system-workqueue submission;
- workqueue thread and native handles.

They require no bound system and create no catalog or lifecycle entry.

### 5.3 Execution owns architecture

The target Execution vocabulary includes:

```cpp
solar::execution::OnDemand
solar::execution::Delayable
solar::execution::Periodic
solar::execution::PollTriggered
solar::execution::WorkQueue
solar::execution::SystemWorkQueue
```

Execution owns:

- registration descriptors and catalogs;
- behavior association;
- semantic owner and registration origin;
- target resolution;
- dependencies;
- activation and admission windows;
- stop and failure policy;
- periodic release semantics;
- registration and executor records;
- system-level queries.

### 5.4 Same mechanism name at different layers

Both of these names are intentional:

```cpp
solar::kernel::WorkQueue<2048>
solar::execution::WorkQueue<"control", ...>
```

The Kernel type is an instantiable primitive. The Execution type is a static
registered executor component that owns a Kernel primitive.

### 5.5 Direct Kernel use remains valid

Application code may use Kernel work directly:

```cpp
static inline solar::kernel::Work interrupt_work{&process_interrupt};

void encoder_isr()
{
    (void)interrupt_work.submit(solar::kernel::system_work_queue);
}
```

Solar does not retroactively discover or own that work. Its application owner
must synchronize it in its own lifecycle hooks when required.

The practical rule is:

```text
local Zephyr mechanism only
    -> solar::kernel

system registration, lifecycle, dependencies, or inspection
    -> solar::execution
```

## 6. Kernel Work Contract

### 6.1 Thin and faithful wrappers

Kernel work wrappers preserve Zephyr semantics and return enough information
to distinguish native outcomes. They must not collapse all non-negative native
results into a generic success.

For ordinary submission, the typed disposition corresponds to:

```cpp
enum class WorkSubmission
{
    AlreadyQueued,        // native 0
    Queued,               // native 1
    RequeuedAfterCurrent, // native 2
};
```

Negative native results map to a compact `kernel::WorkError` or the final
Kernel error vocabulary while retaining the relevant errno reason.

### 6.2 Work initialization

A Kernel work object is initialized once with its handler. Reinitializing a
pending work item is forbidden, matching Zephyr.

C++ wrappers remove ordinary manual `CONTAINER_OF` use. Their object storage
remains stable for the complete pending lifetime.

### 6.3 Native interoperability

Kernel wrappers expose explicitly named native handles where required:

```cpp
work.native_handle();
queue.native_handle();
queue.thread_id();
```

Using a native handle may bypass typed accounting. It does not transfer
lifecycle ownership.

### 6.4 Advisory native state

Kernel may expose native busy flags and remaining delay as advisory facts.
Documentation must state that these are live observations that may become
stale immediately.

Execution must not implement check-then-submit logic from these observations.
It submits directly and interprets the operation result.

## 7. Kernel Workqueue Contract

### 7.1 Owned queue primitive

`kernel::WorkQueue<StackSize>` owns:

- one `k_work_q`;
- correctly aligned static stack storage;
- start configuration passed to Zephyr;
- direct queue and thread accessors.

It exposes faithful operations equivalent to:

```cpp
queue.start(configuration);
queue.drain(plug);
queue.unplug();
queue.stop(timeout);
```

Kernel does not automatically call these operations according to application
lifecycle.

### 7.2 Queue configuration

The C++ configuration represents Zephyr's queue controls:

- thread name;
- native priority;
- yield between work items;
- essential-thread flag where low-level use requires it;
- per-work timeout when Zephyr support is compiled.

The Kernel wrapper does not invent unrelated scheduling policy.

### 7.3 System workqueue primitive

Kernel exposes Zephyr's system workqueue through a stateless typed handle such
as:

```cpp
solar::kernel::system_work_queue
```

It supports item-level submit, schedule, reschedule, cancel, and flush. It does
not expose stop as an owned operation.

## 8. Task Behavior

### 8.1 Canonical behavior

A task behavior is a static type implementing one accepted operation:

```cpp
static Return execute();
```

or:

```cpp
static Return execute(solar::StopToken stop);
```

The token form allows cooperative observation when shutdown requests
quiescence. It does not make blocking appropriate on a shared workqueue.

### 8.2 Accepted return forms

Exactly these return forms are accepted:

```cpp
void
solar::Status
solar::Result<void>
```

Normalization follows the common Phase 0 contract:

- `void` means success;
- `Status::Ok` means success;
- another status is preserved;
- successful `Result<void>` means success;
- failed `Result<void>` preserves its broad status and structured reason where
  retained by the registration record.

Booleans, integers, arbitrary expected types, and generic result-like objects
are rejected.

### 8.3 No task context

Solar passes no system, runtime, executor, registration, or context object to
behavior.

Behavior includes and calls direct typed dependencies and global Solar
subsystems normally.

### 8.4 Bounded shared work

Behavior targeting a shared workqueue must be bounded and must avoid indefinite
blocking. One blocked handler delays every later item on that queue.

Long-lived loops belong in services. Blocking work requiring isolation uses an
explicit application workqueue or service.

### 8.5 Behavior reuse

The same behavior may appear in more than one registration:

```cpp
using FastSampling = solar::execution::Periodic<
    "fast-sampling", SampleSensors, 5ms, ControlWorkQueue>;

using SlowSampling = solar::execution::Periodic<
    "slow-sampling", SampleSensors, 100ms, SystemWorkQueue>;
```

The registrations, not the behavior type, are the execution identities.

## 9. Execution Registration Identity

### 9.1 Type identity

The concrete normalized registration type is authoritative C++ identity.

Two registrations using one behavior remain distinct when their concrete
registration types differ. Exact duplicate registration types are rejected.

### 9.2 Descriptor

Every registration resolves an `execution::Descriptor` under Phase 2 rules.
The convenience registration templates synthesize the minimum descriptor from
their compile-time name:

```cpp
using Sampling = solar::execution::Periodic<
    "sample-sensors", SampleSensors, 20ms, SensorWorkQueue>;
```

Equivalent conceptual metadata is:

```cpp
solar::execution::Descriptor{
    .name = "sample-sensors",
};
```

External descriptor customization remains available through tag-aware
`descriptor_traits`.

### 9.3 Local and stable identity

Every normalized registration receives a dense build-local execution ID.

A stable ID is optional unless a later protocol explicitly exposes the
registration across builds. Remote exposure does not occur automatically.

### 9.4 Owner and origin

Each registration retains:

- semantic owner;
- registration origin;
- behavior type;
- registration kind;
- execution target;
- dependency set.

Subsystem-derived registrations retain their subsystem declaration as origin.

## 10. Registration And Contribution

### 10.1 Component-local tasks

The conventional application alias is:

```cpp
using Tasks = solar::execution::Tasks<...>;
```

Example:

```cpp
struct Navigation
{
    using Tasks = solar::execution::Tasks<
        UpdateOdometry,
        PublishNavigationState>;
};
```

The owner is `Navigation`. The concrete task registrations remain leaves and
do not become child components.

### 10.2 Root registrations

Root task registrations use the Phase 1 blueprint section:

```cpp
solar::Execution<
    PublishMetrics,
    Housekeeping>
```

The application root is their registration origin and semantic owner unless an
explicit owner wrapper says otherwise.

### 10.3 Infrastructure registrations

Bus, parameters, events, logging, metrics, and Remote may derive registrations
during effective-blueprint normalization.

These entries enter the generic execution-registration catalog but do not
pretend to be application tasks.

### 10.4 Catalog kinds

The execution subsystem exposes at least:

- all execution registrations;
- application task registrations;
- executor components;
- execution targets;
- service execution records from Phase 3.

Registration kind distinguishes application task, bus route, parameter
persistence, event processor, log processor, metrics exporter, Remote handler,
and reserved extension kinds.

## 11. Execution Targets

### 11.1 Explicit target

A registration may name its target directly:

```cpp
using Sampling = solar::execution::Periodic<
    "sample-sensors",
    SampleSensors,
    20ms,
    SensorWorkQueue>;
```

An explicit target always wins.

### 11.2 Omitted target sentinel

Convenience templates may omit the target. Internally this produces a
`DefaultTarget` sentinel rather than selecting an executor immediately.

Effective-blueprint normalization resolves the sentinel according to Kconfig.

### 11.3 Kconfig-selected system target

When:

```text
CONFIG_SOLAR_EXECUTION_DEFAULT_SYSTEM_WORKQUEUE=y
```

an omitted target resolves to:

```cpp
solar::execution::SystemWorkQueue
```

The effective registration records that the target came from Kconfig default
resolution.

### 11.4 No selected default

When the Kconfig option is disabled, every registration requiring a workqueue
must name one explicitly.

Omission is a focused compile-time error. Solar does not create a fallback
queue.

### 11.5 No Kconfig application type

Kconfig cannot name an application C++ workqueue type. Solar initially does
not provide a hidden application-wide named-queue default.

Applications wanting a named queue place that type visibly in each relevant
registration or subsystem configuration.

## 12. System Workqueue Target

### 12.1 First-class target

`execution::SystemWorkQueue` is a stateless typed target adapter over
`kernel::system_work_queue`.

It is a valid target for application and infrastructure registrations.

### 12.2 Not an effective component

The system workqueue is a Zephyr platform thread created outside Solar's
component lifecycle. It receives no Solar component lifecycle record.

Execution may expose a focused target descriptor and aggregate facts for
Solar-owned registrations using it.

### 12.3 Solar ownership limit

Solar owns only:

- its work-item storage;
- its registration state;
- its cancellation and flush synchronization objects;
- its counters and failures;
- its behavior invocation.

Solar does not own:

- the system queue thread;
- the system queue stack;
- unrelated Zephyr work items;
- global queue admission;
- global queue shutdown.

### 12.4 Prohibited claims

Solar must never:

- stop the system workqueue;
- plug it;
- globally drain it;
- claim its complete depth;
- claim all ecosystem work is quiescent;
- change its runtime priority or stack;
- forcibly abort it.

### 12.5 Shutdown

At shutdown, each Solar-owned registration targeting the system queue is
individually cancelled, flushed, or otherwise synchronized according to its
stop policy.

The platform queue remains active for Zephyr and for component teardown that
may still require it.

### 12.6 Suitability

The system queue is a strong target for:

- short deferred processing;
- ISR offload;
- coalesced state updates;
- lightweight bus routes;
- event and log ingress draining;
- bounded delayed work.

A named queue is preferred for:

- blocking I/O;
- long computation;
- strict priority or latency isolation;
- unusually large stack requirements;
- execution that may wait on work also using the system queue;
- control-loop timing;
- handlers that could materially delay Zephyr facilities.

## 13. Application Workqueue Executor

### 13.1 Declaration

An application workqueue is an executor component:

```cpp
using ControlWorkQueue = solar::execution::WorkQueue<
    "control-work",
    solar::execution::StackSize<2048>,
    solar::execution::Priority<4>>;
```

It enters the blueprint normally:

```cpp
solar::Executors<ControlWorkQueue>
```

### 13.2 Owned state

The executor type owns static type-owned objects for:

- one `kernel::WorkQueue<StackBytes>`;
- its queue thread and stack;
- queue lifecycle state;
- aggregate Solar registration accounting;
- stop and containment state.

The executor does not own registration payload queues or subsystem semantic
state.

### 13.3 Configuration

Initial typed queue policy includes:

```cpp
solar::execution::StackSize<N>
solar::execution::Priority<P>
solar::execution::YieldBetweenItems
solar::execution::NoYieldBetweenItems
solar::execution::WorkTimeout<Duration>
solar::execution::StopTimeout<Duration>
```

Yielding between items is the default, matching Zephyr.

### 13.4 Nonessential ownership

A Solar-owned workqueue is never marked as an essential Zephyr thread. An
essential workqueue cannot satisfy Solar's normal stop and containment
contract.

### 13.5 One worker thread initially

Initial `execution::WorkQueue` owns one Zephyr workqueue thread. Pools and
parallel execution are deferred.

All items targeting one queue therefore execute serially.

## 14. On-Demand Work

### 14.1 Declaration

```cpp
using Recalculate = solar::execution::OnDemand<
    "recalculate-control",
    RecalculateControl,
    ControlWorkQueue>;
```

### 14.2 Submission

```cpp
auto result = solar::execution::submit<Recalculate>();
```

Submission is non-waiting and mirrors `k_work_submit_to_queue` or
`k_work_submit` after bound registration validation.

`OnDemand` is the initial event-triggered task form: application code or an
owning subsystem explicitly signals that work exists. This use of trigger does
not make observability Events an application callback bus. Any adapter from a
typed subsystem occurrence to work remains an explicit infrastructure
registration.

### 14.3 Native coalescing default

The default admission policy is native Zephyr work coalescing:

- an idle item is queued;
- an already queued item remains once in its current queue position;
- a running item may be queued once more on the queue currently running it;
- cancellation or unavailable queue state rejects submission.

Repeated submission is therefore a statement that work exists, not a promise
of one invocation per call.

### 14.4 Condition-owned work

Code using native coalescing maintains stable shared state expressing what the
handler must process. It submits directly instead of using a racy
check-then-submit sequence.

### 14.5 Counted admission

When every indistinguishable trigger requires one invocation, a registration
may explicitly select:

```cpp
solar::execution::Counted<Capacity>
```

Counted admission owns a bounded pending count. One native work item invokes
the behavior for accepted occurrences and resubmits between bounded batches so
other queue items can progress.

Initial counted overflow is explicit `Reject`. Silent unbounded accumulation
is forbidden.

### 14.6 Payload-free initial task contract

General task submission initially carries no arbitrary payload.

Payload-owning subsystems retain exact typed storage and use a work item to
drain it. This avoids creating a second generic message bus inside execution.

## 15. Delayable Work

### 15.1 Declaration

```cpp
using PersistParameters = solar::execution::Delayable<
    "persist-parameters",
    PersistDirtyParameters,
    StorageWorkQueue>;
```

### 15.2 First-deadline scheduling

```cpp
solar::execution::schedule<PersistParameters>(100ms);
```

If the item is already scheduled or submitted, another `schedule` does not
move its existing deadline. This models a bounded window measured from the
first request.

### 15.3 Last-deadline rescheduling

```cpp
solar::execution::reschedule<PersistParameters>(100ms);
```

`reschedule` replaces an incomplete delay. This models debounce and a quiet
period measured from the most recent request.

### 15.4 Zero delay

A zero delay follows Zephyr's immediate submission semantics. The operation
still returns its actual disposition.

### 15.5 Flush

Flushing delayable work submits scheduled work immediately and waits through
its completion. It is not equivalent to waiting until the original deadline.

### 15.6 Cancellation

Asynchronous cancellation may return while behavior is still running.
Synchronous cancellation waits until the item is idle.

Execution stop paths use synchronous containment from thread context.

## 16. Periodic Work

### 16.1 Declaration

```cpp
using Sampling = solar::execution::Periodic<
    "sample-sensors",
    SampleSensors,
    20ms,
    SensorWorkQueue>;
```

Periodic is a Solar registration policy built over Kernel delayable work. It
is not a separate Kernel primitive pretending Zephyr provides a periodic work
item.

### 16.2 Default cadence

The default is fixed-rate scheduling:

- ideal releases are separated by the declared period;
- queue latency does not redefine the ideal cadence;
- one registration never executes concurrently with itself;
- elapsed ideal releases are counted when execution falls behind;
- the next release is scheduled at the next future cadence boundary.

### 16.3 Default first release

The first release occurs after one period by default.

An explicit `StartImmediately` policy releases once when the registration
activates and then follows its cadence.

### 16.4 Default overrun behavior

The initial default is:

```cpp
solar::execution::overrun::Skip
```

Missed releases do not create an unbounded catch-up backlog. They increment
missed-release and overrun records.

### 16.5 Fixed delay

An explicit fixed-delay policy schedules the next release one period after the
previous behavior completes:

```cpp
solar::execution::periodic::FixedDelay
```

This guarantees a minimum delay between completions and subsequent releases
but intentionally permits cadence drift.

### 16.6 Deadline

An optional typed deadline is measured from ideal release to completion:

```cpp
solar::execution::Deadline<10ms>
```

Deadline misses are diagnostic facts. They do not automatically alter Zephyr
thread priority or stop the registration.

### 16.7 Catch-up policy

Bounded queued or catch-up periodic execution is deferred. It must declare a
finite maximum and cannot overlap one registration without an explicit future
parallel executor contract.

## 17. Poll-Triggered Work

### 17.1 Zephyr capability

Zephyr can arm a work item against a fixed array of poll events so no dedicated
thread waits for the corresponding kernel object.

Solar adopts this capability through:

```cpp
solar::kernel::TriggeredWork
solar::execution::PollTriggered
```

### 17.2 Registration

Illustrative shape:

```cpp
using DrainInput = solar::execution::PollTriggered<
    "drain-input",
    DrainInputBehavior,
    InputPollSet,
    IoWorkQueue>;
```

The poll-set type defines fixed persistent event storage and the supported arm
operation.

### 17.3 Lifetime

The triggered work object and event array are static type-owned state. They
remain valid and immutable throughout arm, trigger, queue, execution, and
cancellation.

### 17.4 Rearming

One-shot and explicit rearm policies are distinct. Automatic rearm occurs only
when declared and must stop rearming after quiescence begins.

### 17.5 Cancellation phases

The adapter handles both:

- cancellation while waiting on poll events;
- ordinary work synchronization after the item has entered a queue.

Exact native phase transitions remain Zephyr-owned.

## 18. Submission API

### 18.1 Ordinary submit

```cpp
template<execution::OnDemandRegistration R>
solar::Result<execution::Submission, execution::Error>
submit();
```

The operation validates registration at compile time and runtime availability
at the call.

### 18.2 ISR submit

```cpp
template<execution::IsrSubmittable R>
solar::Result<execution::Submission, execution::Error>
try_submit_isr();
```

This is also non-waiting. The explicit spelling documents context and rejects
registrations whose admission extension or target is not ISR-compatible.

### 18.3 Ordinary spelling

Native ordinary work submission does not wait for capacity, so the ordinary
API remains `submit`. ISR-capable variants consistently use the Solar-wide
`try_<verb>_isr` spelling to make their restricted, non-waiting contract
visible.

Admission extensions that support finite waiting may add an explicit waiting
operation later. ISR submission never waits.

### 18.4 Schedule APIs

```cpp
solar::execution::schedule<DelayedRegistration>(delay);
solar::execution::reschedule<DelayedRegistration>(delay);
```

Both preserve native first-deadline versus last-deadline meaning.

### 18.5 Cancellation and synchronization

Focused typed operations include:

```cpp
solar::execution::cancel<Registration>();
solar::execution::cancel_sync<Registration>();
solar::execution::flush<Registration>();
```

Their availability depends on registration kind and caller context.

Lifecycle code uses internal explicit-system forms rather than relying on the
global binding during partial teardown.

## 19. Submission Result

### 19.1 Receipt

`execution::Submission` includes at least:

- registration local ID;
- target local ID or system-target marker;
- disposition;
- resulting accepted or coalesced accounting value where meaningful;
- ISR-origin fact;
- optional release sequence.

### 19.2 Disposition

Initial dispositions include:

```cpp
enum class SubmissionDisposition
{
    Queued,
    AlreadyPending,
    RequeuedAfterCurrent,
    Counted,
};
```

`AlreadyPending` is a successful native-coalescing outcome, not queue failure.

### 19.3 Error

`execution::Error` is a compact structured error containing:

- broad reason;
- registration local ID when available;
- target local ID or target kind;
- native errno where useful;
- operation kind;
- caller context;
- current registration availability state.

Broad reasons include:

- subsystem not ready;
- registration inactive;
- executor unavailable;
- queue stopped;
- queue draining or plugged;
- work cancelling;
- counted admission full;
- invalid caller context;
- timeout;
- unsupported operation;
- internal invariant failure.

## 20. Dependencies

### 20.1 Registration dependencies

A task registration declares the components its behavior may access:

```cpp
using Sampling = solar::execution::Periodic<
    "sample-sensors",
    SampleSensors,
    20ms,
    SensorWorkQueue,
    solar::execution::DependsOn<Imu, SampleStore>>;
```

Convenience syntax may place dependency policy in a normalized policy group,
but the effective metadata is explicit.

### 20.2 Owner dependencies

Component-contributed registrations inherit their semantic owner's direct
component dependency closure unless explicitly narrowed by a safe future
policy.

Registration-specific dependencies may add to that set.

### 20.3 Executor dependency

Every registration targeting an application workqueue depends on that executor
component.

The system workqueue is a platform target rather than a component dependency.

### 20.4 Leaf ordering

Registrations do not add fake component nodes. Effective normalization retains
leaf dependency edges so activation and quiescence occur within the valid
component lifetime window.

### 20.5 Uncontained work

If behavior remains in flight, its owner, declared dependencies, target
executor, and their required transitive dependencies remain potentially
accessible. Phase 3 dependency preservation applies.

## 21. Lifecycle

### 21.1 Executor lifecycle

An application `execution::WorkQueue` is a normal executor component with:

```cpp
init()
start()
stop()
deinit()
```

implemented by its generated static type where appropriate.

It receives ordinary lifecycle and executor records.

### 21.2 Task lifecycle

Task registrations are leaves and receive no component lifecycle record.

They have focused activation and execution state under `solar::execution`.

Task behavior has no implicit `init`, `start`, `stop`, or `deinit` hooks. State
requiring component lifecycle belongs to the owner component.

### 21.3 Initialization

Registration initialization:

1. initializes Kernel work storage once;
2. initializes synchronization and bounded admission state;
3. materializes focused records from immutable catalogs;
4. resolves and validates its target;
5. clears runtime counters for the boot.

No behavior runs during registration initialization.

### 21.4 Executor start

Application workqueues start after their dependencies and before registrations
targeting them activate.

Failure to create or start a queue thread is an executor component start
failure and enters the Phase 3 boot report.

### 21.5 Registration activation

Application task admission opens as the system enters `Running`.

Periodic releases and poll-triggered arms begin only after their dependencies
and target are ready. No task behavior is used as implicit boot coordination.

Subsystem infrastructure may have a narrower lifecycle window explicitly
defined by its owning facility specification.

### 21.6 Activation failure

Failure to arm or schedule a required registration is attributed to its owner
with the registration local ID and target. It does not create a fake component
failure node.

## 22. Shutdown And Containment

### 22.1 Registration admission closes first

When stopping begins, external application submission closes before component
destructive teardown.

Calls racing after closure fail as not ready unless already committed under
the registration's synchronization rule.

### 22.2 Stop sequence

For each owner and executor relationship, shutdown performs:

1. close new admission;
2. prevent periodic re-release and poll rearm;
3. cancel or flush delayable items according to stop policy;
4. cancel or drain queued registration work;
5. request cooperative stop for in-flight behavior;
6. wait for bounded quiescence;
7. verify focused records;
8. stop an owned queue only after all registrations are contained.

### 22.3 Stop policies

Initial registration stop policies are:

```cpp
solar::execution::stop::Drain
solar::execution::stop::CancelPending
```

`Drain` allows already accepted work to complete before the timeout.

`CancelPending` prevents not-yet-started work and waits only for behavior
already in flight.

Delayable flush semantics remain explicit because flush causes scheduled work
to run immediately.

### 22.4 No arbitrary handler abort

Solar never asynchronously aborts one arbitrary handler running on a shared
queue. It may request a stop token and wait.

If an owned workqueue cannot quiesce, Phase 3 executor containment policy may
forcibly contain the complete queue thread. That action affects every item on
the queue and is therefore an executor-level failure, not task cancellation.

### 22.5 Owned queue stop

Before calling native queue stop, Solar:

- synchronizes every known delayable and triggered registration;
- closes external submissions;
- drains and plugs the queue;
- confirms no owned registration remains armed;
- invokes bounded `k_work_queue_stop`.

Draining alone is insufficient because delayed items are not yet associated
with the target queue.

### 22.6 System queue stop

Solar synchronizes only its items and never drains, plugs, stops, or aborts the
system queue.

## 23. Cancellation Guarantees

### 23.1 Pending cancellation

Successful synchronous cancellation guarantees the registration's work object
is idle when the call returns unless a separately permitted producer submits
after cancellation completes.

Execution prevents that race during lifecycle stop by closing admission first.

### 23.2 In-flight behavior

Asynchronous cancellation does not stop behavior already executing.

Synchronous cancellation waits for the running handler and native cancellation
state to complete. Behavior accepting `StopToken` may return cooperatively.

### 23.3 Resubmission race

Handlers that resubmit or rearm must check registration admission state. Once
quiescence begins, rearm and chained submission cease.

### 23.4 Per-occurrence cancellation

Initial execution provides registration-level cancellation, not heap-like
handles for every occurrence.

Per-occurrence cancellation is deferred until a bounded identity and storage
model justifies it.

## 24. Timing And Scheduling Ownership

### 24.1 Executor-owned fields

The execution target owns:

- worker thread;
- stack;
- Zephyr priority;
- cooperative versus preemptive context through native priority;
- queue yield policy;
- queue-wide native work timeout;
- queue stop timeout and containment.

### 24.2 Registration-owned fields

The registration owns:

- trigger or timing mode;
- period and initial release;
- deadline metadata;
- admission and coalescing behavior;
- overrun policy;
- registration stop policy;
- behavior failure policy.

### 24.3 No per-item thread priority

One work item cannot claim a different Zephyr thread priority from the queue
that executes it.

Work requiring another priority targets another explicit queue.

### 24.4 No hidden stack estimate

Solar does not infer stack size from behavior. The system workqueue uses
Zephyr Kconfig. Application workqueues declare stack size explicitly.

## 25. Failure Policy

### 25.1 Behavior failure

The default behavior-failure policy is record and continue.

One failed invocation updates the registration record and does not stop the
shared queue or unrelated registrations.

### 25.2 Registration suspension

An explicit policy may suspend future releases after failure:

```cpp
solar::execution::failure::Suspend
```

Reactivation requires an explicit supported operation or later lifecycle
transition. It is not silently retried.

### 25.3 Unexpected executor exit

A Solar-owned workqueue thread exiting or being aborted outside requested
containment is `UnexpectedExit`.

The executor record identifies detection time, active registration where
known, and containment implications.

### 25.4 Native work timeout

When Zephyr work-timeout monitoring is enabled, an overlong handler can abort
the queue thread. For the essential system workqueue this may become a fatal
kernel condition. Solar exposes the effective configuration and does not
pretend this is an ordinary task timeout.

For an application queue, detection updates its executor failure record and
applies Phase 3 dependency preservation.

### 25.5 Reporting failures

Execution does not recursively log or emit events from ISR or critical failure
paths. Optional infrastructure adapters may report failures later from a safe
context.

## 26. Execution Records

### 26.1 Registration record

Each normalized registration has a bounded record containing at least:

- registration identity, descriptor, owner, and origin;
- registration kind and behavior identity;
- target identity and resolution source;
- effective timing, admission, stop, and failure policy;
- initialized, active, accepting, armed, queued, and in-flight facts;
- release attempts;
- submissions and accepted dispositions;
- already-pending and requeued counts;
- counted-admission rejection and high-water facts;
- started, completed, failed, cancelled, and drained counts;
- first and last failure;
- last normalized result;
- last release, start, and completion timestamps;
- last and maximum observed duration when enabled;
- missed releases, overruns, and deadline misses;
- stop requested and quiescent facts.

### 26.2 Executor record

Each application executor record contains at least:

- executor component identity;
- configured stack, priority, yielding, work timeout, and stop timeout;
- initialized, started, accepting, draining, plugged, and stopped facts;
- native thread identity when available;
- active Solar registration when known;
- aggregate Solar submission, start, completion, and failure counts;
- aggregate tracked pending count and high-water mark;
- stop, join, timeout, abort, and containment facts;
- unexpected-exit fact;
- optional native stack and runtime diagnostics.

### 26.3 System target record

The system-workqueue target view contains:

- target identity;
- Zephyr Kconfig stack size, priority, yield, and timeout facts;
- Solar registration count targeting it;
- aggregate Solar-owned submission and completion facts;
- explicit ownership limitations.

It does not claim complete native queue depth or lifecycle ownership.

### 26.4 Synchronization

Thread-context records use bounded mutex-protected or equivalently coherent
copies. ISR counters use atomics or short ISR-safe critical sections.

Queries do not call a vague `snapshot()` and do not infer durable truth from a
single native busy flag.

## 27. Public Inspection

Focused query shapes are:

```cpp
solar::execution::task<Sampling>();
solar::execution::tasks();

solar::execution::registration<LogProcessor>();
solar::execution::registrations();

solar::execution::executor<ControlWorkQueue>();
solar::execution::executors();

solar::execution::target<solar::execution::SystemWorkQueue>();
solar::execution::targets();

solar::execution::service<RemoteService>();
solar::execution::services();
```

`registrations()` is preferred over the earlier illustrative `jobs()` name.
Jobs are transient occurrences; registrations are the static queryable
architecture.

Explicit-system forms remain available for tests:

```cpp
solar::execution::Of<TestApplication>::registration<TestWork>();
```

## 28. Subsystem Integration

### 28.1 Shared protocol

Every subsystem-owned deferred path uses the same internal work-registration
adapter and execution targets.

This does not transfer semantic storage or runtime records into Execution.

### 28.2 Bus

The bus owns route payload storage, overflow, delivery, and handler records.

Execution owns submission to the selected workqueue and generic invocation
facts. A route is an infrastructure registration of kind `BusRoute`, not an
application task.

Native coalescing may schedule one route-drain item for several queued payloads.

### 28.3 Parameters

Deferred parameter persistence uses one delayable infrastructure registration
per effective facility policy, not one thread or timer per parameter.

`schedule` implements a first-change window. `reschedule` implements a quiet
period after the latest change. Parameter state remains canonical in the
Parameters facility.

### 28.4 Events

The Events processor uses coalesced on-demand work to drain its ingress.
Optional slow observers own independent bounded admission and may target a
separate queue.

Event history and policy state remain owned by Events.

### 28.5 Logging

The normal log processor uses coalesced work. Panic handling bypasses the
normal workqueue and follows the synchronous emergency path from Phase 8.

Slow log sinks may own separate bounded registration paths. Log ingress and
history remain owned by Logging.

### 28.6 Metrics

Metrics storage remains passive. Optional periodic exporters use explicit
periodic execution registrations and targets.

### 28.7 Remote

Remote deferred handlers may target workqueues, but Remote owns request
payloads, response state, sessions, authorization, timeout, and correlation.

Phase 10 decides endpoint-specific inline, workqueue, mailbox, and asynchronous
handling using this execution foundation.

### 28.8 Service mailbox

A general `ServiceMailbox<Service>` bus adapter remains deferred.

Services needing mailboxes initially own typed Kernel queues or consume
component-owned storage in their sustained run loop. A mailbox is not faked as
a workqueue executor.

## 29. Subsystem Target Configuration

Subsystems requiring deferred work may name a target in their typed
configuration:

```cpp
solar::events::Configuration<
    solar::events::ExecutionTarget<DiagnosticsWorkQueue>>
```

or:

```cpp
solar::log::Configuration<
    solar::log::ExecutionTarget<
        solar::execution::SystemWorkQueue>>
```

When omitted, the registration follows the Kconfig default-target rule.

If no target resolves, normalization fails. A subsystem does not create a
private queue as fallback.

## 30. Kconfig And C++ Policy

### 30.1 Zephyr-owned Kconfig

Solar uses Zephyr's native system-workqueue configuration directly:

```text
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE
CONFIG_SYSTEM_WORKQUEUE_PRIORITY
CONFIG_SYSTEM_WORKQUEUE_NO_YIELD
CONFIG_WORKQUEUE_WORK_TIMEOUT
CONFIG_SYSTEM_WORKQUEUE_WORK_TIMEOUT_MS
```

Solar does not duplicate these values under Solar-specific symbols.

### 30.2 Solar Kconfig

Solar Kconfig owns:

- whether bound execution integration is compiled;
- whether omitted targets resolve to the system workqueue;
- compile-time diagnostic support;
- hard ceilings required by generic execution metadata;
- default broad stop timeouts where an explicit C++ queue policy omits one.

The central target choice is:

```text
CONFIG_SOLAR_EXECUTION_DEFAULT_SYSTEM_WORKQUEUE
```

Exact help text must state that it creates no Solar queue and makes omitted
targets use Zephyr's existing system workqueue.

### 30.3 C++ policy

C++ declarations own:

- application workqueue membership;
- queue stack and priority;
- target selection;
- task registration membership;
- period, deadline, admission, overrun, stop, and failure policy;
- component and registration dependencies;
- subsystem target override.

### 30.4 Precedence

Target resolution is:

```text
explicit registration or subsystem target
    > Kconfig system-workqueue default
    > compile-time missing-target error
```

A C++ declaration cannot re-enable Kconfig-disabled execution support.

## 31. Resource Accounting

### 31.1 No dynamic allocation

Core execution registration, work storage, queue storage, timing state, and
records require no dynamic allocation.

### 31.2 Per-registration cost

The effective blueprint accounts for each registration's exact state:

- ordinary, delayable, or triggered native work object;
- synchronization object where required;
- stop source when behavior accepts it;
- counted pending state when selected;
- periodic timing state when selected;
- record counters enabled by diagnostics policy.

Unused policy state is absent.

### 31.3 Per-executor cost

Each application workqueue visibly costs:

- one Zephyr queue object;
- one thread control block;
- one declared stack;
- aggregate record state.

This is why Solar does not create a default queue automatically.

### 31.4 System target cost

Targeting the system workqueue adds registration work-item state but no
Solar-owned queue stack or thread.

The project remains responsible for configuring the shared Zephyr stack large
enough for the deepest handler call chain.

## 32. Include Direction

The intended layering is:

```text
solar/kernel/work.hpp
solar/kernel/work_queue.hpp
    -> Zephyr kernel headers and Solar core result/time types

solar/execution/registration.hpp
solar/execution/work_queue.hpp
solar/execution/inspection.hpp
    -> Solar Kernel, catalog, lifecycle, and binding infrastructure

application behavior headers
    -> direct dependency headers and Solar execution declarations

application composition root
    -> behavior, executor, and subsystem declarations
```

Kernel headers never include the application binding, blueprint, or Execution
catalog.

Ordinary component headers never include the composition root.

## 33. Complete Example

```cpp
// execution/control_work.hpp
#pragma once

#include <solar/execution.hpp>

using ControlWorkQueue = solar::execution::WorkQueue<
    "control-work",
    solar::execution::StackSize<3072>,
    solar::execution::Priority<3>,
    solar::execution::YieldBetweenItems,
    solar::execution::StopTimeout<250ms>>;
```

```cpp
// tasks/control_tasks.hpp
#pragma once

#include "devices/imu.hpp"
#include "execution/control_work.hpp"

#include <solar/execution.hpp>

struct UpdateOdometryBehavior
{
    static solar::Result<void> execute();
};

using UpdateOdometry = solar::execution::Periodic<
    "update-odometry",
    UpdateOdometryBehavior,
    10ms,
    ControlWorkQueue,
    solar::execution::DependsOn<Imu>,
    solar::execution::periodic::FixedRate,
    solar::execution::overrun::Skip,
    solar::execution::Deadline<8ms>>;

struct RebuildControllerBehavior
{
    static void execute();
};

using RebuildController = solar::execution::OnDemand<
    "rebuild-controller",
    RebuildControllerBehavior,
    ControlWorkQueue>;
```

```cpp
// facilities/navigation.hpp
#pragma once

#include "tasks/control_tasks.hpp"

#include <solar/facility.hpp>

struct Navigation
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "navigation",
    };

    using Dependencies = solar::Dependencies<Imu>;

    using Tasks = solar::execution::Tasks<
        UpdateOdometry,
        RebuildController>;

    static solar::Result<void> init();
    static solar::Result<void> request_rebuild();
};
```

```cpp
// app/system.hpp
#pragma once

#include "execution/control_work.hpp"
#include "facilities/navigation.hpp"

#include <solar/system.hpp>

using RobotBlueprint = solar::Blueprint<
    solar::Devices<Imu>,
    solar::Facilities<Navigation>,
    solar::Executors<ControlWorkQueue>>;

using RobotSystem = solar::System<RobotBlueprint>;

SOLAR_BIND_SYSTEM(RobotSystem);
```

```cpp
// facilities/navigation.cpp
#include "app/system.hpp"
#include "facilities/navigation.hpp"

solar::Result<void> Navigation::request_rebuild()
{
    return solar::execution::submit<RebuildController>()
        .transform([](const solar::execution::Submission&) {});
}
```

`request_rebuild()` is called during the valid `Running` window. `init()` uses
direct dependency operations and does not submit ordinary application work.

## 34. System Workqueue Example

With:

```text
CONFIG_SOLAR_EXECUTION_DEFAULT_SYSTEM_WORKQUEUE=y
```

the declaration may omit its target:

```cpp
struct RefreshDisplayBehavior
{
    static void execute();
};

using RefreshDisplay = solar::execution::OnDemand<
    "refresh-display",
    RefreshDisplayBehavior>;
```

The effective catalog records:

```text
registration: refresh-display
target: Zephyr system workqueue
target source: Kconfig default
ownership: external platform target
```

The same explicit declaration is always available:

```cpp
using RefreshDisplay = solar::execution::OnDemand<
    "refresh-display",
    RefreshDisplayBehavior,
    solar::execution::SystemWorkQueue>;
```

## 35. Direct Kernel Example

Not every work item needs system registration:

```cpp
struct EncoderDeferred
{
    static void run();

    static inline solar::kernel::Work work{[] {
        run();
    }};
};

void encoder_isr()
{
    (void)EncoderDeferred::work.submit(
        solar::kernel::system_work_queue);
}
```

If this work may still run during component teardown, `Encoder` must cancel or
flush it in its own stop hook. It has no execution registration or focused
system record.

## 36. Compile-Time Validation

Solar must diagnose:

- missing registration descriptor or name;
- duplicate registration type;
- duplicate registration name in the execution catalog;
- invalid behavior signature or return type;
- missing or unregistered application workqueue target;
- omitted target without the Kconfig system default;
- workqueue target under the wrong component category;
- zero stack or period;
- unsupported priority;
- invalid deadline or stop timeout;
- `Counted<0>`;
- ISR submission to a non-ISR-safe admission policy;
- periodic registration using on-demand-only policy;
- delayable operation used on ordinary work;
- poll-triggered registration with nonpersistent or malformed event storage;
- essential policy on a Solar-owned queue;
- work timeout requested without Zephyr support where policy requires it;
- task dependency on an absent component;
- registration dependency cycle through effective ownership;
- subsystem deferred work with no resolved target;
- policy resource requirement beyond Kconfig ceiling.

Diagnostics should identify the registration, target, and conflicting policy.

## 37. Runtime Errors

Runtime operations report, rather than silently ignore:

- submission while inactive;
- queue unavailable or stopped;
- queue draining or plugged;
- work cancellation in progress;
- counted pending capacity exhaustion;
- invalid caller context;
- delayable schedule or reschedule failure;
- triggered-work arm or cancellation failure;
- behavior failure;
- registration quiescence timeout;
- queue drain or stop timeout;
- unexpected queue-thread exit;
- native invariant failure.

Intentional native coalescing is a successful disposition and is counted.

## 38. Verification Requirements

Implementation tests must cover:

- Kernel ordinary work submission outcomes 0, 1, and 2;
- direct system-workqueue submission from thread and ISR;
- queue submission rejection while draining, plugged, stopped, or cancelling;
- schedule preserving the first deadline;
- reschedule replacing the deadline;
- zero-delay behavior;
- synchronous and asynchronous ordinary cancellation;
- delayable cancellation and flush-immediate semantics;
- work resubmission from inside a handler;
- no check-then-submit race in Execution;
- Kconfig system-default resolution;
- missing-target compile failure when the default is disabled;
- explicit system target overriding omission rules;
- application workqueue start, drain, plug, and stop;
- delayed-item containment before queue stop;
- system queue never being globally drained or stopped;
- native coalescing accounting;
- bounded counted admission and full rejection;
- behavior return normalization;
- one behavior used by several registrations;
- fixed-rate cadence and missed-release accounting;
- fixed-delay cadence;
- start-after-period and start-immediately behavior;
- deadline miss and overrun accounting;
- poll-trigger arm, trigger, rearm, and cancellation phases;
- task contribution owner and origin preservation;
- root registration collection;
- executor and dependency validation;
- registration activation only in its valid lifecycle window;
- drain and cancel-pending stop policy;
- in-flight stop-token cooperation;
- uncontained executor dependency preservation;
- unexpected queue-thread exit;
- record synchronization under concurrent query;
- bus route, parameter persistence, event processor, and log processor sharing
  execution without sharing semantic storage;
- no dynamic allocation in core paths.

Tests must run against every supported Zephyr upgrade because workqueue return,
cancellation, and stop behavior are integration-critical.

## 39. Migration From Current Solar

The current implementation in `include/solar/task.hpp` is an implementation
spike, not the final architecture.

Required migration includes:

1. Move faithful work, delayable work, triggered work, and workqueue wrappers
   into `solar::kernel`.
2. Preserve distinct non-negative native submission dispositions.
3. Replace `EventTask` with `execution::OnDemand`.
4. Replace `PeriodicTask` with `execution::Periodic` targeting an explicit or
   Kconfig-resolved queue.
5. Replace `SharedTask` with ordinary registrations targeting one named
   `execution::WorkQueue`.
6. Remove `DedicatedTask` as hidden thread ownership.
7. Represent dedicated execution with an explicit workqueue used by the
   relevant registration, or with a service for sustained loops.
8. Replace `TaskThreadPolicy` with executor queue policy.
9. Remove tasks from component lifecycle kinds and positional `Tasks<...>`
   graph storage.
10. Add the generic execution-registration catalog and component `Tasks`
    contribution adapter.
11. Move task facts out of lifecycle and into focused Execution records.
12. Add target resolution and the system-workqueue Kconfig option.
13. Add Delayable and PollTriggered registrations.
14. Replace unsynchronized counters with coherent records.
15. Integrate subsystem work registrations through one internal adapter.

Temporary compatibility aliases may exist during implementation, but new code
must not retain hidden dedicated task threads or assume every task is a
component.

## 40. Initial Required Capability

The first complete implementation must include:

- Kernel ordinary and delayable work wrappers;
- Kernel owned and system workqueue wrappers;
- faithful native result mapping;
- OnDemand registrations with native coalescing;
- bounded Counted admission;
- Delayable schedule and reschedule;
- Periodic fixed-rate and fixed-delay scheduling;
- skip-on-overrun and deadline diagnostics;
- system-workqueue target;
- application WorkQueue executor components;
- Kconfig system-default target selection;
- task contributions and root registrations;
- registration dependencies;
- lifecycle activation and quiescence;
- drain and cancel-pending stop policy;
- registration and executor records;
- internal subsystem work adapter;
- poll-triggered Kernel primitive and execution registration support.

## 41. Deferred Capabilities

The following are deliberately deferred:

- multi-thread executor pools;
- work stealing;
- dynamic executor creation;
- runtime task registration;
- arbitrary callable submission;
- generic payload-bearing tasks;
- heap-backed futures or promises;
- coroutine scheduling runtime;
- per-occurrence cancellation handles;
- unbounded counted admission;
- bounded periodic catch-up queues;
- overlapping invocation of one registration;
- earliest-deadline-first registration scheduling above Zephyr thread policy;
- runtime migration between queues;
- service mailbox integration;
- controlled in-process reboot reconstruction;
- supervisory restart policy;
- cross-core executor affinity beyond native queue configuration;
- application-defined default target type hidden behind global configuration.

Deferred features must preserve static membership, visible resource ownership,
bounded storage, Zephyr-native semantics, and focused diagnostics.

## 42. Rejected Alternatives

### 42.1 Automatically create a Solar default executor

Rejected because it hides a thread, stack, priority, and memory cost from the
application architecture.

### 42.2 Treat the system workqueue as a discouraged fallback

Rejected because Zephyr deliberately provides it as the normal shared deferred
execution path and recommends additional queues only when coexistence is
unsuitable.

### 42.3 Put work primitives directly in Execution only

Rejected because direct typed C++ access to Zephyr mechanisms belongs in
Kernel and must remain usable without a bound system.

### 42.4 Put catalogs and lifecycle into Kernel work

Rejected because Kernel primitives do not imply system registration or Solar
ownership.

### 42.5 Reimplement the work state machine

Rejected because Zephyr already handles concurrent submission, cancellation,
resubmission, delay, drain, and SMP synchronization.

### 42.6 Collapse native submission results to `Status::Ok`

Rejected because already queued and requeued-after-current have different
observable semantics.

### 42.7 Reject every duplicate native submission

Rejected because native coalescing is useful and is the natural `k_work`
contract.

### 42.8 Promise one invocation per ordinary submit

Rejected because one native work item does not provide an occurrence queue.
Explicit bounded counted admission supplies that promise when required.

### 42.9 One thread per task

Rejected because it wastes stacks and obscures the value of shared deferred
work.

### 42.10 Hidden thread inside `DedicatedTask`

Rejected because thread ownership belongs to a visible executor component.

### 42.11 Make all sustained loops tasks

Rejected because sustained active behavior with cooperative shutdown is a
service.

### 42.12 Globally drain the system workqueue during Solar shutdown

Rejected because Solar does not own unrelated Zephyr work and cannot claim its
quiescence.

### 42.13 Infer queue state from `is_pending()` before submitting

Rejected because Zephyr documents this as a racy check-then-act pattern.

### 42.14 Drain an owned queue without cancelling delayable items

Rejected because delayed items are not associated with the queue until their
deadline and may fire after drain.

### 42.15 Allow essential Solar-owned workqueues

Rejected because essential queues cannot satisfy normal stop semantics.

### 42.16 Mix arbitrary payloads into one execution queue

Rejected because typed subsystems already own correct bounded payload storage.

### 42.17 Automatically expose tasks through Remote

Rejected because registration does not grant protocol identity,
authorization, invocation, or cancellation rights.

### 42.18 Give task behavior lifecycle hooks

Rejected because lifecycle state belongs to its owner component. A task is a
leaf registration.

### 42.19 Call the static catalog `jobs`

Rejected as the canonical name because jobs are runtime occurrences.
`registrations` accurately describes immutable architecture.

## 43. Accepted Decisions

1. Task behavior is independent of trigger, schedule, and execution target.
2. A task behavior is not a component.
3. An execution registration is a leaf.
4. An executor is a component when Solar owns its execution machinery.
5. Zephyr workqueues are the canonical shared-execution foundation.
6. Solar does not reimplement Zephyr's work state machine.
7. Kernel owns direct C++ work and workqueue primitives.
8. Execution owns registration, policy, lifecycle, and diagnostics.
9. Kernel primitives require no application binding.
10. Direct Kernel work is valid but not automatically Solar-owned.
11. Kernel exposes Work, DelayableWork, TriggeredWork, and WorkQueue.
12. Kernel exposes the system workqueue as a native typed handle.
13. Execution exposes OnDemand, Delayable, Periodic, and PollTriggered.
14. Execution exposes WorkQueue executor components and SystemWorkQueue target.
15. The same WorkQueue name at Kernel and Execution layers is intentional.
16. There is no automatically created Solar default executor.
17. Omitted target selection is controlled by Kconfig.
18. The only initial omitted-target default is Zephyr's system workqueue.
19. Without that Kconfig default, omitted targets are compile-time errors.
20. Explicit registration or subsystem target selection always wins.
21. Kconfig cannot name an application C++ workqueue type.
22. The system workqueue is a first-class execution target.
23. The system workqueue is not a Solar component.
24. Solar never globally drains, plugs, stops, or aborts the system workqueue.
25. Solar synchronizes only its own system-workqueue items.
26. System-workqueue configuration uses Zephyr's native Kconfig symbols.
27. Solar does not duplicate system queue stack, priority, yield, or timeout
    configuration.
28. Application workqueues are explicit executor components.
29. Application workqueues visibly own one queue thread and stack.
30. Initial application workqueues are single-threaded.
31. Yield between work items is the default.
32. Solar-owned workqueues are nonessential Zephyr threads.
33. Queue stack and priority belong to the executor.
34. Period, deadline, admission, overrun, and registration stop policy belong
    to the registration.
35. Task behavior accepts execute() or execute(StopToken).
36. Accepted returns are void, Status, and Result<void>.
37. Boolean and arbitrary result-like returns are rejected.
38. No context or system object is passed to behavior.
39. One behavior may back several registrations.
40. Concrete registration type is authoritative C++ identity.
41. Convenience registrations synthesize an execution Descriptor from name.
42. Component-local task contributions use `using Tasks`.
43. Root task registrations use the `Execution<...>` blueprint section.
44. Infrastructure registrations share the execution catalog without becoming
    application tasks.
45. Registration owner and origin survive normalization.
46. Ordinary submission is non-waiting.
47. Native work submission dispositions 0, 1, and 2 remain distinct.
48. Native coalescing is the default OnDemand admission behavior.
49. Already-pending submission is a successful disposition.
50. Native coalescing does not promise one invocation per call.
51. Explicit bounded Counted admission represents repeated indistinguishable
    occurrences.
52. Initial generic task submission carries no arbitrary payload.
53. Payload-owning subsystems keep their typed bounded storage.
54. Delayable schedule preserves the first incomplete deadline.
55. Delayable reschedule replaces the incomplete deadline.
56. Flushing scheduled delayable work submits it immediately and waits.
57. Periodic is an Execution policy built over Kernel delayable work.
58. Fixed-rate is the default periodic cadence.
59. The first periodic release occurs after one period by default.
60. StartImmediately is explicit.
61. Skip is the default periodic overrun policy.
62. Fixed-delay cadence is explicitly available.
63. Deadline misses are diagnostic and do not change queue priority.
64. One registration does not execute concurrently with itself initially.
65. Poll-triggered work is a first-class supported Zephyr capability.
66. Poll-event and triggered-work storage is static and persistent.
67. ISR submission has an explicit API and is always non-waiting.
68. A redundant `try_submit` is not required for native non-waiting work.
69. Execution operations return expected-style structured results.
70. Registration dependencies determine safe activation and preservation.
71. A named workqueue target is a component dependency.
72. The system workqueue is a platform target, not a component dependency.
73. Task registrations receive focused execution state, not lifecycle records.
74. Executor components receive lifecycle and execution records.
75. Application task admission opens at the valid Running window.
76. Shutdown closes admission before cancellation and synchronization.
77. Initial stop policies are Drain and CancelPending.
78. Solar does not asynchronously abort one shared-workqueue handler.
79. Queue-level forced containment is an executor failure affecting all work.
80. Delayable and triggered items are contained before owned queue drain and
    stop.
81. Successful synchronous cancellation proves item idleness after admission
    is closed.
82. Per-occurrence cancellation handles are deferred.
83. Default behavior failure policy records and continues.
84. Explicit failure policy may suspend a registration.
85. Unexpected owned queue exit is an executor failure.
86. Native work timeout is not misrepresented as ordinary behavior timeout.
87. Registration, executor, and system-target records remain focused.
88. System-target records do not claim complete queue depth.
89. Queries prefer `registrations()` over `jobs()` for static architecture.
90. Bus routes reuse work execution while Bus retains payload and route truth.
91. Deferred parameters use delayable work without owning a private thread.
92. Events and Logging use coalesced processor work by default.
93. Panic logging bypasses normal executor dependence.
94. Metrics exporters use explicit periodic registrations when present.
95. Remote retains request and session ownership around deferred handlers.
96. A generic service mailbox remains deferred.
97. Core execution paths require no dynamic allocation.
98. Unused registration policy state is absent.
99. Every additional application workqueue has visible static resource cost.
100. Supported Zephyr upgrades must revalidate workqueue integration semantics.

## 44. Primary References

The Zephyr integration decisions were validated against local Zephyr 4.4.0
source and official documentation:

- [Zephyr Workqueue Threads](https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html)
- [Zephyr Work Queue APIs](https://docs.zephyrproject.org/latest/doxygen/html/group__workqueue__apis.html)
- [Zephyr System Threads](https://docs.zephyrproject.org/latest/kernel/services/threads/system_threads.html)
- [Zephyr Interrupt Offloading](https://docs.zephyrproject.org/latest/kernel/services/interrupts.html)
- [Zephyr Timers](https://docs.zephyrproject.org/latest/kernel/services/timing/timers.html)
- local `zephyrproject/zephyr/include/zephyr/kernel.h`
- local `zephyrproject/zephyr/kernel/work.c`
- local `zephyrproject/zephyr/kernel/system_work_q.c`
- local `zephyrproject/zephyr/kernel/Kconfig`

The implementation must continue checking assumptions against each supported
Zephyr release, especially submission dispositions, triggered-work lifetime,
delayable flush, queue drain, queue stop, and work-timeout behavior.

## 45. Open Questions

There are no blocking architectural questions for Phase 10.

Implementation may refine without changing this contract:

- exact helper-template parameter ordering;
- final names of Kernel result and synchronization wrapper types;
- exact execution descriptor optional fields;
- compact local-ID widths;
- batching budget for Counted registrations;
- fixed-rate tick rounding at exact boundaries;
- poll-set declaration helpers;
- optional source location retained for registrations;
- native queue thread diagnostics available on each board;
- compatibility aliases during migration.

These are implementation details around the accepted Kernel/Execution split,
Zephyr workqueue foundation, explicit executor ownership, and static bounded
registration model.
