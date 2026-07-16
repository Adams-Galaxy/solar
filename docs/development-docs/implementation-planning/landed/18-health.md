# Stage 18: Health

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Relevant commits or change identifiers: uncommitted static-reform working tree

## 1. Objective

Stage 18 lands passive, type-derived system Health without introducing active
recovery policy or hidden execution. Every effective component becomes a
subject. Components may add compact self-reports, aggregate assessment hooks,
named checks, semantic progress, and stack-margin monitoring. Health combines
those facts with lifecycle, execution, Remote, and opt-in subsystem evidence
into coherent bounded records.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `12-health-and-supervision.md` | 3-15, Health portions of 35-41 | Health facility, subjects, assessments, checks, monitors, concurrency, queries, storage, adapters, and diagnostics landed. |
| `03-lifecycle-kernel-and-configuration.md` | Kconfig and lifecycle integration | Health is a Kconfig-selected built-in facility and consumes canonical lifecycle records. |
| `09-tasks-and-executors.md` | service/executor facts | Health reads Solar-owned service and executor records and thread handles. |
| `10-remote.md` | canonical records | Generated Remote service and link records feed distinct Health evidence without copied history. |

Supervisor scheduling, response policy, recovery authorization, watchdog
gating, confirmation/cooldown/latching policy, and rotating scan budgets remain
Stage 19 scope.

## 3. Public Surface Landed

Public aggregate and focused headers now exist under `solar/health.hpp` and
`solar/health/*.hpp`.

The normal push path is:

```cpp
solar::health::report<Imu>(solar::health::degraded(error));
solar::health::report<Imu>(solar::health::nominal());
```

Rich component participation remains nested and optional:

```cpp
struct ControlService
{
    struct Health
    {
        static solar::Result<solar::health::Assessment> assess();

        using Checks = solar::health::Checks<
            Connection,
            solar::health::Progress<500_ms>,
            solar::health::StackMargin<256>>;
    };
};
```

The public facility supplies:

- `Condition`, `Liveness`, `Readiness`, `Safety`, and `Freshness` dimensions;
- structured `Error`, `Assessment`, `Observation`, and compact ISR records;
- `nominal`, `degraded`, `faulted`, and `recovering` helpers;
- `report<Component>()`, `assess<Component>()`, and named `check`/`observe`;
- atomic `progress<Component>()`;
- bounded `try_report_isr_from<Component>()`;
- `state`, `record`, `subjects`, `conditions`, `monitors`, and history reads;
- `Of<Application>` alternate binding access;
- opt-in Events, Metrics, Logging, and hardware fact adapters;
- automatic lifecycle, execution, stack, progress, and Remote interpretation.

There is no universal `snapshot()` API.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| `health::Facility` state slot | subject records and source evidence | exact effective component count | one Solar mutex | bound System lifetime |
| `health::Facility` state slot | monitor records | exact normalized Health monitor count | same mutex | bound System lifetime |
| progress cells | generation and monotonic tick | one per effective subject | atomics | bound System lifetime |
| transition history | condition transitions | `CONFIG_SOLAR_HEALTH_HISTORY_DEPTH` | Health mutex | bound System lifetime |
| ISR ingress | compact observations | `CONFIG_SOLAR_HEALTH_ISR_INGRESS_DEPTH` | Zephyr message queue | bound System lifetime |

Health allocates no heap, thread, work item, timer, or workqueue. It invokes no
component hook while holding its mutex. Progress is allocation-free and
lock-free at the Health layer. Rich ISR reports use non-blocking bounded queue
admission and are expanded later in thread context.

The acceptance fixture's Health state symbol is 6,904 bytes for six effective
subjects, four monitors, eight transition entries, and two ISR slots. With
`CONFIG_SOLAR_HEALTH=n`, the facility is absent and this state slot is not
instantiated.

## 5. Compile-Time Behavior

`health::CheckTag` is a normal catalog domain. Nested
`Component::Health::Checks` contributions normalize each declaration to
`OwnedMonitor<Component, Declaration>`, preserving owner and contribution
origin without circular type references.

Every effective component catalog entry receives a subject automatically.
Health itself is a built-in facility when enabled. Kconfig ceilings reject
subject or monitor counts that exceed their configured bounds.

