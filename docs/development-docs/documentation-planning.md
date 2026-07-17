# Solar Documentation Pass Plan

Status: implemented and audited

Date: 2026-07-17

This document defines the complete documentation pass that follows Solar's
static-system implementation and error-contract refinement. It is the execution
plan for producing Solar's first authoritative documentation set. It is not
itself public product documentation.

The pass is a hard documentation rebuild. The current README and development
records are useful inputs, but no pre-reform public wording or structure needs
to be preserved.

## 1. Outcome

The pass must produce two connected documentation paths:

1. **Using Solar** gives an application author everything needed to install,
   compose, configure, use, test, and diagnose a Solar application.
2. **Understanding Solar** explains ownership, compile-time construction,
   runtime machinery, generated artifacts, subsystem boundaries, and the
   engineering contracts needed to maintain or extend the framework.

These are navigation paths, not separate sites. Public guides link to
architecture where depth is useful. Architecture pages link back to the public
contracts they implement.

The pass is complete when a new user can build and understand a representative
native Solar application from the documentation alone, an experienced user can
find exact subsystem contracts without reading implementation headers, and a
contributor can follow the construction and ownership model without reverse
engineering it from templates.

## 2. Documentation Method

Solar uses the Diataxis model:

- **Tutorials** are guided learning experiences with a concrete outcome.
- **How-to guides** solve a specific practical problem.
- **Reference** states exact API, configuration, protocol, and generated-input
  contracts.
- **Explanation** develops the mental models behind the system.

Every page has one dominant purpose. A reference page does not become a
tutorial, and a tutorial does not attempt to enumerate every option.

Solar's site should feel Zephyr-native. It should retain Zephyr terminology,
link naturally to Zephyr concepts, and describe where Solar is a typed C++
surface over a Zephyr facility. It must not present Solar as a competing RTOS
or hide the devicetree and Kconfig mechanisms on which it depends.

## 3. Audiences And Entry Paths

### 3.1 Application author

Needs to:

- add Solar as a Zephyr module;
- configure C++23 and Solar Kconfig;
- create a Blueprint, define the application binding, and boot once;
- define static components and ordinary project include direction;
- use facilities, services, devices, and execution registrations;
- understand Result, availability, concurrency, ISR, and capacity behavior;
- test on `native_sim` and move to a physical board.

Primary path: getting started, tutorials, concepts, how-to guides, reference.

### 3.2 Subsystem user

Needs the shortest route from a task to declarations, common operations,
errors, context rules, policy, capacity, Kconfig, and examples.

Primary path: subsystem landing page, how-to guide, API reference.

### 3.3 Framework contributor

Needs compile-time derivation, runtime ownership, synchronization, generated
artifacts, test strategy, source boundaries, and extension contracts.

Primary path: architecture and development.

### 3.4 Host-tool author

Needs the Remote protocol, generated manifest and client artifacts, schema
compatibility, framing, sessions, streams, actions, and error representation.

Primary path: Remote tutorial, protocol reference, generated artifact reference.

## 4. Source-Of-Truth Policy

Documentation is derived from several authoritative sources. Their roles must
remain distinct:

| Subject | Authoritative source | Documentation role |
| --- | --- | --- |
| C++ signatures and template contracts | `include/solar/` | Doxygen/Breathe API reference |
| capability and capacity configuration | `zephyr/Kconfig` and included Kconfig files | generated Kconfig reference plus authored guidance |
| devicetree inputs | bindings, Zephyr devicetree, and Solar hardware generator | authored model plus generated fixture reference |
| Remote wire contract | Remote declarations, protocol code, vectors, and generated manifest | protocol and host integration reference |
| normal user behavior | buildable examples and public tests | tutorials and how-to evidence |
| accepted intent | `docs/development-docs/static-reform/design-specs/` | architecture source material |
| exact landed behavior | `docs/development-docs/static-reform/implementation-planning/landed/` | verification and caveat source material |
| future work | `docs/development-docs/future-ideas/` | excluded from current-contract docs unless clearly labelled future work |

