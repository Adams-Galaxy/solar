# Stage NN: Title

Status: landed

Landed date: YYYY-MM-DD

Implementation repository/branch:

Relevant commits or change identifiers:

## 1. Objective

State the concrete capability this stage was required to land.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `NN-spec.md` | sections | complete/refined/deferred |

List any accepted clauses deliberately deferred and where they moved. Do not
describe incomplete required work as landed.

## 3. Public Surface Landed

Describe the public headers, namespaces, concepts, types, functions, Kconfig,
generated artifacts, and normal user shape that now exist.

Include short representative examples where they clarify reality:

```cpp
// Actual supported use after this stage.
```

## 4. Runtime Ownership

Record every runtime owner introduced or changed:

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |

Explicitly state:

- heap behavior;
- thread and stack ownership;
- workqueue usage;
- timers and poll objects;
- static initialization behavior;
- canonical state owner.

## 5. Compile-Time Behavior

Describe:

- catalog or graph derivation;
- strict and relaxed binding behavior where applicable;
- diagnostics and compile-fail contracts;
- Kconfig exclusion;
- generated-code behavior;
- measured or observed compile-time concerns.

## 6. Error And Availability Behavior

List focused errors and the conditions that produce them. Distinguish compile
errors, runtime `Result` errors, unavailable facts, disabled capability, stale
records, timeout, overflow, and policy response.

## 7. Zephyr Integration

Record:

- Zephyr APIs and implementation mechanisms used;
- required or selected Zephyr Kconfig;
- devicetree bindings or generated facts;
- ISR/thread context requirements;
- native handle escape hatches;
- differences between native simulation and target behavior.

## 8. Files Changed

### Added

- `path`

### Reshaped

- `path`

### Removed

- `path`

Do not paste a raw complete diff listing when grouped ownership is clearer.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| command | target | pass/fail | contract |

Include:

- host tests where applicable;
- compile-pass and compile-fail tests;
- native Zephyr tests;
- strict/relaxed variants;
- disabled Kconfig variants;
- concurrency/ISR/stress tests;
- firmware builds at designated gates;
- physical hardware tests where required;
- resource and size measurements.

If an unrelated test remains red, name it and link its owning active stage or
issue. A stage-owned red test prevents landing.

## 10. Specification Refinements

For each amendment:

```text
Observed contract:
Evidence:
Accepted change:
Specifications updated:
Verification added:
```

Write `None` when no specification changed.

## 11. Firmware And Host Impact

Describe migration performed now, intentionally deferred migration, removed old
usage, generated artifact changes, and any integration checkpoint result.

## 12. Known Limits And Deferred Work

List only accepted later work. Distinguish optional extension from missing stage
requirements.

## 13. Documentation Handoff

Record what the later public-documentation pass must explain, including:

- normal user workflow;
- configuration choices;
- error handling;
- concurrency/ISR constraints;
- examples and target caveats;
- links to executable tests that should become documentation examples.

## 14. Closure Statement

State why the stage is complete and identify the roadmap stages now unblocked.

