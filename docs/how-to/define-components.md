# Define Components

Define an ordinary type with a component descriptor, optional dependencies, and
optional lifecycle hooks:

```cpp
struct Imu {
    static constexpr solar::component::Descriptor descriptor{.name = "imu"};
    using Dependencies = solar::Dependencies<SpiBus>;

    static solar::Result<void, ImuError> init() noexcept;
};
```

Place the type in exactly one component category in the Blueprint. Add
declaration aliases such as `Events`, `Metrics`, or `Tasks` directly to the type
that owns those contributions.
