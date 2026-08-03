# Supervision

`supervisor::Monitor<Schema<...>>` stores typed health conditions without
owning recovery policy or an application orchestration service. `Evaluate`
connects a probe explicitly. Applications decide how to react to degraded or
fault conditions.
