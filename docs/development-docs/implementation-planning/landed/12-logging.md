# Stage 12: Logging

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Relevant commits or change identifiers: uncommitted reform working tree

## 1. Objective

Stage 12 lands Solar's unified bounded Logging facility and Zephyr custom
frontend: typed native capture, one canonical sequence, deferred sink routing,
retained history, early platform capture, filtering, accounting, flush/stop,
and panic behavior. Logging has no Remote or transport dependency and owns no
private execution thread.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `08-logging.md` | vocabulary, sources/domains, levels, typed formatting, canonical records, ingress, filtering, processing, sinks, history, lifecycle, Zephyr frontend, panic, Events, configuration | Core architecture is landed. Remote delivery remains an explicit Stage 14 adapter. |
| `01-system-blueprint-and-binding.md` | Kconfig inclusion, typed configuration, strict/relaxed frontend binding | Enabled Logging is an early built-in; explicit configuration while disabled is rejected. |
| `02-identity-contributions-and-catalogs.md` | component sources, `LogSources`, `LogDomains`, descriptors and provenance | Every component is automatically a source; non-components use explicit contributions. |
| `06-events.md` | infrastructure-owned event-to-log adapter | Event materialization remains canonical and the adapter emits with `Origin::Event`. |
| `09-execution.md` | shared deferred processing | The processor is an on-demand coalescing registration on the selected executor. |

## 3. Public Surface Landed

The public aggregate is:

```cpp
#include <solar/log.hpp>
```

Ordinary use is source typed and logger free:

```cpp
solar::log::notice<RemoteService>(
    "Session {} connected", session);

solar::log::warn<RemoteService, solar::log::domain::Transport>(
    solar::log::correlated(request_id),
    "TX queue contains {} frames", pending);

if constexpr (solar::log::enabled<RemoteService,
                                  solar::log::Level::Debug>) {
    solar::log::debug<RemoteService>("Candidate {}", expensive_value());
}
```

Landed levels are Trace, Debug, Info, Notice, Warning, Error, and Fatal.
Normal, `try_`, and explicit ISR capture forms share the same format contract.
`text()` copies dynamic text and `hexdump()` copies bounded labels and bytes.
Capture returns `Result<Receipt, log::Error>` but is intentionally ignorable for
best-effort diagnostics.

Configuration supports global/source/domain compile thresholds, executor
selection, stop policy, static routes, route minimum levels, encoded routes,
and panic-safe routes. Runtime source, domain, and sink minimum levels are
mutable when Kconfig enables filtering.

Queries expose focused facility, source, and sink records plus caller-owned
paged retained history and `latest()`. There is no universal snapshot.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| generated Logging facility | canonical variable-byte ingress and aggregate record | Kconfig exact byte ring; firmware default 4,096 bytes | one Zephyr spinlock | System lifetime |
| generated Logging facility | retained complete-record history | optional Kconfig exact byte ring; firmware default 4,096 bytes | facility spinlock | System lifetime |
| generated Logging facility | source filter/accounting table | exact effective source catalog | atomic thresholds; facility spinlock for records | System lifetime |
| generated Logging facility | domain filter table | exact effective domain catalog | atomic thresholds | System lifetime |
| route type | runtime threshold and sink record | one per static route | atomic threshold; facility spinlock for record updates | System lifetime |
| Zephyr frontend translation unit | pre-activation complete platform records | `CONFIG_SOLAR_LOG_EARLY_RECORDS`; default 4 | native `k_spinlock` | boot lifetime |
| Execution registration | one coalescing processor work item | one pending item | Execution-owned work semantics | facility lifetime |
| renderer | bounded stack output | `CONFIG_SOLAR_LOG_RENDER_BUFFER_BYTES`; default 384 bytes | processor-local | one processed record |

Logging owns no heap, thread, stack, private workqueue, timer, poll object, or
transport. Its default processor uses the Zephyr system workqueue through
Solar Execution. Sinks are static types. A sink that needs independent slow or
transport work owns its own bounded queue/executor beyond the Logging core.