Development records must not be placed in the public navigation or copied
wholesale into public pages. Public documentation describes the current
contract directly and links to public source or tests where useful.

When prose and code disagree, the discrepancy is a defect. The pass must either
fix the implementation or correct the prose before that stage closes.

## 5. Toolchain

The documentation toolchain is locked as follows:

- **Sphinx** owns page assembly, navigation, cross-references, search, HTML
  generation, and warnings;
- **MyST Parser** allows authored Markdown while preserving Sphinx roles,
  directives, substitutions, and `literalinclude`;
- **Doxygen** extracts C++ declarations and XML only;
- **Breathe** renders Doxygen declarations inside authored Sphinx pages;
- **Furo** provides the responsive HTML theme;
- **sphinx-copybutton** provides copy controls for command and code blocks;
- **sphinx-design** may provide restrained callouts, grids, and navigation
  helpers where plain structure is insufficient;
- **Zephyr intersphinx inventory** links Solar terms to the corresponding
  Zephyr documentation without copying Zephyr reference material.

Doxygen HTML is not published. Raw generated XML, Sphinx doctrees, generated
Kconfig pages, and generated reference intermediates belong under the build
directory and are not committed.

### 5.1 Dependency ownership

Documentation Python dependencies live in `docs/requirements.txt` with bounded
versions. They are installed into an isolated environment. The repository must
not depend on globally installed Sphinx packages.

The normal commands will be exposed through CMake:

```sh
cmake -S docs -B build/docs
cmake --build build/docs --target docs-html
cmake --build build/docs --target docs-linkcheck
```

The Sphinx source tree remains buildable directly for debugging, but CI and
documented contributor workflows use the CMake targets.

### 5.2 Sphinx policy

- MyST Markdown is the default authored format.
- Sphinx uses nitpicky cross-reference checking with a small, reviewed ignore
  list only for externally unresolved Zephyr symbols.
- Warnings fail the normal documentation build.
- External link checking is a separate target because network failures must not
  make local authoring unreliable.
- Solar's version is derived from the repository version source rather than
  repeated manually in `conf.py`.
- `development-docs/` is excluded from public navigation and publication input.

### 5.3 Doxygen policy

- Doxygen reads public declarations under `include/solar/`.
- XML generation is enabled; HTML generation is disabled.
- Undocumented declarations are not treated as sufficient public reference.
- Internal `detail` namespaces and implementation-only runtime machinery are
  excluded from normal public API pages unless an architecture page explicitly
  references them.
- Warnings for malformed comments, unresolved public references, and duplicate
  documentation fail the API documentation gate.
- Public comments live on the canonical declaration, not repeated across
  forwarding APIs and implementations.

Multi-line public contracts use `/** ... */`; short declaration summaries may
use `///`. Parameters and template parameters use `@param` and `@tparam`, while
return documentation names the success value and concrete error conditions.
`@note` and `@warning` are reserved for genuine context, lifetime, safety, or
compatibility concerns rather than routine narration.

## 6. Repository Layout

The documentation pass establishes this structure:

```text
docs/
  CMakeLists.txt
  Doxyfile.in
  conf.py
  requirements.txt
  index.md
  _static/
  _templates/
  getting-started/
    index.md
    requirements.md
    install.md
    first-application.md
    next-steps.md
  tutorials/
    index.md
    system-foundations.md
    data-and-observability.md
    remote-host-control.md
    health-and-recovery.md
  how-to/
    index.md
    configure-solar.md
    define-components.md
    use-strict-binding.md
    test-with-native-sim.md
    use-from-isr.md
    tune-capacities.md
    diagnose-boot.md
    generate-hardware-types.md
    generate-remote-artifacts.md
  concepts/
    index.md
    system-and-blueprint.md
    components-and-ownership.md
    identity-contributions-catalogs.md
    lifecycle.md
    result-and-errors.md
    concurrency-and-context.md
    capacity-and-backpressure.md
    availability-and-disabled-features.md
  reference/
    index.md
    api/
    kconfig/
    devicetree/
    remote-protocol/
    generated-artifacts.md
    compatibility.md
  subsystems/
    index.md
    kernel.md
    execution.md
    hardware.md
    bus.md
    parameters.md
    events.md
    metrics.md
    logging.md
    remote.md
    inspection.md
    health.md
    supervisor.md
  architecture/
    index.md
    static-system.md
    blueprint-normalization.md
    catalogs-and-binding.md
    lifecycle-engine.md
    runtime-ownership.md
    kernel-and-zephyr.md
    execution-runtime.md
    subsystem-storage.md
    remote-runtime.md
    health-and-supervision.md
    hardware-generation.md
    failure-containment.md
  development/
    index.md
    setup.md
    testing.md
    documentation.md
    style.md
    adding-a-component-category.md
    adding-a-subsystem.md
    release-and-versioning.md
  examples/
    index.md
  development-docs/
    ...

examples/
  first-application/
  system-composition/
  data-pipeline/
  remote-control/
  supervised-device/
```

