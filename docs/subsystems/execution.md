# Execution

`solar::execution` binds static task behavior to an execution target, trigger,
policy, identity, and System lifecycle. Zephyr workqueues remain the underlying
deferred-execution mechanism.

## Tasks and registrations

```cpp
struct UpdateControl {
    static solar::Result<void> execute();
};

using Update = solar::execution::OnDemand<
    "update-control", UpdateControl, solar::execution::SystemWorkQueue>;
```

Register the type at the composition root or through a component's `using
Tasks` contribution, then call `solar::execution::submit<Update>()`.
Registrations are leaf entries, not components. Their executor and semantic
owner carry lifecycle responsibility.

## Registration families

| Family | Use |
| --- | --- |
| `OnDemand` | Explicit release; native coalescing or bounded counted admission |
| `Delayable` | One release scheduled for a future deadline |
| `Periodic` | Fixed-delay or fixed-rate repeated release |
| `PollTriggered` | Work armed against a fixed Zephyr poll set |

Behavior may accept a `StopToken` for cooperative cancellation. A cancellation
request does not prove that behavior has stopped; synchronous cancellation or
flush is required where containment matters.

## Targets and executors

The Zephyr system workqueue is a first-class, non-owning target. Solar never
stops or drains it globally. An application workqueue is an explicit
`execution::WorkQueue` executor component with visible stack, priority, and
lifecycle.

There is no hidden default Solar executor. An omitted target resolves to the
system workqueue only when `CONFIG_SOLAR_EXECUTION_DEFAULT_SYSTEM_WORKQUEUE=y`;
otherwise it is a compile-time error.

## Services and context

Services are components for sustained execution. Solar owns their thread,
activation barrier, cooperative stop, join, bounded timeout, and configured
forced-abort fallback. Dependencies remain alive until execution is contained.

`try_submit_isr<Registration>()` is the ISR path. Task code runs in its target
workqueue context. Native work coalescing represents at most one queued
occurrence; use `Counted<N>` when each accepted occurrence must be preserved.

See {doc}`../reference/api/execution` and
{doc}`../architecture/kernel-execution-boundary`.
