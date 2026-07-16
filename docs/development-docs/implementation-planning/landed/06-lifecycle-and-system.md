# Stage 06: Lifecycle, Graph, Boot, And Stop

Status: landed

Landed date: 2026-07-16

Implementation repositories/branches:

- `/workspaces/solar`, `static_reform`;
- `/workspaces/ENMT301-RoboCup`, `developmental` for the firmware integration
  checkpoint and development records.

Baseline: Stage 05 landed worktree based on `bed432f pre-solar-implementation`

## 1. Objective

Land Solar's complete static-system lifecycle foundation: deterministic
dependency-ordered boot, explicit activation, reverse teardown and rollback,
bounded coherent records and reports, global bound APIs, and the first hard
firmware migration to the accepted Blueprint/System architecture.

This stage had to make the static System real at runtime without introducing a
runtime System object, context object, service locator, hidden executor, heap,
or second component graph.

## 2. Specification Coverage

| Specification | Coverage | Notes |
| --- | --- | --- |
| `00-design-conventions.md` | static ownership, bounded storage, focused namespaces, no context | complete for lifecycle |
| `00a-modern-cpp-result-and-status.md` | expected-based hook normalization and typed boot/stop errors | complete |
| `01-system-blueprint-and-binding.md` | bound global boot/stop and graph queries | complete for lifecycle and graph |
| `02-identity-contributions-and-catalogs.md` | component local IDs and immutable descriptor views in records | complete |
| `03-lifecycle-kernel-and-configuration.md` | states, hooks, DAG sweeps, rollback, containment, records, reports, Kconfig | complete for the Stage 06 contract |
| `09-tasks-and-executors.md` | prepare/validate/activate and containment integration boundary | protocol landed; concrete services/executors remain Stage 07 |
| `14-integrated-architecture.md` | direct type-owned state and Zephyr-native firmware entry | preserved |
| Stage 06 of `04-implementation-roadmap.md` | implementation, tests, and firmware checkpoint | complete |

Concrete service `run(StopToken)`, owned service threads, executor adapters,
task registrations, unexpected post-activation exits, and execution records
remain Stage 07. Stage 06 provides the protocol and containment points they
must implement; it does not represent those later capabilities with
placeholders.

## 3. Public Surface Landed

The aggregate remains:

```cpp
#include <solar/solar.hpp>
```

With `CONFIG_SOLAR=y` in Zephyr this now includes the lifecycle surface.

### System declaration and entry

```cpp
struct RobotApplication
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "robot.application",
    };
};

using RobotBlueprint =
    solar::Blueprint<solar::Facilities<RobotApplication>>;
using RobotSystem = solar::System<RobotBlueprint>;

SOLAR_BIND_SYSTEM(RobotSystem);

int main()
{
    auto boot = solar::boot();
    return boot ? 0 : -1;
}
```

`RobotSystem::boot()` and `RobotSystem::stop()` are also direct non-throwing
surfaces for explicit-System tests and tooling.

### Lifecycle hooks

Every effective Device, Facility, Service, and Executor may independently omit
or implement:

```cpp
static solar::Status init();
static solar::Result<void> start();
static solar::Status stop();
static solar::Result<void> deinit();
```

Only `Status` and `Result<void>` are accepted. Missing hooks are successful
omissions represented as `HookOutcome::NotPresent`; a present hook returning
`NotSupported` is a real failed invocation.

### Lifecycle vocabulary

`solar/lifecycle/types.hpp` provides:

- `SystemState` and `ComponentState`;
- `ComponentCategory`, `Operation`, and `HookOutcome`;
- `HookRecord`, `ComponentRecord`, and `ComponentPage`;
- `Failure`, bounded `FailureDetails<N>`, and optional catalog subjects;
- `BootReport`, `BootError`, and `BootErrorReason`;
- `StopReport`, `StopError`, and `StopErrorReason`;
- the execution `Containment` result.

Boot and stop reports/errors also have compact aliases at `solar::` so the
actual signature reads naturally:

```cpp
solar::Result<solar::BootReport, solar::BootError>
solar::Result<solar::StopReport, solar::StopError>
```

### Focused queries

```cpp
solar::lifecycle::state();
solar::lifecycle::components();
solar::lifecycle::record<RemoteService>();
solar::lifecycle::boot_report();
solar::lifecycle::stop_report();
```

