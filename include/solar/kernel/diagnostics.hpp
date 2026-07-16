#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/priority.hpp"
#include "solar/kernel/thread.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

inline constexpr bool thread_names_available = IS_ENABLED(CONFIG_THREAD_NAME);
inline constexpr bool stack_diagnostics_available =
    IS_ENABLED(CONFIG_INIT_STACKS) && IS_ENABLED(CONFIG_THREAD_STACK_INFO);
inline constexpr bool runtime_diagnostics_available = IS_ENABLED(CONFIG_THREAD_RUNTIME_STATS);
inline constexpr bool thread_enumeration_available = IS_ENABLED(CONFIG_THREAD_MONITOR);
inline constexpr bool runtime_stack_safety_available =
    stack_diagnostics_available && IS_ENABLED(CONFIG_THREAD_RUNTIME_STACK_SAFETY);

struct StackUsage
{
    std::size_t size{};
    std::size_t used{};
    std::size_t unused{};
};

struct ThreadRuntimeStats
{
    std::optional<std::uint64_t> execution_cycles{};
    std::optional<std::uint64_t> total_cycles{};
    std::optional<std::uint64_t> current_cycles{};
    std::optional<std::uint64_t> peak_cycles{};
    std::optional<std::uint64_t> average_cycles{};
    std::optional<std::uint64_t> idle_cycles{};
};

struct ThreadDiagnostics
{
    ThreadId id{};
    std::optional<std::string_view> name{};
    ThreadExecutionState state{ThreadExecutionState::Unknown};
    std::optional<Priority> priority{};
    std::optional<std::size_t> stack_size{};
    std::optional<std::size_t> stack_used{};
    std::optional<std::size_t> stack_unused{};
    std::optional<std::size_t> stack_warning_margin{};
    std::optional<ThreadRuntimeStats> runtime{};
    TimePoint observed_at{};
};

struct StackSafetyCheck
{
    std::size_t unused{};
    bool threshold_crossed{};
};

[[nodiscard]] inline Result<bool> thread_exited(ThreadId thread) noexcept
{
    if (thread == nullptr) {
        return fail(Status::Invalid);
    }
    if (thread == k_current_get()) {
        return false;
    }
    const int result = k_thread_join(thread, K_NO_WAIT);
    if (result == 0) {
        return true;
    }
    if (result == -EBUSY) {
        return false;
    }
    return fail(status_from_errno(result));
}

[[nodiscard]] inline Result<StackUsage>
stack_usage(ThreadId thread, std::optional<std::size_t> configured_size = std::nullopt) noexcept
{
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
    if (thread == nullptr) {
        return fail(Status::Invalid);
    }

    std::size_t unused{};
    const int result = k_thread_stack_space_get(thread, &unused);
    if (result != 0) {
        return fail(status_from_errno(result));
    }
    const auto size = configured_size.value_or(thread->stack_info.size);
    return StackUsage{.size = size,
                      .used = size >= unused ? size - unused : 0,
                      .unused = unused};
#else
    (void)thread;
    (void)configured_size;
    return fail(Status::NotSupported);
#endif
}

[[nodiscard]] inline Result<ThreadRuntimeStats> runtime_stats(ThreadId thread) noexcept
{
#if defined(CONFIG_THREAD_RUNTIME_STATS)
    if (thread == nullptr) {
        return fail(Status::Invalid);
    }
    k_thread_runtime_stats_t native{};
    const int result = k_thread_runtime_stats_get(thread, &native);
    if (result != 0) {
        return fail(status_from_errno(result));
    }

    ThreadRuntimeStats stats{};
#if defined(CONFIG_SCHED_THREAD_USAGE)
    stats.execution_cycles = native.execution_cycles;
    stats.total_cycles = native.total_cycles;
#endif
#if defined(CONFIG_SCHED_THREAD_USAGE_ANALYSIS)
    stats.current_cycles = native.current_cycles;
    stats.peak_cycles = native.peak_cycles;
    stats.average_cycles = native.average_cycles;
#endif
#if defined(CONFIG_SCHED_THREAD_USAGE_ALL)
    stats.idle_cycles = native.idle_cycles;
#endif
    return stats;
#else
    (void)thread;
    return fail(Status::NotSupported);
#endif
}

[[nodiscard]] inline Status set_runtime_stats(ThreadId thread, bool enabled) noexcept
{
#if defined(CONFIG_THREAD_RUNTIME_STATS)
    if (thread == nullptr) {
        return Status::Invalid;
    }
    return status_from_errno(enabled ? k_thread_runtime_stats_enable(thread)
                                     : k_thread_runtime_stats_disable(thread));
#else
    (void)thread;
    (void)enabled;
    return Status::NotSupported;
#endif
}

