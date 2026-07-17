# Parameters API

Include `<solar/parameters.hpp>` with `CONFIG_SOLAR_PARAMETERS=y`.

```cpp
template <typename... T> struct Parameters;
template <typename... T> struct Changes;

template <typename Parameter> auto get();
template <typename Parameter> auto set(typename Parameter::Value value);
template <typename Parameter> auto get_isr();
template <typename... Assignments> auto set_all(Assignments... values);
```

Validation, storage, access, persistence, transaction, and delay policy types
are declared in the `solar::parameters` namespace. See
{doc}`../../subsystems/parameters`.