The firmware integration image uses 4.26% of Teensy flash and 12.65% of its
primary 256 KiB RAM region. Its linked image reports 89,300 bytes flash usage
and 33,152 bytes RAM usage in Zephyr's region summary.

## 5. Compile-Time Behavior

System normalization automatically adds every effective component to the log
source catalog and all built-in domains to the domain catalog. `LogSources`
and `LogDomains` contribute additional declarations with owner/origin facts.
Source and domain IDs remain deterministic local catalog IDs.

Kconfig removes globally excluded levels. `CompileLevel`, `SourceLevel`, and
`DomainLevel` may only make the effective threshold stricter. In strict mode,
the bound System is visible and below-threshold bodies are not instantiated. In
relaxed mode, the inline call site can know only the Kconfig threshold; bound
C++ thresholds are therefore applied by initialized ingress filters. This is
the accepted relaxed-mode overhead tradeoff and does not restore Kconfig-
removed levels.

The consteval brace-format parser validates field count, escaped braces,
supported format forms, and argument categories. Captured arguments are copied
into a bounded payload; dynamic references are not retained.

Strict mode rejects unregistered source/domain use at the call site. Relaxed
mode uses the same spelling and result types and reports unavailable bindings
through focused runtime errors. Enabled and disabled public headers compile in
isolation.

## 6. Error And Availability Behavior

Focused reasons distinguish disabled capability, not-ready frontend, absent
registration, closed capture, full ingress, contention, timeout, oversized
record, invalid context, encoding failure, sink failure, protocol corruption,
and internal invariants.

Capture dispositions distinguish captured, compile-time filtered, runtime
filtered, and dropped records. Complete records are accepted atomically; the
byte ring never retains a partial record. Ordinary and elevated records cannot
consume the emergency reserve, and ordinary records cannot consume elevated
capacity. Truncation and preceding loss are explicit record flags.

Sink failures are isolated and accounted while all eligible routes are still
attempted. History cursors report stale/gap conditions after eviction. Disabled
explicit capture returns `NotSupported/Disabled` without requiring a System
binding; disabled queries return empty or focused unavailable results.

## 7. Zephyr Integration

`CONFIG_SOLAR_LOG` selects Zephyr logging, `LOG_FRONTEND`, and
`LOG_FRONTEND_ONLY`. `src/log/frontend.cpp` implements Zephyr's C frontend
callbacks and copies cbprintf packages or hexdump bytes before the callback
returns. Platform source/domain metadata and origin remain in the canonical
record.

Before System activation, the frontend retains a bounded array of complete
platform records and marks loss on overflow. Activation installs the typed
capture callback and drains early records. Panic notification is one-way and
switches Logging to panic mode. `fatal()` captures, performs a bounded drain to
panic-safe routes, and enters Zephyr `k_panic()`.

`CONFIG_LOG_PRINTK` controls Zephyr redirected `printk` integration. Tests turn
it off so Ztest output remains visible; production may enable it. ISR callbacks
use explicit non-waiting capture and never format text at ingress.

## 8. Files Changed

### Added

- `include/solar/log/{api,contribution,declaration,facility,platform,policy,protocol,runtime,types}.hpp`
- `include/solar/events/log.hpp`
- `src/log/frontend.cpp`
- `tests/zephyr/logging/`
- `tests/zephyr/logging_availability/`
- `tests/zephyr/logging_disabled/`
- `tests/zephyr/logging_compile_fail/`
- `tests/zephyr/logging_disabled_compile_fail/`
- `tests/zephyr/check_logging_headers.py`
- `tests/zephyr/check_logging_compile_fail.py`

### Reshaped

- `include/solar/log.hpp` and `include/solar/log/format.hpp`
- `include/solar/catalog/descriptor.hpp`
- `include/solar/catalog/builtins.hpp`
- `include/solar/events/contribution.hpp`
- `include/solar/system/{sections,blueprint,system}.hpp`
- `include/solar/solar.hpp`
- `include/solar/metrics/{facility,storage}.hpp`
- `CMakeLists.txt` and `zephyr/Kconfig`
- `firmware/include/app/robot.hpp`, `firmware/src/main.cpp`, and `firmware/prj.conf`

