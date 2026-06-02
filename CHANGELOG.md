# Changelog

## 0.1.0

Initial active-development release.

- Static `solar::System` graph with board, peripherals, devices, facilities, services, tasks, channels, and runtime policy.
- Typed names, dependency validation, and contribution catalogs.
- Passive facilities for events, metrics, and inspection.
- Active service model with threaded `run(ctx, stop_token)` services.
- Direct logging with typed sources, sinks, filters, and formats.
- Event descriptors, fixed history, and direct event sinks.
- Metric descriptors, user-defined units, policies, timers, groups, and snapshots.
- Binary Remote protocol/service with generated core schema descriptors.
- Public `solar::rtos` facade over Zephyr kernel primitives.
- Zephyr application entry support and native simulator preparation.