`components()` returns one coherent bounded value and is convenient for small
systems. Constrained callers can avoid a graph-sized stack value:

```cpp
std::array<solar::lifecycle::ComponentRecord, 8> records;
auto page = solar::lifecycle::component_page(records, offset);
```

The caller owns the buffer. Each page is copied under one record lock and
reports offset, count, total, and whether more records remain.

Explicit application-tag variants remain available through function template
arguments and `solar::lifecycle::Of<TestApplication>`.

There is no universal lifecycle/System snapshot.

### Graph queries

The compile-time bound graph surface is:

```cpp
constexpr auto components = solar::graph::components();
constexpr auto dependencies = solar::graph::dependencies<Navigation>();
```

Both return immutable component descriptor arrays. The underlying graph also
exposes stable `TopologicalOrder` and `ReverseTopologicalOrder` type lists.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| `System::StateSlot<lifecycle...>` | one system state, operation gate, record mutex, exact record array, optional boot/stop reports | exact effective component count; compile-time ceiling | atomic state, atomic operation flag, Zephyr mutex | one type-owned static instance per distinct System type |
| component records | descriptor view, IDs/category/state, four hook records, first failure, counters, earned-stage and containment facts | exactly one per effective component | lifecycle record mutex | System lifetime |
| boot report | primary failure plus bounded cleanup detail and counts | one retained report; detail capacity from Kconfig | lifecycle record mutex | survives rejected repeated boot |
| stop report | bounded failure detail, containment and completed-operation counts | one retained report; detail capacity from Kconfig | lifecycle record mutex | survives rejected stop |
| graph queries | immutable descriptor arrays generated from existing catalogs | exact graph/dependency size | none | compile-time/static descriptor lifetime |
| component page | caller-owned span plus value metadata | caller-selected | one record lock per copied page | call/buffer lifetime |

Lifecycle allocates no heap, owns no thread or stack, creates no workqueue,
timer, poll object, or executor, and performs no runtime registration. The one
record mutex is never held while invoking hooks, preparing/containing
execution, or waiting.

Static initialization constructs one lifecycle `Storage<System>` only when
that System's lifecycle is instantiated. It initializes the Zephyr mutex and
copies immutable descriptor metadata into the exact component record array.

Measured lifecycle storage symbols were:

| Configuration | System | Storage |
| --- | --- | ---: |
| native 64, report capacity 2 | empty Blueprint | 440 B |
| native 64, report capacity 2 | one component | 624 B |
| native 64, report capacity 2 | five components | 1,392 B |
| native firmware, report capacity 8 | one component | 1,104 B |
| Teensy 4.0, report capacity 8 | one component | 992 B |

These sizes are ABI, target, descriptor, and report-capacity dependent. They
show exact graph-derived growth rather than allocation to the 64-component
ceiling.

## 5. Compile-Time Behavior

`ComponentGraph` now performs a stable Kahn-style topological sort. At every
step it selects the first dependency-ready type in normalized component order.
Reverse teardown is the exact reverse of that result.

Lifecycle storage validates all hooks when instantiated. Stable diagnostics
landed for:

- invalid `init()` return;
- invalid `start()` return;
- invalid `stop()` return;
- invalid `deinit()` return;
- lifecycle query for an unregistered component;
- graph query for an unregistered component;
- effective component count above the Kconfig ceiling;
- intentional lifecycle inclusion when Solar is disabled.

Strict and relaxed binding use the same successful boot/stop/lifecycle
spelling and state. Lifecycle binding itself has no relaxed dispatch pointer or
per-call frontend overhead: it resolves the bound System type directly. Later
mutable subsystem operations retain the strict/relaxed frontend model from
Stage 03.

`CONFIG_SOLAR=n` omits lifecycle from `solar/solar.hpp`. Directly including
`solar/lifecycle.hpp` in that configuration produces
`SOLAR_DIAGNOSTIC_LIFECYCLE_DISABLED` rather than undefined Kconfig symbols.
Core, catalog, System declaration machinery, and direct Kernel primitives
retain their independent include behavior.

## 6. Error And Availability Behavior

