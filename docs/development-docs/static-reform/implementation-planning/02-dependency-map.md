# Solar Implementation Dependency Map

Status: accepted

This map separates four relationships that must not be conflated:

1. **C++ include dependency:** one declaration needs another declaration.
2. **compile-time architecture dependency:** normalization or validation needs
   another catalog or concept.
3. **runtime lifecycle dependency:** one effective component must be ready
   before another.
4. **implementation dependency:** a testable implementation capability must
   exist before another stage can close.

A runtime dependency is not inferred merely because one implementation reuses
a header or helper.

## 1. Architectural Layers

```text
Layer 0  Zephyr 4.4 + C++23 + Solar module/Kconfig/test harness
    |
Layer 1  Result/Status, identity, descriptors, type-pack algorithms
    |
Layer 2  contributions, catalogs, Blueprint normalization, binding frontends
    |
Layer 3  Kernel primitives                 Hardware/devicetree primitives
    |                                      |
Layer 4  graph + lifecycle + System ownership
    |
Layer 5  service execution + executors + task registrations
    |
Layer 6  Bus | Parameters | Events | Metrics | Logging core
    |        |            |        |         |
Layer 7  subsystem adapters, persistence, deferred processing, sinks
    |
Layer 8  Remote protocol/generation -> Remote service/links/exposures
    |
Layer 9  Inspection -> Health -> Supervisor
    |
Layer 10 firmware integration, resource audit, target validation
```

This is a dependency view, not a requirement to implement all members of one
layer in one stage.

## 2. Foundation Dependencies

| Capability | Requires | Must not require |
| --- | --- | --- |
| `FixedString` and compile-time identifiers | C++23 language | System binding, Zephyr runtime |
| `Status` | stable small status vocabulary | subsystem error enums, heap |
| `Result<T, E>` | `std::expected`, status/error conversion conventions | system object or universal error |
| descriptor concepts | identity primitives | runtime storage |
| type-list/catalog algorithms | descriptor concepts | lifecycle, kernel |
| contribution collection | catalog entries, provenance, component types | subsystem runtime facility |
| Blueprint classification | tagged sections, type-list algorithms | runtime state |
| effective blueprint | contributions, configuration normalization, Kconfig availability traits | dynamic registration |
| application binding | validated `System<Blueprint>` type | static initialization, storage aggregate object |
| relaxed frontend binding | effective catalogs, boot phase, typed subsystem operation tables | linker registration, implicit state |
| strict frontend binding | application binding and dependent lookup | runtime dispatch |

The compile-time architecture can be tested on the host where useful, but its
canonical build remains a Zephyr C++23 application so Kconfig-selected behavior
is exercised.

## 3. Kernel Boundary

`solar::kernel` depends directly on Zephyr kernel APIs and core Result/time
types. It does not depend on:

- `System`;
- Blueprint or application binding;
- lifecycle records;
- execution registration;
- observability facilities;
- Remote;
- application components.

This permits kernel primitives to be used in Solar internals and ordinary
application code without system participation.

Focused kernel diagnostics may expose native facts, but `solar::execution`
owns the interpretation of Solar services, executors, jobs, and work.

## 4. Hardware Boundary

`solar::hardware` depends on:

- Zephyr devicetree macros and generated devicetree data;
- Zephyr device and driver APIs;
- core Result/error conventions;
- selected kernel primitives for synchronization or async completion where the
  native driver requires them;
- Solar's build-time devicetree generator for ergonomic aliases and metadata.

It does not depend on System, lifecycle, Device components, Health, Remote, or
application catalogs. An application hardware-backed type becomes a Solar
`Device` only when deliberately declared in `Devices<...>`.

Normal DMA remains owned by Zephyr drivers. Solar Hardware does not build an
SoC-independent advanced DMA abstraction.

## 5. System, Graph, And Lifecycle

The static `System<Blueprint>` owns separate type-owned state for the accepted
subsystems. It does not hide one runtime/context object.

Lifecycle requires:

