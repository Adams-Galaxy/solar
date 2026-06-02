# Entry And Profiles

Applications define profiles in project-owned headers such as
`firmware/include/app/robot.hpp`.

A profile exports:

```cpp
struct Robot
{
    using System = solar::System<...>;

    static void awake(System& system, solar::BootReport const& report);
    static void failed(System& system, solar::BootReport const& report);
};
```

Solar entry owns construction and boot. User code describes the system; it does
not manually call `System::Boot()` in normal entry paths.

## Hooks

All hooks are optional:

- `preflight()`: runs before `System` construction for project-specific setup.
- `awake(System&, BootReport const&)`: runs after successful Solar boot.
- `failed(System&, BootReport const&)`: runs after failed Solar boot.
- `exit_code(System const&)`: returns the process exit code for host-capable targets.

## Zephyr Entry

Zephyr applications use a normal `main()`:

```cpp
int main()
{
    return solar::entry::run_zephyr<app::Robot>();
}
```

Zephyr performs kernel and board startup before `main()`. Solar then initializes
profile facilities, constructs the static system graph, boots graph components,
starts service threads, and dispatches `awake` or `failed`.