### Removed

- the superseded synchronous logger/filter/record/sink/source/writer headers;
  Git history remains the archive.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| host configure/build plus `ctest` | GCC 13, C++23 | 47/47 pass | all host and compile-fail regressions |
| focused Logging Twister matrix | native_sim 64, main/availability relaxed+strict/disabled | 4/4 configurations, 13/13 cases | capture, formatting, history, filters, concurrency, ISR, Zephyr/Event adapters, panic routes and policy precedence |
| `check_logging_compile_fail.py` | enabled strict and disabled databases | 5/5 pass | source, domain, format, sink and disabled-configuration diagnostics |
| `check_logging_headers.py` | enabled and disabled databases | 11/11 public headers in each mode | public header self-sufficiency |
| focused Metrics rerun | native_sim relaxed and strict | 2/2 configurations, 14/14 cases | ARM accounting correction preserves Metrics behavior |
| complete `west twister -T tests/zephyr ...` | native_sim 64 | 51/51 configurations pass | all Stage 00-12 native regressions |
| `west build -b native_sim/native/64 firmware` | native firmware | pass | one real bound system integrates Parameters, Events, Metrics, Logging and Execution |
| native firmware runner | native_sim 64 | boot banner reached; runner stopped after idle | target boots successfully; Zephyr application idle is expected |
| `west build -b teensy40 firmware` | Teensy 4.0, ARM GCC 14.3 | pass | target compile/link, frontend, C++23, and 32-bit atomic portability |
| allocator/64-bit atomic symbol scan | native and Teensy application archives | no matches | no application heap dependency and no unresolved ARM 64-bit atomic runtime |
| `git diff --check` | Solar and firmware/docs trees | pass | no whitespace errors |

## 10. Implementation Decisions

### 10.1 Variable-Byte Ring With Complete Push

Problem: typed and Zephyr records vary substantially in size, while partial
records are unusable after overflow or panic.

Constraints: bounded static storage, MPSC capture, ISR use, and priority
reserves.

Options considered: fixed maximum slots; separate per-level queues; byte ring
with reservation/commit; byte ring serialized by a short spinlock.

Decision: use one variable-byte ring with a two-byte length and complete record
copy under a short spinlock. Capacity thresholds enforce ordinary, elevated,
and emergency reserves before mutation.

Physical implementation: `log/facility.hpp::ByteRing` and capture in
`log/runtime.hpp`.

Tests/evidence: concurrent producers, ISR capture, held processor overflow,
priority reserves, and complete drain tests.

Reversal path: a target-proven reservation/commit MPSC ring may replace the
private ring while preserving the canonical record and receipt contract.

### 10.2 Zephyr Frontend Translation Unit

Problem: Zephyr's frontend callbacks are C symbols and cbprintf packages may
reference transient call-site data.

Decision: own the callbacks in one compiled C++ translation unit, copy packages
with Zephyr's self-contained conversion, and retain complete early records
before typed activation.

Physical implementation: `src/log/frontend.cpp`, conditional module CMake, and
`log/platform.hpp`.

Tests/evidence: Zephyr module logs captured after an early pre-boot record,
hexdump/text coverage, source metadata, and panic notification.

Reversal path: a future Zephyr backend import mode can implement the same
platform callback without changing canonical Logging storage.

### 10.3 Relaxed Typed Compile Thresholds

Problem: relaxed inline component code deliberately cannot see the root System,
so it cannot evaluate Blueprint source/domain policy in a consteval call-site
predicate.

Constraints: no root includes in component headers, same normal API spelling,
and negligible accepted relaxed overhead.

Options considered: require root visibility; macros; ignore typed thresholds;
strict compile elimination plus relaxed ingress filtering.

Decision: Kconfig filtering is always compile time; strict mode additionally
eliminates bound typed thresholds; relaxed mode initializes equivalent source
and domain ingress thresholds.

