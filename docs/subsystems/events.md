# Events

Events record structured facts that happened. They carry stable identity,
severity, domain, timestamp/sequence metadata, an optional typed payload, and a
capture/retention policy.

```cpp
struct LinkLost {
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "communication.link-lost",
        .severity = solar::events::Severity::Error,
        .domain = solar::events::domain::Communication,
    };
};

auto observed = solar::events::observe<LinkLost>(3);
```

Use `try_observe` and `try_observe_isr` for no-wait admission. An accepted
observation returns sequence and materialization facts; capture policy may
sample, rate-limit, or aggregate without pretending the occurrence failed.

## Capture and retention

Policies include every occurrence, sampling, rate limiting, aggregate count,
keyed aggregation, transient records, bounded critical retention, and
persistent stores. All queues, history, keys, and reserved slots are bounded.
Backpressure is visible as a typed `events::Error` reason.

## Processors and adapters

Infrastructure observers contribute `EventProcessors` that receive immutable
`RecordView` values on a configured execution target. Built-in adapters can
derive Metrics and Log records from Events without coupling the event type
back to its consumers. A resolving event may name `using Resolves = Fault` to
close active-fault evidence.

The Event Facility owns ingress, sequence assignment, materialization,
history, and processor work. ISR observation copies bounded payload data and
never invokes thread-only processors inline.

See {doc}`../reference/api/events` and {doc}`../tutorials/data-pipeline`.