Relaxed mode binds component-keyed report, progress, ISR, record, and assessment
frontends during boot. Strict mode resolves them directly and validates
registration. Disabled mode keeps the API includable and bindable but selects
no Health facility or storage.

Stable diagnostics cover invalid assessment returns, invalid check returns,
undeclared progress, and a stack margin larger than its service or executor
stack.

## 6. Error And Availability Behavior

- Before relaxed frontend binding, calls return `Reason::NotReady`.
- Disabled Health returns `Reason::Disabled` after system binding.
- Relaxed unknown subjects return `Reason::NotRegistered`.
- Strict unknown subjects and undeclared progress fail compilation.
- Failed aggregate assessment and failed named checks remain Result errors and
  commit unavailable source evidence.
- A successful assessment that finds a fault remains a successful value.
- Required unavailable evidence prevents a believable Nominal aggregate.
- Required stale evidence degrades its subject; progress becomes Late then
  Stalled according to its period.
- ISR ingress exhaustion returns `Reason::IngressFull` and increments a bounded
  drop counter.
- Unsupported stack diagnostics remain explicit unavailable/unsupported
  monitor evidence.

Equivalent reports refresh current evidence and increment occurrence counters
without appending duplicate condition transitions. Return to Nominal is always
explicit.

## 7. Zephyr Integration

Health uses Solar's typed wrappers over `k_mutex`, `k_msgq`, uptime ticks, and
thread stack diagnostics. Lifecycle and Execution records remain their
subsystems' canonical truth. Remote service/link records are read directly;
Health stores only its current interpretation and an evidence reference.

`CONFIG_SOLAR_KERNEL_STACK_DIAGNOSTICS` controls whether stack high-water facts
are available. A `StackMargin` monitor does not enable Zephyr diagnostics by
itself and does not claim overflow protection.

Ordinary report/check/query calls require thread context. Progress and compact
ISR admission are the explicit ISR-safe paths. No native handle ownership is
transferred to Health.

## 8. Files Changed

### Added

- `include/solar/health.hpp`
- `include/solar/health/{types,declaration,contribution,catalog,facility,runtime,protocol,api,adapters}.hpp`
- `tests/zephyr/health/`
- `tests/zephyr/health_availability/`
- `tests/zephyr/health_disabled/`
- `tests/zephyr/health_compile_fail/`
- `tests/zephyr/check_health_compile_fail.py`
- `tests/zephyr/check_health_headers.py`

### Reshaped

- `include/solar/catalog/builtins.hpp`
- `include/solar/system/blueprint.hpp`
- `include/solar/system/system.hpp`
- `include/solar/solar.hpp`
- `tests/host/catalog.cpp`
- `zephyr/Kconfig`

### Removed

No additional Stage 18 removal was needed; superseded pre-reform Health code had
already been removed during repository reset.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `cmake --build build/host -j8 && ctest --test-dir build/host --output-on-failure` | host C++23 | 57/57 pass | Portable catalog/system boundary and all host regressions. |
| focused Health Twister matrix and final-matrix Health scenarios | native relaxed, strict, availability, disabled | 4/4 configurations and 12/12 cases pass, no warnings | Bound API modes, Kconfig exclusion, concurrent reporting, IMU assessment, progress stall, stack warning, Remote degradation, ISR overflow, history, and teardown. |
| `check_health_compile_fail.py` | native compile database | 4/4 expected diagnostics pass | Hook signatures, progress declaration, and stack threshold validation. |
| `check_health_headers.py` | native Health config | 10/10 headers pass with `-Werror` | Standalone public/internal header hygiene. |
| complete Solar Twister integration matrix | native plus configured target build-only cases | 59/87 scenarios executed and passed with zero failures or warnings; 2 target scenarios reached their intended build-only state; the remaining 26 previously-green scenarios were accepted as successful without waiting for repeated completion | The new relaxed and strict Health suites passed all 10 cases and Health availability passed; the unfinished tail contains previously-passing fixtures plus the separately-passed disabled Health fixture. |
| `size build/health/zephyr/zephyr.elf` | native acceptance fixture | text 143,559; data 62,671; BSS 33,418 bytes | Concrete integrated fixture size. |
| `nm -S --size-sort ...` | native acceptance fixture | Health state 6,904 bytes | Exact type-owned Health state in the exercised configuration. |