The final file names may be tightened while writing, but the information
architecture and ownership above are fixed. Redirects are unnecessary because
there is no supported pre-reform public site.

## 7. Navigation

The top-level site navigation is:

1. Getting Started
2. Tutorials
3. How-To Guides
4. Concepts
5. Subsystems
6. Reference
7. Architecture
8. Development

The home page states what Solar is, identifies C++23 and Zephyr as requirements,
and provides direct routes for a first application, subsystem lookup, API
lookup, and architecture. It must not become a marketing landing page.

Every subsystem is discoverable by:

- the user task it solves;
- its Solar namespace and exact type names;
- its declaration or contribution role;
- its relevant Kconfig symbols;
- its architecture owner.

Pages use stable labels for cross-references instead of fragile relative links.
External Zephyr concepts are linked at first meaningful use.

## 8. Writing Contracts

### 8.1 Public API comments

Every public declaration documents what is not safely inferable from its
signature. Depending on the declaration, that includes:

- purpose and preconditions;
- template parameter roles;
- ownership and lifetime;
- success value and typed error conditions;
- blocking, timeout, and backpressure behavior;
- thread, ISR, callback, and reentrancy constraints;
- static storage or resource implications;
- availability when a subsystem is disabled;
- relationship to the underlying Zephyr primitive;
- native-handle escape behavior.

Comments should be concise. Long explanations belong in concepts, subsystem,
or architecture pages and are linked with Sphinx/Breathe references.

### 8.2 Authored prose

- Use the common path first and advanced policy second.
- Name the owning subsystem and state owner explicitly.
- Distinguish compile-time diagnostics, runtime Results, stored observations,
  and policy responses.
- Never describe a deferred capability as available.
- Prefer exact types and symbols over vague phrases such as "the runtime".
- Explain Zephyr integration at the point where it changes configuration or
  behavior.
- Avoid repeating complete API signatures already rendered by Breathe.

### 8.3 Result and error examples

All new examples follow the landed convention:

```cpp
solar::Result<void> init()
{
    if (!ready()) {
        return solar::fail<solar::Error>({
            .status = solar::Status::NotReady,
        });
    }
    return {};
}
```

Typed errors remain rich until an explicit boundary classifies them with
`status_of(error)`. Documentation must not imply automatic logging, event
publication, Remote serialization, or exception-like propagation.

## 9. Example Policy

Examples are executable documentation, not decorative snippets.

### 9.1 Canonical source

- Complete examples live under repository-root `examples/` as Zephyr
  applications.
- Tutorials include source from those files with MyST `literalinclude` and
  named regions.
- A code block may be written directly in prose only when it is deliberately
  partial and labelled as such.
- Test fixtures can support reference claims, but user-facing tutorials should
  not depend on test-only scaffolding as their canonical source.

### 9.2 Required example set

1. `first-application`: smallest complete native application, relaxed binding,
   lifecycle, boot Result, and one component.
2. `system-composition`: devices, facilities/services, dependencies,
   contributions, strict-binding variant, and inspection of boot state.
3. `data-pipeline`: Bus, Parameters, Events, Metrics, and Logging working
   together with bounded policies.
