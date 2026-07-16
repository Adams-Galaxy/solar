# Solar Implementation Planning

Status: accepted

This directory turns the accepted Solar design specifications into an
executable implementation program. These files are development artifacts, not
public user documentation.

The authoritative architecture remains in
`development-docs/design-specs/00-*.md` through
`development-docs/design-specs/14-integrated-architecture.md`. Planning files
may sequence, group, and verify that architecture, but must not silently change
it.

## Planning Set

| Artifact | Purpose |
| --- | --- |
| `01-repository-inventory.md` | Classify current Solar, firmware, host-tool, and temporary reference areas as keep, reshape, replace, remove, or defer. |
| `02-dependency-map.md` | Show compile-time, runtime, build-time, and implementation dependencies between architectural capabilities. |
| `03-stage-workflow-and-verification.md` | Define how every stage is researched, implemented, tested, closed, and summarized. |
| `04-implementation-roadmap.md` | Define the ordered implementation stages, green checkpoints, migration points, and completion gates. |
| `landed-summary-template.md` | Provide the required engineering record produced when a stage closes. |
| `landed/` | Hold completed stage summaries during implementation. |

## Locked Migration Policy

Solar is pre-release and this reform is a hard architectural migration.

- No source compatibility is required.
- No deprecated aliases or compatibility adapters are required.
- No old and new architecture may coexist merely to keep callers compiling.
- Obsolete implementation and tests may be deleted.
- Git history is the archive; no legacy source archive is kept in-tree.
- Intermediate commits may be red while a stage is active.
- A stage closes only at its declared green checkpoint.
- Solar's scoped tests must pass at every stage close.
- Firmware builds are required only at roadmap-designated integration gates.
- The workspace-pinned Zephyr release is the initial supported baseline.
- `native_sim/native/64` is the primary runtime test platform.
- Teensy 4.0 is the primary firmware compile target.
- Physical target tests are required only where the stage owns hardware-facing
  behavior.

## Language And Build Baseline

The implementation targets C++23 and the current workspace Zephyr baseline.
The expected application configuration begins with:

```text
CONFIG_CPP=y
CONFIG_STD_CPP23=y
CONFIG_REQUIRES_FULL_LIBCPP=y
```

Solar is a Zephyr module first. Host-only tests may support metaprogramming and
format-independent unit coverage, but they cannot replace Zephyr builds for
kernel, Kconfig, devicetree, ISR, lifecycle, or execution behavior.

## Binding Modes

Relaxed catalog binding is the default:

```text
CONFIG_SOLAR_STRICT_CATALOG_BINDING=n
```

Relaxed and strict builds use one architecture and one canonical state. The
test matrix must cover both modes whenever a stage adds a bound subsystem API.

## Change Control

Implementation may expose a genuine contradiction or infeasible detail in an
accepted specification. When that happens, the stage must:

1. record the evidence;
2. update the affected design specification explicitly;
3. update dependent planning artifacts;
4. describe the change in the landed summary;
5. rerun all affected gates.

Internal names and file placement may be refined without reopening
architecture. Public ownership, semantics, error behavior, concurrency, and
cross-subsystem boundaries may not drift silently.

## Stage Closure

Each stage follows the workflow in
`03-stage-workflow-and-verification.md` and closes with a file under:

```text
development-docs/implementation-planning/landed/
```

The landed summaries, accepted specifications, final code, and test evidence
become the inputs to a separate public-documentation pass after implementation.
