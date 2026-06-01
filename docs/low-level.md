# Low Level Boundary

Solar does not call Arduino, FreeRTOS, POSIX, or simulator APIs directly from core runtime code. Those operations sit behind project-owned `low_level` facade headers.

Examples:

- `low_level/gpio.hpp`
- `low_level/serial.hpp`
- `low_level/rtos/thread.hpp`
- `low_level/rtos/sync.hpp`
- `low_level/rtos/time.hpp`

Build configuration selects one implementation:

- `LOW_LEVEL_TEENSYDUINO`: Teensyduino plus FreeRTOS.
- `LOW_LEVEL_SIMULATED`: host C++ simulation.

Solar public APIs remain stable:

```cpp
solar::rtos::Thread
solar::rtos::Queue<T, N>
solar::rtos::ThisThread::sleep_for(...)
```

The platform layer is project-owned. Solar provides runtime architecture and primitive vocabulary; the application/platform code owns board-specific pins, buses, serial ports, and peripheral traits.

## Why This Boundary Exists

- Firmware and simulation can share the same app graph.
- Solar does not own a specific board package.
- Tests can run on desktop without changing app code.
- Hardware code remains visible and explicit in the project.
