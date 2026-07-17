# Stage 19: Supervisor

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`.

Relevant commits or change identifiers: working-tree implementation pass; no
stage commit was requested.

## 1. Objective

Stage 19 lands one optional, dedicated Supervisor service over the passive
Health facility. It evaluates centralized typed response policy, coordinates
explicit component recovery and safe-state hooks, retains bounded response
evidence, and gates an optional watchdog provider from its own execution
domain.

The default remains observation-only. Solar does not infer permission to
recover, stop, reboot, panic, enter safe state, or feed a watchdog.

## 2. Specification Coverage

The implementation covers the accepted Supervisor portions of
`12-health-and-supervision.md`:

- one Kconfig-generated service with a dedicated Solar-owned thread;
- Health refresh and due response evaluation at a configurable base cadence;
- early wake from thread and ISR Health reports;
- fault, degradation, stall, and recovery-failure triggers;
- observe, warn, latch, recover, safe-state, component-stop, system-stop,
  reboot-request, watchdog-withhold, and panic primitives;
- bounded retries, cooldown, response budget, response history, and latching;
- component-owned `Health::recover()` and static safe-state `enter()` hooks;
- focused state, subject-response, history, and watchdog queries;
- Supervisor progress, stack-margin, lifecycle, and execution visibility;
- a provider boundary for Zephyr task-watchdog or typed hardware-watchdog
  adapters without Supervisor owning either implementation.

## 3. Public Shape

Application policy remains centralized and compact:

```cpp
using RobotSupervision = solar::supervisor::Policy<
    solar::supervisor::OnFault<
        Imu,
        solar::supervisor::Warn,
        solar::supervisor::TryRecover<Imu>>,
    solar::supervisor::OnRecoveryFailure<
        Imu,
        solar::supervisor::EnterSafeState<NavigationSafe>>,
    solar::supervisor::OnStall<
        ControlService,
        solar::supervisor::StopFeedingWatchdog>>;

using Robot = solar::System<solar::Blueprint<
    solar::Devices<Imu>,
    solar::Services<ControlService>,
    solar::supervisor::Configuration<
        RobotSupervision,
        solar::supervisor::Watchdog<SystemWatchdog>>>>;
