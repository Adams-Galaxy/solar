#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <type_traits>

#include "solar/supervisor/service.hpp"
#include "solar/system/binding.hpp"

namespace solar::supervisor
{

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_SUPERVISOR)

namespace detail
{

template <typename Application>
using BoundService = typename bound_system_t<Application>::SupervisorService;

[[nodiscard]] constexpr Error query_error(Status status, Reason reason = Reason::NotReady) noexcept
{
    return {.status = status, .reason = reason, .operation = Operation::Query};
}

template <typename ServiceT> [[nodiscard]] Result<StateRecord, Error> copy_state() noexcept
{
    auto& storage = detail::storage<ServiceT>();
    auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
    if (!guard) {
        return fail(query_error(guard.error()));
    }
    return storage.state;
}

template <typename ServiceT> [[nodiscard]] Result<WatchdogRecord, Error> copy_watchdog() noexcept
{
    auto& storage = detail::storage<ServiceT>();
    auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
    if (!guard) {
        return fail(query_error(guard.error()));
    }
    return storage.watchdog;
}

template <typename ServiceT, typename Component>
[[nodiscard]] Result<SubjectRecord, Error> copy_subject() noexcept
{
    using Rules = typename ServiceT::Architecture::ResponsePolicy::RuleTypes;
    SubjectRecord result{};
    auto& storage = detail::storage<ServiceT>();
    auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
    if (!guard) {
        return fail(query_error(guard.error()));
    }
    for_each_type<Rules>([&]<typename Rule> {
        if constexpr (std::is_same_v<typename RuleTraits<Rule>::Subject, Component>) {
            constexpr auto index = TypeIndex<Rule, Rules>::value;
            const auto& candidate = ServiceT::subject_records[index];
            if (candidate.last_response_at >= result.last_response_at) {
                result = candidate;
            }
            result.latched = result.latched || candidate.latched;
            result.active = result.active || candidate.active;
        }
    });
    return result;
}

template <typename ServiceT>
[[nodiscard]] Result<ResponsePage, Error>
copy_responses(ResponseCursor cursor, std::span<ResponseRecord> destination) noexcept
{
    auto& storage = detail::storage<ServiceT>();
    auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
    if (!guard) {
        return fail(query_error(guard.error()));
    }
    const auto oldest = storage.next_sequence > storage.history.size()
                            ? storage.next_sequence - storage.history.size()
                            : std::uint64_t{1};
    auto sequence = (std::max)(cursor.sequence, oldest);
    std::size_t written{};
    while (sequence < storage.next_sequence && written < destination.size()) {
        destination[written++] = storage.history[(sequence - 1) % storage.history.size()];
        ++sequence;
    }
    return ResponsePage{
        .next = ResponseCursor{sequence}, .written = written, .overwritten = storage.overwritten};
}

} // namespace detail

template <typename Application = DefaultApplication>
[[nodiscard]] Result<StateRecord, Error> state() noexcept
{
    return detail::copy_state<detail::BoundService<Application>>();
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<WatchdogRecord, Error> watchdog() noexcept
{
    return detail::copy_watchdog<detail::BoundService<Application>>();
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<SubjectRecord, Error> record() noexcept
{
    return detail::copy_subject<detail::BoundService<Application>, Component>();
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<ResponsePage, Error> responses(ResponseCursor cursor,
                                                    std::span<ResponseRecord> destination) noexcept
{
    return detail::copy_responses<detail::BoundService<Application>>(cursor, destination);
}

template <typename Application = DefaultApplication> void wake() noexcept
{
    detail::BoundService<Application>::state_storage.wake.give();
}

template <typename Application = DefaultApplication> struct Of
{
    [[nodiscard]] static Result<StateRecord, Error> state() noexcept
    {
        return supervisor::state<Application>();
    }

    [[nodiscard]] static Result<WatchdogRecord, Error> watchdog() noexcept
    {
        return supervisor::watchdog<Application>();
    }

    template <typename Component>
    [[nodiscard]] static Result<SubjectRecord, Error> record() noexcept
    {
        return supervisor::record<Component, Application>();
    }
};

#else

[[nodiscard]] constexpr Error disabled_error() noexcept
{
    return {
        .status = Status::NotSupported, .reason = Reason::Disabled, .operation = Operation::Query};
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<StateRecord, Error> state() noexcept
{
    return fail(disabled_error());
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<WatchdogRecord, Error> watchdog() noexcept
{
    return fail(disabled_error());
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<SubjectRecord, Error> record() noexcept
{
    return fail(disabled_error());
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<ResponsePage, Error> responses(ResponseCursor,
                                                    std::span<ResponseRecord>) noexcept
{
    return fail(disabled_error());
}

#endif

} // namespace solar::supervisor
