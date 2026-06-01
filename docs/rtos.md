# RTOS

`solar::rtos` is the stable public API for threading and synchronization. Concrete behavior comes from the selected `low_level::rtos` implementation.

Public wrappers include:

- `Thread`
- `ThreadStorage<N>`
- `Queue<T, N>`
- `Semaphore`
- `BinarySemaphore`
- `Mutex`
- `RecursiveMutex`
- `EventFlags`
- `Timer`
- `Deadline`
- `ThisThread`
- notifications

## Priority

Solar exposes a portable priority ladder:

```cpp
solar::rtos::Priority::Idle
solar::rtos::Priority::Low
solar::rtos::Priority::Normal
solar::rtos::Priority::High
solar::rtos::Priority::Realtime
```

Each band has numbered intermediate steps. Solar maps this ladder onto the backend's native priority count.

## Deadlines

`Deadline` represents an absolute tick deadline with optional grace. It is useful when one operation has a single time budget spanning multiple waits.

## Static Allocation

Solar services and tasks use static thread storage where supported by the low-level backend. This supports the static-after-boot runtime policy.
