# Stage Workflow And Verification

Status: accepted implementation process

This document defines how implementation stages are opened, executed, and
closed. It is intentionally more rigorous than a checklist of code files: each
stage must connect accepted specification clauses to tested behavior and leave
a usable engineering record.

## 1. Stage States

Every roadmap stage has one state:

| State | Meaning |
| --- | --- |
| `planned` | Scope and prerequisites are known; work has not started. |
| `active` | Implementation may be incomplete and scoped builds may be red. |
| `verification` | Intended code is present; only fixes, tests, measurement, and documentation remain. |
| `landed` | All declared closure gates pass and the landed summary exists. |
| `blocked` | A concrete external or architectural blocker is recorded; ordinary incomplete work is not called blocked. |

Only `landed` stages satisfy downstream roadmap prerequisites.

## 2. Stage Opening Contract

Before editing implementation, create or update the stage tracker with:

- stage objective;
- accepted specification sections in scope;
- explicit exclusions;
- prerequisite stages and evidence that they landed;
- current files to keep, reshape, replace, or remove;
- expected public headers and Kconfig surface;
- expected runtime owners and static capacities;
- applicable test and target gates;
- known firmware and host-tool impact;
- stage-specific risks.

This opening contract belongs in the active implementation tracker. It is not a
new speculative design specification.

## 3. Required Implementation Loop

### Step 1: Read the accepted contract

Read the complete owning design specification and every cross-reference named
by the dependency map. Extract behavior into a stage checklist. Do not implement
from memory or from the current old header shape.

### Step 2: Gather implementation context

Inspect:

- current Solar files and pending user changes;
- current firmware consumers;
- relevant Zephyr 4.4 headers, implementation, Kconfig, tests, and samples;
- current generated build facts when devicetree or Kconfig is involved;
- supported compiler/library behavior for modern C++ features;
- existing tests whose intent should be retained.

For Zephyr integration, primary local Zephyr source and official Zephyr
documentation are authoritative. Existing Solar wrappers are evidence, not
authority.

### Step 3: Establish the stage test scaffold

Add the test directories, configurations, fixtures, fake providers, and
compile-fail cases needed by the intended behavior. Tests may initially fail
while the stage is active.

Tests must describe contracts rather than reproduce implementation internals.

### Step 4: Remove superseded scope

Delete or reshape old code as soon as its replacement boundary is understood.
Do not leave an old API active beside a new API to make migration appear green.

Deletion must preserve unrelated user changes. Git history is the only legacy
archive.

### Step 5: Implement progressively

Prefer thin vertical increments that can be exercised:

1. types and concepts;
2. normalization and diagnostics;
3. static ownership/storage;
4. ordinary thread behavior;
5. timeout, failure, and unavailable behavior;
6. ISR or concurrency behavior;
7. inspection/accounting;
8. Kconfig exclusion and size behavior.

Do not implement every declaration first and defer all behavior until the end.

### Step 6: Test and adjust

Run the smallest relevant tests continuously, then broaden to stage gates.
Adjust implementation and tests together where the accepted semantics remain
unchanged.

If implementation evidence contradicts a spec, follow the amendment process in
Section 9 before continuing.

### Step 7: Verify resources and platform behavior

Record relevant:

- static storage owners and sizes;
- threads, stacks, workqueues, timers, and poll objects;
- heap use or proof of absence;
- ISR constraints;
- synchronization and lock ordering;
- binary-size changes where meaningful;
- native and target-specific behavior.

### Step 8: Run closure gates

Run every test class declared by the stage. A known unrelated red test must be
identified explicitly; it cannot be hidden by reporting only a partial command.

### Step 9: Write the landed summary

Create:

```text
development-docs/implementation-planning/landed/NN-stage-name.md
```

using `landed-summary-template.md`. Record exact commands and outcomes.

### Step 10: Update planning state

Mark the stage landed, update repository dispositions affected by implementation
discoveries, and unlock downstream stages. Do not silently mark future work
complete.

## 4. Green Checkpoint Policy

Intermediate commits inside an active stage may be red. A stage may close only
when:

- all tests owned by that stage pass;
- all previously landed Solar tests still pass, unless the stage explicitly and
  correctly replaced them;