4. `remote-control`: link, queryable/streamable data, action, generated
   manifest, and host interaction.
5. `supervised-device`: hardware-facing device shape, health assessment,
   recovery, safe state, and Supervisor policy.

Every example has its own `sample.yaml`, `CMakeLists.txt`, `prj.conf`, source,
and short README that points to the corresponding tutorial. Examples are built
through Twister on `native_sim/native/64`; hardware-specific variants have
compile-only board gates where physical execution is unavailable.

### 9.3 Snippet verification

The documentation build verifies that every `literalinclude` path and named
region resolves. CI separately compiles all complete examples. Small public API
snippets that cannot be included from an application are collected into
compile-pass translation units rather than trusted by inspection.

## 10. Standard Subsystem Page

Every subsystem landing page uses the same compact contract:

1. purpose and non-goals;
2. ownership and lifecycle participation;
3. smallest common-path declaration and use;
4. contributions and Blueprint configuration;
5. operations and typed errors;
6. thread, ISR, callback, and synchronization rules;
7. capacity, timeout, overflow, and backpressure behavior;
8. Kconfig and generated inputs;
9. disabled and unavailable behavior;
10. testing guidance;
11. API reference links;
12. architecture links.

Sections with no meaningful behavior say so briefly rather than inventing
uniform complexity.

## 11. Required Coverage

### 11.1 Foundations

- C++23, Zephyr SDK/toolchain, module registration, and Kconfig requirements;
- static application binding and global frontends;
- Blueprint sections, policies, and normalization;
- component categories, dependencies, contributions, descriptors, identity,
  catalogs, and graph validation;
- strict and relaxed binding;
- lifecycle phases, hooks, failure behavior, reports, shutdown, and reboot
  rejection;
- `Result<T, E>`, `solar::Error`, typed errors, `fail<ErrorType>`, and monadic
  composition;
- concurrency, ISR, ownership, static lifetime, and bounded storage.

### 11.2 Runtime and hardware foundations

- Kernel wrappers and native Zephyr boundaries;
- threads, synchronization, queues, timers, work, polling, diagnostics, and
  fatal handling;
- execution registrations, tasks, executors, services, workqueues, periodic
  work, cancellation, and containment;
- hardware endpoint typing, devicetree aliases, GPIO, SPI, I2C, UART, ADC, PWM,
  counter, watchdog, RTIO/async paths, and advanced-DMA non-goals.

### 11.3 Facilities and services

- Bus;
- Parameters and persistence;
- Events and processors;
- Metrics and reducers;
- Logging, sinks, formatting, panic behavior, and `Notice`;
- Remote links, sessions, schemas, data capabilities, actions, topics, streams,
  collection safety, buffering, backpressure, generation, and host artifacts;
- Inspection's intentionally narrow role;
- Health evidence, component self-assessment, progress, history, and checks;
- Supervisor policy, thread monitoring, recovery, safe state, and watchdog
  integration.

### 11.4 Reference

- public C++ declarations grouped by subsystem;
- every Solar Kconfig symbol, default, dependency, range, and override rule;
- devicetree inputs and generated hardware type mapping;
- Remote protocol versioning, framing, schema rules, status/error encoding, and
  generated files;
- supported Zephyr/C++/toolchain contract;
- disabled-feature and strict/relaxed behavior matrix;
- known target constraints and intentionally unsupported capabilities.

Generated reference pages are produced by repository-owned scripts under
`tools/docs/`. Kconfig generation uses Zephyr's `kconfiglib` in a controlled
Zephyr configuration environment and fails when a Solar symbol lacks reference
metadata. Devicetree reference uses checked native and Teensy fixture builds so
target-dependent generated aliases are not presented as universal. Remote
reference is derived from protocol declarations, checked vectors, and a
controlled generated manifest. Authored introductions remain committed; the
mechanical symbol tables are build products.

### 11.5 Architecture

