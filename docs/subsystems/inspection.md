# Inspection

Inspection is an optional generic discovery and paging layer over facts owned
by other subsystems. It exists for host tools, manifests, diagnostic capture,
and tests that cannot name application C++ types at compile time.

Typed firmware code should keep using direct APIs such as
`metrics::get<LoopTime>()` or `lifecycle::record<Motor>()`. A generic consumer
uses collection types and stable IDs:

```cpp
auto available = solar::inspection::collections();
std::array<solar::inspection::MetricValues::Record, 8> records{};
auto page = solar::inspection::query<solar::inspection::MetricValues>(
    {.page = {.limit = records.size()}}, records);
```

Each collection has a descriptor, query type, record schema, provider, paging
contract, revision/freshness behavior, and explicit support result. Providers
copy bounded records from canonical subsystem APIs; Inspection owns no duplicate
runtime truth, worker, or sampling schedule.

Collections are independently exposable through Remote authorization. The
presence of Inspection does not make every diagnostic record remotely visible.
Formatting and CBOR encoding happen after source locks are released.

Inspection is read-only. Mutation, reset, stop, and recovery remain direct
subsystem APIs or explicit Actions. See {doc}`../reference/api/inspection`.
