#pragma once

#include <cmath>
#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>

#include "solar/events/api.hpp"
#include "solar/metrics/api.hpp"

namespace solar::events::metrics
{

namespace detail
{

template <typename Target, typename Source>
[[nodiscard]] Result<Target> checked_numeric_cast(Source source) noexcept
{
    using From = std::remove_cvref_t<Source>;
    static_assert(std::is_arithmetic_v<From> && std::is_arithmetic_v<Target>,
                  "SOLAR_DIAGNOSTIC_EVENT_METRIC_PROJECTION: metric projections must be numeric");

    if constexpr (std::same_as<From, Target>) {
        return source;
    } else if constexpr (std::same_as<Target, bool>) {
        return (source == From{0} || source == From{1}) ? Result<Target>{static_cast<bool>(source)}
                                                        : Result<Target>{fail(Status::Overflow)};
    } else if constexpr (std::same_as<From, bool>) {
        return static_cast<Target>(source);
    } else if constexpr (std::is_integral_v<From> && std::is_integral_v<Target>) {
        return std::in_range<Target>(source) ? Result<Target>{static_cast<Target>(source)}
                                             : Result<Target>{fail(Status::Overflow)};
    } else {
        const auto wide = static_cast<long double>(source);
        if (!std::isfinite(wide) ||
            wide < static_cast<long double>(std::numeric_limits<Target>::lowest()) ||
            wide > static_cast<long double>(std::numeric_limits<Target>::max())) {
            return fail(Status::Overflow);
        }
        if constexpr (std::is_integral_v<Target>) {
            if (std::trunc(wide) != wide) {
                return fail(Status::Overflow);
            }
        }
        const auto converted = static_cast<Target>(source);
        if constexpr (std::is_floating_point_v<Target>) {
            if (!std::isfinite(converted) || static_cast<long double>(converted) != wide) {
                return fail(Status::Overflow);
            }
        }
        return converted;
    }
}

template <typename MetricT>
[[nodiscard]] Result<typename MetricT::Value> projected(auto value,
                                                        solar::metrics::Operation operation)
{
    auto converted = checked_numeric_cast<typename MetricT::Value>(value);
    if (!converted) {
        const auto error = solar::metrics::detail::reject_adapter_conversion<MetricT>(operation);
        return fail(error.status);
    }
    return *converted;
}

} // namespace detail

template <typename MetricT> struct Increment
{
    template <typename EventT>
    [[nodiscard]] static Result<void> apply(RecordView record,
                                            const typename EventT::Payload*) noexcept
    {
        using Value = typename MetricT::Value;
        if (record.header.occurrence_count >
            static_cast<std::uint64_t>(std::numeric_limits<Value>::max())) {
            return fail(Status::Overflow);
        }
        auto updated =
            solar::metrics::add<MetricT>(static_cast<Value>(record.header.occurrence_count));
        return updated ? Result<void>{} : Result<void>{fail(updated.error().status)};
    }
};

template <typename MetricT, auto Projection> struct Add
{
    template <typename EventT>
    [[nodiscard]] static Result<void> apply(RecordView,
                                            const typename EventT::Payload* payload) noexcept
    {
        static_assert(!payload_free_v<EventT>,
                      "SOLAR_DIAGNOSTIC_EVENT_METRIC_PAYLOAD: projected adapter requires payload");
        auto converted =
            detail::projected<MetricT>((*payload).*Projection, solar::metrics::Operation::Add);
        if (!converted) {
            return fail(converted.error());
        }
        auto updated = solar::metrics::add<MetricT>(*converted);
        return updated ? Result<void>{} : Result<void>{fail(updated.error().status)};
    }
};

template <typename MetricT, auto Projection> struct Set
{
    template <typename EventT>
    [[nodiscard]] static Result<void> apply(RecordView,
                                            const typename EventT::Payload* payload) noexcept
    {
        static_assert(!payload_free_v<EventT>,
                      "SOLAR_DIAGNOSTIC_EVENT_METRIC_PAYLOAD: projected adapter requires payload");
        auto converted =
            detail::projected<MetricT>((*payload).*Projection, solar::metrics::Operation::Set);
        if (!converted) {
            return fail(converted.error());
        }
        auto updated = solar::metrics::set<MetricT>(*converted);
        return updated ? Result<void>{} : Result<void>{fail(updated.error().status)};
    }
};

template <typename MetricT, auto Projection> struct Observe
{
    template <typename EventT>
    [[nodiscard]] static Result<void> apply(RecordView,
                                            const typename EventT::Payload* payload) noexcept
    {
        static_assert(!payload_free_v<EventT>,
                      "SOLAR_DIAGNOSTIC_EVENT_METRIC_PAYLOAD: projected adapter requires payload");
        auto converted =
            detail::projected<MetricT>((*payload).*Projection, solar::metrics::Operation::Observe);
        if (!converted) {
            return fail(converted.error());
        }
        auto updated = solar::metrics::observe<MetricT>(*converted);
        return updated ? Result<void>{} : Result<void>{fail(updated.error().status)};
    }
};

template <typename MetricT, auto Projection> struct Record
{
    template <typename EventT>
    [[nodiscard]] static Result<void> apply(RecordView,
                                            const typename EventT::Payload* payload) noexcept
    {
        static_assert(!payload_free_v<EventT>,
                      "SOLAR_DIAGNOSTIC_EVENT_METRIC_PAYLOAD: projected adapter requires payload");
        auto updated = solar::metrics::record<MetricT>((*payload).*Projection);
        return updated ? Result<void>{} : Result<void>{fail(updated.error().status)};
    }
};

template <typename EventT, typename... Operations> struct On
{
    using EventRole = InfrastructureObserver;
    using EventType = EventT;

    [[nodiscard]] static Result<void> process(RecordView record) noexcept
    {
        if constexpr (payload_free_v<EventT>) {
            return apply_all(record, static_cast<const typename EventT::Payload*>(nullptr));
        } else {
            auto payload = solar::events::decode<EventT>(record);
            if (!payload) {
                return fail(payload.error().status);
            }
            return apply_all(record, &*payload);
        }
    }

  private:
    [[nodiscard]] static Result<void> apply_all(RecordView record,
                                                const typename EventT::Payload* payload) noexcept
    {
        Status first_failure = Status::Ok;
        (
            [&] {
                auto result = Operations::template apply<EventT>(record, payload);
                if (!result && first_failure == Status::Ok) {
                    first_failure = result.error();
                }
            }(),
            ...);
        return first_failure == Status::Ok ? Result<void>{} : Result<void>{fail(first_failure)};
    }
};

template <typename Adapter> struct ProcessorFor;

template <typename EventT, typename... Operations> struct ProcessorFor<On<EventT, Operations...>>
{
    using type = Process<EventT, On<EventT, Operations...>>;
};

template <typename... AdapterTypes> struct Adapters
{
    using Processors = solar::events::Processors<typename ProcessorFor<AdapterTypes>::type...>;
};

} // namespace solar::events::metrics

template <typename Component>
struct solar::events::event_processor_extensions<Component,
                                                 std::void_t<typename Component::EventMetrics>>
{
    using type = typename Component::EventMetrics::Processors;
};
