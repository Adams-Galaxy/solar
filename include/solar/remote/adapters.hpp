#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "solar/bus/api.hpp"
#include "solar/events/api.hpp"
#include "solar/execution/api.hpp"
#include "solar/lifecycle/engine.hpp"
#include "solar/log/api.hpp"
#include "solar/metrics/api.hpp"
#include "solar/parameters/api.hpp"
#include "solar/remote/api.hpp"
#include "solar/remote/contribution.hpp"
#include "solar/remote/declaration.hpp"

namespace solar::remote::adapters
{

struct ReadOnly
{};

struct ReadWrite
{};

template <typename Endpoint> struct ScalarValue
{
    using Scalar = typename Endpoint::Scalar;
    Scalar value{};
};

template <typename Endpoint> struct EventStatsValue
{
    std::uint64_t attempts{};
    std::uint64_t captured{};
    std::uint64_t rejected{};
    std::uint64_t retained{};
    std::uint64_t known_lost{};
    std::uint64_t processor_failures{};
};

template <typename Endpoint> struct LogStatsValue
{
    std::uint64_t attempted{};
    std::uint64_t captured{};
    std::uint64_t processed{};
    std::uint64_t dropped{};
    std::uint64_t sink_failures{};
    bool panic{};
};

template <typename Endpoint> struct ExecutionStatsValue
{
    std::uint64_t submissions{};
    std::uint64_t started{};
    std::uint64_t completed{};
    std::uint64_t failed{};
    std::uint64_t cancelled{};
    std::uint32_t pending{};
    bool active{};
};

template <typename Endpoint> struct ComponentStatsValue
{
    lifecycle::ComponentState state{lifecycle::ComponentState::Registered};
    Status last_status{Status::NotReady};
    std::uint32_t transitions{};
    std::uint32_t attempts{};
    bool execution_contained{};
};

template <typename Endpoint> struct GraphStatsValue
{
    std::uint32_t components{};
    std::uint32_t devices{};
    std::uint32_t facilities{};
    std::uint32_t services{};
    std::uint32_t executors{};
};

template <typename Endpoint, std::size_t TextCapacity> struct LogEntryValue
{
    std::uint64_t sequence{};
    std::int64_t timestamp{};
    std::uint16_t source{};
    std::uint16_t domain{};
    log::Level level{log::Level::Info};
    BoundedText<TextCapacity> text{};
};

template <typename ParameterT, DataId EndpointId, TypeId SchemaId, typename Access = ReadOnly>
struct Parameter
{
    static_assert(parameters::Parameter<ParameterT>,
                  "SOLAR_DIAGNOSTIC_REMOTE_PARAMETER_ADAPTER: adapter source must be a "
                  "Parameter declaration");
    static_assert(std::is_same_v<Access, ReadOnly> || std::is_same_v<Access, ReadWrite>,
                  "SOLAR_DIAGNOSTIC_REMOTE_PARAMETER_ACCESS: Parameter adapter access must be "
                  "ReadOnly or ReadWrite");

    using Scalar = typename ParameterT::Value;
    using Value = ScalarValue<Parameter>;
    inline static constexpr DataDescriptor descriptor{
        .id = EndpointId,
        .name = descriptor_traits<parameters::Tag, ParameterT>::descriptor.name,
        .description = descriptor_traits<parameters::Tag, ParameterT>::descriptor.description,
        .version = descriptor_traits<parameters::Tag, ParameterT>::descriptor.version,
    };
    inline static constexpr SchemaDescriptor schema_descriptor{
        .id = SchemaId,
        .name = descriptor_traits<parameters::Tag, ParameterT>::descriptor.name,
        .description = descriptor_traits<parameters::Tag, ParameterT>::descriptor.description,
        .version = descriptor_traits<parameters::Tag, ParameterT>::descriptor.version,
    };

    [[nodiscard]] static Result<Value> read() noexcept
    {
        auto result = parameters::get<ParameterT>();
        if (!result) {
            return fail<solar::Error>({.status = result.error().status});
        }
        return Value{.value = *result};
    }

    [[nodiscard]] static Result<void> write(const Value& value) noexcept
        requires std::same_as<Access, ReadWrite>
    {
        auto result = parameters::set<ParameterT>(value.value);
        if (!result) {
            return fail<solar::Error>({.status = result.error().status});
        }
        return {};
    }

