#pragma once

#include <algorithm>
#include <cstdint>
#include <tuple>

#include "solar/core/spin_mutex.hpp"
#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

namespace solar::supervisor
{

enum class Condition : std::uint8_t
{
    Unknown,
    Healthy,
    Degraded,
    Fault
};
template <typename... Checks> struct Schema
{
    using Entries = TypeList<Checks...>;
};
template <typename Check> struct CheckSlot
{
    Condition condition{Condition::Unknown};
    std::uint64_t revision{};
};
template <typename SchemaT> class Monitor;

/** Health state storage; reaction and restart policy stay in adapters/services. */
template <typename... Checks> class Monitor<Schema<Checks...>>
{
  public:
    [[nodiscard]] Result<void> initialize()
    {
        SpinGuard lock{mutex_};
        slots_ = {};
        return {};
    }
    template <typename Check> [[nodiscard]] Result<void> report(Condition condition)
    {
        static_assert(contains_v<Check, TypeList<Checks...>>);
        SpinGuard lock{mutex_};
        auto& slot = std::get<CheckSlot<Check>>(slots_);
        slot.condition = condition;
        ++slot.revision;
        return {};
    }
    template <typename Check> [[nodiscard]] CheckSlot<Check> state() const
    {
        static_assert(contains_v<Check, TypeList<Checks...>>);
        SpinGuard lock{mutex_};
        return std::get<CheckSlot<Check>>(slots_);
    }
    [[nodiscard]] Condition overall() const
    {
        SpinGuard lock{mutex_};
        Condition result{Condition::Unknown};
        std::apply(
            [&](const auto&... slots) { ((result = (std::max)(result, slots.condition)), ...); },
            slots_);
        return result;
    }

  private:
    std::tuple<CheckSlot<Checks>...> slots_{};
    mutable SpinMutex mutex_{};
};

template <typename Application, typename SchemaT> struct StaticMonitor
{
    inline static Monitor<SchemaT> storage{};
    [[nodiscard]] static Result<void> initialize()
    {
        return storage.initialize();
    }
    template <typename Check> [[nodiscard]] static auto report(Condition value)
    {
        return storage.template report<Check>(value);
    }
    template <typename Check> [[nodiscard]] static auto state()
    {
        return storage.template state<Check>();
    }
    [[nodiscard]] static Condition overall()
    {
        return storage.overall();
    }
};

} // namespace solar::supervisor
