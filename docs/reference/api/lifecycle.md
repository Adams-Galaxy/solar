# Lifecycle API

```cpp
template <typename Application = DefaultApplication>
[[nodiscard]] auto solar::boot() noexcept;

[[nodiscard]] auto solar::stop() noexcept;
```

The global functions operate on the application System selected by
`solar::binding::Traits`. The same operations are available directly as
`System::boot<Application>()` and `System::stop()`.

Components may define any of these static hooks:

```cpp
static auto init() -> solar::Result<void>;
static auto start() -> solar::Result<void>;
static auto stop() -> solar::Result<void>;
static auto deinit() -> solar::Result<void>;
```

Missing hooks are optional. Lifecycle invokes present hooks in dependency
order, records every attempt, and rolls completed work back in reverse order
when boot fails. See {doc}`../../concepts/lifecycle` and
{doc}`../../how-to/diagnose-boot`.