    using Capabilities =
        std::conditional_t<std::same_as<Access, ReadWrite>,
                           remote::Capabilities<remote::Query<&read>, remote::Update<&write>>,
                           remote::Capabilities<remote::Query<&read>>>;
};

template <typename MetricT, typename View, DataId EndpointId, TypeId SchemaId,
          std::uint32_t RateHz = 2>
struct Metric
{
    static_assert(metrics::Metric<MetricT>,
                  "SOLAR_DIAGNOSTIC_REMOTE_METRIC_ADAPTER: adapter source must be a Metric");
    static_assert(metrics::detail::ViewTraits<MetricT, View>::valid,
                  "SOLAR_DIAGNOSTIC_REMOTE_METRIC_VIEW: selected metric view is unavailable");
    static_assert(RateHz > 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_METRIC_RATE: metric stream rate must be positive");

    using Scalar = metrics::detail::view_value_t<MetricT, View>;
    using Value = ScalarValue<Metric>;
    inline static constexpr DataDescriptor descriptor{
        .id = EndpointId,
        .name = descriptor_traits<metrics::Tag, MetricT>::descriptor.name,
        .description = descriptor_traits<metrics::Tag, MetricT>::descriptor.description,
        .version = descriptor_traits<metrics::Tag, MetricT>::descriptor.version,
    };
    inline static constexpr SchemaDescriptor schema_descriptor{
        .id = SchemaId,
        .name = descriptor_traits<metrics::Tag, MetricT>::descriptor.name,
        .description = descriptor_traits<metrics::Tag, MetricT>::descriptor.description,
        .version = descriptor_traits<metrics::Tag, MetricT>::descriptor.version,
    };

    [[nodiscard]] static Result<Value> read() noexcept
    {
        auto result = metrics::get_view<MetricT, View>();
        if (!result) {
            return fail<solar::Error>({.status = result.error().status});
        }
        return Value{.value = *result};
    }

    using Capabilities = remote::Capabilities<
        remote::Query<&read>,
        remote::OutStream<remote::Poll<&read>, remote::Latest, remote::MaxRate<RateHz>>>;
};

template <DataId EndpointId, TypeId SchemaId, typename Application = DefaultApplication>
struct LifecycleState
{
    using Scalar = lifecycle::SystemState;
    using Value = ScalarValue<LifecycleState>;
    inline static constexpr DataDescriptor descriptor{
        .id = EndpointId,
        .name = "solar.lifecycle.state",
        .description = "Current Solar system lifecycle state",
    };
    inline static constexpr SchemaDescriptor schema_descriptor{
        .id = SchemaId,
        .name = "solar.lifecycle.SystemState",
    };

    [[nodiscard]] static Value read() noexcept
    {
        return {.value = lifecycle::Of<Application>::state()};
    }

    using Capabilities = remote::Capabilities<remote::Query<&read>>;
};

template <bus::Message MessageT, DataId EndpointId> struct BusInput
{
    using Value = MessageT;
    inline static constexpr DataDescriptor descriptor{
        .id = EndpointId,
        .name = descriptor_traits<bus::MessageTag, MessageT>::descriptor.name,
        .description = descriptor_traits<bus::MessageTag, MessageT>::descriptor.description,
        .version = descriptor_traits<bus::MessageTag, MessageT>::descriptor.version,
    };

    [[nodiscard]] static Result<void> write(const Value& value) noexcept
    {
        auto result = bus::emit<MessageT>(value);
        if (!result) {
            return fail<solar::Error>({.status = result.error().status});
        }
        return {};
    }

    using Capabilities = remote::Capabilities<remote::Update<&write>>;
};

template <events::Event EventT, DataId EndpointId, TypeId SchemaId> struct EventStats
{
    using Value = EventStatsValue<EventStats>;
    inline static constexpr DataDescriptor descriptor{
        .id = EndpointId,
        .name = descriptor_traits<events::Tag, EventT>::descriptor.name,
        .description = "Focused event capture and delivery counters",
        .version = descriptor_traits<events::Tag, EventT>::descriptor.version,
    };
    inline static constexpr SchemaDescriptor schema_descriptor{
        .id = SchemaId,
        .name = descriptor_traits<events::Tag, EventT>::descriptor.name,
        .description = "Solar event statistics",
        .version = descriptor_traits<events::Tag, EventT>::descriptor.version,
    };

    [[nodiscard]] static Result<Value> read() noexcept
    {
        auto result = events::record<EventT>();
        if (!result) {
            return fail<solar::Error>({.status = result.error().status});
        }
        return Value{
            .attempts = result->attempts,
            .captured = result->captured,
            .rejected =
                result->rate_limited + result->aggregation_rejected + result->ingress_rejected,
            .retained = result->retained,
            .known_lost = result->known_lost,
            .processor_failures = result->processor_failures,
        };
    }

    using Capabilities = remote::Capabilities<remote::Query<&read>>;
};

template <DataId EndpointId, TypeId SchemaId, typename Application = DefaultApplication>
struct LogStats
{
    using Value = LogStatsValue<LogStats>;
    inline static constexpr DataDescriptor descriptor{
        .id = EndpointId,
        .name = "solar.log.stats",
        .description = "Focused Solar logging counters",
    };
    inline static constexpr SchemaDescriptor schema_descriptor{
        .id = SchemaId,
        .name = "solar.log.Stats",
    };

