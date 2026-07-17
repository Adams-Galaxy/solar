# Parameters

Parameters are typed, named System variables with validation, access,
synchronization, change notification, and optional persistence.

```cpp
struct DriveKp {
    using Value = float;
    static constexpr solar::parameters::Descriptor descriptor{.name = "drive.kp"};
    static constexpr Value default_value = 1.0F;
    using Validation = solar::parameters::Range<0.0F, 10.0F,
                                                 solar::parameters::Clamp>;
};
```

Contribute with `using Parameters = parameters::Parameters<DriveKp>` or the
root `solar::Parameters<...>` section. Use `get<T>()`, `set<T>(value)`, and
transactional `set_all(assign<A>(...), assign<B>(...))`.

## Storage and access

The declaration selects immutable, atomic, spin-locked, or mutex-protected
storage. Ordinary access is thread-safe. Only parameters with a compatible
storage policy expose ISR reads or writes. Read-only and privileged parameters
enforce authority at compile time.

Validation may accept, reject, clamp, or adjust a candidate. `set()` returns
the committed value and revision, so callers do not need a second read to learn
what validation stored.

## Change hooks

Components opt in with `using ParameterChanges = parameters::Changes<T...>`
and overload `changed(const parameters::Change<T>&)`. Hooks run after commit
through bounded Execution registrations. They are suitable for invalidating a
local PID cache; they are not part of the storage lock transaction.

## Persistence

Persistence is declaration-selected: manual, immediate, deferred, or a
transactional group. Stores satisfy a small typed adapter and may use Zephyr
Settings. Stable IDs and versions are required for persisted declarations;
migration converts older payloads. Runtime commit and persistence failure are
reported separately, avoiding ambiguity about whether RAM changed.

See {doc}`../reference/api/parameters` and
{doc}`../how-to/persist-parameters`.