- why there is no runtime System object;
- distributed static ownership and initialization order;
- Blueprint normalization and built-in facility insertion;
- contribution discovery, catalog construction, graph validation, and binding;
- lifecycle graph traversal and state records;
- frontend dispatch in relaxed and strict modes;
- canonical subsystem storage and synchronization;
- kernel, execution, hardware, and device boundary distinctions;
- service containment and shutdown;
- Remote protocol and runtime data flow;
- health evidence and Supervisor response flow;
- devicetree and Remote generation pipelines;
- resource bounds and failure containment.

## 12. Diagrams

Diagrams are used only where they reduce real cognitive load. The required set
is:

- application composition and include direction;
- Blueprint normalization and catalog derivation;
- boot/lifecycle sequence;
- runtime ownership map;
- execution submission and service containment;
- Remote receive/transmit and data-collection paths;
- health evidence to Supervisor response flow;
- devicetree generation pipeline.

Diagram source is Graphviz text committed beside the page and rendered through
Sphinx's built-in Graphviz integration. Generated SVG or bitmap output is not
the source of truth. This keeps diagrams deterministic and avoids adding a
browser-side renderer to the published site.

## 13. Build And Quality Gates

### 13.1 Required local gates

```sh
cmake -S docs -B build/docs
cmake --build build/docs --target docs-html
cmake --build build/docs --target docs-linkcheck
```

The HTML gate requires:

- zero Sphinx warnings;
- zero unresolved internal references;
- zero Doxygen warnings in documented public declarations;
- all includes and generated pages present;
- no development records in the public toctree.

### 13.2 Required code gates

