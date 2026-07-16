#pragma once

#include <atomic>

#include "solar/execution/service.hpp"
#include "solar/kernel/spinlock.hpp"
#include "solar/kernel/thread.hpp"
#include "solar/lifecycle/engine.hpp"

namespace solar::execution::detail
{

struct ServiceStateKey
{};

[[nodiscard]] constexpr ThreadState thread_state(kernel::ThreadExecutionState state) noexcept
{
    return static_cast<ThreadState>(state);
}

template <typename System, typename Component> struct ServiceState
{
    using Policy = component_service_policy<Component>;
    static_assert(Policy::priority < CONFIG_NUM_PREEMPT_PRIORITIES,
                  "SOLAR_DIAGNOSTIC_INVALID_SERVICE_PRIORITY: service priority exceeds Zephyr's "
                  "configured preemptive range");

    kernel::Thread<Policy::stack_bytes> thread{};
    kernel::StopSource stop_source{};
    kernel::SpinLock record_lock{};
    ServiceRecord record{};

    template <typename Mutator> void mutate(Mutator&& mutator) noexcept
    {
        auto guard = record_lock.acquire();
        mutator(record);
    }

    [[nodiscard]] ServiceRecord copy() noexcept
    {
        auto guard = record_lock.acquire();
        record.thread_state = thread_state(thread.state());
        record.thread = thread.native_handle();
        return record;
    }
};

template <typename System, typename Component> [[nodiscard]] auto& service_state() noexcept
{
    using State = ServiceState<System, Component>;
    return System::template StateSlot<Component, ServiceStateKey, State>::value;
}

template <typename System, typename Component> void service_entry(void*) noexcept
{
    auto& state = service_state<System, Component>();
    state.mutate([&](ServiceRecord& record) {
        record.thread_started = true;
        record.running = true;
        record.thread_state = ThreadState::Running;
    });

    auto result = invoke_service<Component>(state.stop_source.token());
    const bool stop_requested = state.stop_source.stop_requested();
    Status status = result ? Status::Ok : result.error();
    if (result && !stop_requested) {
        status = Status::UnexpectedExit;
    }

    state.mutate([&](ServiceRecord& record) {
        record.running = false;
        record.exited = true;
        record.exited_after_stop = stop_requested;
        record.stop_requested = stop_requested;
        record.run_status = status;
        record.thread_state = ThreadState::Exited;
    });

    if (status != Status::Ok) {
        lifecycle::report_execution_failure<System, Component>(status);
    }
}

template <typename System, typename Component> [[nodiscard]] Result<void> prepare_service() noexcept
{
    static_assert(DeclaredService<Component>,
                  "SOLAR_DIAGNOSTIC_MISSING_SERVICE_EXECUTION: service must declare using "
                  "Execution = solar::execution::Service<...>");
    static_assert(!DeclaredService<Component> || ValidServiceRun<Component>,
                  "SOLAR_DIAGNOSTIC_INVALID_SERVICE_RUN: service must implement static Status or "
                  "Result<void> run(StopToken)");

    using Policy = component_service_policy<Component>;
    auto& state = service_state<System, Component>();
    using Components = typename System::Catalogs::template Of<component::Tag>;
    constexpr auto component_entry = Components::template entry<Component>();
    state.mutate([&](ServiceRecord& record) {
        record = {
            .component = component_entry.local_id,
            .stack_bytes = Policy::stack_bytes,
            .priority = static_cast<int>(Policy::priority),
            .stop_timeout = Policy::stop_timeout,
            .run_status = Status::NotReady,
            .thread_state = ThreadState::Empty,
            .containment = ContainmentState::NotPrepared,
            .abort_on_timeout = Policy::abort_on_timeout,
        };
    });

    const auto priority = kernel::Priority::template preemptive<Policy::priority>();
    const auto status = state.thread.prepare(
        &service_entry<System, Component>, nullptr,
        kernel::ThreadConfiguration{.priority = priority, .name = nullptr, .options = 0});
    state.mutate([&](ServiceRecord& record) {
        record.thread_created = status == Status::Ok;
        record.thread_state = thread_state(state.thread.state());
        record.thread = state.thread.native_handle();
        record.containment =
            status == Status::Ok ? ContainmentState::Prepared : ContainmentState::NotPrepared;
        record.run_status = status == Status::Ok ? Status::NotReady : status;
    });
    return status == Status::Ok ? Result<void>{} : Result<void>{fail(status)};
}

template <typename System, typename Component>
[[nodiscard]] Result<void> validate_service() noexcept
{
    const auto& state = service_state<System, Component>();
    return state.thread.state() == kernel::ThreadExecutionState::Prepared
               ? Result<void>{}
               : Result<void>{fail(Status::NotReady)};
}

template <typename System, typename Component> void activate_service() noexcept
{
    auto& state = service_state<System, Component>();
    const auto status = state.thread.start();
    if (status != Status::Ok) {
        state.mutate([&](ServiceRecord& record) {
            record.run_status = status;
            record.containment = ContainmentState::Uncontained;
        });
        lifecycle::report_execution_failure<System, Component>(status);
    }
}

template <typename System, typename Component> [[nodiscard]] Status request_service_stop() noexcept
{
    auto& state = service_state<System, Component>();
    auto result = state.stop_source.request_stop();
    if constexpr (requires { Component::notify_stop(); }) {
        Component::notify_stop();
    }
    state.mutate([&](ServiceRecord& record) { record.stop_requested = true; });
    return result ? Status::Ok : result.error();
}

template <typename System, typename Component>
[[nodiscard]] lifecycle::Containment contain_service() noexcept
{
    using Policy = component_service_policy<Component>;
    auto& state = service_state<System, Component>();
    state.mutate([](ServiceRecord& record) { record.join_attempted = true; });

    auto status = state.thread.join(kernel::Timeout::after(Policy::stop_timeout));
    if (status == Status::Ok) {
        const auto run_status = state.copy().run_status;
        state.mutate([](ServiceRecord& record) {
            record.joined = true;
            record.running = false;
            record.containment = ContainmentState::Clean;
        });
        return {.status = run_status == Status::NotReady ? Status::Ok : run_status,
                .contained = true};
    }

    const bool timed_out = status == Status::Timeout || status == Status::WouldBlock;
    state.mutate([&](ServiceRecord& record) { record.timed_out = timed_out; });
    if (!timed_out) {
        state.mutate(
            [](ServiceRecord& record) { record.containment = ContainmentState::Uncontained; });
        return {.status = status, .contained = false};
    }

    if constexpr (Policy::abort_on_timeout) {
        state.mutate([](ServiceRecord& record) { record.abort_attempted = true; });
        const auto abort_status = state.thread.abort();
        const bool aborted = abort_status == Status::Ok || abort_status == Status::Already;
        state.mutate([&](ServiceRecord& record) {
            record.abort_succeeded = aborted;
            record.abort_failed = !aborted;
            record.running = !aborted;
            record.containment = aborted ? ContainmentState::Forced : ContainmentState::Uncontained;
        });
        return {
            .status = aborted ? Status::Timeout : abort_status,
            .contained = aborted,
            .forced = aborted,
            .timed_out = true,
            .abort_attempted = true,
            .abort_failed = !aborted,
        };
    }

    state.mutate([](ServiceRecord& record) { record.containment = ContainmentState::Uncontained; });
    return {.status = Status::Timeout, .contained = false, .timed_out = true};
}

} // namespace solar::execution::detail

namespace solar::lifecycle
{

template <typename System, typename Component>
    requires(std::is_same_v<typename System::Effective::template CategoryOf<Component>,
                            category::Service>)
struct ExecutionProtocol<System, Component>
{
    static constexpr bool participates = true;

    [[nodiscard]] static Result<void> prepare() noexcept
    {
        return execution::detail::prepare_service<System, Component>();
    }

    [[nodiscard]] static Result<void> validate_activation() noexcept
    {
        return execution::detail::validate_service<System, Component>();
    }

    static void activate() noexcept
    {
        execution::detail::activate_service<System, Component>();
    }

    [[nodiscard]] static Status request_stop() noexcept
    {
        return execution::detail::request_service_stop<System, Component>();
    }

    [[nodiscard]] static Containment contain() noexcept
    {
        return execution::detail::contain_service<System, Component>();
    }

    template <typename Visitor> static void visit_uncontained_dependencies(Visitor&&) noexcept {}
};

} // namespace solar::lifecycle
