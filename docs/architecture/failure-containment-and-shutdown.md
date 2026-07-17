# Failure Containment And Shutdown

## Boot

Lifecycle walks dependency order through initialization, execution preparation,
start, and final activation. A failed hook records the component and phase,
then rolls completed work back in reverse order. Prepared services do not run
application behavior until the System reaches its activation barrier.

## Runtime

Subsystems retain focused errors at their owning boundary. Health may adapt
those facts into evidence; Supervisor may apply declared response. A typed
failure never grants automatic permission to restart, reboot, or continue
unsafe actuation.

## Stop

1. Admission closes for work that would create new dependent activity.
2. Services and registrations receive cooperative stop/cancellation.
3. Owned queues and work are joined, flushed, or synchronously cancelled within
   configured bounds.
4. Uncontained work is recorded and dependencies it can access remain alive.
5. Components stop and deinitialize in reverse dependency order.

Solar never globally drains or stops Zephyr's system workqueue. It contains
only its own registered items. Forced thread abort is a configured final
fallback after bounded cooperative stop, not ordinary cancellation semantics.

Fatal Zephyr handling is separate and panic-safe. It cannot assume scheduler,
heap, ordinary mutex, asynchronous sink, Health, or Supervisor availability.