```sh
west twister -T examples -p native_sim/native/64
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Documentation changes that alter public comments only do not require every
Solar runtime test locally, but CI continues to run the normal repository
matrix. Any source adjustment made to reconcile documentation with behavior
uses the owning subsystem's full test gates.

### 13.3 Review audits

- **Fresh-user audit:** follow installation through first native build in a
  clean workspace.
- **Task audit:** locate solutions for ten representative tasks using only site
  navigation and search.
- **API audit:** select representative public symbols from every subsystem and
  verify signature, errors, context, and ownership.
- **Zephyr audit:** check that Kconfig, devicetree, workqueue, driver, and kernel
  terminology agrees with Zephyr.
- **Architecture audit:** trace boot, one emitted event, one Remote action, and
  one Supervisor recovery from public call to state owner.
- **Future-claim audit:** ensure deferred ideas are not presented as landed.
- **Mobile/readability audit:** verify tables, code, navigation, and diagrams at
  narrow and desktop widths.

## 14. Publication And Versioning

The first implementation gate produces a browsable HTML artifact in CI. Public
hosting is enabled only after the content-complete audit.

Publication follows these rules:

- the default branch publishes `latest`;
- release tags publish an immutable version path;
- the site displays Solar, Zephyr, C++ standard, and compatibility versions;
- generated API reference is built from the same commit as authored prose;
- failed documentation or example gates prevent publication;
- development records and build intermediates are not published;
- hosting-provider-specific deployment remains a thin CI adapter and does not
  shape the source tree.

No separate documentation-versioning framework is required for the first
release. CI path versioning is sufficient until multiple supported release
lines make a version switcher valuable.

## 15. Documentation Stage Workflow

Each stage follows the same workflow:

1. read the relevant design specs, landed summaries, public headers, Kconfig,
   generators, and tests;
2. inventory exact claims and identify implementation/documentation conflicts;
3. create or refine the canonical buildable example;
4. write public API comments and authored pages together;
5. build docs continuously with warnings fatal;
6. compile and run examples and focused subsystem tests;
7. perform the stage-specific user-path and architecture checks;
8. update the coverage matrix;
9. write a concise landed documentation summary.

Minor implementation defects discovered while documenting may be fixed and
recorded in the stage summary. A conflict that would materially reshape a
public API or later documentation stages pauses progression for design input.

Documentation landed summaries live under:

```text
docs/development-docs/documentation-pass/landed/
```

The active coverage matrix lives at:

```text
docs/development-docs/documentation-pass/coverage.md
```

## 16. Implementation Stages

### Stage 0: Contract inventory and cleanup

Objective: establish exact scope before writing public prose.

Deliverables:

- create the documentation-pass coverage matrix;
- map every public aggregate header, namespace, subsystem, Kconfig group,
  generator, example candidate, design spec, and landed summary;
- classify public, advanced-public, and internal declarations;
- identify stale README claims and duplicate/dead documentation;
- define terminology, capitalization, code style, labels, and page templates;
- record implementation conflicts requiring correction.

Gate: every planned page and public subsystem has an owner and source set.

### Stage 1: Toolchain and site skeleton

Objective: make documentation a first-class build product.

Deliverables:

- add Sphinx, MyST, Doxygen, Breathe, and optional diagram dependencies;
- add `docs/CMakeLists.txt`, `conf.py`, `Doxyfile.in`, requirements, theme, and
  static assets;
- establish navigation, labels, substitutions, API groups, and generated-output
  directories;
- add warning-fatal HTML and separate link-check targets;
- add CI artifact build;
- publish a skeletal page in every top-level section.

Gate: a clean environment builds the complete skeleton with zero warnings.

### Stage 2: Executable first application and getting started

Objective: provide the shortest trustworthy path to a successful Solar build.

Deliverables:

- add `examples/first-application`;
- replace the stale top-level README with a concise current project entry;
- write requirements, module installation, configuration, first application,
  boot Result, and next steps;
- document native simulation setup and common setup failures;
- establish literal-include and example-region conventions.

Gate: a fresh workspace can follow the guide and pass the native example.

### Stage 3: Core model and system composition

Objective: teach the language used by every later subsystem.

Deliverables:

- add the system-composition example;
- document System/Blueprint/binding, components, dependencies, contributions,
  descriptors, identity, catalogs, lifecycle, Result/errors, concurrency,
  availability, and strict/relaxed behavior;
- add API comments and Breathe pages for core, component, catalog, system, and
  lifecycle public surfaces;
- write static-system, normalization, catalogs/binding, and lifecycle-engine
  architecture pages.

Gate: readers can predict composition, boot ordering, ownership, and failure
shape without reading implementation templates.

### Stage 4: Kernel, execution, and hardware foundations

Objective: document Solar's typed Zephyr foundations and runtime ownership.

Deliverables:

- document Kernel primitives and native handles;
- document tasks, registrations, executors, services, workqueues, cancellation,
  and containment;
- document devicetree-first hardware endpoints and supported driver surfaces;
- add API comments/reference and the related architecture pages;
- add generation and ISR/concurrency how-to guides;
- establish compile-only hardware example gates.

Gate: a user can choose the correct primitive, executor, or hardware wrapper and
state its context and lifetime rules.

### Stage 5: Data and observability facilities

Objective: document the facilities commonly combined in application logic.

Deliverables:

- add the data-pipeline example;
- document Bus, Parameters, Events, Metrics, and Logging in dependency order;
- cover declarations, contributions, persistence, processors, reducers, sinks,
  formatting, policies, capacities, backpressure, ISR paths, and typed errors;
- add subsystem API pages, Kconfig guidance, and storage architecture.

Gate: the integrated example builds/runs and each facility satisfies the
standard subsystem page contract.

### Stage 6: Remote and host integration

Objective: make Solar's primary development and host-control surface fully
usable from documentation.

Deliverables:

- add the remote-control example and host script;
- document links, sessions, schemas, data capabilities, queries, streams,
  actions, topics, inbound paths, thread-safe collection, buffering,
  backpressure, and execution;
- document manifest/client generation and compatibility;
- publish protocol vectors and generated-artifact reference;
- write Remote runtime and data-flow architecture.

Gate: a reader can build firmware, generate host artifacts, connect, query,
invoke an action, and consume a stream using only the tutorial and reference.

### Stage 7: Inspection, health, and supervision

Objective: document diagnosis, evidence, recovery, and safety response.

Deliverables:

- add the supervised-device example;
- document Inspection's narrow query role;
- document component health assessment, reports, progress, history, and checks;
- document Supervisor policy, thread monitoring, recovery, safe state, and
  watchdog provider integration;
- write diagnosis and boot-failure how-to guides;
- write health/Supervisor ownership and response-flow architecture.

Gate: the example demonstrates a fault, recorded evidence, attempted recovery,
and deterministic policy response.

### Stage 8: Complete generated and configuration reference

Objective: make non-C++ inputs as searchable and precise as the API reference.

Deliverables:

- generate and annotate the complete Kconfig reference;
- document devicetree inputs and generated hardware aliases from controlled
  fixtures;
- complete Remote protocol and generated-file reference;
- add compatibility, target constraints, and disabled-feature matrices;
- cross-link Kconfig, devicetree, C++ declarations, and subsystem pages.

Gate: every supported configuration and generated input is reachable from both
its subsystem and reference index.

### Stage 9: Architecture and contributor completion

Objective: complete the maintainer path without leaking incidental internals
into user documentation.

Deliverables:

- complete runtime ownership, subsystem storage, failure containment, shutdown,
  code-generation, and cross-subsystem dependency pages;
- write contributor setup, testing, style, documentation, extension, and
  release/versioning guides;
- add maintainable diagrams and source links;
- document how to add a component category and subsystem.

Gate: architecture traces agree with code ownership and tests, and a contributor
can make and verify a small extension from the guide.

### Stage 10: Integrated audit and publication

Objective: close the pass as a coherent product rather than a pile of pages.

Deliverables:

- run every build, example, link, API, configuration, and publication gate;
- perform fresh-user, task, Zephyr-native, architecture, future-claim, and
  responsive-layout audits;
- remove duplication, orphan pages, placeholders, and stale terms;
- verify search terms and cross-navigation for all subsystems;
- record known target-only gates honestly;
- enable versioned publication and write the documentation handoff summary.

Gate: all completion criteria below are satisfied.

## 17. Coverage Matrix

The Stage 0 matrix tracks, at minimum:

| Topic | Public page | API group | Kconfig | Devicetree/generated | Example | Architecture | Tests | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |

Rows exist for core/system, lifecycle, catalog, Kernel, execution, hardware,
Bus, Parameters, Events, Metrics, Logging, Remote, Inspection, Health, and
Supervisor. A row is complete only when links resolve and its evidence gate
passes.

The matrix is a development artifact, not a public feature table.

## 18. Completion Criteria

The documentation pass is complete only when:

- the site builds from a clean environment with warnings fatal;
- all internal cross-references and literal includes resolve;
- every public subsystem has a complete landing page and API group;
- all public aggregate headers and intended public declarations are covered;
- every Kconfig symbol and supported generated input is documented;
- all five canonical examples compile, and all runnable native examples pass;
- the first-application and Remote tutorials pass fresh-user audits;
- errors, ISR rules, lifetime, capacity, backpressure, and disabled behavior are
  stated wherever they affect use;
- strict and relaxed binding are both documented and tested;
- architecture identifies canonical state and synchronization owners;
- no deferred feature is presented as current behavior;
- README, site version, and compatibility statements agree;
- CI produces the site artifact and release publication is reproducible;
- the final coverage matrix has no required incomplete rows;
- a landed summary records commands, evidence, accepted caveats, and future
  documentation maintenance responsibilities.

## 19. Explicit Non-Goals

This pass does not:

- redesign public APIs merely to make them easier to explain;
- publish development specs as user documentation;
- create an in-firmware CLI or serial-text workflow that Solar does not own;
- promise advanced SoC-specific DMA wrappers;
- document the deferred coroutine runtime as current capability;
- build a full hand-written reference that duplicates Doxygen declarations;
- require live physical hardware for pages whose accepted evidence is
  compile-only, provided that limitation is explicit;
- add a sophisticated multi-version frontend before multiple supported release
  lines exist.

## 20. First Action

Implementation begins with Stage 0, not with prose drafting. The first change
creates the coverage matrix, inventories the actual public surface and stale
claims, and resolves terminology. Stage 1 then lands the documentation build so
every following page and API comment is verified continuously.
