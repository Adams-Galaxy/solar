#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "solar/core/status.hpp"
#include "solar/health/types.hpp"

namespace solar::supervisor
{

using Tick = std::int64_t;
using SubjectId = health::SubjectId;

enum class ServiceState : std::uint8_t
{
    Stopped,
    Starting,
    Running,
    Stopping,
    Faulted,
};

enum class Phase : std::uint8_t
{
    Idle,
    RefreshHealth,
    EvaluatePolicy,
    ExecuteResponses,
    EvaluateWatchdog,
    Complete,
};

enum class Trigger : std::uint8_t
{
    Fault,
    Degraded,
    Stall,
    RecoveryFailure,
};

enum class Action : std::uint8_t
{
    Observe,
    Warn,
    Latch,
    TryRecover,
    EnterSafeState,
    RequestStop,
    RequestSystemStop,
    RequestReboot,
    StopFeedingWatchdog,
    Panic,
};

enum class Outcome : std::uint8_t
{
    Succeeded,
    Failed,
    Deferred,
    Unsupported,
    Requested,
    Suppressed,
};

enum class Operation : std::uint8_t
{
    Start,
    Cycle,
    Response,
    Recover,
    SafeState,
    Stop,
    Watchdog,
    Query,
};

enum class Reason : std::uint8_t
{
    Disabled,
    NotReady,
    Unsupported,
    HealthRefreshFailed,
    ResponseFailed,
    RecoveryFailed,
    SafeStateFailed,
    WatchdogFailed,
    BudgetExhausted,
    InternalInvariant,
};

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::InternalInvariant};
    Operation operation{Operation::Query};
    SubjectId subject{};
    std::uint32_t detail{};

    constexpr bool operator==(const Error&) const = default;
};

struct StateRecord
{
    ServiceState state{ServiceState::Stopped};
    Phase last_phase{Phase::Idle};
    Tick started_at{};
    Tick last_cycle_started{};
    Tick last_cycle_completed{};
    Tick last_cycle_duration{};
    Tick maximum_cycle_duration{};
    std::uint64_t cycle_generation{};
    std::uint64_t health_generation{};
    std::uint32_t cycles{};
    std::uint32_t overruns{};
    std::uint32_t refresh_failures{};
    std::uint32_t deferred_responses{};
    bool cycle_complete{};
    bool system_stop_requested{};
    bool reboot_requested{};
    bool panic_requested{};
};

struct ResponseRecord
{
    std::uint64_t sequence{};
    SubjectId subject{};
    Trigger trigger{Trigger::Fault};
    Action action{Action::Observe};
    Outcome outcome{Outcome::Succeeded};
    Status status{Status::Ok};
    Tick attempted_at{};
    std::uint32_t attempt{};
    std::uint64_t health_generation{};
};

struct SubjectRecord
{
    SubjectId subject{};
    Trigger last_trigger{Trigger::Fault};
    Action last_action{Action::Observe};
    Outcome last_outcome{Outcome::Succeeded};
    Tick last_response_at{};
    std::uint32_t attempts{};
    bool active{};
    bool latched{};
};

struct WatchdogRecord
{
    bool configured{};
    bool enabled{};
    bool feed_permitted{};
    bool deliberately_withheld{};
    Tick last_feed_at{};
    std::uint32_t feeds{};
    std::uint32_t withheld{};
    std::uint32_t failures{};
    Status last_status{Status::Ok};
};

struct ResponseCursor
{
    std::uint64_t sequence{1};
};

struct ResponsePage
{
    ResponseCursor next{};
    std::size_t written{};
    std::uint64_t overwritten{};
};

} // namespace solar::supervisor
