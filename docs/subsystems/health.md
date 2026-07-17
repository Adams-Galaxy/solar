# Health

Health owns bounded evidence and assessment truth for System subjects. Every
effective component receives a subject automatically when Health is enabled;
custom participation is optional.

## Common path

Components can push domain knowledge directly:

```cpp
solar::health::report<Imu>(solar::health::degraded(error));
solar::health::report<Imu>(solar::health::nominal());
```

ISR reporting uses compact no-wait observations and a bounded ingress queue.
Overflow is recorded; it is never silently converted to nominal state.

## Component assessment

For pull assessment and recovery, add a nested declaration:

```cpp
struct Imu {
    struct Health {
        using Checks = solar::health::Checks<Connection,
                                               solar::health::Progress<100_ms>>;
        static solar::Result<solar::health::Assessment> assess();
        static solar::Result<void> recover();
    };
};
```

A successful `faulted()` assessment means the check ran and found a fault. A
failed `Result` means assessment itself could not run; Health preserves that
difference.

Named checks add focused evidence without separate circular signal types.
Generic monitors cover lifecycle, execution, progress freshness, stack margin,
and selected subsystem records. Components advance `health::progress<T>()`
only after useful work, not merely because their thread woke.

## Assessment shape

Condition, liveness, readiness, safety, and freshness remain orthogonal.
Evidence records source, direct/reported/inferred quality, availability, time,
and error. Transition history is bounded and reports overwrite loss.

Health records facts and interpretation; it does not decide system response.
See {doc}`supervisor` and {doc}`../reference/api/health`.
