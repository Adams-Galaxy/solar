# Supervise A Device

The runnable `examples/supervised-device` application demonstrates the complete
fault path without physical hardware.

## Give the Device health meaning

```{literalinclude} ../../examples/supervised-device/src/main.cpp
:language: cpp
:start-after: // [device]
:end-before: // [device]
```

The Device owns connection knowledge and recovery behavior. Health invokes the
optional nested protocol; no separate signal type points back at the Device.

## Declare system response

```{literalinclude} ../../examples/supervised-device/src/main.cpp
:language: cpp
:start-after: // [policy]
:end-before: // [policy]
```

The first rule warns and attempts recovery. If recovery fails, the second rule
enters the application safe state. The Watchdog provider is fed only after an
acceptable completed supervision cycle.

The sample boots, injects a connection fault, wakes Supervisor, then verifies
the faulted Health record, recovery attempt, safe-state response record, and
watchdog activity:

```sh
west twister -T examples/supervised-device -p native_sim/native/64 --inline-logs
```

On hardware, replace the fake provider with a board-owned Watchdog adapter and
make safe-state entry directly de-energize outputs. Do not defer urgent
containment solely to the periodic Supervisor service.