    [[nodiscard]] static Value read() noexcept
    {
        const auto record = log::record<Application>();
        return {
            .attempted = record.attempted,
            .captured = record.captured,
            .processed = record.processed,
            .dropped = record.dropped,
            .sink_failures = record.sink_failures,
            .panic = record.panic,
        };
    }

    using Capabilities = remote::Capabilities<remote::Query<&read>>;
};

template <execution::Registration RegistrationT, DataId EndpointId, TypeId SchemaId,
          typename Application = DefaultApplication>
struct ExecutionStats
{
    using Value = ExecutionStatsValue<ExecutionStats>;
    inline static constexpr DataDescriptor descriptor{
        .id = EndpointId,
        .name = RegistrationT::descriptor.name,
        .description = "Focused Solar execution registration counters",
    };
    inline static constexpr SchemaDescriptor schema_descriptor{
        .id = SchemaId,
        .name = RegistrationT::descriptor.name,
        .description = "Solar execution registration statistics",
    };

    [[nodiscard]] static Result<Value> read() noexcept
    {
        auto result = execution::registration<RegistrationT, Application>();
        if (!result) {
            return fail<solar::Error>({.status = result.error().status});
        }
        return Value{
            .submissions = result->submissions,
            .started = result->started,
            .completed = result->completed,
            .failed = result->failed,
            .cancelled = result->cancelled,
            .pending = result->pending_count,
            .active = result->active,
        };
    }

    using Capabilities = remote::Capabilities<remote::Query<&read>>;
};

template <typename Component, DataId EndpointId, TypeId SchemaId,
          typename Application = DefaultApplication>
struct ComponentStats
{
    using Value = ComponentStatsValue<ComponentStats>;
    inline static constexpr DataDescriptor descriptor{
        .id = EndpointId,
        .name = descriptor_traits<component::Tag, Component>::descriptor.name,
        .description = "Focused Solar component lifecycle state",
    };
    inline static constexpr SchemaDescriptor schema_descriptor{
        .id = SchemaId,
        .name = descriptor_traits<component::Tag, Component>::descriptor.name,
        .description = "Solar component lifecycle statistics",
    };

    [[nodiscard]] static Result<Value> read() noexcept
    {
        auto result = lifecycle::Of<Application>::template record<Component>();
        if (!result) {
            return fail<solar::Error>({.status = status_of(result.error())});
        }
        return Value{
            .state = result->state,
            .last_status = result->last_status,
            .transitions = result->transitions,
            .attempts = result->attempts,
            .execution_contained = result->execution_contained,
        };
    }

    using Capabilities = remote::Capabilities<remote::Query<&read>>;
};

template <DataId EndpointId, TypeId SchemaId, typename Application = DefaultApplication>
struct GraphStats
{
    using Value = GraphStatsValue<GraphStats>;
    inline static constexpr DataDescriptor descriptor{
        .id = EndpointId,
        .name = "solar.graph.stats",
        .description = "Effective Solar graph category counts",
    };
    inline static constexpr SchemaDescriptor schema_descriptor{
        .id = SchemaId,
        .name = "solar.graph.Stats",
    };