- effective component graph;
- `Dependencies<...>` validation and topological order;
- Result/Status normalization;
- kernel mutex/thread primitives;
- bounded lifecycle/report storage;
- service/executor preparation hooks supplied by Execution.

Execution and lifecycle cooperate through a narrow activation protocol:

1. lifecycle invokes component `init()` and `start()` hooks;
2. execution prepares service threads and executor registrations without
   allowing user work to run;
3. lifecycle commits components and system to `Running`;
4. execution releases the final activation barrier;
5. stop closes admission, requests cooperative termination, applies timeout
   policy, and returns focused results to lifecycle;
6. lifecycle performs reverse-order stop and deinit.

Neither subsystem owns the other's records.

## 6. Execution Dependencies

| Execution capability | Dependencies |
| --- | --- |
| dedicated service thread | Kernel Thread, StopToken, lifecycle component identity |
| custom executor | Kernel primitives selected by that executor, lifecycle component participation |
| Zephyr system workqueue target | Kernel Work/DelayableWork adapter, Kconfig-selected target policy |
| owned workqueue executor | Kernel WorkQueue and static stack storage |
| periodic registration | Kernel timing/work plus execution admission |
| event-triggered registration | owning trigger adapter plus execution target |
| execution records | focused Kernel facts and execution-owned counters/state |

Solar provides no hidden default executor. Kconfig may select the Zephyr system
workqueue as the default target. Otherwise omitted execution targets are
normalization errors.

## 7. Built-In Inclusion Classes

| Class | Built-ins | Inclusion rule |
| --- | --- | --- |
| demand-derived | Bus, Parameters, Events, Metrics, Remote | Kconfig capability enabled and effective catalog/configuration demands the subsystem |
| Kconfig-selected | Logging, Health, Inspection | present whenever selected by Kconfig |
| required-derived | Supervisor support, Remote facility/service required by links, other explicit provider relationships | pulled in by another selected effective component |

All included built-ins appear in the effective graph and lifecycle records.
Disabled capability plus intentional blueprint use is a normalization error.

## 8. Core Subsystem Dependencies

### 8.1 Bus

Bus core requires catalogs, binding, bounded storage where routes need it, and
Kernel synchronization. Delivery policies add dependencies:

- inline route: no executor;
- queued/deferred route: declared execution target;
- latest-value route: route-owned coherent storage;
- ISR emission: every effective route must be ISR-compatible.

Bus does not depend on observability Events. Diagnostics may be adapted to
Events later through an explicit one-way adapter.

### 8.2 Parameters

Parameters core requires catalogs, binding, static typed storage, Kernel
synchronization, and validation policy. Optional dependencies are explicit:

- persistence adapter -> Zephyr settings/storage provider;
- deferred persistence -> declared execution target;
- change hook -> declared execution target when not inline;
- Remote exposure -> Remote adapter depends on Parameters, never the reverse.

### 8.3 Events

Events core requires catalogs, binding, bounded ingress/history, timestamps,
and synchronization. Optional processors may require execution.

One-way adapters are permitted:

```text
Event -> Metric update
Event -> Log record
Event -> Remote exposure
```

Metrics, Logging, and Remote never become owners of the canonical event.

### 8.4 Metrics

Metrics core requires catalogs, binding, catalog-derived static storage,
timestamps where selected, and concurrency policy. It does not require Events
or Logging. Event-to-metric adapters depend on both and remain outside either
core.

### 8.5 Logging

Logging core is Kconfig-selected early infrastructure. It requires Kernel
atomics/synchronization, bounded record storage, timestamp/context capture, and
Zephyr logging integration when enabled.

Logging does not depend on Remote or a transport service. Sinks are leaf
adapters:

```text
Logging core <- producers
Logging core -> local sink
Logging core -> transport sink -> transport provider
Logging core -> Remote adapter -> Remote
```

The adapter depends on both sides and activates after its provider. This avoids
a Logging/Remote lifecycle cycle while preserving early capture.

## 9. Remote Dependencies

