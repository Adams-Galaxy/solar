# Logging

Solar Logging captures structured, bounded records and routes formatted output
to explicit sinks. It intentionally replaces Zephyr Logging as the application
logging frontend while providing an optional Zephyr ingress bridge.

```cpp
auto captured = solar::log::notice<Controller>(
    solar::log::correlated(request_id), "target changed to {}", value);
```

Levels are `Trace`, `Debug`, `Info`, `Notice`, `Warning`, `Error`, and
`Critical`. `Notice` marks operationally notable information without implying
a warning.

## Sources, domains, and filtering

Components are automatic log sources and may contribute additional
`LogSources`. A call may select a domain. Compile-time filtering removes
disabled sites; runtime source, domain, and sink thresholds can filter
captured records without recompilation.

## Capture and formatting

Format strings are compile-time checked on the normal path. Arguments are
encoded into bounded ingress storage and rendered later by the Logging
Facility, keeping producers independent from sink latency. `text()` and
`hexdump()` provide bounded dynamic alternatives.

ISR calls use `try_*_isr`, never block, and admit only the supported bounded
argument forms. Elevated reserve protects higher levels under ordinary ingress
pressure. Every capture reports captured, filtered, elevated, or dropped
disposition.

## Sinks and panic

Configure sinks with `log::To<Sink, MinimumLevel<...>, ...>`. A sink consumes a
`RecordView` plus rendered text and owns its external transport. Retained
history is a built-in bounded sink. Panic-safe sinks may participate in the
restricted fatal path; ordinary asynchronous draining is not assumed there.

Event-to-Log adapters turn selected Events into records while preserving event
origin and correlation. See {doc}`../reference/api/logging` and
{doc}`../how-to/add-log-sink`.