- expected compile-fail cases fail for the intended diagnostic reason;
- the declared native Zephyr build passes;
- strict and relaxed variants pass when the stage adds a bound API;
- Kconfig-disabled variants pass when the stage adds an optional capability;
- no forbidden compatibility layer remains;
- the landed summary is complete.

Firmware is required only at roadmap-designated integration checkpoints. At
those checkpoints both native and Teensy 4.0 firmware builds must pass unless
the checkpoint explicitly requires physical target execution as well.

## 5. Verification Classes

### 5.1 Host compile-pass tests

Use for pure C++23 facilities that do not depend on Zephyr configuration or
runtime behavior:

- `Result` composition;
- descriptor concepts;
- stable identity algorithms;
- type-list normalization;
- contribution collection;
- selected protocol codec golden vectors where the same implementation is
  built for host tests.

Host tests supplement but do not replace Zephyr tests.

### 5.2 Compile-fail contract tests

Required for architecture diagnostics such as:

- duplicate blueprint sections;
- repeated component identity;
- missing component dependency;
- dependency cycles;
- conflicting contribution ownership;
- duplicate stable external identity;
- disabled but intentionally configured subsystem;
- invalid policy combinations;
- strict unregistered operation;
- ISR operation with incompatible effective policy;
- exceeded compile-time hard ceiling.

Do not use unsupported Twister schema fields to expect a failed build. Stage 00
must establish a supported harness that:

1. configures a small Zephyr application;
2. invokes its build as a test action or script;
3. requires non-zero completion;
4. matches a stable Solar diagnostic token rather than an entire compiler
   message;
5. fails if compilation unexpectedly succeeds or fails for an unrelated reason.

### 5.3 Native Zephyr runtime tests

`native_sim/native/64` is the primary runtime platform. Use Ztest or another
Zephyr-native harness for:

- lifecycle ordering and rollback;
- mutex, timeout, queue, poll, work, timer, and service behavior;
- relaxed frontend binding and runtime errors;
- static subsystem storage behavior;
- multi-threaded and bounded-overflow tests;
- Remote in-memory link/session tests;
- Supervisor fake-clock/provider tests.

Tests must use finite timeouts and report enough focused state to diagnose
failure.

### 5.4 Strict and relaxed matrix

Every bound subsystem stage adds at least:

| Build | Expected evidence |
| --- | --- |
| relaxed + registered | ordinary inline call works and reaches canonical state |
| relaxed + pre-boot | `NotReady` |
| relaxed + unregistered | `NotRegistered` |
| relaxed + subsystem disabled | `Disabled` or documented no-op for that API |
| strict + registered | call compiles and reaches canonical state |
| strict + unregistered | focused compile failure |
| strict multi-TU | one canonical state, no duplicate storage |

Strict and relaxed builds expose the same successful-operation spelling and
result types.

### 5.5 ISR tests

ISR-safe contracts require more than calling a function from an ordinary test
thread. Applicable stages must verify:

- explicit `try_<verb>_isr` spelling;
- non-waiting behavior;
- no mutex, allocation, formatting, or destructor deferral;
- compatible Zephyr ISR context where native simulation supports it;
- compile-time rejection of incompatible route or policy;
- bounded ingress overflow accounting;
- later thread-context processing where applicable.

Physical target ISR smoke tests are added when native simulation cannot model
the relevant driver or timing behavior.

### 5.6 Concurrency and stress tests

Applicable tests include:

- many producers and one consumer;
- coherent read/write under mutex or atomic policy;
- overflow under sustained producer load;
- stop during blocked service wait;
- cancellation after work admission;
- slow Remote client isolation;
- logging/event reserved-capacity behavior;
- frontend binding before concurrent component execution begins.

Stress tests remain bounded and deterministic enough for CI. Longer soak tests
may be separate but must have documented commands.

### 5.7 Protocol and generation tests

Remote and hardware generation require:

- deterministic output from identical input;
- golden vectors;
- malformed input and version mismatch cases;
- generated firmware artifacts compiling in Zephyr;
- generated host artifacts importing and round-tripping;
- schema identity stability;
- stale generated artifact detection;
- devicetree fixture coverage for present, absent, disabled, aliased, and
  unsupported nodes.