| Condition | Result |
| --- | --- |
| another boot/stop orchestration owns the operation gate | `Busy` reason and `Status::Busy` |
| boot while already `Running` | `AlreadyRunning` and `Status::Already` |
| boot after stop or failure | `RebootUnsupported` and `Status::NotSupported` |
| stop outside `Running`, or `Failed` with retained resources | `InvalidState` and `Status::NotReady` |
| component init/start failure | `ComponentFailure`, exact status, stable primary failure |
| execution prepare/validation failure | `ExecutionFailure`, exact operation/status |
| ordinary stop/deinit/request-stop failure | teardown continues; bounded failure detail retained |
| forced but successful containment | dependencies may tear down, but stop/rollback remains abnormal |
| uncontained execution | component and transitive dependencies are cleanup-blocked |
| report detail exceeds capacity | total remains exact and `truncated()` is true |
| record/report lock unavailable in an invalid context | focused `Status` failure |
| report queried before one exists | `Status::NotReady` |
| component page offset exceeds total | `Status::Invalid` |

The first boot-preventing failure is never replaced by cleanup failure. Reports
survive rejected repeated operations. A system with uncontained execution
remains eligible for later best-effort `stop()` attempts.

## 7. Zephyr Integration

Lifecycle uses:

- `kernel::Mutex`/native `k_mutex` for coherent records and reports;
- atomics for the public System state and non-blocking operation gate;
- Stage 05's execution containment vocabulary through the generic protocol;
- generated Kconfig symbols as the only capability/default source.

Kconfig added:

- `CONFIG_SOLAR_LIFECYCLE_MAX_COMPONENTS`, default 64;
- `CONFIG_SOLAR_LIFECYCLE_REPORT_FAILURE_CAPACITY`, default 8;
- `CONFIG_SOLAR_SERVICE_STOP_TIMEOUT_MS`, default 100;
- `CONFIG_SOLAR_SERVICE_ABORT_ON_STOP_TIMEOUT`, default enabled.

The service defaults become concrete policy inputs in Stage 07. They do not
create a hidden service or thread in Stage 06.

Lifecycle mutation and mutable record queries are thread-context APIs. There
is no ISR-safe spelling and no attempt to acquire a Zephyr mutex from ISR
context. Immutable graph descriptors remain compile-time values.

## 8. Files Changed

### Solar added

- `include/solar/lifecycle/engine.hpp`
- `include/solar/lifecycle/hooks.hpp`
- `include/solar/lifecycle/protocol.hpp`
- `include/solar/lifecycle/types.hpp`
- `include/solar/system/query.hpp`
- `tests/zephyr/lifecycle/`
- `tests/zephyr/lifecycle_compile_fail/`
- `tests/zephyr/check_lifecycle_headers.py`

### Solar reshaped

- `include/solar/lifecycle.hpp`
- `include/solar/solar.hpp`
- `include/solar/system.hpp`
- `include/solar/system/api.hpp`
- `include/solar/system/graph.hpp`
- `include/solar/system/system.hpp`
- `zephyr/Kconfig`

### Firmware reshaped

- `firmware/CMakeLists.txt`
- `firmware/prj.conf`
- `firmware/include/app/robot.hpp`
- `firmware/src/main.cpp`
- `firmware/README.md`

### Firmware removed

- `firmware/include/system/system.hpp`
- `firmware/include/system/board.hpp`
- `firmware/include/devices/devices.hpp`
- `firmware/include/generated/remote/manifest.hpp`
- `firmware/remote/app/example.solar.yaml`

