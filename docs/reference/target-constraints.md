# Target Constraints

Solar exposes the capabilities Zephyr provides for the configured target. A
portable declaration can therefore compile differently when a board lacks the
required driver API, RTIO adapter, stack diagnostics, runtime statistics, or
thread monitoring support.

Use these gates:

1. Kconfig validates global feature dependencies.
2. Devicetree and bindings validate endpoint shape.
3. C++ concepts/static assertions validate typed declarations.
4. `ready()` and typed driver results validate runtime device state.
5. Target compile/link fixtures prove real driver integration.
6. Physical tests prove electrical, timing, and transport behavior.

Do not infer the sixth gate from the fifth. Record unavailable physical
evidence explicitly in releases and incident reports.
