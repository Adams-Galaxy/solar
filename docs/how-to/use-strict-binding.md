# Use Strict Binding

Enable strict compile-time frontend binding:

```text
CONFIG_SOLAR_STRICT_CATALOG_BINDING=y
```

The System binding and normal subsystem calls do not change. Unregistered types
that would produce a runtime availability error in relaxed mode become compile
diagnostics. Use strict mode in larger applications and release builds when the
stronger catalog contract is worth longer, more coupled template diagnostics.

Maintain at least one strict build in CI even when ordinary development uses
relaxed mode.