### 5.8 Firmware compile gates

At designated roadmap checkpoints:

- build the firmware for `native_sim/native/64`;
- build for Teensy 4.0;
- confirm C++23 flags and expected Solar Kconfig;
- record stack/static/binary summaries where tools provide them;
- do not repair firmware through compatibility aliases.

### 5.9 Physical hardware gates

Required only for stages that make claims native simulation cannot prove:

- GPIO direction/read/write and interrupts;
- SPI/I2C/UART async driver behavior;
- ADC/PWM/counter integration;
- watchdog reset behavior;
- Remote physical link framing and backpressure;
- stack/timing observations representative of target execution.

Tests must distinguish unavailable board capability from Solar failure.

### 5.10 Size and exclusion tests

For optional subsystems:

- a disabled and unused capability adds no facility storage or thread;
- demand-derived facilities disappear when their catalogs are empty;
- Kconfig-selected facilities appear exactly when selected;
- descriptor strings and optional accounting can be removed as specified;
- no hidden heap dependency appears;
- one system state is emitted across multiple translation units.

Exact byte budgets are refined after the first implementation. Early stages
record deltas rather than inventing unsupported thresholds.

## 6. Test Directory Shape

The intended Solar test organization is:

```text
tests/
  host/
    core/
    meta/
    protocol/
  compile_fail/
    blueprint/
    contributions/
    binding/
    policies/
  zephyr/
    smoke/
    kernel/
    lifecycle/
    execution/
    bus/
    parameters/
    events/
    metrics/
    logging/
    remote/
    inspection/
    health/
    hardware/
  fixtures/
    devicetree/
    remote/
```

Final CMake/Twister details are established by Stage 00. Tests should be grouped
by owning contract, not by whichever header happens to contain implementation.

## 7. Kconfig Workflow

When a stage adds Kconfig:

1. distinguish capability, hard ceiling, default, and integration symbols;
2. use typed blueprint policy for application C++ types and local architecture;
3. validate precedence: declaration > blueprint policy > Kconfig default;
4. test enabled, disabled, minimum, maximum, and invalid combinations;
5. avoid fallback `config.hpp` behavior;
6. verify generated `autoconf.h` is the sole build configuration source;
7. record Zephyr symbols selected or depended upon.

Kconfig must not name application C++ types.

## 8. Code Review Checklist

Before verification, review the stage for:

- ownership matching the accepted subsystem;
- no universal context/runtime object;
- no dynamic registration;
- no hidden default executor;
- no component header including the composition root;
- no broad snapshot API;
- no subsystem silently owning another subsystem's state;
- bounded storage and finite waits;
- explicit ISR behavior;
- focused `Result` errors;
- C++23 use that simplifies rather than obscures;
- Zephyr-native mechanisms and naming;
- no stale old architecture aliases;
- diagnostics that name the failed type and constraint.

## 9. Specification Amendment Process

An amendment is justified only by concrete implementation or platform evidence,
not convenience alone.

The stage owner records:

```text
Observed contract:
Implementation/platform evidence:
Why the accepted form cannot or should not remain:
Alternatives evaluated:
Chosen amendment:
Affected specs and stages:
Verification added:
```

Update the owning spec first, then code and planning dependencies. The landed
summary links the amendment. A change that alters another already-landed stage
reopens and reruns its affected gates.

## 10. Landed Summary Quality

A landed summary must let the later documentation pass answer:

- what users can actually write;
- what happens at compile time and runtime;
- what memory, threads, and Kconfig are involved;
- what errors and unavailable states exist;
- what is tested and on which platform;
- what remains deferred.

It must not merely list commits or filenames.

## 11. Final Implementation Closure

The implementation pass closes only when:

- every roadmap stage is landed or explicitly moved to a later accepted
  program;
- native Solar tests are green;
- strict and relaxed binding matrices are green;
- firmware builds for native simulation and Teensy 4.0;
- required physical hardware smoke tests pass;
- no compatibility architecture remains;
- every stage has a landed summary;
- the implementation inventory reflects final reality;
- the public-documentation pass has a complete handoff package.
