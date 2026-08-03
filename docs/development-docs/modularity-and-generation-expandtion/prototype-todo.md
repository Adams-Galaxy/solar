# Prototype Implementation And Test Plan

Date: 2026-08-03

Status: complete; evaluated in `prototype-evaluation.md`

## 1. Purpose

These prototypes validate the remaining API choices before Solar's production
facilities and System are migrated. They are thin vertical slices, not partial
rewrites of every existing subsystem.

Prototype code should be isolated behind an experimental namespace, target, or
fixture so current production behaviour remains available while the model is
evaluated.

## 2. Global Gates

The prototype phase is complete only when it demonstrates:

- one implementation shared by object and typed module ownership;
- no dependency on System for standalone module operations;
- one static application and one static System;
- no runtime context-object propagation through services;
- no strict/relaxed binding path in the prototype;
- schemas generated before System composition;
- no duplicate handwritten firmware/host schema;
- deterministic and source-located generation;
- generated application-specific Python models and endpoint namespaces;
- exact-interface validation and useful mismatch diagnostics;
- get, set, call, output stream, and input stream behaviour end to end;
- dynamic and generated Python access sharing one session; and
- a successful `native_sim` to generated-client integration test.

## 3. Phase 0: Prototype Scaffolding

- [x] Choose an isolated prototype source and test location.
- [x] Add a host compile target for standalone module tests.
- [x] Add a small Zephyr `native_sim` prototype application.
- [x] Add a Python test package for generated-client fixtures.
- [x] Record generated output under the build tree, not normal source.
- [x] Make generated include paths visible in compile commands.
- [x] Add a prototype README stating that names are not yet public API.
- [x] Preserve the current implementation and tests during the experiment.

Gate:

- [x] Existing supported C++ and Python tests still run independently of the
      prototype targets.

## 4. Phase 1: Standalone Parameter Module

Implement a minimal parameter module first because it exercises storage,
validation, identity, typed access, and System lifecycle integration.

- [x] Define a small experimental parameter `Schema`.
- [x] Implement an explicitly owned `Store<Schema>`.
- [x] Support initialization, `get`, `set`, validation, and revision.
- [x] Keep storage statically bounded and allocation-free.
- [x] Make synchronization ownership explicit.
- [x] Ensure parameter core headers do not include System, lifecycle, execution,
      Remote, logging, or persistence headers.
- [x] Implement a typed static facade such as
      `StaticStore<Application, Schema>`.
- [x] Make the facade forward to exactly one `Store<Schema>` implementation.
- [x] Verify two object stores are independent.
- [x] Verify two application tags produce independent static stores.
- [x] Verify one application tag produces one canonical static store.
- [x] Add compile-fail coverage for a parameter absent from the schema.
- [x] Inspect generated symbols and storage to confirm there is no duplicate
      implementation or state.

Gate:

- [x] A host program can use the store without including or constructing a
      Solar System.
- [x] Application code can call an explicit typed API such as
      `app::parameters::set<DriveKp>(...)`.

## 5. Phase 2: Minimal Generic Composer

- [x] Define the smallest useful experimental module traits or concept.
- [x] Define `Compose<...>` aggregation without subsystem-specific branches.
- [x] Prototype variadic `Own<Module...>`.
- [x] Initialize and stop the parameter module through `Own`.
- [x] Reject duplicate ownership at compile time.
- [x] Reject missing module dependencies at compile time.
- [x] Implement one explicit named `Connect<Adapter, Endpoint...>` fixture.
- [x] Prove a third-party fixture module composes without editing System.
- [x] Compare explicit-adapter syntax against an unambiguous shorthand and
      retain the version with better diagnostics.
- [x] Verify System remains a non-instantiated static type.

Gate:

- [x] The generic composer contains no hard-coded parameter-specific branch.
- [x] A third-party counter or fixture module participates in lifecycle and one
      connection using the same contracts as the parameter module.

## 6. Phase 3: Minimal IDL Compiler And IR

Support only the smallest contract needed for the end-to-end prototype:

- one scalar parameter;
- one enumeration;
- one structure;
- one action;
- one output stream; and
- one input stream.

Tasks:

- [x] Select the prototype source syntax and strict parser.
- [x] Preserve file, line, and declaration source locations.
- [x] Define a normalized, language-independent Solar IR.
- [x] Parse project metadata and imported interface files.
- [x] Resolve names and reject duplicate declarations.
- [x] Validate type references and bounded representations.
- [x] Assign deterministic temporary IDs for the prototype.
- [x] Add the first interface lock-file format.
- [x] Preserve existing IDs across source reordering.
- [x] Reject retired-ID reuse and incompatible changes.
- [x] Emit a deterministic JSON form of the normalized IR for inspection.
- [x] Add a generator version to all artifacts.
- [x] Emit dependency information for imported interface files.

Tests:

- [x] Golden parse/IR test.
- [x] Byte-for-byte deterministic regeneration test.
- [x] Unknown type diagnostic with source location.
- [x] Duplicate name and ID diagnostics.
- [x] Invalid default and bounds diagnostics.
- [x] Rename and lock-file stability test.
- [x] Breaking-change classification test.

Gate:

- [x] All code-generation backends can consume the normalized IR without
      reading source YAML directly.

## 7. Phase 4: Generated C++ Application Schema

- [x] Generate the prototype enum and structure as readable C++ types.
- [x] Generate descriptors and stable IDs.
- [x] Generate the parameter schema.
- [x] Generate action and stream declarations.
- [x] Generate a project/application contribution header.
- [x] Include the generated parameter schema in the Phase 1 store.
- [x] Integrate generation before C++ compilation.
- [x] Expose the generated include directory to clangd.
- [x] Add a manual generation command suitable for IDE refresh.
- [x] Reject generated declarations that exceed configured hard bounds.
- [x] Confirm no handwritten duplicate of the generated schema remains in the
      fixture application.

