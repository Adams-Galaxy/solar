#pragma once

#include <algorithm>
#include <span>

#include "solar/inspection/collections.hpp"
#include "solar/lifecycle/engine.hpp"

#if defined(CONFIG_SOLAR_EXECUTION)
#include "solar/execution/api.hpp"
#endif
#if defined(CONFIG_SOLAR_METRICS)
#include "solar/metrics/api.hpp"
#endif
#if defined(CONFIG_SOLAR_REMOTE)
#endif

namespace solar::inspection::detail
{

template <typename System, CollectionType Collection> struct InspectionProvider;

[[nodiscard]] constexpr Error source_error(Status status, Operation operation,
                                           LocalId collection = {}) noexcept
{
    const auto reason = [&] {
        switch (status) {
        case Status::NotReady:
            return Reason::Unavailable;
        case Status::NotSupported:
            return Reason::Unsupported;
        case Status::Busy:
        case Status::WouldBlock:
            return Reason::Busy;
        case Status::NotFound:
            return Reason::NotFound;
        case Status::Invalid:
            return Reason::InvalidRequest;
        case Status::NoSpace:
        case Status::NoBuffer:
            return Reason::NoSpace;
        default:
            return Reason::SourceFailed;
        }
    }();
    return {.status = status, .reason = reason, .operation = operation, .collection = collection};
}

template <typename Collection>
[[nodiscard]] constexpr PageResult
page_result(std::size_t offset, std::size_t written, std::size_t total, LocalId collection,
            PageConsistency consistency_value, Freshness freshness = Freshness::Current) noexcept
{
    const auto next_offset = offset + written;
    return {
        .written = written,
        .next = Cursor{.collection = collection, .offset = static_cast<std::uint32_t>(next_offset)},
        .has_more = next_offset < total,
        .consistency = consistency_value,
        .freshness = freshness,
    };
}

template <typename System> struct InspectionProvider<System, Components>
{
    static Result<PageResult, Error> query(const Components::Query& query,
                                           std::span<Components::Record> output,
                                           LocalId collection) noexcept
    {
        constexpr auto records = System::catalog::components();
        const auto offset = static_cast<std::size_t>(query.page.cursor.offset);
        if (offset > records.size()) {
            return fail<Error>(source_error(Status::Invalid, Operation::Query, collection));
        }
        const auto count = (std::min)(output.size(), records.size() - offset);
        if (count != 0) {
            std::copy_n(records.begin() + offset, count, output.begin());
        }
        return page_result<Components>(offset, count, records.size(), collection,
                                       PageConsistency::StablePage);
    }
};

template <typename System> struct InspectionProvider<System, LifecycleComponents>
{
    static Result<PageResult, Error> query(const LifecycleComponents::Query& query,
                                           std::span<LifecycleComponents::Record> output,
                                           LocalId collection) noexcept
    {
        const auto offset = static_cast<std::size_t>(query.page.cursor.offset);
        auto page = lifecycle::Engine<System>::component_page(output, offset);
        if (!page) {
            return fail<Error>(source_error(status_of(page.error()), Operation::Query, collection));
        }
        return page_result<LifecycleComponents>(page->offset, page->count, page->total, collection,
                                                PageConsistency::StablePage);
    }
};

#if defined(CONFIG_SOLAR_EXECUTION)
template <typename System> struct InspectionProvider<System, ExecutionRegistrations>
{
    static Result<PageResult, Error> query(const ExecutionRegistrations::Query& query,
                                           std::span<ExecutionRegistrations::Record> output,
                                           LocalId collection) noexcept
    {
        const auto offset = static_cast<std::size_t>(query.page.cursor.offset);
        const auto all = execution::detail::registration_records<System>(
            typename System::EffectiveExecutionRegistrations{});
        if (offset > all.size()) {
            return fail<Error>(source_error(Status::Invalid, Operation::Query, collection));
        }
        const auto count = (std::min)(output.size(), all.size() - offset);
        if constexpr (std::tuple_size_v<decltype(all)> != 0) {
            std::copy_n(all.begin() + offset, count, output.begin());
        }
        return page_result<ExecutionRegistrations>(offset, count, all.size(), collection,
                                                   PageConsistency::PerRecord);
    }
};
#endif

#if defined(CONFIG_SOLAR_METRICS)
template <typename System> struct InspectionProvider<System, MetricValues>
{
    static Result<PageResult, Error> query(const MetricValues::Query& query,
                                           std::span<MetricValues::Record> output,
                                           LocalId collection) noexcept
    {
        const auto requested_offset = static_cast<std::size_t>(query.page.cursor.offset);
        std::size_t visited{};
        std::size_t written{};
        std::size_t total{};
        std::optional<metrics::Error> failure;
        for_each_type<typename System::MetricArchitecture::Metrics>([&]<typename MetricT> {
            auto reading = metrics::detail::read_metric<System, MetricT>(false);
            if (!reading) {
                failure = reading.error();
                return;
            }
            metrics::detail::emit_metric_views<System, MetricT>(
                *reading,
                [&](metrics::ViewKind kind, std::uint16_t view_index, std::uint16_t record_index,
                    metrics::UnitDescriptor unit, auto value) {
                    if (visited++ >= requested_offset && written < output.size()) {
                        output[written++] = metrics::detail::make_view_record<System, MetricT>(
                            *reading, kind, record_index, unit, value);
                        output[written - 1].view_index = view_index;
                    }
                    ++total;
                });
        });
        if (failure) {
            return fail<Error>(source_error(failure->status, Operation::Query, collection));
        }
        if (requested_offset > total) {
            return fail<Error>(source_error(Status::Invalid, Operation::Query, collection));
        }
        return page_result<MetricValues>(requested_offset, written, total, collection,
                                         PageConsistency::PerRecord, Freshness::Unknown);
    }
};
#endif

#if defined(CONFIG_SOLAR_REMOTE)
template <typename System> struct InspectionProvider<System, RemoteLinks>
{
    static Result<PageResult, Error> query(const RemoteLinks::Query& query,
                                           std::span<RemoteLinks::Record> output,
                                           LocalId collection) noexcept
    {
        const auto records = []<typename... LinkTypes>(TypeList<LinkTypes...>) {
            return std::array<remote::LinkRecord, sizeof...(LinkTypes)>{
                System::RemoteService::template link_record<
                    LinkTypes, static_cast<std::uint16_t>(System::RemoteLinkCatalog::template Entry<
                                                              LinkTypes>::local_id.value)>()...};
        }(typename System::RemoteArchitecture::Links{});
        const auto offset = static_cast<std::size_t>(query.page.cursor.offset);
        if (offset > records.size()) {
            return fail<Error>(source_error(Status::Invalid, Operation::Query, collection));
        }
        const auto count = (std::min)(output.size(), records.size() - offset);
        if constexpr (std::tuple_size_v<decltype(records)> != 0) {
            std::copy_n(records.begin() + offset, count, output.begin());
        }
        return page_result<RemoteLinks>(offset, count, records.size(), collection,
                                        PageConsistency::PerRecord);
    }
};
#endif

template <typename System, CollectionType Collection>
[[nodiscard]] Result<PageResult, Error>
query_provider(const typename Collection::Query& query,
               std::span<typename Collection::Record> output, LocalId collection) noexcept
{
    if constexpr (requires {
                      InspectionProvider<System, Collection>::query(query, output, collection);
                  }) {
        return InspectionProvider<System, Collection>::query(query, output, collection);
    } else if constexpr (requires { Provider<Collection>::query(query, output); }) {
        return Provider<Collection>::query(query, output);
    } else {
        static_assert(solar::detail::dependent_false_v<Collection>,
                      "SOLAR_DIAGNOSTIC_INSPECTION_PROVIDER_MISSING: collection has no provider");
    }
}

} // namespace solar::inspection::detail
