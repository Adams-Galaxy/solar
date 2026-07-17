# Bus

The Bus carries typed application messages from producers to statically known
subscribers. It replaces ad hoc channel wiring without becoming an event log,
parameter store, or runtime broker.

## Declare and contribute

```cpp
struct TargetCommand {
    float value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "drive.target"};
};

struct Producer { using Messages = solar::bus::Messages<TargetCommand>; };

struct Consumer {
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<TargetCommand, solar::bus::delivery::Inline>>;
    static solar::Result<void> handle(const TargetCommand&);
};
```

Component-local aliases are collected into one effective message and
subscription catalog. Root declarations use `solar::Bus<bus::Messages<...>,
bus::Subscriptions<...>>` when no component is the natural contributor.

## Delivery policies

`Inline` invokes subscribers in the emitter's thread context. It is compact
and ordered, but handlers must be bounded and must not assume another thread.
`InlineIsr` is an explicitly ISR-safe variant.

Deferred routes target an Execution workqueue:

- `Queued<Target, N, Overflow>` preserves accepted values in bounded storage;
- `Latest<Target>` retains only the latest value;
- `Coalesced<Target>` records that work is needed without retaining payloads.

Overflow is explicit: reject, drop newest, drop oldest, or bounded thread-only
wait. ISR emission never waits. Emission reports partial delivery and route
failures through `bus::Error` and focused route records.

## Ownership and shutdown

The built-in Bus Facility owns route storage and work registrations. Message
values are copied into deferred storage; references are not retained across
contexts. During stop, new admission closes before queued routes are cancelled
or drained according to policy.

See {doc}`../tutorials/data-pipeline` and {doc}`../reference/api/bus`.
