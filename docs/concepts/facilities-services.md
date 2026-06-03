# Facilities And Services

Solar separates passive capability from active behavior.

## Facilities

Facilities live under `solar::facilities`. They are passive graph objects. A facility can own state, provide static APIs, expose lifecycle hooks, and contribute metrics/events/Remote vocabulary, but it does not own a thread.

Examples:

- `solar::facilities::Events`
- `solar::facilities::Metrics`
- `solar::facilities::Inspection`
- a project-owned parameter store
- a calibration store

Facilities are initialized and started during `System::Boot()` before devices are started. This makes them available to devices and services during normal runtime.

## Services

Services live under `solar::services`. They are active runtime actors. Every service declares a thread spec with a stack size in bytes:

```cpp
using Thread = solar::ServiceSpec<Name, 2048, solar::kernel::Priority::Normal>;
```

and implements:

```cpp
void run(Context& ctx, solar::StopToken stop);
```

The service owns its run loop. Solar does not provide a polling service model. If a service needs periodic behavior, it should sleep or wait inside `run`:

```cpp
void run(Context& ctx, solar::StopToken stop)
{
    while (!stop.stop_requested())
    {
        do_work(ctx);
        solar::kernel::ThisThread::sleep_for(solar::Milliseconds{10});
    }
}
```

## Choosing The Right One

Use a facility when:

- the component is a store, catalog, read model, or shared capability;
- it does not need its own thread;
- other components should query or update it directly;
- its behavior is driven by callers.

Use a service when:

- it owns ongoing behavior;
- it waits on IO, notifications, or timers;
- it should run independently after boot;
- it needs cooperative shutdown.

`Remote` is a service. `Metrics` is a facility. `Events` is a facility even though it can write to sinks, because event emission is caller-driven and does not require a worker thread.
