# Entry And Profiles

Applications define profiles in project-owned headers such as `firmware/include/app/robot.hpp` and `firmware/include/app/simulated.hpp`.

A profile exports:

```cpp
struct Robot
{
    using System = solar::System<...>;

    static void awake(System& system, solar::BootReport const& report);
    static void failed(System& system, solar::BootReport const& report);
};
```

Solar entry owns construction and boot. User code describes the system; it does not manually call `System::Boot()` in normal entry paths.

## Hooks

All hooks are optional:

- `preflight()`: runs before simulated system construction; useful for host sockets, files, or external system setup.
- `awake(System&, BootReport const&)`: runs after successful Solar boot.
- `failed(System&, BootReport const&)`: runs after failed Solar boot.
- `finished(System&)`: simulated runner completion policy.
- `exit_code(System const&)`: simulated runner exit code policy.

## Firmware Entry

Arduino/Teensyduino firmware can use:

```cpp
SOLAR_ARDUINO_ENTRY(app::Robot)
```

The generated `setup()` initializes profile facilities and boots the system. The generated `loop()` only yields; services own ongoing behavior through their threads.

## Simulated Entry

Host simulation can call:

```cpp
return solar::entry::run<app::Simulated>();
```

The simulated runner performs preflight, facility init/start, system boot, and then waits until `finished(system)` returns true or the process is interrupted by project code.
