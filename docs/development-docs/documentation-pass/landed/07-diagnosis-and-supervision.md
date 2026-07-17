# Stage 7: Inspection, Health, And Supervision

Status: complete

## Landed

- Added the runnable `examples/supervised-device` sample.
- Documented generic Inspection, component Health, evidence, checks, progress,
  response policy, recovery, safe state, and watchdog gating.
- Added focused API, architecture, tutorial, and runtime diagnosis pages.

## Evidence

- Supervised-device passed on `native_sim/native/64` with no warnings.
- The example recorded a fault, attempted recovery, entered safe state, and
  observed a watchdog feed after an acceptable supervision cycle.

## Decisions

The sample waits for the first Supervisor cycle explicitly. `boot()` starts the
service but does not claim that periodic assessment has already completed.
