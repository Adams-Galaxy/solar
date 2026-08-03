# Define Components

Define an ordinary type with a component descriptor, optional dependencies, and
optional lifecycle hooks:

```cpp
struct Imu {
    static constexpr solar::component::Descriptor descriptor{.name = "imu"};
    using Dependencies = solar::TypeList<SpiBus>;

    static solar::Result<void, ImuError> init() noexcept;
};
```

Own the type once in a `solar::Compose` expression. Put generated declaration
roles in a `solar::Contributes<Component, Role>` entry, keeping contract
existence separate from behavior ownership.
