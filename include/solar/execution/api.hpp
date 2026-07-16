#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <span>

#include "solar/execution/protocol.hpp"

namespace solar::execution
{

template <OnDemandRegistration Registration, typename Application = DefaultApplication>
[[nodiscard]] Result<Submission, Error> submit() noexcept
{
    return frontend::Operation<detail::SubmitFrontend, Registration, Application>::call();
}

template <OnDemandRegistration Registration, typename Application = DefaultApplication>
[[nodiscard]] Result<Submission, Error> try_submit_isr() noexcept
{
    return frontend::Operation<detail::SubmitIsrFrontend, Registration, Application>::call();
}

template <SchedulableRegistration Registration, typename Rep, typename Period,
          typename Application = DefaultApplication>
[[nodiscard]] Result<Submission, Error> schedule(std::chrono::duration<Rep, Period> delay) noexcept
{
    return frontend::Operation<detail::ScheduleFrontend<false>, Registration, Application>::call(
        std::chrono::duration_cast<std::chrono::nanoseconds>(delay));
}

template <SchedulableRegistration Registration, typename Rep, typename Period,
          typename Application = DefaultApplication>
[[nodiscard]] Result<Submission, Error>
reschedule(std::chrono::duration<Rep, Period> delay) noexcept
{
    return frontend::Operation<detail::ScheduleFrontend<true>, Registration, Application>::call(
        std::chrono::duration_cast<std::chrono::nanoseconds>(delay));
}

template <Registration RegistrationT, typename Application = DefaultApplication>
[[nodiscard]] Result<Cancellation, Error> cancel() noexcept
{
    return frontend::Operation<detail::CancelFrontend<false>, RegistrationT, Application>::call();
}

template <Registration RegistrationT, typename Application = DefaultApplication>
[[nodiscard]] Result<Cancellation, Error> cancel_sync() noexcept
{
    return frontend::Operation<detail::CancelFrontend<true>, RegistrationT, Application>::call();
}

template <Registration RegistrationT, typename Application = DefaultApplication>
[[nodiscard]] Result<Cancellation, Error> flush() noexcept
{
    return frontend::Operation<detail::FlushFrontend, RegistrationT, Application>::call();
}

template <Registration RegistrationT, typename Application = DefaultApplication>
[[nodiscard]] Result<RegistrationRecord, Error> registration() noexcept
{
    return frontend::Operation<detail::RecordFrontend, RegistrationT, Application>::call();
}

namespace detail
{

template <typename System, typename... Entries>
[[nodiscard]] auto registration_records(TypeList<Entries...>) noexcept
{
    return std::array<RegistrationRecord, sizeof...(Entries)>{
        registration_state<System, typename Entries::Declaration>().copy()...};
}

template <typename System, typename... Services>
[[nodiscard]] auto service_records(TypeList<Services...>) noexcept
{
    return std::array<ServiceRecord, sizeof...(Services)>{
        service_state<System, Services>().copy()...};
}

template <typename System, typename Executor>
[[nodiscard]] ExecutorRecord executor_record() noexcept;

template <typename System, typename... Executors>
[[nodiscard]] auto executor_records(TypeList<Executors...>) noexcept
{
    return std::array<ExecutorRecord, sizeof...(Executors)>{
        executor_record<System, Executors>()...};
}

template <typename System, typename Executor>
[[nodiscard]] ExecutorRecord executor_record() noexcept
{
    auto record = executor_state<System, Executor>().copy();
    record.active_registrations = 0;
    record.submissions = 0;
    record.started_items = 0;
    record.completed_items = 0;
    record.failed_items = 0;
    record.pending_count = 0;
    record.pending_high_water = 0;
    for_each_target_registration<System, Executor>([&]<typename Registration> {
        const auto registration = registration_state<System, Registration>().copy();
        record.active_registrations += std::size_t{registration.active};
        record.submissions += registration.submissions;
        record.started_items += registration.started;
        record.completed_items += registration.completed;
        record.failed_items += registration.failed;
        record.pending_count += registration.pending_count;
        record.pending_high_water += registration.pending_high_water;
    });
    return record;
}

} // namespace detail

template <typename Application = DefaultApplication> struct Of
{
    using System = bound_system_t<Application>;

    template <Registration RegistrationT>
    [[nodiscard]] static RegistrationRecord registration() noexcept
    {
        static_assert(System::ExecutionCatalog::template contains<RegistrationT>,
                      "SOLAR_DIAGNOSTIC_UNREGISTERED_EXECUTION_QUERY: registration is absent from "
                      "the bound execution catalog");
        return detail::registration_state<System, RegistrationT>().copy();
    }

    [[nodiscard]] static auto registrations() noexcept
    {
        return detail::registration_records<System>(
            typename System::EffectiveExecutionRegistrations{});
    }

    [[nodiscard]] static auto tasks() noexcept
    {
        return registrations();
    }

    template <typename Service> [[nodiscard]] static ServiceRecord service() noexcept
    {
        static_assert(
            contains_v<Service, typename System::Effective::EffectiveServices>,
            "SOLAR_DIAGNOSTIC_UNREGISTERED_SERVICE_QUERY: service is absent from Services<...>");
        return detail::service_state<System, Service>().copy();
    }

    [[nodiscard]] static auto services() noexcept
    {
        return detail::service_records<System>(typename System::Effective::EffectiveServices{});
    }

    template <typename Executor> [[nodiscard]] static ExecutorRecord executor() noexcept
    {
        static_assert(
            contains_v<Executor, typename System::Effective::UserExecutors>,
            "SOLAR_DIAGNOSTIC_UNREGISTERED_EXECUTOR_QUERY: executor is absent from Executors<...>");
        return detail::executor_record<System, Executor>();
    }

    [[nodiscard]] static auto executors() noexcept
    {
        return detail::executor_records<System>(typename System::Effective::UserExecutors{});
    }

    [[nodiscard]] static SystemTargetRecord system_target() noexcept
    {
        SystemTargetRecord record{};
        detail::for_each_registration<System>([&]<typename RegistrationT> {
            if constexpr (std::is_same_v<detail::resolved_target_t<System, RegistrationT>,
                                         SystemWorkQueue>) {
                const auto registration_record =
                    detail::registration_state<System, RegistrationT>().copy();
                ++record.registration_count;
                record.submissions += registration_record.submissions;
                record.completions += registration_record.completed;
            }
        });
        return record;
    }

    [[nodiscard]] static RecordPage registration_page(std::span<RegistrationRecord> destination,
                                                      std::size_t offset = 0) noexcept
    {
        const auto all = registrations();
        const auto start = offset < all.size() ? offset : all.size();
        const auto count = std::min(destination.size(), all.size() - start);
        for (std::size_t index = 0; index < count; ++index) {
            destination[index] = all[start + index];
        }
        return {.offset = start, .count = count, .total = all.size()};
    }
};

template <typename Service, typename Application = DefaultApplication>
[[nodiscard]] ServiceRecord service() noexcept
{
    return Of<Application>::template service<Service>();
}

template <typename Application = DefaultApplication> [[nodiscard]] auto services() noexcept
{
    return Of<Application>::services();
}

template <typename Executor, typename Application = DefaultApplication>
[[nodiscard]] ExecutorRecord executor() noexcept
{
    return Of<Application>::template executor<Executor>();
}

template <typename Application = DefaultApplication> [[nodiscard]] auto executors() noexcept
{
    return Of<Application>::executors();
}

template <typename Application = DefaultApplication> [[nodiscard]] auto registrations() noexcept
{
    return Of<Application>::registrations();
}

template <typename Application = DefaultApplication> [[nodiscard]] auto tasks() noexcept
{
    return Of<Application>::tasks();
}

template <typename Application = DefaultApplication>
[[nodiscard]] SystemTargetRecord system_target() noexcept
{
    return Of<Application>::system_target();
}

template <typename Application = DefaultApplication>
[[nodiscard]] RecordPage registration_page(std::span<RegistrationRecord> destination,
                                           std::size_t offset = 0) noexcept
{
    return Of<Application>::registration_page(destination, offset);
}

} // namespace solar::execution
