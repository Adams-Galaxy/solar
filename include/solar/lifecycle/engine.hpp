#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include "solar/kernel/mutex.hpp"
#include "solar/lifecycle/hooks.hpp"
#include "solar/lifecycle/protocol.hpp"
#include "solar/system/binding.hpp"
#include "solar/system/system.hpp"

namespace solar::lifecycle
{

namespace detail
{

struct StateOwner;
struct StateKey;

template <typename Category> consteval ComponentCategory category_of()
{
    if constexpr (std::is_same_v<Category, category::Device>) {
        return ComponentCategory::Device;
    } else if constexpr (std::is_same_v<Category, category::Facility>) {
        return ComponentCategory::Facility;
    } else if constexpr (std::is_same_v<Category, category::Service>) {
        return ComponentCategory::Service;
    } else {
        static_assert(std::is_same_v<Category, category::Executor>);
        return ComponentCategory::Executor;
    }
}

template <typename System, typename Component> consteval std::size_t component_index()
{
    using Catalog = typename System::Catalogs::template Of<component::Tag>;
    return Catalog::template Entry<Component>::local_id.index();
}

template <typename Component> consteval HookRecord initial_init_hook()
{
    return {.outcome = HasInit<Component> ? HookOutcome::NotAttempted : HookOutcome::NotPresent};
}

template <typename Component> consteval HookRecord initial_start_hook()
{
    return {.outcome = HasStart<Component> ? HookOutcome::NotAttempted : HookOutcome::NotPresent};
}

template <typename Component> consteval HookRecord initial_stop_hook()
{
    return {.outcome = HasStop<Component> ? HookOutcome::NotAttempted : HookOutcome::NotPresent};
}

template <typename Component> consteval HookRecord initial_deinit_hook()
{
    return {.outcome = HasDeinit<Component> ? HookOutcome::NotAttempted : HookOutcome::NotPresent};
}

template <typename System> struct Storage
{
    static constexpr std::size_t component_count = list_size_v<typename System::Components>;
    static_assert(component_count <= CONFIG_SOLAR_LIFECYCLE_MAX_COMPONENTS,
                  "SOLAR_DIAGNOSTIC_LIFECYCLE_COMPONENT_CEILING: effective component count "
                  "exceeds CONFIG_SOLAR_LIFECYCLE_MAX_COMPONENTS");

    Storage() noexcept
    {
        for_each_type<typename System::Components>([this]<typename Component> {
            static_assert(validate_hooks<Component>());
            constexpr auto index = component_index<System, Component>();
            using Catalog = typename System::Catalogs::template Of<component::Tag>;
            records[index] = {
                .descriptor = Catalog::descriptors()[index],
                .local_id = Catalog::template Entry<Component>::local_id,
                .category = category_of<typename System::graph::template Category<Component>>(),
                .state = ComponentState::Registered,
                .init = initial_init_hook<Component>(),
                .start = initial_start_hook<Component>(),
                .stop = initial_stop_hook<Component>(),
                .deinit = initial_deinit_hook<Component>(),
            };
        });
    }

    std::atomic<SystemState> system_state{SystemState::Dormant};
    std::atomic_flag operation_active = ATOMIC_FLAG_INIT;
    kernel::Mutex records_mutex{};
    std::array<ComponentRecord, component_count> records{};
    std::optional<BootReport> last_boot{};
    std::optional<StopReport> last_stop{};
};

template <typename System>
using StateSlot = typename System::template StateSlot<StateOwner, StateKey, Storage<System>>;

template <typename System> [[nodiscard]] Storage<System>& storage() noexcept
{
    return StateSlot<System>::value;
}

template <typename System> class OperationGuard
{
  public:
    OperationGuard() noexcept
        : owned_(!storage<System>().operation_active.test_and_set(std::memory_order_acquire))
    {}

    ~OperationGuard()
    {
        if (owned_) {
            storage<System>().operation_active.clear(std::memory_order_release);
        }
    }

    OperationGuard(const OperationGuard&) = delete;
    OperationGuard& operator=(const OperationGuard&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return owned_;
    }

  private:
    bool owned_{};
};

template <typename System, typename Function>
[[nodiscard]] Result<void> with_records(Function&& function) noexcept
{
    auto lock = kernel::LockGuard<kernel::Mutex>::acquire(storage<System>().records_mutex);
    if (!lock) {
        return fail<solar::Error>(lock.error());
    }
    std::forward<Function>(function)(storage<System>());
    return {};
}

template <typename System, typename Component, typename Function>
[[nodiscard]] Result<void> mutate_record(Function&& function) noexcept
{
    return with_records<System>([&](auto& state) {
        std::forward<Function>(function)(state.records[component_index<System, Component>()]);
    });
}

template <typename System, typename Component>
[[nodiscard]] Failure make_failure(Operation operation, Status status, bool primary) noexcept
{
    using Catalog = typename System::Catalogs::template Of<component::Tag>;
    using Entry = typename Catalog::template Entry<Component>;
    return {
        .component = Entry::local_id,
        .category = category_of<typename System::graph::template Category<Component>>(),
        .operation = operation,
        .status = status,
        .primary = primary,
    };
}

template <typename System, typename Component> void record_failure(const Failure& failure) noexcept
{
    (void)mutate_record<System, Component>([&](ComponentRecord& record) {
        record.state = ComponentState::Failed;
        record.last_operation = failure.operation;
        record.last_status = failure.status;
        if (!record.first_failure) {
            record.first_failure = failure;
        }
        ++record.transitions;
    });
}

template <typename System, typename Component> [[nodiscard]] ComponentRecord record_copy() noexcept
{
    ComponentRecord copy{};
    (void)with_records<System>(
        [&](const auto& state) { copy = state.records[component_index<System, Component>()]; });
    return copy;
}

template <typename System, typename Component>
[[nodiscard]] Result<void> initialize_component() noexcept
{
    (void)mutate_record<System, Component>([](ComponentRecord& record) {
        record.state = ComponentState::Initializing;
        record.last_operation = Operation::Init;
        ++record.transitions;
        ++record.attempts;
        if constexpr (HasInit<Component>) {
            ++record.init.attempts;
        }
    });

    auto result = invoke_init<Component>();
    (void)mutate_record<System, Component>([&](ComponentRecord& record) {
        record.last_status = result ? Status::Ok : status_of(result.error());
        if constexpr (HasInit<Component>) {
            record.init.outcome = result ? HookOutcome::Succeeded : HookOutcome::Failed;
            record.init.status = record.last_status;
        }
        if (result) {
            record.init_succeeded = true;
            record.state = ComponentState::Initialized;
        }
        ++record.transitions;
    });
    return result;
}

template <typename System, typename Component> [[nodiscard]] Result<void> start_component() noexcept
{
    (void)mutate_record<System, Component>([](ComponentRecord& record) {
        record.state = ComponentState::Starting;
        record.last_operation = Operation::Start;
        ++record.transitions;
        ++record.attempts;
        if constexpr (HasStart<Component>) {
            ++record.start.attempts;
        }
    });

    auto result = invoke_start<Component>();
    (void)mutate_record<System, Component>([&](ComponentRecord& record) {
        record.last_status = result ? Status::Ok : status_of(result.error());
        if constexpr (HasStart<Component>) {
            record.start.outcome = result ? HookOutcome::Succeeded : HookOutcome::Failed;
            record.start.status = record.last_status;
        }
    });
    if (!result) {
        return result;
    }

    if constexpr (ExecutionProtocol<System, Component>::participates) {
        (void)mutate_record<System, Component>([](ComponentRecord& record) {
            record.last_operation = Operation::PrepareExecution;
            ++record.attempts;
        });
        result = ExecutionProtocol<System, Component>::prepare();
        if (!result) {
            (void)mutate_record<System, Component>(
                [&](ComponentRecord& record) { record.last_status = status_of(result.error()); });
            return result;
        }
        (void)mutate_record<System, Component>([](ComponentRecord& record) {
            record.execution_prepared = true;
            record.execution_contained = false;
            record.last_status = Status::Ok;
        });
    }

    (void)mutate_record<System, Component>([](ComponentRecord& record) {
        record.start_succeeded = true;
        record.last_status = Status::Ok;
        ++record.transitions;
    });
    return {};
}

template <typename System, typename Component>
[[nodiscard]] Result<void> validate_execution() noexcept
{
    if constexpr (!ExecutionProtocol<System, Component>::participates) {
        return {};
    } else {
        if (!record_copy<System, Component>().execution_prepared) {
            return {};
        }
        (void)mutate_record<System, Component>([](ComponentRecord& record) {
            record.last_operation = Operation::ValidateExecution;
            ++record.attempts;
        });
        auto result = ExecutionProtocol<System, Component>::validate_activation();
        if (!result) {
            (void)mutate_record<System, Component>(
                [&](ComponentRecord& record) { record.last_status = status_of(result.error()); });
        }
        return result;
    }
}

template <typename System, typename Component> void activate_execution() noexcept
{
    if constexpr (ExecutionProtocol<System, Component>::participates) {
        if (record_copy<System, Component>().execution_prepared) {
            (void)mutate_record<System, Component>([](ComponentRecord& record) {
                record.last_operation = Operation::ActivateExecution;
                record.last_status = Status::Ok;
                ++record.attempts;
            });
            ExecutionProtocol<System, Component>::activate();
        }
    }
}

template <typename System, typename Component>
[[nodiscard]] Result<void>
stop_component(FailureDetails<report_failure_capacity>& failures) noexcept
{
    auto before = record_copy<System, Component>();
    if (!before.start_succeeded || before.cleanup_blocked) {
        return {};
    }

    (void)mutate_record<System, Component>([](ComponentRecord& record) {
        record.state = ComponentState::Stopping;
        record.last_operation = Operation::Stop;
        ++record.transitions;
        ++record.attempts;
        if constexpr (HasStop<Component>) {
            ++record.stop.attempts;
        }
    });
    auto result = invoke_stop<Component>();
    (void)mutate_record<System, Component>([&](ComponentRecord& record) {
        record.start_succeeded = false;
        record.last_status = result ? Status::Ok : status_of(result.error());
        if constexpr (HasStop<Component>) {
            record.stop.outcome = result ? HookOutcome::Succeeded : HookOutcome::Failed;
            record.stop.status = record.last_status;
        }
        record.state =
            result && !record.first_failure ? ComponentState::Stopped : ComponentState::Failed;
        ++record.transitions;
    });
    if (!result) {
        const auto failure =
            make_failure<System, Component>(Operation::Stop, status_of(result.error()), false);
        failures.add(failure);
        record_failure<System, Component>(failure);
    }
    return result;
}

template <typename System, typename Component>
[[nodiscard]] Result<void>
deinitialize_component(FailureDetails<report_failure_capacity>& failures) noexcept
{
    auto before = record_copy<System, Component>();
    if (!before.init_succeeded || before.cleanup_blocked) {
        return {};
    }

    (void)mutate_record<System, Component>([](ComponentRecord& record) {
        record.state = ComponentState::Deinitializing;
        record.last_operation = Operation::Deinit;
        ++record.transitions;
        ++record.attempts;
        if constexpr (HasDeinit<Component>) {
            ++record.deinit.attempts;
        }
    });
    auto result = invoke_deinit<Component>();
    (void)mutate_record<System, Component>([&](ComponentRecord& record) {
        record.init_succeeded = false;
        record.last_status = result ? Status::Ok : status_of(result.error());
        if constexpr (HasDeinit<Component>) {
            record.deinit.outcome = result ? HookOutcome::Succeeded : HookOutcome::Failed;
            record.deinit.status = record.last_status;
        }
        record.state = result && !record.first_failure ? ComponentState::Deinitialized
                                                       : ComponentState::Failed;
        ++record.transitions;
    });
    if (!result) {
        const auto failure =
            make_failure<System, Component>(Operation::Deinit, status_of(result.error()), false);
        failures.add(failure);
        record_failure<System, Component>(failure);
    }
    return result;
}

template <typename System, typename Component>
void mark_dependency_closure(std::size_t& preserved, bool count_component = true) noexcept
{
    bool newly_blocked = false;
    (void)mutate_record<System, Component>([&](ComponentRecord& record) {
        newly_blocked = !record.cleanup_blocked;
        record.cleanup_blocked = true;
    });
    if (newly_blocked && count_component) {
        ++preserved;
    }
    using Dependencies = typename System::Graph::template DependenciesOf<Component>;
    for_each_type<Dependencies>(
        [&]<typename Dependency> { mark_dependency_closure<System, Dependency>(preserved, true); });
}

template <typename System, typename Component, typename Report>
void request_execution_stop(Report& report) noexcept
{
    if constexpr (ExecutionProtocol<System, Component>::participates) {
        const auto record = record_copy<System, Component>();
        if (!record.execution_prepared || record.execution_contained) {
            return;
        }
        (void)mutate_record<System, Component>([](ComponentRecord& value) {
            value.last_operation = Operation::RequestExecutionStop;
            ++value.attempts;
        });
        const auto result = ExecutionProtocol<System, Component>::request_stop();
        if (!result && status_of(result.error()) != Status::Already) {
            const auto status = status_of(result.error());
            const auto failure =
                make_failure<System, Component>(Operation::RequestExecutionStop, status, false);
            report.failures.add(failure);
            record_failure<System, Component>(failure);
        }
    }
}

template <typename System, typename Component, typename Report>
void contain_execution(Report& report) noexcept
{
    if constexpr (ExecutionProtocol<System, Component>::participates) {
        const auto before = record_copy<System, Component>();
        if (!before.execution_prepared || before.execution_contained) {
            return;
        }
        (void)mutate_record<System, Component>([](ComponentRecord& value) {
            value.last_operation = Operation::ContainExecution;
            ++value.attempts;
        });
        const auto containment = ExecutionProtocol<System, Component>::contain();
        if (containment.timed_out) {
            ++report.join_timeouts;
        }
        if (containment.abort_attempted) {
            ++report.abort_attempts;
        }
        if (containment.abort_failed) {
            ++report.abort_failures;
        }
        if (containment.forced) {
            ++report.forced_exits;
        } else if (containment.contained && containment.status == Status::Ok) {
            ++report.clean_exits;
        }

        (void)mutate_record<System, Component>([&](ComponentRecord& record) {
            record.execution_contained = containment.contained;
            if (containment.contained) {
                record.execution_prepared = false;
            }
            record.last_status = containment.status;
        });

        if (!containment.contained) {
            ++report.uncontained_execution;
            mark_dependency_closure<System, Component>(report.preserved_dependencies, false);
            auto preserve = [&]<typename Dependency> {
                mark_dependency_closure<System, Dependency>(report.preserved_dependencies, true);
            };
            if constexpr (requires {
                              ExecutionProtocol<System, Component>::visit_uncontained_dependencies(
                                  preserve);
                          }) {
                ExecutionProtocol<System, Component>::visit_uncontained_dependencies(preserve);
            }
        }
        if (containment.status != Status::Ok || containment.forced || !containment.contained) {
            const auto status =
                containment.status == Status::Ok ? Status::UnexpectedExit : containment.status;
            const auto failure =
                make_failure<System, Component>(Operation::ContainExecution, status, false);
            report.failures.add(failure);
            record_failure<System, Component>(failure);
        }
    }
}

template <typename System, typename Report> void request_system_execution_stop(Report& report)
{
    if constexpr (SystemExecutionProtocol<System>::participates) {
        const auto result = SystemExecutionProtocol<System>::request_stop();
        if (!result && status_of(result.error()) != Status::Already) {
            const auto status = status_of(result.error());
            auto failure =
                SystemExecutionProtocol<System>::failure(Operation::RequestExecutionStop, status);
            failure.primary = false;
            report.failures.add(failure);
        }
    }
}

template <typename System, typename Report> void contain_system_execution(Report& report)
{
    if constexpr (SystemExecutionProtocol<System>::participates) {
        const auto containment = SystemExecutionProtocol<System>::contain();
        if (!containment.contained) {
            const auto count = SystemExecutionProtocol<System>::uncontained_count();
            report.uncontained_execution += count == 0 ? 1 : count;
            SystemExecutionProtocol<System>::visit_uncontained_dependencies(
                [&]<typename Dependency> {
                    mark_dependency_closure<System, Dependency>(report.preserved_dependencies,
                                                                true);
                });
        }
        if (containment.status != Status::Ok || containment.forced || !containment.contained) {
            auto failure = SystemExecutionProtocol<System>::failure(
                Operation::ContainExecution,
                containment.status == Status::Ok ? Status::UnexpectedExit : containment.status);
            failure.primary = false;
            report.failures.add(failure);
        }
    }
}

template <typename System> [[nodiscard]] bool has_resources() noexcept
{
    bool resources = false;
    (void)with_records<System>([&](const auto& state) {
        for (const auto& record : state.records) {
            resources = resources || record.init_succeeded || record.start_succeeded ||
                        (record.execution_prepared && !record.execution_contained);
        }
    });
    return resources;
}

template <typename System> void publish(const BootReport& report) noexcept
{
    (void)with_records<System>([&](auto& state) { state.last_boot = report; });
}

template <typename System> void publish(const StopReport& report) noexcept
{
    (void)with_records<System>([&](auto& state) { state.last_stop = report; });
}

template <typename System> [[nodiscard]] std::optional<Failure> commit_catalog_activation() noexcept
{
    std::optional<Failure> failure;
    for_each_type<typename System::Catalogs::CatalogTypes>([&]<typename Catalog> {
        using Protocol = CatalogActivationProtocol<typename Catalog::Tag>;
        if constexpr (Protocol::template participates<System>) {
            if (failure) {
                return;
            }
            auto result = Protocol::template commit<System>();
            if (!result) {
                failure = Protocol::template failure<System>(Operation::ActivateExecution,
                                                             status_of(result.error()));
                failure->primary = true;
            }
        }
    });
    return failure;
}

template <typename System> void activate_catalog_runtime() noexcept
{
    for_each_type<typename System::Catalogs::CatalogTypes>([]<typename Catalog> {
        using Protocol = CatalogActivationProtocol<typename Catalog::Tag>;
        if constexpr (Protocol::template participates<System>) {
            Protocol::template activate<System>();
        }
    });
}

struct RollbackReportAdapter
{
    FailureDetails<report_failure_capacity>& failures;
    std::size_t& uncontained_execution;
    std::size_t& preserved_dependencies;
    std::size_t clean_exits{};
    std::size_t forced_exits{};
    std::size_t join_timeouts{};
    std::size_t abort_attempts{};
    std::size_t abort_failures{};
};

template <typename System> void rollback(BootReport& report, bool started_phase) noexcept
{
    storage<System>().system_state.store(SystemState::RollingBack, std::memory_order_release);
    report.rollback_attempted = true;

    RollbackReportAdapter execution_report{.failures = report.cleanup_failures,
                                           .uncontained_execution = report.uncontained_execution,
                                           .preserved_dependencies = report.preserved_dependencies};
    if (started_phase) {
        request_system_execution_stop<System>(execution_report);
        contain_system_execution<System>(execution_report);
        for_each_type<typename System::Graph::ReverseTopologicalOrder>([&]<typename Component> {
            request_execution_stop<System, Component>(execution_report);
        });
        for_each_type<typename System::Graph::ReverseTopologicalOrder>(
            [&]<typename Component> { contain_execution<System, Component>(execution_report); });
        for_each_type<typename System::Graph::ReverseTopologicalOrder>([&]<typename Component> {
            (void)stop_component<System, Component>(report.cleanup_failures);
        });
    }

    for_each_type<typename System::Graph::ReverseTopologicalOrder>([&]<typename Component> {
        (void)deinitialize_component<System, Component>(report.cleanup_failures);
    });
    report.rollback_completed = report.uncontained_execution == 0;
    report.final_state = SystemState::Failed;
    storage<System>().system_state.store(SystemState::Failed, std::memory_order_release);
}

} // namespace detail

template <typename System, typename Component> void report_execution_failure(Status status) noexcept
{
    static_assert(contains_v<Component, typename System::Components>,
                  "SOLAR_DIAGNOSTIC_UNREGISTERED_EXECUTION_COMPONENT: execution failure owner is "
                  "absent from the effective component graph");
    const auto failure =
        detail::make_failure<System, Component>(Operation::ActivateExecution, status, false);
    detail::record_failure<System, Component>(failure);
    (void)detail::mutate_record<System, Component>([&](ComponentRecord& record) {
        record.state = ComponentState::Failed;
        record.last_operation = Operation::ActivateExecution;
        record.last_status = status;
        ++record.transitions;
    });

    auto& system_state = detail::storage<System>().system_state;
    auto current = system_state.load(std::memory_order_acquire);
    while ((current == SystemState::Running || current == SystemState::Starting) &&
           !system_state.compare_exchange_weak(current, SystemState::Failed,
                                               std::memory_order_acq_rel)) {
    }
}

template <typename System> struct Engine
{
    using Components = typename System::Components;
    static constexpr std::size_t component_count = list_size_v<Components>;

    [[nodiscard]] static Result<BootReport, BootError> boot() noexcept
    {
        detail::OperationGuard<System> operation;
        if (!operation) {
            return fail<BootError>(
                {.reason = BootErrorReason::Busy, .status = solar::Status::Busy});
        }

        auto& state = detail::storage<System>();
        const auto initial = state.system_state.load(std::memory_order_acquire);
        if (initial == SystemState::Running) {
            return fail<BootError>(
                {.reason = BootErrorReason::AlreadyRunning, .status = solar::Status::Already});
        }
        if (initial != SystemState::Dormant) {
            return fail<BootError>({.reason = BootErrorReason::RebootUnsupported,
                                    .status = solar::Status::NotSupported});
        }

        BootReport report{.initial_state = initial};
        std::optional<Failure> primary;
        bool execution_failure = false;

        state.system_state.store(SystemState::Initializing, std::memory_order_release);
        for_each_type<typename System::Graph::TopologicalOrder>([&]<typename Component> {
            if (primary) {
                return;
            }
            auto result = detail::initialize_component<System, Component>();
            if (!result) {
                primary = detail::make_failure<System, Component>(Operation::Init,
                                                                  status_of(result.error()), true);
                detail::record_failure<System, Component>(*primary);
                return;
            }
            ++report.initialized_components;
        });

        if (primary) {
            report.primary_failure = primary;
            detail::rollback<System>(report, false);
            detail::publish<System>(report);
            return fail<BootError>({.reason = BootErrorReason::ComponentFailure,
                                    .status = primary->status,
                                    .failure = primary});
        }
        report.initialization_completed = true;

        state.system_state.store(SystemState::Starting, std::memory_order_release);
        for_each_type<typename System::Graph::TopologicalOrder>([&]<typename Component> {
            if (primary) {
                return;
            }
            auto result = detail::start_component<System, Component>();
            if (!result) {
                const auto record = detail::record_copy<System, Component>();
                execution_failure = record.last_operation == Operation::PrepareExecution;
                primary = detail::make_failure<System, Component>(record.last_operation,
                                                                  status_of(result.error()), true);
                detail::record_failure<System, Component>(*primary);
                return;
            }
            ++report.started_components;
        });

        if (!primary) {
            if constexpr (SystemExecutionProtocol<System>::participates) {
                auto result = SystemExecutionProtocol<System>::prepare();
                if (!result) {
                    execution_failure = true;
                    primary = SystemExecutionProtocol<System>::failure(Operation::PrepareExecution,
                                                                       status_of(result.error()));
                    primary->primary = true;
                }
            }
        }

        if (!primary) {
            for_each_type<typename System::Graph::TopologicalOrder>([&]<typename Component> {
                if (primary) {
                    return;
                }
                auto result = detail::validate_execution<System, Component>();
                if (!result) {
                    execution_failure = true;
                    primary = detail::make_failure<System, Component>(
                        Operation::ValidateExecution, status_of(result.error()), true);
                    detail::record_failure<System, Component>(*primary);
                }
            });
        }

        if (!primary) {
            if constexpr (SystemExecutionProtocol<System>::participates) {
                auto result = SystemExecutionProtocol<System>::validate_activation();
                if (!result) {
                    execution_failure = true;
                    primary = SystemExecutionProtocol<System>::failure(Operation::ValidateExecution,
                                                                       status_of(result.error()));
                    primary->primary = true;
                }
            }
        }

        if (!primary) {
            primary = detail::commit_catalog_activation<System>();
        }

        if (primary) {
            report.primary_failure = primary;
            detail::rollback<System>(report, true);
            detail::publish<System>(report);
            return fail<BootError>({.reason = execution_failure ? BootErrorReason::ExecutionFailure
                                                                : BootErrorReason::ComponentFailure,
                                    .status = primary->status,
                                    .failure = primary});
        }

        for_each_type<typename System::Graph::TopologicalOrder>([]<typename Component> {
            (void)detail::mutate_record<System, Component>([](ComponentRecord& record) {
                record.state = ComponentState::Running;
                ++record.transitions;
            });
        });
        report.start_completed = true;
        report.final_state = SystemState::Running;
        state.system_state.store(SystemState::Running, std::memory_order_release);
        detail::publish<System>(report);

        if constexpr (SystemExecutionProtocol<System>::participates) {
            SystemExecutionProtocol<System>::activate();
        }
        for_each_type<typename System::Graph::TopologicalOrder>(
            []<typename Component> { detail::activate_execution<System, Component>(); });
        detail::activate_catalog_runtime<System>();
        return report;
    }

    [[nodiscard]] static Result<StopReport, StopError> stop() noexcept
    {
        detail::OperationGuard<System> operation;
        if (!operation) {
            return fail<StopError>(
                {.reason = StopErrorReason::Busy, .status = solar::Status::Busy});
        }

        auto& state = detail::storage<System>();
        const auto initial = state.system_state.load(std::memory_order_acquire);
        if (initial != SystemState::Running &&
            !(initial == SystemState::Failed && detail::has_resources<System>())) {
            return fail<StopError>(
                {.reason = StopErrorReason::InvalidState, .status = solar::Status::NotReady});
        }

        StopReport report{.initial_state = initial};
        state.system_state.store(SystemState::Stopping, std::memory_order_release);

        detail::request_system_execution_stop<System>(report);
        detail::contain_system_execution<System>(report);
        for_each_type<typename System::Graph::ReverseTopologicalOrder>(
            [&]<typename Component> { detail::request_execution_stop<System, Component>(report); });
        for_each_type<typename System::Graph::ReverseTopologicalOrder>(
            [&]<typename Component> { detail::contain_execution<System, Component>(report); });
        for_each_type<typename System::Graph::ReverseTopologicalOrder>([&]<typename Component> {
            const auto before = detail::record_copy<System, Component>();
            if (before.start_succeeded && !before.cleanup_blocked) {
                (void)detail::stop_component<System, Component>(report.failures);
                ++report.stopped_components;
            }
        });

        state.system_state.store(SystemState::Deinitializing, std::memory_order_release);
        for_each_type<typename System::Graph::ReverseTopologicalOrder>([&]<typename Component> {
            const auto before = detail::record_copy<System, Component>();
            if (before.init_succeeded && !before.cleanup_blocked) {
                (void)detail::deinitialize_component<System, Component>(report.failures);
                ++report.deinitialized_components;
            }
        });

        const bool clean = report.failures.total == 0 && report.uncontained_execution == 0;
        report.final_state = clean ? SystemState::Stopped : SystemState::Failed;
        state.system_state.store(report.final_state, std::memory_order_release);
        detail::publish<System>(report);
        if (!clean) {
            return fail<StopError>({.reason = StopErrorReason::ShutdownFailed,
                                    .status = solar::Status::Error,
                                    .failure = report.failures.retained == 0
                                                   ? std::nullopt
                                                   : std::optional{report.failures.entries[0]}});
        }
        return report;
    }

    [[nodiscard]] static SystemState state() noexcept
    {
        return detail::storage<System>().system_state.load(std::memory_order_acquire);
    }

    [[nodiscard]] static Result<std::array<ComponentRecord, component_count>> components() noexcept
    {
        std::array<ComponentRecord, component_count> copy{};
        const auto status =
            detail::with_records<System>([&](const auto& state) { copy = state.records; });
        if (!status) {
            return fail<solar::Error>(status.error());
        }
        return copy;
    }

    [[nodiscard]] static Result<ComponentPage>
    component_page(std::span<ComponentRecord> destination, std::size_t offset = 0) noexcept
    {
        if (offset > component_count) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        const auto available = component_count - offset;
        const auto count = destination.size() < available ? destination.size() : available;
        const auto status = detail::with_records<System>([&](const auto& state) {
            for (std::size_t index = 0; index < count; ++index) {
                destination[index] = state.records[offset + index];
            }
        });
        if (!status) {
            return fail<solar::Error>(status.error());
        }
        return ComponentPage{.offset = offset, .count = count, .total = component_count};
    }

    template <typename Component> [[nodiscard]] static Result<ComponentRecord> record() noexcept
    {
        static_assert(contains_v<Component, Components>,
                      "SOLAR_DIAGNOSTIC_UNREGISTERED_LIFECYCLE_COMPONENT: requested component is "
                      "absent from the effective System graph");
        ComponentRecord copy{};
        const auto status = detail::with_records<System>([&](const auto& state) {
            copy = state.records[detail::component_index<System, Component>()];
        });
        if (!status) {
            return fail<solar::Error>(status.error());
        }
        return copy;
    }

    [[nodiscard]] static Result<BootReport> boot_report() noexcept
    {
        std::optional<BootReport> copy;
        const auto status =
            detail::with_records<System>([&](const auto& state) { copy = state.last_boot; });
        if (!status) {
            return fail<solar::Error>(status.error());
        }
        if (!copy) {
            return fail<solar::Error>({.status = solar::Status::NotReady});
        }
        return *copy;
    }

    [[nodiscard]] static Result<StopReport> stop_report() noexcept
    {
        std::optional<StopReport> copy;
        const auto status =
            detail::with_records<System>([&](const auto& state) { copy = state.last_stop; });
        if (!status) {
            return fail<solar::Error>(status.error());
        }
        if (!copy) {
            return fail<solar::Error>({.status = solar::Status::NotReady});
        }
        return *copy;
    }
};

template <typename Application> struct Of
{
    using System = bound_system_t<Application>;

    [[nodiscard]] static SystemState state() noexcept
    {
        return Engine<System>::state();
    }

    [[nodiscard]] static auto components() noexcept
    {
        return Engine<System>::components();
    }

    [[nodiscard]] static auto component_page(std::span<ComponentRecord> destination,
                                             std::size_t offset = 0) noexcept
    {
        return Engine<System>::component_page(destination, offset);
    }

    template <typename Component> [[nodiscard]] static auto record() noexcept
    {
        return Engine<System>::template record<Component>();
    }

    [[nodiscard]] static auto boot_report() noexcept
    {
        return Engine<System>::boot_report();
    }

    [[nodiscard]] static auto stop_report() noexcept
    {
        return Engine<System>::stop_report();
    }
};

} // namespace solar::lifecycle

