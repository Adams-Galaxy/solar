# Supervisor

Supervisor is an optional Solar Service that periodically refreshes Health,
matches explicit response rules, coordinates bounded recovery, and gates an
application-provided watchdog.

```cpp
using Policy = solar::supervisor::Policy<
    solar::supervisor::OnFault<Imu,
        solar::supervisor::Warn,
        solar::supervisor::TryRecover<Imu>>,
    solar::supervisor::OnRecoveryFailure<Imu,
        solar::supervisor::EnterSafeState<DriveSafe>>>;
```

Responses include observe, warn, latch, component recovery, safe-state entry,
component/system stop requests, watchdog withholding, reboot request, and
panic. Only declared actions run. Immediate physical containment remains the
component's responsibility when waiting for a supervisory cycle would be
unsafe.

## Cadence and recovery

The dedicated service wakes periodically and on relevant reports. Each cycle
has a bounded number of responses. Recovery cooldown, retry behavior, latching,
and escalation are deterministic policy. Response history records attempted
action and outcome without replacing Health evidence.

## Watchdog integration

A Watchdog provider implements bounded `start`, `feed`, and `stop`. Supervisor
feeds only after a complete acceptable cycle and withholds feed after a policy
gate fails. The provider owns hardware-window details. Supervisor cannot prove
its own liveness; if it stalls, progress becomes stale and feed ceases, allowing
an independent hardware watchdog to act.

Supervisor does not make software-only safety guarantees. Hardware interlocks,
electrical safe states, and target reset behavior remain system responsibilities.

See {doc}`../tutorials/supervised-device` and
{doc}`../reference/api/supervisor`.
