# Remote API

Include `<solar/remote.hpp>`. Runtime integration requires
`CONFIG_SOLAR_REMOTE=y` and at least one effective Link.

```cpp
template <typename Value> struct Schema;
template <FieldId Id, auto Member, bool Required = true> struct Field;
template <typename... Fields> struct Fields;

template <typename... Capabilities> struct Capabilities;
template <auto Reader> struct Query;
template <auto Writer> struct Update;
template <typename... Policy> struct Watch;
template <typename Acquisition, typename... Policy> struct OutStream;
template <auto Consumer, typename... Policy> struct InStream;
template <auto Callback> struct OnOpen;
template <auto Callback> struct OnClose;
template <typename Group, typename Behavior> struct Exclusive;

template <typename Data> auto write(const typename Data::Value&);
template <typename Topic> auto publish(const typename Topic::Value&);
template <typename Endpoint> bool interested();
```

Inbound streams are explicitly opened and token-scoped. A declaration may
place related controls in one stable, manifest-visible exclusive group:

```cpp
struct DriveControl {
    static constexpr remote::InStreamGroupDescriptor descriptor{
        .id = remote::InStreamGroupId{0x4101},
        .name = "drive.control",
        .description = "Mutually exclusive drive command modes",
    };
};

using Capabilities = remote::Capabilities<remote::InStream<
    &consume, remote::OnOpen<&opened>, remote::OnClose<&closed>,
    remote::Exclusive<DriveControl, remote::Replace>,
    remote::ReliableWindow<2>, remote::MaxRate<100>,
    remote::On<ControlQueue>>>;
```

`OnOpen` and `OnClose` run on the selected execution target. Replacement
invalidates the previous token and queued generation before the replacement is
activated. Consumers should still perform their final generation/deadman check
immediately before affecting hardware because already-running application work
cannot be forcibly unwound.

Contribution groups are `ContributeData`, `ContributeActions`,
`ContributeTopics`, `ContributeStreams`, `ContributeSchemas`, and
`ContributeLinks`. Focused records are under `remote::records`.

See {doc}`../../subsystems/remote` for ownership and acquisition semantics.