namespace solar
{

template <typename Application> [[nodiscard]] auto boot() noexcept
{
    using System = bound_system_t<Application>;
    return System::template boot<Application>();
}

template <typename Application> [[nodiscard]] auto stop() noexcept
{
    return lifecycle::Engine<bound_system_t<Application>>::stop();
}

namespace lifecycle
{

template <typename Application> [[nodiscard]] auto state() noexcept
{
    return Of<Application>::state();
}

template <typename Application> [[nodiscard]] auto components() noexcept
{
    return Of<Application>::components();
}

template <typename Application>
[[nodiscard]] auto component_page(std::span<ComponentRecord> destination,
                                  std::size_t offset) noexcept
{
    return Of<Application>::component_page(destination, offset);
}

template <typename Application>
[[nodiscard]] auto component_page(std::span<ComponentRecord> destination) noexcept
{
    return Of<Application>::component_page(destination, 0);
}

template <typename Component, typename Application> [[nodiscard]] auto record() noexcept
{
    return Of<Application>::template record<Component>();
}

template <typename Application> [[nodiscard]] auto boot_report() noexcept
{
    return Of<Application>::boot_report();
}

template <typename Application> [[nodiscard]] auto stop_report() noexcept
{
    return Of<Application>::stop_report();
}

} // namespace lifecycle

} // namespace solar