Gate:

- [x] Editing the IDL and rebuilding updates both the generated C++ type and its
      module schema before System composition.

## 8. Phase 5: Component Participation API

Create two service fixtures:

1. an application-specific service using `app::parameters`; and
2. a reusable service parameterized by module types or a narrow capability
   bundle.

Tasks:

- [x] Prototype parameter participation such as `Uses`, `Reads`, or `Writes`.
- [x] Prototype exactly-one action handling.
- [x] Prototype output-stream publishing.
- [x] Prototype input-stream consumption.
- [x] Keep declarations of existence solely in generated schema.
- [x] Validate component roles during System composition.
- [x] Reject a missing action handler.
- [x] Reject two exclusive action handlers.
- [x] Reject a component reference to an undeclared schema item.
- [x] Compare direct application access with reusable typed-module injection.
- [x] Record include dependencies and error quality for both forms.

Gate:

- [x] Application-specific service code remains at least as readable as the
      current contribution-based API.
- [x] Reusable services require no runtime context object.

## 9. Phase 6: Generated Python Package Over A Fake Session

- [x] Generate frozen slotted dataclasses for object schemas.
- [x] Generate closed and open enum models.
- [x] Generate nested namespaces from dotted Solar names.
- [x] Generate typed parameter `get` and `set` wrappers.
- [x] Generate callable action wrappers.
- [x] Generate output-stream subscription wrappers.
- [x] Generate input-stream producer wrappers.
- [x] Retain endpoint descriptors on every generated wrapper.
- [x] Bind the generated client to an existing `solar_remote` session.
- [x] Share the session codec and subscription router with dynamic access.
- [x] Embed protocol and interface metadata.
- [x] Implement exact-interface validation.
- [x] Implement one compatible-additive validation case.
- [x] Produce semantic mismatch diagnostics.
- [x] Generate package metadata with a compatible `solar-remote` dependency.

Tests:

- [x] Python type-check generated public examples.
- [x] Round-trip generated models through the standard SDK codec.
- [x] Fake-session parameter get/set test.
- [x] Fake-session action request/response test.
- [x] Output-stream async context and cancellation test.
- [x] Input-stream credit/send test.
- [x] Generated and dynamic access concurrency test.
- [x] Exact manifest acceptance and rejection tests.
- [x] Additive compatible-manifest test.
- [x] Invalid dotted-name/path collision test.
- [x] Verify generated package contains no transport implementation.

Gate:

- [x] The generated API feels like native async Python and delegates all
      protocol work to `solar_remote`.

## 10. Phase 7: Final-ELF And Shipment Generation

- [x] Emit generated declaration metadata into the firmware fixture.
- [x] Produce the effective manifest from the linked ELF.
- [x] Verify the linked manifest against the generated source contract.
- [x] Generate exact Python artifacts from the effective manifest.
- [x] Emit protocol, interface, and build identities separately.
- [x] Generate `shipment.json` and a compatibility report.
- [x] Build an installable application-client wheel.
- [x] Install the wheel into an isolated test environment.
- [x] Bind it to a fake session reporting the shipment manifest.
- [x] Reject a mismatched exact build only when exact-build policy is selected.

Gate:

- [x] One firmware build produces a self-consistent firmware manifest, client
      package, digest set, and shipment description.

## 11. Phase 8: `native_sim` End-To-End Test

- [x] Build the generated-schema fixture for `native_sim`.
- [x] Start Remote over the existing simulator channel.
- [x] Open the channel through the accepted host ownership boundary.
- [x] Start one standard `solar_remote` session.
- [x] Bind the generated client to that session.
- [x] Get and set the generated parameter.
- [x] Call the generated action.
- [x] Consume the generated output stream.
- [x] Send through the generated input stream.
- [x] Use dynamic access concurrently with generated access.
- [x] Restart the simulator and verify session-epoch/rebind behaviour.
- [x] Capture meaningful diagnostics for an intentionally incompatible client.

Gate:

- [x] The same authored IDL drives the firmware types, effective manifest,
      standard SDK codec, and generated Python API with no duplicate schema.

## 12. Phase 9: Evaluation And API Lock

Do not migrate production facilities until this evaluation is complete.

- [x] Compare final object/static module names.
- [x] Select final `Compose`, `Own`, and `Connect` syntax.
- [x] Select contribution-role vocabulary.
- [x] Select project manifest and IDL file naming.
- [x] Select generated include and namespace conventions.
- [x] Select generated Python package and namespace conventions.
- [x] Select exact-versus-compatible default binding policy.
- [x] Review compile-time diagnostics and template depth.
- [x] Review host and Zephyr code size and static storage.
- [x] Review build latency and incremental regeneration.
- [x] Review IDE completion for generated C++ and Python.
- [x] Record rejected alternatives and the evidence for rejection.
- [x] Update these design documents with final API examples.
- [x] Write a staged production migration plan.

Final gate:

- [x] The remaining API decisions are documented and supported by working C++,
      Python, host, and `native_sim` evidence.

## 13. Production Work Explicitly Deferred

The prototype phase does not yet authorize:

- rewriting all existing facilities;
- removing current binding support from production code;
- changing every application example;
- implementing the complete IDL type system;
- publishing generated packages;
- adding third-party generator plugins; or
- deleting the current Remote manifest path.

Those actions follow the API lock and production migration plan.
