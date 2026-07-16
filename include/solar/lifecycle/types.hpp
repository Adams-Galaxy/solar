#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "solar/component.hpp"
#include "solar/core/status.hpp"

namespace solar::lifecycle
{

enum class SystemState : std::uint8_t
{
    Dormant,
    Initializing,
    Starting,
    Running,
    Stopping,
    Deinitializing,
    RollingBack,
    Stopped,
    Failed,
};

enum class ComponentState : std::uint8_t
{
    Registered,
    Initializing,
    Initialized,
    Starting,
    Running,
    Stopping,
    Stopped,
    Deinitializing,
    Deinitialized,
    Failed,
};

enum class ComponentCategory : std::uint8_t
{
    Device,
    Facility,
    Service,
    Executor,
};

enum class Operation : std::uint8_t
{
    None,
    Init,
    Start,
    PrepareExecution,
    ValidateExecution,
    ActivateExecution,
    RequestExecutionStop,
    ContainExecution,
    Stop,
    Deinit,
};

enum class HookOutcome : std::uint8_t
{
    NotPresent,
    NotAttempted,
    Succeeded,
    Failed,
};

struct HookRecord
{
    HookOutcome outcome{HookOutcome::NotPresent};
    Status status{Status::Ok};
    std::uint16_t attempts{};
};

struct CatalogSubject
{
    std::uint64_t kind{};
    std::uint16_t local_id{};
};

struct Failure
{
    component::LocalId component{};
    ComponentCategory category{ComponentCategory::Facility};
    Operation operation{Operation::None};
    Status status{Status::Error};
    bool primary{};
    std::optional<CatalogSubject> catalog{};
};

struct ComponentRecord
{
    component::DescriptorView descriptor{};
    component::LocalId local_id{};
    ComponentCategory category{ComponentCategory::Facility};
    ComponentState state{ComponentState::Registered};
    HookRecord init{};
    HookRecord start{};
    HookRecord stop{};
    HookRecord deinit{};
    Operation last_operation{Operation::None};
    Status last_status{Status::Ok};
    std::optional<Failure> first_failure{};
    std::uint32_t transitions{};
    std::uint32_t attempts{};
    bool init_succeeded{};
    bool start_succeeded{};
    bool execution_prepared{};
    bool execution_contained{true};
    bool cleanup_blocked{};
};

struct ComponentPage
{
    std::size_t offset{};
    std::size_t count{};
    std::size_t total{};

    [[nodiscard]] constexpr bool has_more() const noexcept
    {
        return offset + count < total;
    }
};

template <std::size_t Capacity> struct FailureDetails
{
    std::array<Failure, Capacity> entries{};
    std::size_t retained{};
    std::size_t total{};

    [[nodiscard]] constexpr bool truncated() const noexcept
    {
        return total > retained;
    }

    constexpr void add(const Failure& failure) noexcept
    {
        ++total;
        if (retained < Capacity) {
            entries[retained++] = failure;
        }
    }
};

inline constexpr std::size_t report_failure_capacity =
    CONFIG_SOLAR_LIFECYCLE_REPORT_FAILURE_CAPACITY;

struct BootReport
{
    SystemState initial_state{SystemState::Dormant};
    SystemState final_state{SystemState::Dormant};
    bool initialization_completed{};
    bool start_completed{};
    std::optional<Failure> primary_failure{};
    std::size_t initialized_components{};
    std::size_t started_components{};
    bool rollback_attempted{};
    bool rollback_completed{};
    FailureDetails<report_failure_capacity> cleanup_failures{};
    std::size_t uncontained_execution{};
    std::size_t preserved_dependencies{};
};

enum class BootErrorReason : std::uint8_t
{
    Busy,
    AlreadyRunning,
    RebootUnsupported,
    ComponentFailure,
    ExecutionFailure,
    InternalInvariant,
};

struct BootError
{
    BootErrorReason reason{BootErrorReason::InternalInvariant};
    Status status{Status::Error};
    std::optional<Failure> failure{};
};

struct StopReport
{
    SystemState initial_state{SystemState::Dormant};
    SystemState final_state{SystemState::Dormant};
    std::size_t clean_exits{};
    std::size_t forced_exits{};
    std::size_t join_timeouts{};
    std::size_t abort_attempts{};
    std::size_t abort_failures{};
    std::size_t stopped_components{};
    std::size_t deinitialized_components{};
    std::size_t uncontained_execution{};
    std::size_t preserved_dependencies{};
    FailureDetails<report_failure_capacity> failures{};
};

enum class StopErrorReason : std::uint8_t
{
    Busy,
    InvalidState,
    ShutdownFailed,
    InternalInvariant,
};

struct StopError
{
    StopErrorReason reason{StopErrorReason::InternalInvariant};
    Status status{Status::Error};
    std::optional<Failure> failure{};
};

struct Containment
{
    Status status{Status::Ok};
    bool contained{true};
    bool forced{};
    bool timed_out{};
    bool abort_attempted{};
    bool abort_failed{};
};

} // namespace solar::lifecycle

namespace solar
{

using lifecycle::BootError;
using lifecycle::BootErrorReason;
using lifecycle::BootReport;
using lifecycle::StopError;
using lifecycle::StopErrorReason;
using lifecycle::StopReport;

} // namespace solar