The removed firmware files depended entirely on the positional System,
context-based board hooks, old logging, old Remote schema, entry profiles,
Tasks, and Channels. Git history remains the archive.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `west twister -T tests/zephyr ... --warnings-as-errors -j 1` | native 64, all Stage 00-06 suites | 15/15 configurations, 54/54 cases, no warnings | full native regression matrix before final paging addition |
| `west twister -T tests/zephyr/lifecycle -T tests/zephyr/smoke ...` | strict/relaxed lifecycle plus relaxed/strict/disabled aggregate | 5/5 configurations, 23/23 cases, no warnings | final lifecycle API, paging, binding, and exclusion behavior |
| eight `expect_failure.py` wrapped Zephyr builds | native 64 | 8/8 expected tokens observed | exact hook, query, ceiling, and disabled diagnostics |
| `check_lifecycle_headers.py` | real generated lifecycle compile database | 6/6 headers pass with `-Werror` | isolated public lifecycle/query includes |
| host configure/build and `ctest` | GCC 13, strict/relaxed/LTO | 47/47 pass | Stage 00-03 host and System API regressions |
| `west build -b native_sim/native/64 firmware` | native firmware, relaxed default | pass | C++23 module discovery, bound Blueprint, direct boot link |
| native firmware runner | native 64 | reached Zephyr boot and normal idle; manually stopped | executable launches; native Zephyr remains alive after main returns |
| `west build -b teensy40 firmware` | Teensy 4.0 / ARM GCC 14.3 | pass | target compile/link and C++23 support |
| Teensy linker memory report | Teensy 4.0 | flash 43,656 B; RAM 8,384 B | foundation firmware resource checkpoint |
| `nm -S`, compile databases, generated `.config` | native and Teensy | measured/inspected | exact storage symbols, `-std=c++23`, relaxed default, Kconfig capacities |
| `git diff --check` in both repositories | source trees | clean | no whitespace errors |

The runtime matrix covers successful mixed `Status`/`Result<void>` hooks,
passive components, empty systems, stable topological sweeps, reverse stop,
transitional queries from hooks, final activation admission, repeated boot,
real concurrent boot rejection, init/start rollback, primary failure
preservation, stop/deinit failure, prepare/validation failure, clean/forced/
uncontained execution, failed-system stop retry, independent branch cleanup,
report truncation, record detail recovery, and caller-buffered paging.

## 10. Specification Refinements

None. The implementation selected concrete return/storage forms where the
accepted specifications deliberately allowed them.

### Local decision: stable topological materialization

Problem: Stage 03 validated the DAG but did not materialize an order for
runtime sweeps.

Constraints: deterministic normalized order, compile-time graph, no runtime
scan/storage.

Options considered: DFS postorder; compile-time Kahn selection; runtime degree
table.

Decision: compile-time stable Kahn selection using normalized component order
as the ready-node tie break.

Why: the resulting order directly matches the accepted deterministic rule and
is easy to reverse for teardown.

Physical implementation: `include/solar/system/graph.hpp`.

Tests/evidence: cross-category dependency order static assertions and runtime
hook traces.

Reversal path: replace only `detail::TopologicalSort`; public graph types and
lifecycle sweeps consume its result generically.

### Local decision: lifecycle operation serialization

Problem: boot/stop must reject concurrent or reentrant mutation without
holding a record mutex across hooks.

Constraints: non-blocking Busy result, no deadlock, coherent short record
locks.

Options considered: one recursive mutex; one try-lock mutex; atomic operation
flag plus record mutex.

Decision: one atomic operation gate and a separate non-recursive record mutex.

Why: hook reentrancy fails immediately, while queries remain available during
transitions.

Physical implementation: `include/solar/lifecycle/engine.hpp`.

Tests/evidence: a real second Zephyr thread observes `Initializing`, queries
the component, receives `Busy`, releases the hook, and joins cleanly.

Reversal path: replace `OperationGuard` without changing records or public
results.

### Local decision: execution protocol boundary

Problem: Stage 06 must prove activation/containment semantics before concrete
service and executor owners exist.

Constraints: no placeholder thread, hidden executor, or second boot engine.

Options considered: category-specific branches in Engine; virtual runtime
owner; static `ExecutionProtocol<System, Component>` customization.

Decision: a default no-op static protocol specialized only by components that
own execution.

Why: Stage 07 can attach exact owners to the existing lifecycle transaction,
and passive components pay no runtime cost.

Physical implementation: `include/solar/lifecycle/protocol.hpp` and protocol
calls in `engine.hpp`.

Tests/evidence: prepare and validation rollback, no activation before final
commit, clean/forced/uncontained containment.

Reversal path: Stage 07 may wrap or constrain the customization internally
without changing Engine's semantic phases.

### Local decision: caller-buffered record paging

Problem: copying every record is ergonomic for small systems but can create a
large stack value near the 64-component ceiling.

Constraints: preserve `components()`, no heap, no mutable reference after
unlock, useful Remote/Inspection traversal.

Options considered: expose locked references; fixed Kconfig page storage;
caller-owned span; remove all-components convenience.

