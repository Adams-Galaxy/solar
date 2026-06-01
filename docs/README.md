# Solar Documentation

Solar is a static-first C++20 runtime layer for robotics firmware. A robot is declared as a compile-time graph, validated by concepts and static assertions, then instantiated as tuple-owned runtime objects.

This documentation set is the working reference for Solar. The code is under active development, so these documents describe the current intended shape and should evolve with each architectural slice.

## Start Here

- [Architecture](architecture.md): high-level shape and design philosophy.
- [System Graph](concepts/system-graph.md): `System`, graph containers, names, dependencies, and context lookup.
- [Facilities And Services](concepts/facilities-services.md): passive facilities vs active threaded services.
- [Contributions](concepts/contributions.md): automatic static catalogs for metrics, events, and Remote vocabulary.
- [Entry And Profiles](entry-and-profiles.md): robot/sim profiles and Solar-owned boot.
- [Low Level Boundary](low-level.md): how Solar stays portable across Teensy, simulation, and future backends.

## Runtime Subsystems

- [Logging](observability/logging.md): direct static logger with typed sources, sinks, filters, and formats.
- [Events](observability/events.md): typed event descriptors, fixed history, and direct sinks.
- [Metrics](observability/metrics.md): typed observable state, units, policies, timers, and groups.
- [Remote](remote.md): binary protocol, generated schema, descriptors, and service shape.
- [RTOS](rtos.md): stable RTOS API over low-level FreeRTOS/simulated implementations.

## Project Conventions

- [Style And Naming](style-and-naming.md): namespace, type, and API conventions used by Solar.
- [Documentation Guide](documentation-guide.md): how to document Solar components as the package grows.
