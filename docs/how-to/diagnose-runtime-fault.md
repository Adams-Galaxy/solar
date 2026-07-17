# Diagnose A Runtime Fault

1. Read `health::record<Subject>()` and note condition, liveness, readiness,
   freshness, primary evidence, and last error.
2. Read the relevant named monitor/check records to distinguish direct,
   reported, and inferred evidence.
3. Page Health transition history to establish when state changed and whether
   records were overwritten.
4. If Supervisor is enabled, read `supervisor::record<Subject>()`, state,
   response history, and watchdog record.
5. Follow evidence references to the canonical lifecycle, execution, Remote,
   or subsystem record.
6. Correlate retained Events and Logs by timestamp/correlation ID.

An unavailable check is not a nominal result. A failed recovery is not the
original fault. Keep source evidence, assessment failure, attempted response,
and response outcome distinct in the incident report.