Decision: retain `components()` and add `component_page(span, offset)`.

Why: callers choose their own bounded storage and each page remains coherent
without hidden global buffers.

Physical implementation: `types.hpp`, `engine.hpp`, and `system/api.hpp`.

Tests/evidence: two-record pages traverse a five-component system, final-page
metadata and invalid offset are checked in strict and relaxed builds.

Reversal path: a later collection/view facade can delegate to the same paging
operation while preserving record ownership.

### Local decision: disabled aggregate boundary

Problem: Lifecycle ceilings do not exist when `CONFIG_SOLAR=n`, but the Zephyr
aggregate initially included lifecycle solely because `__ZEPHYR__` was set.

Constraints: Kconfig is authoritative; independent core/System/Kernel headers
remain usable; intentional disabled use must be focused.

Options considered: fallback constants; keep lifecycle declarations with
dummy state; capability-gate the aggregate.

Decision: gate lifecycle on `CONFIG_SOLAR` and reject direct disabled inclusion
with a stable diagnostic.

Why: no fallback configuration or false runtime capability is introduced.

Physical implementation: `include/solar/solar.hpp` and
`include/solar/lifecycle.hpp`.

Tests/evidence: disabled smoke passes and direct disabled inclusion observes
`SOLAR_DIAGNOSTIC_LIFECYCLE_DISABLED`.

Reversal path: if lifecycle becomes independently selectable, replace the
guard with that dedicated Kconfig capability.

## 11. Firmware And Host Impact

Firmware is now fully migrated through the foundation checkpoint:

- C++23 is selected in CMake and Kconfig;
- `app::RobotApplication` is a passive Facility component;
- `RobotBlueprint` and `RobotSystem` are the sole composition root;
- exactly one `SOLAR_BIND_SYSTEM` exists;
- `main()` calls `solar::boot()` directly and handles its Result;
- positional Board, Peripherals, Runtime, Tasks, Channels, old facilities,
  old services, context hooks, entry profile, logging, and Remote are absent;
- obsolete generated Remote artifacts were removed rather than preserved as a
  compatibility island.

The firmware remains on its development branch as requested. Solar alone is on
`static_reform`.

Hardware aliases/devices will be rebuilt at Stages 16-17. Logging and Remote
return only through their accepted subsystem implementations at Stages 12-14.

Host Remote tools are unchanged because protocol migration begins at Stage 13.

## 12. Known Limits And Deferred Work

- `ExecutionProtocol` has no concrete service/executor owner until Stage 07.
- The service stop timeout and abort Kconfig defaults are consumed in Stage 07.
- Automatic transition to Failed after an unexpected running service exit
  requires Stage 07's execution owner and record callback.
- Component-local rich typed errors remain in future focused subsystem records;
  lifecycle owns only normalized `last_status` and first failure.
- Built-ins participate because effective Blueprint normalization places them
  in the same component list; concrete built-in facilities arrive with their
  subsystem stages.
- Controlled in-process reboot/reset remains intentionally unsupported.
- Physical target execution is not required at this compile-only foundation
  firmware gate.

## 13. Documentation Handoff

The later public documentation pass should explain:

- declaring components, dependencies, a Blueprint, System, and binding;
- exact optional hook forms and absent-hook behavior;
- global versus explicit application-tag lifecycle APIs;
- boot/rollback/stop ordering and activation semantics;
- handling typed boot/stop errors and separately retained reports;
- using focused records, full components, and caller-buffered paging;
- graph descriptor queries and build-local versus stable identity;
- thread-context query requirements;
- Kconfig ceilings and disabled lifecycle behavior;
- current no-reboot policy;
- why services and tasks are added through Execution rather than lifecycle
  hooks alone.

`tests/zephyr/lifecycle/src/fixture.hpp`, its Ztest suite, and the migrated
firmware `app/robot.hpp` are the executable source examples for that pass.

## 14. Closure Statement

Stage 06 is complete. Solar now has one bounded, deterministic, directly
type-owned lifecycle transaction; exact hook contracts; stable failure and
cleanup evidence; coherent focused queries; a real activation/containment
boundary; and a C++23 firmware using the accepted static System architecture on
both native simulation and Teensy 4.0.

Stage 07, Services, Executors, and Tasks, is now unblocked.