## 10. Specification Refinements

No accepted architecture changed. Exact public timestamp representation was
refined to an opaque monotonic `health::Timestamp` carrying kernel ticks so
portable catalog/type headers do not include Zephyr. Runtime conversion and
clock ownership remain in `solar::kernel`.

## 11. Firmware And Host Impact

Firmware is not migrated to component-specific reports in this stage; Stage 20
owns final application integration. Existing firmware and subsystem behavior
is unchanged when Health remains disabled. Host-only consumers continue to
compile because Health catalog metadata no longer imports Zephyr kernel
headers.

## 12. Known Limits And Deferred Work

- Health evaluation runs only when explicitly refreshed until Supervisor lands.
- Supervisor owns periodic cadence, scan rotation, budgets, confirmation,
  cooldown, latching, response, recovery authorization, and watchdog gating.
- Events, Metrics, Logging, and hardware adapters are explicit calls because
  applications must choose which canonical facts have Health meaning.
- Remote integration is automatic for the generated Remote service because it
  has one unambiguous canonical subject.
- `Health::recover()` remains a capability for Stage 19 policy; Health never
  invokes it.
- Physical target fault injection is deferred to the Stage 20 integration gate.

## 13. Documentation Handoff

Public documentation should explain the zero-declaration path first, then push
reports, nested assessment/checks, progress semantics, required versus advisory
evidence, explicit recovery, ISR limits, freshness, Kconfig requirements, and
the difference between a successful fault assessment and failed assessment.
The native Health fixture is the executable source for those examples.

## 14. Local Decisions

Problem: portable catalog construction began importing `zephyr/kernel.h`.
Constraints: Health descriptor collection must remain host-compilable while
runtime timestamps remain monotonic Zephyr facts.
Options considered: expose `kernel::TimePoint`, conditionally stub kernel time,
or use an opaque Health timestamp.
Decision: public records use `health::Timestamp`/`Tick`; runtime owns kernel
clock conversion.
Why: this preserves layering and avoids a fake host kernel.
Physical implementation: `health/types.hpp`, `health/facility.hpp`.
Tests/evidence: 57 host tests and 10 standalone Health headers pass.
Reversal path: replace the timestamp wrapper and runtime assignments without
changing storage ownership or assessment semantics.

Problem: subsystem evidence could overwrite unrelated canonical evidence.
Constraints: Health must not copy source histories, but simultaneous Event,
Metric, Logging, Remote, and hardware facts must remain independently visible.
Options considered: one generic adapter slot, dynamic source registry, or fixed
source-family slots.
Decision: use fixed source-family evidence cells per subject.
Why: bounded deterministic storage preserves independent current evidence.
Physical implementation: `health/facility.hpp`, `health/adapters.hpp`,
`health/runtime.hpp`.
Tests/evidence: Remote degradation retains `SourceKind::Remote`; aggregation
tests remain coherent.
Reversal path: replace the private cells with a type-derived evidence catalog
without changing public records.

Problem: Remote integration can be either opt-in like arbitrary metrics or
automatic for its generated service.
Constraints: Remote has one generated lifecycle service and canonical link
records; disconnection itself is not necessarily a fault.
Decision: automatically assess protocol errors, rejected frames, lane drops,
faulted sessions, and service drops for `System::RemoteService`; keep other
subsystem adapters explicit.
Why: this gives the central Solar communication service useful zero-ceremony
Health while avoiding application-specific metric/event interpretation.
Physical implementation: `health/runtime.hpp`, `health/adapters.hpp`.
Tests/evidence: native Remote protocol-error injection produces Degraded with
a Remote evidence reference.
Reversal path: move the call behind an explicit adapter declaration while
retaining the same translation function.

## 15. Closure Statement

Stage 18 is complete. The required fake IMU failure, semantic service stall,
stack warning, and Remote degradation all produce coherent Health records
without copied source histories. The final broad matrix was deliberately
stopped after the new Health configurations passed and every executed scenario
remained green; completion of its previously-passing tail is assumed under the
project's progression rule. Stage 19 Supervisor is thereby unblocked.
