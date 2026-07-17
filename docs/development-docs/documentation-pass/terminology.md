# Documentation Terminology

Status: locked for first public pass

## Product And Platform

- **Solar**: the framework and Zephyr module. Capitalize the product name.
- **Zephyr**: the RTOS and build ecosystem Solar integrates with.
- **C++23**: Solar's required C++ language/library baseline.
- **application**: a firmware application using Solar.
- **host**: software communicating with firmware through Remote.

## Composition

- **System**: the user-facing static type produced by `solar::System<Blueprint>`.
  Do not describe it as a runtime object or context.
- **Blueprint**: the compile-time declaration of application components,
  catalogs, execution registrations, and subsystem configuration.
- **application binding**: the specialization selected by
  `SOLAR_BIND_SYSTEM` or `SOLAR_BIND_SYSTEM_FOR`.
- **component**: a type participating in the System graph and optionally in
  lifecycle, health, identity, and contributions.
- **device**: an application component representing meaningful hardware-backed
  behavior. It is distinct from a raw Zephyr device or Solar hardware endpoint.
- **facility**: passive system capability that may own storage and lifecycle but
  does not imply a dedicated thread.
- **service**: one-per-System active component with contained execution.
- **executor**: an execution component that accepts work registrations.
- **task** or **work registration**: a leaf unit scheduled through execution;
  it is not a component category.

## Compile-Time Model

- **contribution**: declarations exposed by a component for collection into a
  subsystem catalog.
- **catalog**: validated compile-time collection of declarations for one role.
- **descriptor**: compile-time identity and metadata attached to a declaration.
- **stable ID** and **local ID**: wire/stable identity and compact System-local
  identity respectively; never use them interchangeably.
- **strict binding**: compile-time frontend dispatch requiring registered types.
- **relaxed binding**: table-backed frontend dispatch with runtime availability
  errors; the default prototyping mode.

## Runtime And Errors

- **lifecycle**: ordered init, start, stop, and deinit opportunities plus state
  and reports. Hooks are optional; lifecycle participation is not a base class.
- **Kernel**: Solar's typed C++ surface over Zephyr kernel primitives. Capitalize
  when naming the Solar subsystem; use lowercase for generic kernel concepts.
- **execution**: System integration for registrations, executors, tasks,
  services, and workqueues.
- **Result**: `solar::Result<T, E>`, Solar's constrained alias of
  `std::expected`.
- **error**: a concrete `ErrorType` value carrying domain information.
- **Status**: broad cross-domain classification, not a normal fallible return.
- **availability**: whether a frontend/subsystem operation can currently be
  used. Disabled, unbound, and unregistered are distinct states.
- **capacity**: compile-time/Kconfig bounded storage or concurrency limit.
- **backpressure**: the declared response when bounded downstream capacity is
  unavailable.

## Subsystems

- **Bus**, **Parameters**, **Events**, **Metrics**, **Logging**, **Remote**,
  **Inspection**, **Health**, and **Supervisor** are capitalized when naming a
  Solar subsystem.
- **Remote**: Solar's typed host communication subsystem, not a CLI.
- **Inspection**: bounded query access to selected internal state; not a broad
  observability umbrella.
- **Health**: evidence and assessments about subjects.
- **Supervisor**: the service that evaluates evidence and applies response
  policy.
- **hardware endpoint**: a typed wrapper around a devicetree-backed Zephyr
  driver operation.
- **devicetree**: Zephyr's hardware-description mechanism. Use Zephyr's spelling
  as one lowercase word.
- **workqueue**: use Zephyr's one-word spelling.

## Prohibited Or Misleading Terms

- Do not call System storage a context, singleton object, or service locator.
- Do not call a service instance a task.
- Do not call Status an exception or error hierarchy.
- Do not call Inspection a snapshot API.
- Do not imply Solar owns a serial console, shell, or text CLI.
- Do not describe generated devicetree aliases as portable across boards.
- Do not use "automatic" without naming the responsible compile-time rule,
  Kconfig default, or runtime owner.
