# Solar Modularity And Generation Expansion

Date: 2026-08-03

Status: production implementation active; phases 0-7 verified, standalone
module migration active

## Purpose

This directory records the accepted direction for making Solar a modular modern
C++ framework over Zephyr and making generation a first-class part of the
developer experience.

The central outcome is:

> An application declares its contracts once. Solar generates the repetitive
> C++, exact firmware interface metadata, and a native application-specific
> Python client. Handwritten C++ remains responsible for application behaviour.

Solar will retain one canonical static application and one static System. The
redesign does not introduce a runtime System object or require application code
to pass a context object through every service.

## Documents

- [Production implementation plan](implementation-plan.md) is the governing,
  gated execution script. It defines phase order, evidence requirements,
  deletion gates, drift guards, and the current progress state.
- [Module ownership convention](module-ownership.md) defines direct objects,
  typed static facades, bounded capacity, and generic System ownership.
- [Locked architectural decisions](architecture.md) defines standalone modules,
  application ownership, static composition, contributions, and the eventual
  removal of strict and relaxed binding.
- [Project manifest, IDL, and generation](generation.md) defines authored input,
  normalized representation, generated artifacts, identity, and the build
  pipeline.
- [Generated Python application clients](generated-python-client.md) defines the
  boundary between `solar_remote`, Station, and generated project packages.
- [Prototype implementation and test plan](prototype-todo.md) records the
  completed experimental checklist and its original validation gates.
- [Prototype evaluation and initial API lock](prototype-evaluation.md) records
  the tested decisions, evidence, rejected alternatives, and staged production
  migration.
- [Phase 0 baseline and prototype disposition](phase-0-baseline.md) records the
  supported toolchains, objective baseline, resource measurements, and the fate
  of each prototype artifact.

The implementation plan governs execution. The architecture, generation, and
client documents explain the decisions it implements. The prototype checklist
and evaluation are evidence inputs; they are not production APIs or a second
roadmap.

## Decision Status

The architecture and ownership boundaries in these documents are accepted.
The prototypes selected initial names and validated ergonomics. Production work
now follows the implementation plan; changing an accepted invariant requires
updating the relevant design document and the plan before code diverges.

The object/static facade convention, `Own`/`Connect` composition vocabulary,
contribution roles, versioned IDL, generated Python namespace, effective
shipment verification, and genuine wire-level streams are now production
shapes. The active gate is completing the remaining standalone modules and
their explicit adapters. Hardware migration and legacy deletion remain gated
behind that.

The exact-interface default, compatible-additive opt-in, and separate exact-build
policy are now accepted. Any remaining spelling decision must preserve the
constraints recorded here.
