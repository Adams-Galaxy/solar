#pragma once

#include <chrono>
#include <type_traits>

#include <zephyr/sys/util.h>

#include "solar/component.hpp"
#include "solar/execution/policy.hpp"
#include "solar/execution/registration.hpp"
#include "solar/system/sections.hpp"

namespace solar::execution
{

namespace detail
{

template <typename T> struct IsStackSize : std::false_type
{};

template <std::size_t Bytes> struct IsStackSize<StackSize<Bytes>> : std::true_type
{};

template <typename T> struct IsPriority : std::false_type
{};

template <std::uint32_t Level> struct IsPriority<Priority<Level>> : std::true_type
{};

template <typename T> struct IsStopTimeout : std::false_type
{};

template <DurationValue Value> struct IsStopTimeout<StopTimeout<Value>> : std::true_type
{};

template <typename T> struct IsWorkTimeout : std::false_type
{};

template <DurationValue Value> struct IsWorkTimeout<WorkTimeout<Value>> : std::true_type
{};

template <typename T> struct IsAbortOnTimeout : std::false_type
{};

template <bool Enabled> struct IsAbortOnTimeout<AbortOnTimeout<Enabled>> : std::true_type
{};

template <typename T>
inline constexpr bool is_yield_policy_v =
    std::is_same_v<T, YieldBetweenItems> || std::is_same_v<T, NoYieldBetweenItems>;

template <typename T> struct IsYieldPolicy : std::bool_constant<is_yield_policy_v<T>>
{};

[[nodiscard]] consteval Milliseconds duration_milliseconds(DurationValue value)
{
    return std::chrono::ceil<Milliseconds>(value.duration());
}

template <typename... Policies> struct WorkQueuePolicy
{
    static_assert(
        ((IsStackSize<Policies>::value || IsPriority<Policies>::value ||
          IsStopTimeout<Policies>::value || IsWorkTimeout<Policies>::value ||
          IsAbortOnTimeout<Policies>::value || is_yield_policy_v<Policies> ||
          IsDependencies<Policies>::value) &&
         ...),
        "SOLAR_DIAGNOSTIC_UNKNOWN_WORK_QUEUE_POLICY: WorkQueue contains an unsupported policy");

    using Stack = typename SelectOption<void, IsStackSize, Policies...>::type;
    static_assert(!std::is_void_v<Stack>, "SOLAR_DIAGNOSTIC_WORK_QUEUE_STACK_REQUIRED: application "
                                          "WorkQueue must declare StackSize<N>");

    using PriorityPolicy = typename SelectOption<Priority<0>, IsPriority, Policies...>::type;
    using StopTimeoutPolicy = typename SelectOption<void, IsStopTimeout, Policies...>::type;
    using WorkTimeoutPolicy = typename SelectOption<void, IsWorkTimeout, Policies...>::type;
    using AbortPolicy = typename SelectOption<void, IsAbortOnTimeout, Policies...>::type;
    using YieldPolicy = typename SelectOption<YieldBetweenItems, IsYieldPolicy, Policies...>::type;
    using DependenciesPolicy =
        typename SelectOption<DependsOn<>, IsDependencies, Policies...>::type;

    static constexpr std::size_t stack_bytes = Stack::value;
    static constexpr std::uint32_t priority = PriorityPolicy::value;
    static constexpr bool yields_between_items = std::is_same_v<YieldPolicy, YieldBetweenItems>;
    static constexpr Milliseconds work_timeout = [] {
        if constexpr (std::is_void_v<WorkTimeoutPolicy>) {
            return Milliseconds{0};
        } else {
            return duration_milliseconds(WorkTimeoutPolicy::value);
        }
    }();
    static constexpr Milliseconds stop_timeout = [] {
        if constexpr (std::is_void_v<StopTimeoutPolicy>) {
            return Milliseconds{CONFIG_SOLAR_EXECUTOR_STOP_TIMEOUT_MS};
        } else {
            return duration_milliseconds(StopTimeoutPolicy::value);
        }
    }();
    static constexpr bool abort_on_timeout = [] {
        if constexpr (std::is_void_v<AbortPolicy>) {
            return bool{IS_ENABLED(CONFIG_SOLAR_EXECUTOR_ABORT_ON_STOP_TIMEOUT)};
        } else {
            return AbortPolicy::value;
        }
    }();
    using Dependencies = typename DependenciesPolicy::ComponentsList;
};

} // namespace detail

template <FixedString Name, typename... Policies> struct WorkQueue
{
    using SolarExecutionWorkQueueMarker = void;
    using Policy = detail::WorkQueuePolicy<Policies...>;

    template <typename List> struct RebindDependencies;

    template <typename... Components> struct RebindDependencies<TypeList<Components...>>
    {
        using type = solar::Dependencies<Components...>;
    };

    using Dependencies = typename RebindDependencies<typename Policy::Dependencies>::type;

    static constexpr component::Descriptor descriptor{
        .name = Name.view(),
        .description = "Solar owned workqueue executor",
    };
};

template <FixedString Name, typename... Policies> struct target_traits<WorkQueue<Name, Policies...>>
{
    static constexpr bool valid = true;
    static constexpr TargetKind kind = TargetKind::OwnedWorkQueue;
};

template <typename T>
concept WorkQueueExecutor = requires { typename T::SolarExecutionWorkQueueMarker; };

} // namespace solar::execution
