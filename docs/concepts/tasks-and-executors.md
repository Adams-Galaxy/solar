# Tasks And Executors

A task is static repeatable behavior. An execution policy determines where and
when it runs; registering a task does not imply allocating a thread stack.

```cpp
struct SampleSensors
{
    static solar::Status execute();
};

using SampleTask = solar::PeriodicTask<
    solar::Name<"sample-sensors">,
    SampleSensors,
    20>;
```

Solar provides four explicit shapes:

- `EventTask<Name, Behavior>` uses Zephyr's system work queue.
- `PeriodicTask<Name, Behavior, PeriodMs>` uses delayable work.
- `SharedTask<Name, Behavior, Executor>` submits to a `SharedExecutor`, allowing
  many task types to share one stack.
- `DedicatedTask<Name, Behavior, ThreadPolicy>` owns a thread explicitly. Its
  behavior may accept `kernel::StopToken` for cooperative shutdown.

Each policy exposes `execution()` separately from `System::lifecycle`. Work
policies cancel synchronously, shared executors drain queued work before
stopping, and dedicated tasks request stop and join using their declared
timeout.
