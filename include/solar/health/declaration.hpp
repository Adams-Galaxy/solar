#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <type_traits>

#include "solar/core/time.hpp"
#include "solar/health/types.hpp"

namespace solar::health
{

template <DurationValue Period> struct Progress
{
    static_assert(Period.positive(),
                  "SOLAR_DIAGNOSTIC_HEALTH_PROGRESS_PERIOD: Progress period must be positive");
    static constexpr auto period = Period.duration();
};

template <std::size_t Bytes> struct StackMargin
{
    static_assert(Bytes > 0, "SOLAR_DIAGNOSTIC_HEALTH_STACK_MARGIN: StackMargin must be non-zero");
    static constexpr std::size_t bytes = Bytes;
};

struct Execution
{};

struct Signal
{};

template <typename Checker> struct Check
{
    using CheckerType = Checker;
};

template <typename Component, typename Declaration> struct OwnedMonitor
{
    using Subject = Component;
    using DeclarationType = Declaration;
};

namespace detail
{

template <typename T> struct IsProgress : std::false_type
{};

template <DurationValue Period> struct IsProgress<Progress<Period>> : std::true_type
{};

template <typename T> struct IsStackMargin : std::false_type
{};

template <std::size_t Bytes> struct IsStackMargin<StackMargin<Bytes>> : std::true_type
{};

template <typename T> struct IsCheckWrapper : std::false_type
{};

template <typename Checker> struct IsCheckWrapper<Check<Checker>> : std::true_type
{
    using CheckerType = Checker;
};

template <typename Declaration> consteval MonitorKind monitor_kind()
{
    if constexpr (IsProgress<Declaration>::value) {
        return MonitorKind::Progress;
    } else if constexpr (IsStackMargin<Declaration>::value) {
        return MonitorKind::StackMargin;
    } else if constexpr (std::is_same_v<Declaration, Execution>) {
        return MonitorKind::Execution;
    } else if constexpr (std::is_same_v<Declaration, Signal>) {
        return MonitorKind::Signal;
    } else {
        return MonitorKind::Check;
    }
}

template <typename Declaration> consteval std::int64_t period_ns()
{
    if constexpr (requires { Declaration::period; }) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Declaration::period).count();
    }
    return 0;
}

template <typename Declaration> consteval std::int64_t stale_after_ns()
{
    if constexpr (requires { Declaration::stale_after; }) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Declaration::stale_after)
            .count();
    } else if constexpr (period_ns<Declaration>() > 0) {
        return period_ns<Declaration>() * 2;
    }
    return 0;
}

template <typename Declaration> consteval std::size_t stack_margin_bytes()
{
    if constexpr (IsStackMargin<Declaration>::value) {
        return Declaration::bytes;
    }
    return 0;
}

template <typename Declaration> consteval bool required()
{
    if constexpr (requires { Declaration::required; }) {
        return static_cast<bool>(Declaration::required);
    }
    return IsProgress<Declaration>::value || std::is_same_v<Declaration, Execution>;
}

template <typename Declaration> struct CheckerFor
{
    using type = Declaration;
};

template <typename Checker> struct CheckerFor<Check<Checker>>
{
    using type = Checker;
};

template <typename Declaration> using checker_for_t = typename CheckerFor<Declaration>::type;

template <typename Declaration> consteval std::string_view default_name()
{
    constexpr auto kind = monitor_kind<Declaration>();
    switch (kind) {
    case MonitorKind::Progress:
        return "progress";
    case MonitorKind::StackMargin:
        return "stack-margin";
    case MonitorKind::Execution:
        return "execution";
    case MonitorKind::Signal:
        return "signal";
    case MonitorKind::Check:
        return "check";
    }
    return "check";
}

template <typename Declaration> consteval CheckDescriptor descriptor()
{
    using Source = checker_for_t<Declaration>;
    if constexpr (requires {
                      { Source::descriptor } -> std::convertible_to<CheckDescriptor>;
                  }) {
        auto value = static_cast<CheckDescriptor>(Source::descriptor);
        value.kind = monitor_kind<Declaration>();
        if (value.period_ns == 0) {
            value.period_ns = period_ns<Source>();
        }
        if (value.stale_after_ns == 0) {
            value.stale_after_ns = stale_after_ns<Source>();
        }
        if (value.stack_margin_bytes == 0) {
            value.stack_margin_bytes = stack_margin_bytes<Declaration>();
        }
        if constexpr (requires { Source::required; }) {
            value.required = static_cast<bool>(Source::required);
        } else if constexpr (IsProgress<Declaration>::value ||
                             std::is_same_v<Declaration, Execution>) {
            value.required = true;
        }
        return value;
    } else {
        return {.name = default_name<Declaration>(),
                .kind = monitor_kind<Declaration>(),
                .period_ns = period_ns<Declaration>(),
                .stale_after_ns = stale_after_ns<Declaration>(),
                .stack_margin_bytes = stack_margin_bytes<Declaration>(),
                .required = required<Declaration>()};
    }
}

} // namespace detail

} // namespace solar::health

template <typename Component, typename Declaration>
struct solar::descriptor_traits<solar::health::CheckTag,
                                solar::health::OwnedMonitor<Component, Declaration>>
{
    static constexpr auto descriptor = solar::health::detail::descriptor<Declaration>();
};
