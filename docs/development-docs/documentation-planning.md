# Solar Documentation Planning

Status: deferred until error-contract refinement and repository-wide alignment

This document records the intended shape of Solar's public and internal
documentation. It is a planning anchor, not yet the public documentation
itself.

The documentation pass should begin after the error and result convention is
finalized, migrated across the repository, and reflected in examples. This
avoids teaching an API vocabulary that is immediately replaced.

## Goals

Solar needs two complete but connected bodies of documentation:

1. **Using Solar**: everything an application author needs to configure,
   compose, use, test, and diagnose a Solar application.
2. **Understanding Solar**: the ownership, compile-time construction, runtime
   machinery, generated artifacts, subsystem boundaries, and engineering
   decisions needed to maintain or extend the framework.

These are audiences and navigation paths, not isolated documentation silos.
Public guides should link to deeper architecture when useful, while
architecture pages should link back to the public contracts they implement.

## Documentation Method

Solar should use the Diataxis model:

- **Tutorials** provide guided learning experiences with a concrete outcome.
- **How-to guides** solve a specific practical problem.
- **Reference** states exact API and configuration contracts.
- **Explanation** develops the mental model behind the system.

Pages should have one dominant purpose. A reference page should not turn into
a tutorial, and a tutorial should not attempt to enumerate every option.

## Proposed Toolchain

Use:

- **Sphinx** for authored documentation, navigation, cross-references, search,
  versioned output, and site generation;
- **Doxygen** to extract C++ declarations and API comments;
- **Breathe** to expose Doxygen output as Sphinx domains and directives;
- **MyST Parser** if Markdown remains the preferred authored format;
- Zephyr-compatible styling and conventions where they improve integration and
  familiarity.

Generated API reference supports authored documentation. It must not replace
conceptual descriptions, lifecycle rules, concurrency contracts, examples, or
Kconfig guidance.

## Proposed Public Structure

```text
docs/
  index.md
  getting-started/
  tutorials/
  guides/
  concepts/
  reference/
    api/
    kconfig/
    devicetree/
    generated/
  architecture/
  development/
  examples/
  development-docs/
```

Intended roles:

- `getting-started/`: installation, Zephyr module inclusion, first build, and
  the smallest complete Solar application;
- `tutorials/`: progressive applications that teach system composition and
  subsystem use;
- `guides/`: task-focused operational recipes;
- `concepts/`: components, facilities, services, tasks, catalogs, binding,
  lifecycle, execution, errors, identity, and ownership;
- `reference/`: precise public API, Kconfig, devicetree, protocol, generated
  schema, and compatibility contracts;
- `architecture/`: internal construction, storage, threading, code generation,
  subsystem internals, and cross-subsystem dependency direction;
- `development/`: contributor setup, tests, style, release process, supported
  toolchains, and extension guidance;
- `examples/`: buildable examples used by and linked from the prose;
- `development-docs/`: historical design inputs, accepted design specs,
  implementation records, future ideas, and active planning.

## Public Documentation Coverage

The first complete public pass should include:

- module installation and Zephyr workspace setup;
- Kconfig and devicetree integration;
- application Blueprint and system binding;
- component categories and ordinary include direction;
- lifecycle and boot behavior;
- kernel primitives and execution;
- bus, parameters, events, metrics, logging, and Remote;
- inspection, health, and supervision;
- hardware wrappers and generated hardware types;
- the `Result<T, E>` and error convention;
- strict and relaxed binding;
- concurrency, ISR, storage, and lifetime rules;
- testing applications and subsystem declarations;
- disabled-feature behavior;
- resource and target constraints.

Every subsystem should provide:

1. a short purpose and ownership statement;
2. a minimal common-path example;
3. declaration and contribution examples;
4. API reference;
5. Kconfig and generated-input reference;
6. lifecycle and availability behavior;
7. concurrency and ISR rules;
8. error behavior;
9. capacity and backpressure rules;
10. testing guidance;
11. links to relevant architecture.

## Architecture Coverage

Architecture documentation should explain:

- static system construction and why there is no runtime System object;
- Blueprint normalization and generated built-in facilities;
- contribution discovery and catalog construction;
- strict and relaxed frontend binding;
- distributed static ownership and initialization order;
- lifecycle graph traversal and records;
- Zephyr kernel and hardware boundaries;
- executor and service runtime ownership;
- subsystem canonical storage and synchronization;
- Remote protocol generation and host integration;
- health evidence and Supervisor policy flow;
- devicetree code generation;
- resource bounds, failure containment, and shutdown behavior.

Architecture pages should distinguish accepted contract from incidental
implementation detail. Source links and diagrams should be maintainable and
checked where practical.

## Source And Example Policy

- Public declarations should carry concise Doxygen comments for contract,
  parameters, return errors, thread context, ISR use, and lifetime.
- Authored pages should own explanations and walkthroughs.
- Examples shown in documentation should be compiled in CI whenever practical.
- Larger examples should live as buildable source and be included into prose,
  avoiding independently drifting copies.
- Examples should show the ergonomic common path first, then policy and
  advanced forms.
- Every error example should use the final repository-wide error convention.
- Zephyr-native names and concepts should be retained when Solar is wrapping a
  Zephyr facility rather than inventing a competing vocabulary.

## Navigation Principles

- A new user should reach a successful native build quickly.
- A subsystem should be discoverable by task, concept, and exact API name.
- Public and architecture pages should cross-link without requiring readers to
  understand internals for ordinary use.
- Kconfig symbols, devicetree inputs, generated types, C++ APIs, and protocol
  schemas should link to one another.
- Version and compatibility information should be visible from the top-level
  navigation.

## Planned Documentation Pass

When documentation work begins:

1. audit the finalized public headers and landed implementation summaries;
2. establish terminology, page templates, navigation, and cross-reference
   conventions;
3. scaffold Sphinx, MyST, Doxygen, and Breathe;
4. build a minimal site in CI;
5. write getting started and the core system mental model;
6. document subsystems in dependency order;
7. write architecture pages from accepted specs and landed evidence;
8. compile-test examples and check links;
9. add versioning and publication automation;
10. perform a user-path and contributor-path audit.

The accepted static-reform specifications and landed summaries are source
material. They should not simply be published as the user documentation.