[[nodiscard]] inline Status set_stack_warning_margin(ThreadId thread,
                                                      std::size_t margin) noexcept
{
#if defined(CONFIG_THREAD_RUNTIME_STACK_SAFETY) && defined(CONFIG_INIT_STACKS) &&               \
    defined(CONFIG_THREAD_STACK_INFO)
    if (thread == nullptr) {
        return Status::Invalid;
    }
    // Zephyr 4.4's public syscall names do not match the implementation names.
    // Keep the compatibility workaround local to this native wrapper.
    if (margin > thread->stack_info.size) {
        return Status::Invalid;
    }
    thread->stack_info.usage.unused_threshold = margin;
    return Status::Ok;
#else
    (void)thread;
    (void)margin;
    return Status::NotSupported;
#endif
}

namespace detail
{

inline void stack_safety_handler(const k_thread*, std::size_t, void* argument) noexcept
{
    *static_cast<bool*>(argument) = true;
}

} // namespace detail

[[nodiscard]] inline Result<StackSafetyCheck> check_stack_safety(ThreadId thread,
                                                                 bool full_scan) noexcept
{
#if defined(CONFIG_THREAD_RUNTIME_STACK_SAFETY) && defined(CONFIG_INIT_STACKS) &&               \
    defined(CONFIG_THREAD_STACK_INFO)
    if (thread == nullptr) {
        return fail(Status::Invalid);
    }
    std::size_t unused{};
    bool crossed{};
    const int result = full_scan
                           ? k_thread_runtime_stack_safety_full_check(
                                 thread, &unused, &detail::stack_safety_handler, &crossed)
                           : k_thread_runtime_stack_safety_threshold_check(
                                 thread, &unused, &detail::stack_safety_handler, &crossed);
    if (result != 0) {
        return fail(status_from_errno(result));
    }
    return StackSafetyCheck{.unused = unused, .threshold_crossed = crossed};
#else
    (void)thread;
    (void)full_scan;
    return fail(Status::NotSupported);
#endif
}

[[nodiscard]] inline Result<ThreadDiagnostics>
thread_diagnostics(ThreadId thread,
                   std::optional<std::size_t> configured_stack_size = std::nullopt) noexcept
{
    if (thread == nullptr) {
        return fail(Status::Invalid);
    }

    ThreadDiagnostics diagnostics{.id = thread, .observed_at = now()};
    const auto exited = thread_exited(thread);
    if (exited) {
        diagnostics.state = *exited ? ThreadExecutionState::Exited : ThreadExecutionState::Running;
    }

#if defined(CONFIG_THREAD_NAME)
    if (const auto* name = k_thread_name_get(thread); name != nullptr && name[0] != '\0') {
        diagnostics.name = std::string_view{name};
    }
#endif

    if (exited && !*exited) {
        if (const auto priority = Priority::from_native(k_thread_priority_get(thread)); priority) {
            diagnostics.priority = *priority;
        }
    }

    if (const auto usage = stack_usage(thread, configured_stack_size); usage) {
        diagnostics.stack_size = usage->size;
        diagnostics.stack_used = usage->used;
        diagnostics.stack_unused = usage->unused;
#if defined(CONFIG_THREAD_RUNTIME_STACK_SAFETY)
        diagnostics.stack_warning_margin = thread->stack_info.usage.unused_threshold;
#endif
    }

    if (const auto stats = runtime_stats(thread); stats) {
        diagnostics.runtime = *stats;
    }
    return diagnostics;
}

template <std::size_t StackBytes>
[[nodiscard]] Result<ThreadDiagnostics> thread_diagnostics(const Thread<StackBytes>& thread) noexcept
{
    auto diagnostics =
        thread_diagnostics(thread.native_handle(), Thread<StackBytes>::stack_size());
    if (diagnostics) {
        diagnostics->state = thread.state();
    }
    return diagnostics;
}

using ThreadVisitor = void (*)(ThreadId thread, void* user_data) noexcept;

namespace detail
{

struct ThreadVisitorContext
{
    ThreadVisitor visitor{};
    void* user_data{};
};

inline void visit_thread(const k_thread* thread, void* context_pointer) noexcept
{
    auto& context = *static_cast<ThreadVisitorContext*>(context_pointer);
    context.visitor(const_cast<k_thread*>(thread), context.user_data);
}

} // namespace detail

[[nodiscard]] inline Status for_each_thread_locked(ThreadVisitor visitor,
                                                    void* user_data = nullptr) noexcept
{
#if defined(CONFIG_THREAD_MONITOR)
    if (visitor == nullptr) {
        return Status::Invalid;
    }
    detail::ThreadVisitorContext context{.visitor = visitor, .user_data = user_data};
    k_thread_foreach(&detail::visit_thread, &context);
    return Status::Ok;
#else
    (void)visitor;
    (void)user_data;
    return Status::NotSupported;
#endif
}

[[nodiscard]] inline Status for_each_thread_unlocked(ThreadVisitor visitor,
                                                      void* user_data = nullptr) noexcept
{
#if defined(CONFIG_THREAD_MONITOR)
    if (visitor == nullptr) {
        return Status::Invalid;
    }
    detail::ThreadVisitorContext context{.visitor = visitor, .user_data = user_data};
    k_thread_foreach_unlocked(&detail::visit_thread, &context);
    return Status::Ok;
#else
    (void)visitor;
    (void)user_data;
    return Status::NotSupported;
#endif
}

} // namespace solar::kernel
