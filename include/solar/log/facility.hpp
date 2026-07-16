#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>

#include "solar/log/contribution.hpp"
#include "solar/log/format.hpp"
#include "solar/log/policy.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_LOG)
#include <zephyr/sys/util_macro.h>

#include "solar/execution/registration.hpp"
#include "solar/kernel/spinlock.hpp"
#endif

namespace solar::log
{

struct Tag
{};

#if defined(CONFIG_SOLAR_LOG)
inline constexpr bool available = true;
#else
inline constexpr bool available = false;
#endif

struct RetainedHistory
{
    static constexpr SourceDescriptor descriptor{
        .name = "solar.log.history",
        .description = "Bounded complete-record log history",
    };
};

template <typename Architecture> struct Facility;

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_LOG)
namespace detail
{

template <typename Axis, typename Policies> struct PolicyForAxis;

template <typename Axis> struct PolicyForAxis<Axis, TypeList<>>
{
    using type = NoPolicy;
};

template <typename Axis, typename Head, typename... Tail>
struct PolicyForAxis<Axis, TypeList<Head, Tail...>>
{
  private:
    using Remaining = typename PolicyForAxis<Axis, TypeList<Tail...>>::type;
    using HeadAxis = typename subsystem_policy_traits<Tag, Head>::Axis;

  public:
    using type = std::conditional_t<std::is_same_v<Axis, HeadAxis>, Head, Remaining>;
};

template <typename Axis, typename Policies>
using policy_for_axis_t = typename PolicyForAxis<Axis, Policies>::type;

template <typename Wrapper, bool Missing = std::is_same_v<Wrapper, NoPolicy>> struct ExecutorFrom
{
    using type = typename Wrapper::ExecutorType;
};

template <typename Wrapper> struct ExecutorFrom<Wrapper, true>
{
    using type = std::conditional_t<IS_ENABLED(CONFIG_SOLAR_LOG_PROCESSOR_SYSTEM_WORKQUEUE_DEFAULT),
                                    execution::SystemWorkQueue, execution::DefaultTarget>;
};

template <typename Configuration>
using processor_target_t =
    typename ExecutorFrom<policy_for_axis_t<ProcessorExecutorAxis, Configuration>>::type;

template <typename Wrapper, bool Missing = std::is_same_v<Wrapper, NoPolicy>> struct StopFrom
{
    using type = typename Wrapper::PolicyType;
};

template <typename Wrapper> struct StopFrom<Wrapper, true>
{
    using type = stop::Drain;
};

template <typename Configuration>
using stop_policy_t = typename StopFrom<policy_for_axis_t<StopAxis, Configuration>>::type;

template <typename Configuration>
using routes_from_t = routes_t<Configuration>;

template <typename Executor, typename Components> struct ExecutorDependencies
{
    static_assert(!std::is_same_v<Executor, execution::DefaultTarget>,
                  "SOLAR_DIAGNOSTIC_LOG_PROCESSOR_EXECUTOR_REQUIRED: logging requires an explicit "
                  "executor or enabled system-workqueue default");
    static_assert(execution::target_traits<Executor>::valid,
                  "SOLAR_DIAGNOSTIC_LOG_PROCESSOR_EXECUTOR: configured logging target is invalid");
    static_assert(std::is_same_v<Executor, execution::SystemWorkQueue> ||
                      contains_v<Executor, Components>,
                  "SOLAR_DIAGNOSTIC_LOG_PROCESSOR_EXECUTOR_MISSING: configured logging executor "
                  "is absent from the component graph");
    using type = std::conditional_t<std::is_same_v<Executor, execution::SystemWorkQueue>,
                                    TypeList<>, TypeList<Executor>>;
};

template <typename List> struct AsDependencies;

template <typename... Types> struct AsDependencies<TypeList<Types...>>
{
    using type = Dependencies<Types...>;
};

template <typename... Routes> consteval bool validate_routes(Sinks<Routes...>)
{
    using SinkTypes = TypeList<typename route_traits<Routes>::Sink...>;
    static_assert(unique_types_v<SinkTypes>,
                  "SOLAR_DIAGNOSTIC_LOG_DUPLICATE_SINK: a sink may be routed only once");
    static_assert(
        ((std::is_same_v<typename route_traits<Routes>::Sink, RetainedHistory> ||
          requires { route_traits<Routes>::Sink::descriptor.name; }) && ...),
        "SOLAR_DIAGNOSTIC_LOG_SINK_DESCRIPTOR: sink requires static descriptor metadata");
    return true;
}

struct StoredRecord
{
    RecordHeader header{};
    std::array<std::byte, CONFIG_SOLAR_LOG_MAX_RECORD_BYTES> payload{};

