#pragma once

#include <cstdint>
#include <limits>

#include "solar/core/status.hpp"

namespace solar
{

enum class LifecycleState : std::uint8_t
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
    Disabled,
};

enum class SystemState : std::uint8_t
{
    Dormant,
    Booting,
    Initialized,
    Starting,
    Running,
    Stopping,
    Stopped,
    Failed,
};

enum class LifecycleOperation : std::uint8_t
{
    None,
    Init,
    Start,
    Run,
    Stop,
    Deinit,
};

enum class ComponentKind : std::uint8_t
{
    Unknown,
    Board,
    Peripheral,
    Device,
    Facility,
    Service,
    Task,
    Channel,
};

constexpr const char *component_kind_name(ComponentKind kind)
{
    switch (kind)
    {
    case ComponentKind::Board:
        return "board";
    case ComponentKind::Peripheral:
        return "peripheral";
    case ComponentKind::Device:
        return "device";
    case ComponentKind::Facility:
        return "facility";
    case ComponentKind::Service:
        return "service";
    case ComponentKind::Task:
        return "task";
    case ComponentKind::Channel:
        return "channel";
    case ComponentKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

class ComponentId
{
public:
    using Value = std::uint16_t;
    static constexpr Value InvalidValue = std::numeric_limits<Value>::max();

    constexpr ComponentId() = default;
    constexpr explicit ComponentId(Value value) : value_(value) {}

    constexpr bool valid() const
    {
        return value_ != InvalidValue;
    }

    constexpr Value value() const
    {
        return value_;
    }

    friend constexpr bool operator==(ComponentId, ComponentId) = default;

private:
    Value value_ = InvalidValue;
};

struct ComponentDescriptor
{
    ComponentId id{};
    const char *name = "";
    ComponentKind kind = ComponentKind::Unknown;

    constexpr ComponentDescriptor() = default;

    constexpr ComponentDescriptor(ComponentId component_id,
                                  const char *component_name,
                                  ComponentKind component_kind)
        : id(component_id),
          name(component_name == nullptr ? "" : component_name),
          kind(component_kind)
    {
    }

    // Transitional compatibility for current category-only boot reporting.
    constexpr ComponentDescriptor(const char *component_name)
        : name(component_name == nullptr ? "" : component_name)
    {
    }

    constexpr bool identified() const
    {
        return id.valid();
    }
};

struct LifecycleHooks
{
    bool init = false;
    bool start = false;
    bool run = false;
    bool stop = false;
    bool deinit = false;
};

struct LifecycleFailure
{
    LifecycleOperation operation = LifecycleOperation::None;
    Status status = Status::Ok;

    constexpr bool present() const
    {
        return status != Status::Ok;
    }
};

struct LifecycleRecord
{
    ComponentDescriptor component{};
    LifecycleHooks hooks{};
    LifecycleState state = LifecycleState::Registered;
    LifecycleOperation last_operation = LifecycleOperation::None;
    Status last_status = Status::Ok;
    LifecycleFailure first_failure{};
    std::uint32_t transition_count = 0;
    bool initialized_successfully = false;
    bool started_successfully = false;
    bool deinitialized_successfully = false;

    constexpr bool failed() const
    {
        return state == LifecycleState::Failed;
    }

    constexpr bool has_failure() const
    {
        return first_failure.present();
    }
};

constexpr bool can_transition(SystemState from, SystemState to)
{
    if (from == to)
    {
        return true;
    }

    switch (from)
    {
    case SystemState::Dormant:
        return to == SystemState::Booting;
    case SystemState::Booting:
        return to == SystemState::Initialized || to == SystemState::Failed;
    case SystemState::Initialized:
        return to == SystemState::Starting || to == SystemState::Stopping ||
               to == SystemState::Failed;
    case SystemState::Starting:
        return to == SystemState::Running || to == SystemState::Stopping ||
               to == SystemState::Failed;
    case SystemState::Running:
        return to == SystemState::Stopping || to == SystemState::Failed;
    case SystemState::Stopping:
        return to == SystemState::Stopped || to == SystemState::Failed;
    case SystemState::Stopped:
    case SystemState::Failed:
        return false;
    }

    return false;
}

constexpr bool can_transition(LifecycleState from, LifecycleState to)
{
    if (from == to)
    {
        return true;
    }

    switch (from)
    {
    case LifecycleState::Registered:
        return to == LifecycleState::Initializing ||
               to == LifecycleState::Disabled;
    case LifecycleState::Initializing:
        return to == LifecycleState::Initialized ||
               to == LifecycleState::Failed;
    case LifecycleState::Initialized:
        return to == LifecycleState::Starting ||
               to == LifecycleState::Stopping ||
               to == LifecycleState::Deinitializing ||
               to == LifecycleState::Failed;
    case LifecycleState::Starting:
        return to == LifecycleState::Running ||
               to == LifecycleState::Stopping ||
               to == LifecycleState::Failed;
    case LifecycleState::Running:
        return to == LifecycleState::Stopping ||
               to == LifecycleState::Failed;
    case LifecycleState::Stopping:
        return to == LifecycleState::Stopped ||
               to == LifecycleState::Failed;
    case LifecycleState::Stopped:
        return to == LifecycleState::Deinitializing ||
               to == LifecycleState::Failed;
    case LifecycleState::Deinitializing:
        return to == LifecycleState::Deinitialized ||
               to == LifecycleState::Failed;
    case LifecycleState::Failed:
        return to == LifecycleState::Stopping ||
               to == LifecycleState::Deinitializing;
    case LifecycleState::Deinitialized:
    case LifecycleState::Disabled:
        return false;
    }

    return false;
}

} // namespace solar