    [[nodiscard]] static Value read() noexcept
    {
        using System = bound_system_t<Application>;
        return {
            .components = static_cast<std::uint32_t>(list_size_v<typename System::Components>),
            .devices =
                static_cast<std::uint32_t>(list_size_v<typename System::Effective::UserDevices>),
            .facilities = static_cast<std::uint32_t>(
                list_size_v<typename System::Effective::EffectiveFacilities>),
            .services = static_cast<std::uint32_t>(
                list_size_v<typename System::Effective::EffectiveServices>),
            .executors =
                static_cast<std::uint32_t>(list_size_v<typename System::Effective::UserExecutors>),
        };
    }

    using Capabilities = remote::Capabilities<remote::Query<&read>>;
};

template <events::Event EventT, TopicId EndpointId, std::size_t Depth = 8>
    requires(!events::payload_free_v<EventT> && SchemaType<typename EventT::Payload>)
struct EventTopic
{
    using EventRole = events::InfrastructureObserver;

    static constexpr component::Descriptor descriptor{
        .name = descriptor_traits<events::Tag, EventT>::descriptor.name,
        .description = "Explicit Event to Remote Topic adapter",
    };

    struct Topic
    {
        using Value = typename EventT::Payload;
        inline static constexpr TopicDescriptor descriptor{
            .id = EndpointId,
            .name = descriptor_traits<events::Tag, EventT>::descriptor.name,
            .description = descriptor_traits<events::Tag, EventT>::descriptor.description,
            .version = descriptor_traits<events::Tag, EventT>::descriptor.version,
        };
        using Publication =
            remote::Watch<remote::Queue<Depth, remote::DropOldest>, remote::MultipleProducers>;
    };

    using EventProcessors = events::Processors<events::Process<EventT, EventTopic>>;
    using RemoteTopics = remote::ContributeTopics<Topic>;

    [[nodiscard]] static Result<void> process(events::RecordView record) noexcept
    {
        auto payload = events::decode<EventT>(record);
        if (!payload) {
            return fail<solar::Error>({.status = payload.error().status});
        }
        auto published = remote::publish<Topic>(*payload);
        return published ? Result<void>{}
                         : Result<void>{fail<solar::Error>({.status = published.error().status})};
    }
};

template <std::size_t TextCapacity, std::size_t Depth, TopicId EndpointId, TypeId SchemaId>
struct LogTopic
{
    static constexpr component::Descriptor descriptor{
        .name = "solar.remote.log-topic",
        .description = "Explicit Logging to Remote Topic adapter",
    };

    struct Topic
    {
        using Value = LogEntryValue<LogTopic, TextCapacity>;
        inline static constexpr TopicDescriptor descriptor{
            .id = EndpointId,
            .name = "solar.log.live",
            .description = "Live bounded Solar log records",
        };
        using Publication =
            remote::Watch<remote::Queue<Depth, remote::DropOldest>, remote::MultipleProducers>;
    };

    struct Sink
    {
        static constexpr log::SinkDescriptor descriptor{
            .name = "solar.remote.logs",
            .description = "Remote live log publication sink",
        };

        [[nodiscard]] static Result<void> consume(log::RecordView record,
                                                  std::string_view rendered) noexcept
        {
            typename Topic::Value value{
                .sequence = record.header.sequence,
                .timestamp = record.header.timestamp,
                .source = record.header.source.value,
                .domain = record.header.domain.value,
                .level = record.header.level,
            };
            const auto size = std::min(rendered.size(), TextCapacity);
            std::memcpy(value.text.storage.data(), rendered.data(), size);
            value.text.size = static_cast<std::uint16_t>(size);
            auto published = remote::publish<Topic>(value);
            return published
                       ? Result<void>{}
                       : Result<void>{fail<solar::Error>({.status = published.error().status})};
        }
    };