Physical implementation: `log/policy.hpp`, `system/system.hpp`, `log/api.hpp`,
and `log/runtime.hpp`.

Tests/evidence: the availability fixture proves strict compile filtering and
relaxed runtime filtering for the same `CompileLevel<Notice>` policy.

Reversal path: future language reflection or generated project policy headers
could expose bound thresholds earlier without changing call sites.

### 10.4 Cortex-M Metrics Accounting Portability

Problem: the integration build found unconditional 64-bit atomic accounting in
otherwise locked Metrics paths, and the ARM SDK does not ship `libatomic`.

Decision: keep 64-bit public records but protect aggregate accounting with the
existing facility spinlock and contention fallback state with a dedicated
spinlock. Truly atomic metric slots remain available only when all required
atomics are target lock-free.

Physical implementation: `metrics/facility.hpp` and `metrics/storage.hpp`.

Tests/evidence: full Metrics relaxed/strict tests, native firmware, Teensy link,
and no undefined `__atomic_*_8` symbols.

Reversal path: a future target-native lock-free 64-bit backend may replace the
private accounting synchronization without changing records or APIs.

## 11. Specification Refinements

Observed contract: `08-logging.md` required bound source/domain compile
thresholds while the accepted relaxed architecture keeps the composition root
out of ordinary component headers.

Evidence: an inline relaxed call site can evaluate Kconfig and declaration-only
facts, but cannot name the bound System or its Blueprint policy. Encoding cannot
be conditionally removed using information unavailable in that translation
unit.

Accepted change: Kconfig remains compile-time in every mode; strict mode also
compile-filters bound typed thresholds; relaxed mode applies those thresholds
at ingress with the already accepted negligible frontend overhead.

Specifications updated: `08-logging.md`, Section 20.

Verification added: `logging_availability` executes the same
`CompileLevel<Notice>` configuration in relaxed and strict builds and verifies
runtime-filtered versus compile-filtered disposition.

## 12. Firmware And Host Impact

The firmware root component now contributes `DriveGain`, `BootObserved`,
`StartupRuns`, and `StartupWork`. Its `main()` boots once and exercises the
global Parameters, Events, Metrics, Execution, and Logging frontends. Logging
routes to retained history and uses shared system-workqueue execution.

The first target integration also closed the Stage 11 32-bit accounting defect
described above. Host Blueprints remain Kconfig-neutral: Logging is unavailable
unless the Zephyr build explicitly defines `CONFIG_SOLAR_LOG`.

## 13. Known Limits And Deferred Work

- Remote streaming, schema identity, batching, and backpressure belong to
  Stages 13-14;
- console, file, network, and transport sinks are application/infrastructure
  types rather than hidden defaults;
- a slow asynchronous sink must own its bounded queue and executor;
- compatibility with an application-owned alternate Zephyr frontend is a later
  optional mode;
- richer format grammar, dictionaries, source-location retention, per-level
  wait policies, and durable history remain optional extensions;
- physical panic/fatal termination is not invoked in Ztest; routing and panic
  notification are tested before the terminal Zephyr boundary.

## 14. Documentation Handoff

Public documentation should explain source/domain contributions, compile and
runtime filtering, ignorable receipts, bounded format ownership, normal/try/ISR
forms, static sink design, history paging, Zephyr frontend-only consequences,
`printk` policy, panic-safe routes, shared Execution ownership, strict versus
relaxed threshold behavior, and Remote adapter ownership. The Logging and
firmware fixtures are the executable examples.

## 15. Closure Statement

Stage 12 is complete because canonical bounded capture, typed formatting,
filtering, static routing, history, early Zephyr ingestion, Event adaptation,
flush/stop/panic behavior, focused records, disabled behavior, and the firmware
integration gate are implemented; all focused, host, complete native, and
Teensy checks pass; and Logging introduces no hidden thread, heap, transport,
or Remote dependency. Stage 13 Remote protocol and generation is unblocked.
