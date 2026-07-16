#pragma once

#include <type_traits>

#include <zephyr/sys/util.h>

#include "solar/execution/work_queue.hpp"
#include "solar/kernel/stop.hpp"

namespace solar::execution
{

template <typename Declaration> struct service_policy_traits
{
    static constexpr bool valid = false;
};

template <typename... Policies> struct service_policy_traits<Service<Policies...>>
{
    static_assert(
        ((detail::IsStackSize<Policies>::value || detail::IsPriority<Policies>::value ||
          detail::IsStopTimeout<Policies>::value || detail::IsAbortOnTimeout<Policies>::value) &&
         ...),
        "SOLAR_DIAGNOSTIC_UNKNOWN_SERVICE_POLICY: Service contains an unsupported policy");

    static constexpr bool valid = true;
    using Stack = typename detail::SelectOption<StackSize<CONFIG_SOLAR_SERVICE_STACK_SIZE>,
                                                detail::IsStackSize, Policies...>::type;
    using PriorityPolicy =
        typename detail::SelectOption<Priority<0>, detail::IsPriority, Policies...>::type;
    using StopTimeoutPolicy =
        typename detail::SelectOption<void, detail::IsStopTimeout, Policies...>::type;
    using AbortPolicy =
        typename detail::SelectOption<void, detail::IsAbortOnTimeout, Policies...>::type;

    static constexpr std::size_t stack_bytes = Stack::value;
    static constexpr std::uint32_t priority = PriorityPolicy::value;
    static constexpr Milliseconds stop_timeout = [] {
        if constexpr (std::is_void_v<StopTimeoutPolicy>) {
            return Milliseconds{CONFIG_SOLAR_SERVICE_STOP_TIMEOUT_MS};
        } else {
            return detail::duration_milliseconds(StopTimeoutPolicy::value);
        }
    }();
    static constexpr bool abort_on_timeout = [] {
        if constexpr (std::is_void_v<AbortPolicy>) {
            return bool{IS_ENABLED(CONFIG_SOLAR_SERVICE_ABORT_ON_STOP_TIMEOUT)};
        } else {
            return AbortPolicy::value;
        }
    }();
};

template <typename Component, typename = void> struct component_service_policy
{
    static constexpr bool declared = false;
    static constexpr bool valid = false;
    static constexpr std::size_t stack_bytes = CONFIG_SOLAR_SERVICE_STACK_SIZE;
    static constexpr std::uint32_t priority = 0;
    static constexpr Milliseconds stop_timeout{CONFIG_SOLAR_SERVICE_STOP_TIMEOUT_MS};
    static constexpr bool abort_on_timeout =
        bool{IS_ENABLED(CONFIG_SOLAR_SERVICE_ABORT_ON_STOP_TIMEOUT)};
};

template <typename Component>
struct component_service_policy<Component, std::void_t<typename Component::Execution>>
    : service_policy_traits<typename Component::Execution>
{
    static constexpr bool declared = true;
};

template <typename Component>
concept DeclaredService =
    component_service_policy<Component>::declared && component_service_policy<Component>::valid;

namespace detail
{

template <typename Component>
concept StatusServiceRun = requires(StopToken token) {
    { Component::run(token) } -> std::same_as<Status>;
};

template <typename Component>
concept ResultServiceRun = requires(StopToken token) {
    { Component::run(token) } -> std::same_as<Result<void>>;
};

template <typename Component>
concept ValidServiceRun = StatusServiceRun<Component> || ResultServiceRun<Component>;

template <typename Component> [[nodiscard]] Result<void> invoke_service(StopToken token) noexcept
{
    static_assert(!DeclaredService<Component> || ValidServiceRun<Component>,
                  "SOLAR_DIAGNOSTIC_INVALID_SERVICE_RUN: service must implement static Status or "
                  "Result<void> run(StopToken)");
    if constexpr (StatusServiceRun<Component>) {
        const auto status = Component::run(token);
        return status == Status::Ok ? Result<void>{} : Result<void>{fail(status)};
    } else if constexpr (ResultServiceRun<Component>) {
        return Component::run(token);
    } else {
        return fail(Status::Invalid);
    }
}

} // namespace detail

} // namespace solar::execution