    inline static constexpr SchemaDescriptor schema_descriptor{
        .id = SchemaId,
        .name = "solar.log.Entry",
    };
    using RemoteTopics = remote::ContributeTopics<Topic>;
};

} // namespace solar::remote::adapters

template <typename Endpoint>
    requires requires {
        typename Endpoint::Scalar;
        { Endpoint::schema_descriptor } -> std::convertible_to<solar::remote::SchemaDescriptor>;
    }
struct solar::remote::Schema<solar::remote::adapters::ScalarValue<Endpoint>>
{
    using Value = solar::remote::adapters::ScalarValue<Endpoint>;
    static constexpr SchemaDescriptor descriptor = Endpoint::schema_descriptor;
    using Fields = remote::Fields<remote::Field<1, &Value::value>>;
    static constexpr std::size_t max_encoded_size = sizeof(typename Endpoint::Scalar) + 16;
    static constexpr Codec codec = Codec::Cbor;
};

#define SOLAR_DETAIL_REMOTE_ADAPTER_SCHEMA(VALUE_TEMPLATE, ...)                                    \
    template <typename Endpoint>                                                                   \
    struct solar::remote::Schema<solar::remote::adapters::VALUE_TEMPLATE<Endpoint>>                \
    {                                                                                              \
        using Value = solar::remote::adapters::VALUE_TEMPLATE<Endpoint>;                           \
        static constexpr SchemaDescriptor descriptor = Endpoint::schema_descriptor;                \
        using Fields = remote::Fields<__VA_ARGS__>;                                                \
        static constexpr std::size_t max_encoded_size = sizeof(Value) + 32;                        \
        static constexpr Codec codec = Codec::Cbor;                                                \
    }

SOLAR_DETAIL_REMOTE_ADAPTER_SCHEMA(EventStatsValue, remote::Field<1, &Value::attempts>,
                                   remote::Field<2, &Value::captured>,
                                   remote::Field<3, &Value::rejected>,
                                   remote::Field<4, &Value::retained>,
                                   remote::Field<5, &Value::known_lost>,
                                   remote::Field<6, &Value::processor_failures>);
SOLAR_DETAIL_REMOTE_ADAPTER_SCHEMA(LogStatsValue, remote::Field<1, &Value::attempted>,
                                   remote::Field<2, &Value::captured>,
                                   remote::Field<3, &Value::processed>,
                                   remote::Field<4, &Value::dropped>,
                                   remote::Field<5, &Value::sink_failures>,
                                   remote::Field<6, &Value::panic>);
SOLAR_DETAIL_REMOTE_ADAPTER_SCHEMA(ExecutionStatsValue, remote::Field<1, &Value::submissions>,
                                   remote::Field<2, &Value::started>,
                                   remote::Field<3, &Value::completed>,
                                   remote::Field<4, &Value::failed>,
                                   remote::Field<5, &Value::cancelled>,
                                   remote::Field<6, &Value::pending>,
                                   remote::Field<7, &Value::active>);
SOLAR_DETAIL_REMOTE_ADAPTER_SCHEMA(ComponentStatsValue, remote::Field<1, &Value::state>,
                                   remote::Field<2, &Value::last_status>,
                                   remote::Field<3, &Value::transitions>,
                                   remote::Field<4, &Value::attempts>,
                                   remote::Field<5, &Value::execution_contained>);
SOLAR_DETAIL_REMOTE_ADAPTER_SCHEMA(GraphStatsValue, remote::Field<1, &Value::components>,
                                   remote::Field<2, &Value::devices>,
                                   remote::Field<3, &Value::facilities>,
                                   remote::Field<4, &Value::services>,
                                   remote::Field<5, &Value::executors>);

#undef SOLAR_DETAIL_REMOTE_ADAPTER_SCHEMA

template <typename Endpoint, std::size_t Capacity>
struct solar::remote::Schema<solar::remote::adapters::LogEntryValue<Endpoint, Capacity>>
{
    using Value = solar::remote::adapters::LogEntryValue<Endpoint, Capacity>;
    static constexpr SchemaDescriptor descriptor = Endpoint::schema_descriptor;
    using Fields =
        remote::Fields<remote::Field<1, &Value::sequence>, remote::Field<2, &Value::timestamp>,
                       remote::Field<3, &Value::source>, remote::Field<4, &Value::domain>,
                       remote::Field<5, &Value::level>, remote::Field<6, &Value::text>>;
    static constexpr std::size_t max_encoded_size = Capacity + 64;
    static constexpr Codec codec = Codec::Cbor;
};
