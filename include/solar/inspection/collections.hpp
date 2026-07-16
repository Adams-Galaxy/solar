#pragma once

#include "solar/component.hpp"
#include "solar/inspection/declaration.hpp"

#if defined(CONFIG_SOLAR_INSPECTION)
#include "solar/lifecycle/types.hpp"

#if defined(CONFIG_SOLAR_EXECUTION)
#include "solar/execution/types.hpp"
#endif
#if defined(CONFIG_SOLAR_METRICS)
#include "solar/metrics/types.hpp"
#endif
#if defined(CONFIG_SOLAR_REMOTE)
#include "solar/remote/types.hpp"
#endif
#endif

namespace solar::inspection
{

#if defined(CONFIG_SOLAR_INSPECTION)

namespace detail
{
inline constexpr std::uint16_t configured_maximum_page =
#if defined(CONFIG_SOLAR_INSPECTION_MAX_PAGE_RECORDS)
    CONFIG_SOLAR_INSPECTION_MAX_PAGE_RECORDS;
#else
    32;
#endif

inline constexpr CapabilitySet local_query_capabilities = capability(OperationCapability::Query);
inline constexpr CapabilitySet remote_query_capabilities =
#if defined(CONFIG_SOLAR_INSPECTION_REMOTE)
    local_query_capabilities | capability(OperationCapability::Remote);
#else
    local_query_capabilities;
#endif
} // namespace detail

struct Components
{
    using Record = component::DescriptorView;
    using Query = BasicQuery;

    static constexpr Descriptor descriptor{
        .name = "components",
        .description = "Effective component descriptors",
        .stable_id = Id{0x6D9A0001U},
        .subsystem = Subsystem::Graph,
        .capabilities = detail::remote_query_capabilities,
        .consistency_modes = consistency(Consistency::StablePage),
        .synchronization = Synchronization::None,
        .context = Context::Any,
        .cost = Cost::LinearPage,
        .maximum_page = detail::configured_maximum_page,
        .record_size = sizeof(Record),
        .query_size = sizeof(Query),
    };
};

struct LifecycleComponents
{
    using Record = lifecycle::ComponentRecord;
    using Query = BasicQuery;

    static constexpr Descriptor descriptor{
        .name = "lifecycle.components",
        .description = "Coherent component lifecycle records",
        .stable_id = Id{0x6D9A0002U},
        .subsystem = Subsystem::Lifecycle,
        .capabilities = detail::remote_query_capabilities,
        .consistency_modes = consistency(Consistency::StablePage),
        .synchronization = Synchronization::MutexCopy,
        .context = Context::Thread,
        .cost = Cost::LinearPage,
        .maximum_page = detail::configured_maximum_page,
        .record_size = sizeof(Record),
        .query_size = sizeof(Query),
        .may_block = true,
    };
};

#if defined(CONFIG_SOLAR_EXECUTION)
struct ExecutionRegistrations
{
    using Record = execution::RegistrationRecord;
    using Query = BasicQuery;

    static constexpr Descriptor descriptor{
        .name = "execution.registrations",
        .description = "Execution registration records",
        .stable_id = Id{0x6D9A0003U},
        .subsystem = Subsystem::Execution,
        .capabilities = detail::remote_query_capabilities,
        .consistency_modes = consistency(Consistency::PerRecord),
        .synchronization = Synchronization::SpinCopy,
        .context = Context::Thread,
        .cost = Cost::LinearPage,
        .maximum_page = detail::configured_maximum_page,
        .record_size = sizeof(Record),
        .query_size = sizeof(Query),
    };
};
#endif

#if defined(CONFIG_SOLAR_METRICS)
struct MetricValues
{
    using Record = metrics::MetricViewRecord;
    using Query = BasicQuery;

    static constexpr Descriptor descriptor{
        .name = "metrics.values",
        .description = "Typed metric values projected as scalar view records",
        .stable_id = Id{0x6D9A0004U},
        .subsystem = Subsystem::Metrics,
        .capabilities = detail::remote_query_capabilities,
        .consistency_modes = consistency(Consistency::PerRecord),
        .synchronization = Synchronization::SourceDefined,
        .context = Context::Thread,
        .cost = Cost::LinearPage,
        .maximum_page = detail::configured_maximum_page,
        .record_size = sizeof(Record),
        .query_size = sizeof(Query),
        .may_block = true,
        .values_may_be_stale = true,
    };
};
#endif

#if defined(CONFIG_SOLAR_REMOTE)
struct RemoteLinks
{
    using Record = remote::LinkRecord;
    using Query = BasicQuery;

    static constexpr Descriptor descriptor{
        .name = "remote.links",
        .description = "Remote link and session accounting",
        .stable_id = Id{0x6D9A0005U},
        .subsystem = Subsystem::Remote,
        .capabilities = detail::remote_query_capabilities,
        .consistency_modes = consistency(Consistency::PerRecord),
        .synchronization = Synchronization::SourceDefined,
        .context = Context::Thread,
        .cost = Cost::LinearPage,
        .maximum_page = detail::configured_maximum_page,
        .record_size = sizeof(Record),
        .query_size = sizeof(Query),
    };
};
#endif

using BuiltinCollections = TypeList<Components, LifecycleComponents
#if defined(CONFIG_SOLAR_EXECUTION)
                                    ,
                                    ExecutionRegistrations
#endif
#if defined(CONFIG_SOLAR_METRICS)
                                    ,
                                    MetricValues
#endif
#if defined(CONFIG_SOLAR_REMOTE)
                                    ,
                                    RemoteLinks
#endif
                                    >;
#else
using BuiltinCollections = TypeList<>;
#endif

} // namespace solar::inspection
