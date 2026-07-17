# Diagnose Boot

Inspect the typed error returned by `solar::boot()` first. Then query the
retained report:

```cpp
const auto report = solar::lifecycle::boot_report();
```

The report separates the primary failure from bounded rollback failures and
records which components initialized or started. Query
`solar::lifecycle::record<Component>()` for hook outcomes and execution
containment facts. Do not replace this evidence with a single vague snapshot.