Remote is intentionally split.

### 9.1 Protocol core and generation

Requires core descriptors, stable identity, deterministic CBOR, packed Stream
schemas, bounded framing, and generated host/firmware schema artifacts. It can
be tested with in-memory bytes and no physical link.

### 9.2 Runtime facility and service

Requires:

- protocol core;
- Execution service and selected work/mailbox paths;
- Kernel queues/poll/synchronization;
- configured async Link types;
- catalog-derived exposure adapters;
- bounded session, request, response, stream, and reassembly storage.

Remote reads or adapts canonical subsystem state. It never owns Parameters,
Metrics, Events, Logs, lifecycle, execution, health, or hardware facts.

## 10. Inspection, Health, And Supervisor

### 10.1 Inspection

Inspection depends on focused descriptor/record providers already implemented.
It owns only collection descriptors, paging/formatting state, and bounded query
coordination. It does not own copied subsystem truth.

### 10.2 Health

Health depends on:

- lifecycle component identity;
- component-owned assessment hooks and named Checks;
- focused Kernel and Execution diagnostics;
- optional adapters for Events, Metrics, Logging, Remote, and hardware facts;
- bounded Health records and evidence references.

Health is passive assessment. It may exist without Supervisor.

### 10.3 Supervisor

Supervisor depends on Health, Execution, Kernel timing/thread/watchdog
boundaries, and explicit response policy. It is an active service and reports
its own state back to Health without recursively supervising its reporting
path.

Hardware watchdog integration is deferred until the required board/driver
support is available, but the policy and feeding boundary are implemented and
testable with a fake provider.

## 11. Cross-Subsystem Cycle Audit

| Potential cycle | Resolution |
| --- | --- |
| System <-> components | Components never include the composition root; root includes component declarations. |
| Logging <-> Remote | Remote consumes Logging through a leaf adapter; Logging core never depends on Remote. |
| Events <-> Logging | Event-to-log adapter depends on both; Logging failures do not automatically emit Events. |
| Events <-> Metrics | Event-to-metric adapter is one-way; Metrics core does not emit Events implicitly. |
| Health <-> Supervisor | Health assesses Supervisor state; Supervisor consumes Health assessments. The Supervisor's own report path is non-recursive and excluded from self-triggered response. |
| Lifecycle <-> Execution | Narrow prepare/activate/stop protocol; each owns separate canonical records. |
| Hardware <-> Device | Hardware is independent; application Device types consume Hardware. |
| Inspection <-> providers | Inspection reads provider-owned records; providers never require Inspection. |
| Remote <-> subsystem state | Exposure adapters read owning subsystems; subsystem cores never require Remote. |
| relaxed frontend <-> System state | Frontend stores only a non-owning operation binding; canonical state remains type-owned by the selected system/facility. |

## 12. Include Direction

The project include DAG is:

```text
Solar core/kernel/hardware headers
        ^
descriptor and direct dependency headers
        ^
component headers
        ^
application composition root
        ^
strict component source definitions and application main
```

Forbidden:

```text
component header -> application composition root
Solar subsystem -> application component header
Solar subsystem -> firmware-defined System type
```

Relaxed component source and inline implementations do not need the root.
Strict bound definitions include the root from their `.cpp` files.

## 13. Implementation Critical Path

The shortest path to a representative running system is:

```text
module/test reset
 -> Result/core identity
 -> catalogs/contributions
 -> Blueprint/System/binding
 -> Kernel essentials
 -> lifecycle/boot/stop
 -> service execution
 -> one bound subsystem frontend
 -> representative firmware integration
```

The shortest path to complete Solar then branches and rejoins:

```text
                   -> Bus -----------\
                   -> Parameters -----\
Execution/System -->> Events ----------+-> Remote -> Inspection -> Health/Supervisor
                   -> Metrics --------/
                   -> Logging -------/
Kernel/Core -------> Hardware -----------------------------> firmware target closure
```

Stages may implement branches in a different serial order, but may not violate
these prerequisite edges.
