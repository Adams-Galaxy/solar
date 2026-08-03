# Metrics

`metrics::MetricStore<Schema<...>>` owns typed current values and bounded
numeric summaries. Sampling policy is outside storage. `Inspect` and `Export`
are adapters over the same object or application-scoped static facade.