```

Focused query results are mutex-coherent and error-aware:

```cpp
auto service = solar::supervisor::state();
auto imu = solar::supervisor::record<Imu>();
auto watchdog = solar::supervisor::watchdog();
auto page = solar::supervisor::responses(cursor, destination);
```

When `CONFIG_SOLAR_SUPERVISOR=n`, these query functions remain storage-free and
return `Reason::Disabled`. Deliberate Supervisor policy in a Blueprint is a
compile-time unavailable-policy error.

## 4. Runtime Ownership

| Owner | Resource | Bound |
| --- | --- | --- |
| generated Supervisor service | one service thread and stack | `CONFIG_SOLAR_SUPERVISOR_SERVICE_STACK_SIZE` |
| Supervisor service | cycle mutex, record mutex, binary wake semaphore | one each |
| response policy | retry/cooldown state | exactly one cell per typed rule |
| response history | complete response records | `CONFIG_SOLAR_SUPERVISOR_RESPONSE_HISTORY_DEPTH` |
| Health | canonical component assessment, progress, and monitor records | existing Stage 18 bounds |
| watchdog provider | enforcement backend and native device/channel state | provider-owned |

The acceptance fixture's Supervisor policy state is 680 bytes. Its generic
Solar service execution state, including the configured 4096-byte stack, is
4424 bytes. No heap allocation, dynamic registry, workqueue, timer, or hidden
default watchdog is introduced.

## 5. Cycle And Concurrency

Each cycle serializes through a dedicated mutex, refreshes Health outside the
Supervisor record mutex, evaluates typed rules, executes at most the configured
response budget, evaluates watchdog permission, records completion, and marks
Supervisor progress.

Health owns only an optional static wake callback. Supervisor binds its ISR-safe
binary-semaphore signal during `System::boot()`. This lets push reports wake the
service without Health depending on Supervisor and without invoking policy in
the reporting thread.

Watchdog feed is permitted only after a completed Health refresh when policy
has not withheld feed and canonical system Health is not Faulted, Unsafe, or
Stale. Because only the Supervisor execution domain calls `feed()`, a stalled
Supervisor naturally stops feeding.

## 6. Response Semantics

- `Observe`, `Warn`, and `Latch` always produce canonical response records.
- `TryRecover<T>` requires and invokes `T::Health::recover()` only when named by
  policy.
- recovery-failure escalation is scoped to the same component subject.
- `EnterSafeState<T>` requires bounded static `T::enter()` capability.
- `RequestStop<T>` uses the existing Lifecycle execution protocol.
- system stop and reboot are retained requests; Supervisor never joins or
  recreates its own System from inside its thread.
- `StopFeedingWatchdog` withholds feed for the current evaluated cycle.
- `Panic` records the request before crossing the Kernel fatal boundary.
- retry attempts and cooldown are reset when a trigger clears.
- the cycle mutex prevents recursive or concurrent policy execution.

## 7. System Binding Decision

### Problem

The generated Supervisor component is formed while `System` is still being
normalized. Direct use of `bound_system_t` from that component creates an
include and type-formation cycle.

### Decision

The architecture-only generated service owns a typed cycle function pointer.
`System::boot()` binds `cycle<Service, CompletedSystem>` before lifecycle
activation. The pointer targets a fully static template instantiation; it does
not point to a runtime object and performs no registry lookup.

This preserves:

- ordinary component headers including Solar only;
- no application root include inside Solar internals;
- one completed System type as the source of graph and catalog truth;
- no context object, service instance, allocation, or dynamic discovery;
- direct testability of the same cycle engine with explicit time values.

Physical implementation: `system/system.hpp`, `supervisor/service.hpp`.

## 8. Kconfig

`CONFIG_SOLAR_SUPERVISOR` depends on Health and Execution and defaults off.
Kconfig owns the base period, grace, service stack, service priority, maximum
responses per cycle, response-history depth, recovery attempt ceiling, and
recovery cooldown. Typed Blueprint policy owns semantic response and watchdog
provider selection.

## 9. Tests And Evidence

| Gate | Result | Evidence |
| --- | --- | --- |
| focused Supervisor Twister matrix | 5/5 configurations and 5/5 cases pass, no warnings | relaxed/strict rich policy, relaxed/strict default policy, disabled frontend |
| expected diagnostics | 5/5 pass | empty rule, missing recovery, missing safe state, missing watchdog feed, absent rule subject |
| header hygiene | 5/5 headers pass with `-Werror` | standalone API, policy, service, types, aggregate |
| host cross-stage rebuild | complete | all host C++23 targets rebuild after Blueprint and Health wake changes |
| active acceptance fixture | pass | warning record, recovery failure, safe-state escalation, recovery success, semantic service stall, watchdog withholding, early wake, starvation, clean stop |
| integrated fixture size | text 115,413; data 8,982; BSS 29,516 bytes | native simulator policy fixture |

The complete pre-Supervisor matrix was previously green. Per the project
progression rule, already-passing unrelated configurations were not rebuilt a
second time after the focused Stage 19 matrix passed.

## 10. Deferred Extensions

- board-specific production watchdog providers and feed-window policy;
- generic in-process component restart after Lifecycle defines truthful reset;
- logging/event adapter policy beyond canonical `Warn` response records;
- more elaborate multi-domain safety gates;
- reboot execution after platform reset policy is accepted.

These are extension points, not hidden partial implementations.

## 11. Closure

Stage 19 is complete. Supervisor is a visible, bounded service; Health remains
the canonical passive truth owner; application policy remains explicit; and
watchdog enforcement remains provider-owned. Stage 20 integration closure is
unblocked.
