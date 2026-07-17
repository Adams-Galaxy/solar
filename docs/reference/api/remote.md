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

template <typename Data> auto write(const typename Data::Value&);
template <typename Topic> auto publish(const typename Topic::Value&);
template <typename Endpoint> bool interested();
```

Contribution groups are `ContributeData`, `ContributeActions`,
`ContributeTopics`, `ContributeStreams`, `ContributeSchemas`, and
`ContributeLinks`. Focused records are under `remote::records`.

See {doc}`../../subsystems/remote` for ownership and acquisition semantics.