    [[nodiscard]] RecordView view() const noexcept
    {
        return {.header = header,
                .payload = std::span<const std::byte>{payload}.first(header.payload_size)};
    }
};

template <std::size_t Capacity> class ByteRing
{
  public:
    [[nodiscard]] bool push(std::span<const std::byte> value, std::size_t admission_limit) noexcept
    {
        const auto required = sizeof(std::uint16_t) + value.size();
        if (value.size() > std::numeric_limits<std::uint16_t>::max() || required > admission_limit ||
            used_ + required > admission_limit) {
            return false;
        }
        const auto size = static_cast<std::uint16_t>(value.size());
        write(std::as_bytes(std::span{&size, std::size_t{1}}));
        write(value);
        return true;
    }

    [[nodiscard]] bool pop(std::span<std::byte> output, std::size_t& written) noexcept
    {
        written = 0;
        if (used_ < sizeof(std::uint16_t)) {
            return false;
        }
        std::uint16_t size{};
        peek(std::as_writable_bytes(std::span{&size, std::size_t{1}}));
        if (used_ < sizeof(size) + size || output.size() < size) {
            return false;
        }
        discard(sizeof(size));
        read(output.first(size));
        written = size;
        return true;
    }

    void clear() noexcept
    {
        head_ = 0;
        tail_ = 0;
        used_ = 0;
    }

    [[nodiscard]] std::size_t used() const noexcept
    {
        return used_;
    }

  private:
    void write(std::span<const std::byte> value) noexcept
    {
        for (const auto byte : value) {
            bytes_[tail_] = byte;
            tail_ = (tail_ + 1U) % Capacity;
        }
        used_ += value.size();
    }

    void peek(std::span<std::byte> output) const noexcept
    {
        auto cursor = head_;
        for (auto& byte : output) {
            byte = bytes_[cursor];
            cursor = (cursor + 1U) % Capacity;
        }
    }

    void read(std::span<std::byte> output) noexcept
    {
        peek(output);
        discard(output.size());
    }

    void discard(std::size_t size) noexcept
    {
        head_ = (head_ + size) % Capacity;
        used_ -= size;
    }

    std::array<std::byte, Capacity> bytes_{};
    std::size_t head_{};
    std::size_t tail_{};
    std::size_t used_{};
};

class CompactHistory
{
  public:
    struct AppendResult
    {
        std::size_t evicted{};
        bool stored{};
    };

    void clear() noexcept;
    [[nodiscard]] AppendResult append(const StoredRecord& record) noexcept;
    [[nodiscard]] HistoryPage read(Cursor cursor, std::span<Record> output) const noexcept;
    [[nodiscard]] Result<Record, Error> latest() const noexcept;
    [[nodiscard]] std::size_t used() const noexcept
    {
        return used_;
    }
    [[nodiscard]] std::uint64_t evicted() const noexcept
    {
        return evicted_;
    }

  private:
#if defined(CONFIG_SOLAR_LOG_HISTORY)
    std::array<std::byte, CONFIG_SOLAR_LOG_HISTORY_BYTES> bytes_{};
#else
    std::array<std::byte, 1> bytes_{};
#endif
    std::size_t used_{};
    std::uint64_t evicted_{};
};

template <typename FacilityT> struct ProcessorBehavior
{
    [[nodiscard]] static Result<void> execute() noexcept
    {
        return FacilityT::run_processor();
    }
};

template <typename FacilityT>
using ProcessorRegistration =
    execution::OnDemand<"log.processor", ProcessorBehavior<FacilityT>,
                        typename FacilityT::ProcessorTarget, execution::NativeCoalescing,
                        execution::stop::Drain>;

} // namespace detail
#endif

