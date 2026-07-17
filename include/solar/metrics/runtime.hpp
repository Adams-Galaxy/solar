#pragma once

#include "solar/metrics/facility.hpp"

#if defined(CONFIG_SOLAR_METRICS)

namespace solar::metrics::detail
{

template <typename System, typename MetricT>
[[nodiscard]] constexpr Error make_error(Operation operation, Status status, Reason reason) noexcept
{
    return {
        .status = status,
        .reason = reason,
        .operation = operation,
        .metric = System::MetricCatalog::template Entry<MetricT>::local_id,
    };
}

template <typename System, typename MetricT>
[[nodiscard]] Result<void, Error> prepare(Operation operation, bool mutation, bool isr) noexcept
{
    using Facility = typename System::MetricFacility;
    if (!Facility::ready.load(std::memory_order_acquire)) {
        return fail<Error>(
            make_error<System, MetricT>(operation, Status::NotReady, Reason::NotReady));
    }
    if (mutation && !Facility::accepting.load(std::memory_order_acquire)) {
        return fail<Error>(
            make_error<System, MetricT>(operation, Status::NotReady, Reason::Closed));
    }
    if (isr != kernel::in_isr()) {
        return fail<Error>(
            make_error<System, MetricT>(operation, Status::Invalid, Reason::InvalidContext));
    }
    return {};
}

template <typename System, typename MetricT, typename Function>
[[nodiscard]] Result<Update, Error> mutate_metric(Operation operation, bool no_wait, bool isr,
                                                  bool reset, Function&& function) noexcept
{
    using Facility = typename System::MetricFacility;
    auto prepared = prepare<System, MetricT>(operation, true, isr);
    if (!prepared) {
        Facility::account_failure(prepared.error());
        return fail<Error>(prepared.error());
    }
    auto result = function(Facility::template slot<MetricT>, no_wait, isr, operation);
    if (result) {
        Facility::account_success(reset);
    } else {
        Facility::account_failure(result.error());
    }
    return result;
}

template <typename System, typename MetricT>
[[nodiscard]] Result<Update, Error> add_metric(typename MetricT::Value amount, bool no_wait,
                                               bool isr, Operation operation) noexcept
{
    return mutate_metric<System, MetricT>(
        operation, no_wait, isr, false,
        [amount](auto& slot, bool try_only, bool interrupt, Operation op) {
            return slot.add(amount, try_only, interrupt, op);
        });
}

template <typename System, typename MetricT>
[[nodiscard]] Result<Update, Error> set_metric(typename MetricT::Value value, bool no_wait,
                                               bool isr, Operation operation) noexcept
{
    return mutate_metric<System, MetricT>(
        operation, no_wait, isr, false,
        [value](auto& slot, bool try_only, bool interrupt, Operation op) {
            return slot.set(value, try_only, interrupt, op);
        });
}

template <typename System, typename MetricT>
[[nodiscard]] Result<Update, Error> observe_metric(typename MetricT::Value value, bool no_wait,
                                                   bool isr, Operation operation) noexcept
{
    return mutate_metric<System, MetricT>(
        operation, no_wait, isr, false,
        [value](auto& slot, bool try_only, bool interrupt, Operation op) {
            return slot.observe(value, try_only, interrupt, op);
        });
}

template <typename System, typename MetricT>
[[nodiscard]] Result<Reading<MetricT>, Error> read_metric(bool no_wait) noexcept
{
    using Facility = typename System::MetricFacility;
    const auto operation = no_wait ? Operation::TryGet : Operation::Get;
    auto prepared = prepare<System, MetricT>(operation, false, false);
    if (!prepared) {
        Facility::account_failure(prepared.error());
        return fail<Error>(prepared.error());
    }
    auto result = Facility::template slot<MetricT>.get(no_wait);
    if (!result) {
        Facility::account_failure(result.error());
    }
    return result;
}

template <typename System, typename MetricT>
[[nodiscard]] Result<Update, Error> reset_metric(bool no_wait) noexcept
{
    return mutate_metric<System, MetricT>(
        Operation::Reset, no_wait, false, true,
        [](auto& slot, bool try_only, bool, Operation) { return slot.reset(try_only); });
}

template <typename System, typename MetricT>
[[nodiscard]] Error reject_metric(Operation operation, Status status, Reason reason,
                                  bool no_wait) noexcept
{
    using Facility = typename System::MetricFacility;
    auto prepared = prepare<System, MetricT>(operation, true, false);
    if (!prepared) {
        Facility::account_failure(prepared.error());
        return prepared.error();
    }
    auto error = Facility::template slot<MetricT>.reject(operation, status, reason, no_wait);
    Facility::account_failure(error);
    return error;
}

template <typename System, typename MetricT>
[[nodiscard]] Result<MetricRecord, Error> metric_record() noexcept
{
    using Facility = typename System::MetricFacility;
    auto prepared = prepare<System, MetricT>(Operation::Query, false, false);
    if (!prepared) {
        return fail<Error>(prepared.error());
    }
    return Facility::template slot<MetricT>.record();
}

} // namespace solar::metrics::detail

#endif