#if !defined(__ZEPHYR__) || !defined(CONFIG_SOLAR_LOG)

template <typename Components, typename Configuration> struct Architecture
{
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;
    using BootstrapDependencies = TypeList<>;
    static constexpr bool demanded = list_size_v<Configuration> != 0;
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    static constexpr component::Descriptor descriptor{
        .name = "solar.logging",
        .description = "Unified bounded logging",
    };
    template <typename> static void activate_runtime() noexcept {}
};

#else

template <typename Components, typename Configuration> struct Architecture
{
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;
    using ProcessorTarget = detail::processor_target_t<Configuration>;
    using BootstrapDependencies =
        typename detail::ExecutorDependencies<ProcessorTarget, Components>::type;
    using Routes = detail::routes_from_t<Configuration>;
    static constexpr bool demanded = list_size_v<Configuration> != 0;

    static_assert(detail::validate_routes(Routes{}));

    static_assert(CONFIG_SOLAR_LOG_ELEVATED_RESERVE_BYTES + CONFIG_SOLAR_LOG_EMERGENCY_BYTES <=
                      CONFIG_SOLAR_LOG_INGRESS_BYTES,
                  "SOLAR_DIAGNOSTIC_LOG_RESERVE_CEILING: log reserves exceed ingress capacity");
    static_assert(CONFIG_SOLAR_LOG_MAX_RECORD_BYTES + sizeof(std::uint16_t) <=
                      CONFIG_SOLAR_LOG_INGRESS_BYTES,
                  "SOLAR_DIAGNOSTIC_LOG_RECORD_CEILING: maximum record cannot fit ingress");
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    using Configuration = typename Architecture::ConfigurationPolicies;
    using ProcessorTarget = typename Architecture::ProcessorTarget;
    using ProcessorStopPolicy = detail::stop_policy_t<Configuration>;
    using Routes = typename Architecture::Routes;
    using Dependencies =
        typename detail::AsDependencies<typename Architecture::BootstrapDependencies>::type;
    using ProcessorRegistration = detail::ProcessorRegistration<Facility>;
    using Tasks = execution::Tasks<ProcessorRegistration>;

    static constexpr component::Descriptor descriptor{
        .name = "solar.logging",
        .description = "Unified bounded logging",
    };

    inline static detail::ByteRing<CONFIG_SOLAR_LOG_INGRESS_BYTES> ingress{};
    inline static detail::CompactHistory history{};
    inline static kernel::SpinLock lock{};
    inline static FacilityRecord record{};
    inline static std::atomic_bool ready{};
    inline static std::atomic_bool accepting{};
    inline static std::atomic_bool processor_pending{};
    inline static std::atomic_bool panic_mode{};
#if defined(CONFIG_ZTEST)
    inline static std::atomic_bool test_hold_processor{};
#endif
    using ScheduleProcessor = Result<void> (*)(bool) noexcept;
    using ProcessRecord = Result<void> (*)(const detail::StoredRecord&) noexcept;
    inline static ScheduleProcessor schedule_processor{};
    inline static ProcessRecord process_record{};

    [[nodiscard]] static Result<void> init() noexcept;
    [[nodiscard]] static Result<void> start() noexcept;
    [[nodiscard]] static Result<void> stop() noexcept;
    [[nodiscard]] static Result<void> deinit() noexcept;
    [[nodiscard]] static Result<void> run_processor() noexcept;
    [[nodiscard]] static Result<void> flush() noexcept;

    template <typename System> static void activate_runtime() noexcept;
};

#endif

} // namespace solar::log

template <typename Architecture> struct solar::builtin_traits<solar::log::Facility<Architecture>>
{
    static constexpr bool enabled = solar::log::available;
    static constexpr bool always_present = solar::log::available;
    using Requirements = solar::TypeList<>;

    template <typename> static constexpr bool demanded = Architecture::demanded;
};

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_LOG)
template <typename Component, typename Architecture, typename AllComponents>
struct solar::generated_component_dependency<Component, solar::log::Facility<Architecture>,
                                             AllComponents>
    : std::bool_constant<
          !std::is_same_v<Component, solar::log::Facility<Architecture>> &&
          !solar::contains_v<Component, typename Architecture::BootstrapDependencies>>
{};
#endif
