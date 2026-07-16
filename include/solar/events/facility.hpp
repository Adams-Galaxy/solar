#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include "solar/events/catalog.hpp"
#include "solar/events/contribution.hpp"
#include "solar/events/processor.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_EVENTS)
#include <zephyr/sys/util_macro.h>

#include "solar/execution/registration.hpp"
#include "solar/kernel/spinlock.hpp"
#endif

namespace solar::events
{

#if defined(CONFIG_SOLAR) && !defined(CONFIG_SOLAR_EVENTS)
inline constexpr bool enabled = false;
#else
inline constexpr bool enabled = true;
#endif

template <typename Architecture> struct Facility;

namespace detail
{

template <typename List> struct DeclarationsOf;

template <typename... Entries> struct DeclarationsOf<TypeList<Entries...>>
{
    using type = TypeList<typename Entries::Declaration...>;
};

template <typename List> using declarations_of_t = typename DeclarationsOf<List>::type;

} // namespace detail

#if !defined(__ZEPHYR__) || !defined(CONFIG_SOLAR_EVENTS)

template <typename EventDeclarations, typename ProcessorDeclarations, typename Components,
          typename Configuration>
struct Architecture
{
    using Events = EventDeclarations;
    using Processors = ProcessorDeclarations;
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;
    using BootstrapDependencies = TypeList<>;
    static constexpr bool demanded = list_size_v<Events> != 0 || list_size_v<Processors> != 0 ||
                                     list_size_v<ConfigurationPolicies> != 0;
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    using EventTypes = typename Architecture::Events;
    using ProcessorTypes = typename Architecture::Processors;

    static constexpr component::Descriptor descriptor{
        .name = "solar.events",
        .description = "Structured observability events",
    };

    template <typename> static void activate_runtime() noexcept {}
};

#else

namespace detail
{

template <typename Axis, typename Policies> struct PolicyForAxis;

template <typename Needle, typename List> struct TypeIndex;

template <typename Needle, typename... Tail>
struct TypeIndex<Needle, TypeList<Needle, Tail...>> : std::integral_constant<std::size_t, 0>
{};

template <typename Needle, typename Head, typename... Tail>
struct TypeIndex<Needle, TypeList<Head, Tail...>>
    : std::integral_constant<std::size_t, 1 + TypeIndex<Needle, TypeList<Tail...>>::value>
{};

template <typename Needle, typename List>
inline constexpr std::size_t type_index_v = TypeIndex<Needle, List>::value;

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

template <typename Wrapper, typename Fallback, bool Missing = std::is_same_v<Wrapper, NoPolicy>>
struct UnwrapPolicy
{
    using type = typename Wrapper::PolicyType;
};

template <typename Wrapper, typename Fallback> struct UnwrapPolicy<Wrapper, Fallback, true>
{
    using type = Fallback;
};

template <typename Axis, typename Configuration, typename Fallback>
using configured_policy_t =
    typename UnwrapPolicy<policy_for_axis_t<Axis, Configuration>, Fallback>::type;

#if defined(CONFIG_SOLAR_EVENTS_DEFAULT_RETENTION_TRANSIENT)
using KconfigRetention = retention::Transient;
#else
using KconfigRetention = retention::Buffered;
#endif

#if defined(CONFIG_SOLAR_EVENTS_DEFAULT_STOP_CANCEL)
using KconfigStop = stop::CancelPending;
#else
using KconfigStop = stop::Drain;
#endif

template <typename EventT, typename Configuration> struct EventPolicies
{
    using Capture = resolve_policy_t<
        typename DeclaredCapture<EventT>::type,
        configured_policy_t<DefaultCaptureAxis, Configuration, capture::EveryOccurrence>,
        capture::EveryOccurrence>;
    using Retention =
        resolve_policy_t<typename DeclaredRetention<EventT>::type,
                         configured_policy_t<DefaultRetentionAxis, Configuration, KconfigRetention>,
                         KconfigRetention>;
};

template <typename Wrapper, bool Missing = std::is_same_v<Wrapper, NoPolicy>> struct ResolveExecutor
{
    using type = typename Wrapper::ExecutorType;
};

template <typename Wrapper> struct ResolveExecutor<Wrapper, true>
{
    using type =
        std::conditional_t<IS_ENABLED(CONFIG_SOLAR_EVENTS_PROCESSOR_SYSTEM_WORKQUEUE_DEFAULT),
                           execution::SystemWorkQueue, execution::DefaultTarget>;
};

template <typename Configuration> struct ConfiguredExecutor
{
    using type =
        typename ResolveExecutor<policy_for_axis_t<ProcessorExecutorAxis, Configuration>>::type;
};

template <typename Configuration> struct ConfiguredStop
{
    using type = configured_policy_t<ProcessorStopAxis, Configuration, KconfigStop>;
};

template <typename Executor, typename Components> struct ExecutorDependencies
{
    static_assert(!std::is_same_v<Executor, execution::DefaultTarget>,
                  "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_EXECUTOR_REQUIRED: event processing requires "
                  "an explicit executor or enabled system-workqueue default");
    static_assert(execution::target_traits<Executor>::valid,
                  "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_EXECUTOR: configured event processor target "
                  "is not a valid execution target");
    static_assert(std::is_same_v<Executor, execution::SystemWorkQueue> ||
                      contains_v<Executor, Components>,
                  "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_EXECUTOR_MISSING: named event processor "
                  "executor is absent from the component graph");
    using type = std::conditional_t<std::is_same_v<Executor, execution::SystemWorkQueue>,
                                    TypeList<>, TypeList<Executor>>;
};

template <typename List> struct AsDependencies;

template <typename... Types> struct AsDependencies<TypeList<Types...>>
{
    using type = Dependencies<Types...>;
};

template <typename EventT, typename Policies> consteval bool validate_event()
{
    static_assert(EventDeclaration<EventT>,
                  "SOLAR_DIAGNOSTIC_EVENT_DECLARATION: event requires Payload and Descriptor");
    static_assert(payload_free_v<EventT> || !borrowed_payload_v<PayloadOf<EventT>>,
                  "SOLAR_DIAGNOSTIC_EVENT_PAYLOAD_BORROWED: event payload cannot be a pointer, "
                  "reference, span, or string view");
    static_assert(Event<EventT>,
                  "SOLAR_DIAGNOSTIC_EVENT_PAYLOAD: event requires a descriptor and void or "
                  "trivially copyable, trivially destructible Payload");
    static_assert(payload_size_v<EventT> <= CONFIG_SOLAR_EVENTS_MAX_PAYLOAD_BYTES,
                  "SOLAR_DIAGNOSTIC_EVENT_PAYLOAD_SIZE: event payload exceeds "
                  "CONFIG_SOLAR_EVENTS_MAX_PAYLOAD_BYTES");
    static_assert(payload_alignment_v<EventT> <= CONFIG_SOLAR_EVENTS_MAX_PAYLOAD_ALIGNMENT,
                  "SOLAR_DIAGNOSTIC_EVENT_PAYLOAD_ALIGNMENT: event payload exceeds configured "
                  "alignment ceiling");
    static_assert(CaptureTraits<typename Policies::Capture>::valid,
                  "SOLAR_DIAGNOSTIC_EVENT_CAPTURE_POLICY: invalid event capture policy");
    static_assert(RetentionTraits<typename Policies::Retention>::valid,
                  "SOLAR_DIAGNOSTIC_EVENT_RETENTION_POLICY: invalid event retention policy");
    using Capture = CaptureTraits<typename Policies::Capture>;
    using Retention = RetentionTraits<typename Policies::Retention>;
    static_assert(!Capture::aggregate ||
                      Capture::key_capacity <= CONFIG_SOLAR_EVENTS_MAX_AGGREGATION_KEYS,
                  "SOLAR_DIAGNOSTIC_EVENT_AGGREGATION_CEILING: event aggregation key capacity "
                  "exceeds Kconfig ceiling");
    if constexpr (Retention::persistent) {
        static_assert(IS_ENABLED(CONFIG_SOLAR_EVENTS_PERSISTENCE),
                      "SOLAR_DIAGNOSTIC_EVENT_PERSISTENCE_DISABLED: persistent event retention "
                      "requires CONFIG_SOLAR_EVENTS_PERSISTENCE");
        static_assert(PersistentStore<typename Retention::StoreType>,
                      "SOLAR_DIAGNOSTIC_EVENT_PERSISTENCE_STORE: persistent event store must "
                      "implement initialize() and write(RecordView)");
        static_assert(descriptor_traits<Tag, EventT>::descriptor.stable_id.has_value(),
                      "SOLAR_DIAGNOSTIC_EVENT_PERSISTENCE_STABLE_ID: persistent event requires "
                      "stable identity");
    }
    if constexpr (Capture::aggregate && Capture::keyed) {
        static_assert(AggregationKeyExtractor<typename Capture::KeyType, EventT>,
                      "SOLAR_DIAGNOSTIC_EVENT_AGGREGATION_KEY: keyed aggregation requires a "
                      "trivial equality-comparable Key::Value and static Key::get(payload)");
    }
    return true;
}

template <typename EventT, typename = void> struct ResolvedEvent
{
    using type = void;
};

template <typename EventT> struct ResolvedEvent<EventT, std::void_t<typename EventT::Resolves>>
{
    using type = typename EventT::Resolves;
};

template <typename EventT, typename Events> consteval bool validate_resolution()
{
    using Resolved = typename ResolvedEvent<EventT>::type;
    static_assert(std::is_void_v<Resolved> || contains_v<Resolved, Events>,
                  "SOLAR_DIAGNOSTIC_EVENT_RECOVERY_UNREGISTERED: recovery references an "
                  "unregistered event");
    return true;
}

struct StoredRecord
{
    RecordHeader header{};
    std::array<std::byte, CONFIG_SOLAR_EVENTS_MAX_PAYLOAD_BYTES> payload{};

    [[nodiscard]] RecordView view() const noexcept
    {
        return {.header = header,
                .payload = std::span<const std::byte>{payload}.first(header.payload_size)};
    }
};

template <std::size_t Capacity> class FixedRing
{
  public:
    [[nodiscard]] bool push(const StoredRecord& record) noexcept
    {
        if (size_ == Capacity) {
            return false;
        }
        records_[tail_] = record;
        tail_ = (tail_ + 1U) % Capacity;
        ++size_;
        return true;
    }

    [[nodiscard]] bool pop(StoredRecord& record) noexcept
    {
        if (size_ == 0) {
            return false;
        }
        record = records_[head_];
        head_ = (head_ + 1U) % Capacity;
        --size_;
        return true;
    }

    [[nodiscard]] const StoredRecord* front() const noexcept
    {
        return size_ == 0 ? nullptr : &records_[head_];
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return size_;
    }

    template <typename Function> void for_each(Function&& function) const noexcept
    {
        for (std::size_t offset{}; offset < size_; ++offset) {
            function(records_[(head_ + offset) % Capacity]);
        }
    }

    void clear() noexcept
    {
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

  private:
    std::array<StoredRecord, Capacity> records_{};
    std::size_t head_{};
    std::size_t tail_{};
    std::size_t size_{};
};

template <> class FixedRing<0>
{
  public:
    [[nodiscard]] bool push(const StoredRecord&) noexcept
    {
        return false;
    }
    [[nodiscard]] bool pop(StoredRecord&) noexcept
    {
        return false;
    }
    [[nodiscard]] const StoredRecord* front() const noexcept
    {
        return nullptr;
    }
    [[nodiscard]] std::size_t size() const noexcept
    {
        return 0;
    }
    template <typename Function> void for_each(Function&&) const noexcept {}
    void clear() noexcept {}
};

template <typename EventT, typename Capture, bool Aggregate = Capture::aggregate,
          bool Keyed = Capture::keyed>
struct AggregationState
{};

template <typename EventT, typename Capture> struct AggregationState<EventT, Capture, true, false>
{
    std::int64_t window_start{};
    std::uint32_t count{};
    StoredRecord representative{};
};

template <typename EventT, typename Capture> struct AggregationState<EventT, Capture, true, true>
{
    using Key = typename Capture::KeyType::Value;

    struct Entry
    {
        Key key{};
        std::int64_t window_start{};
        std::uint32_t count{};
        StoredRecord representative{};
        bool occupied{};
    };

    std::array<Entry, Capture::key_capacity> entries{};
};

template <std::size_t Capacity> struct ConditionTable
{
    struct Entry
    {
        ConditionRecord record{};
        bool occupied{};
    };

    std::array<Entry, Capacity> entries{};
};

template <typename EventT, typename Policies, std::size_t SourceCapacity> struct EventState
{
    using Capture = CaptureTraits<typename Policies::Capture>;
    using Retention = RetentionTraits<typename Policies::Retention>;

    FixedRing<Retention::reserved_slots> critical_ingress{};
    FixedRing<Retention::reserved_slots> critical_history{};
    std::size_t critical_inflight{};
    EventRecord record{};
    std::int64_t policy_window_start{};
    std::uint64_t policy_counter{};
    [[no_unique_address]] AggregationState<EventT, Capture> aggregation{};
    ConditionTable<SourceCapacity> conditions{};
};

template <typename Events, typename Configuration> struct PersistenceStores;

template <typename Retention, bool Persistent = Retention::persistent> struct StoreList
{
    using type = TypeList<>;
};

template <typename Retention> struct StoreList<Retention, true>
{
    using type = TypeList<typename Retention::StoreType>;
};

template <typename Configuration> struct PersistenceStores<TypeList<>, Configuration>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail, typename Configuration>
struct PersistenceStores<TypeList<Head, Tail...>, Configuration>
{
  private:
    using Retention = RetentionTraits<typename EventPolicies<Head, Configuration>::Retention>;
    using Current = typename StoreList<Retention>::type;

  public:
    using type = unique_t<
        concat_t<Current, typename PersistenceStores<TypeList<Tail...>, Configuration>::type>>;
};

class CompactHistory
{
  public:
    struct AppendResult
    {
        std::size_t evicted{};
        bool stored{};
    };

    void clear() noexcept
    {
        used_ = 0;
        evicted_ = 0;
    }

    [[nodiscard]] AppendResult append(const StoredRecord& record) noexcept;
    [[nodiscard]] HistoryPage read(Cursor cursor, std::span<Record> output,
                                   std::optional<LocalId> filter = std::nullopt) const noexcept;
    [[nodiscard]] Result<Record, Error> latest(std::optional<LocalId> filter) const noexcept;
    [[nodiscard]] std::optional<StoredRecord>
    next(Sequence sequence, std::optional<LocalId> filter = std::nullopt) const noexcept;
    [[nodiscard]] std::size_t used() const noexcept
    {
        return used_;
    }
    [[nodiscard]] std::uint64_t evicted() const noexcept
    {
        return evicted_;
    }

  private:
    static constexpr std::size_t prefix_size = sizeof(std::uint16_t);
    std::array<std::byte, CONFIG_SOLAR_EVENTS_HISTORY_BYTES> bytes_{};
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
    execution::OnDemand<"events.processor", ProcessorBehavior<FacilityT>,
                        typename FacilityT::ProcessorTarget, execution::NativeCoalescing,
                        execution::stop::Drain>;

} // namespace detail

template <typename EventDeclarations, typename ProcessorDeclarations, typename Components,
          typename Configuration>
struct Architecture
{
    using Events = EventDeclarations;
    using Processors = ProcessorDeclarations;
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;
    using Stores = typename detail::PersistenceStores<Events, Configuration>::type;
    using ProcessorTarget = typename detail::ConfiguredExecutor<Configuration>::type;
    using BootstrapDependencies =
        typename detail::ExecutorDependencies<ProcessorTarget, Components>::type;

    template <typename EventT> using Policies = detail::EventPolicies<EventT, Configuration>;

    static_assert([]<typename... EventsT>(TypeList<EventsT...>) {
        return (detail::validate_event<EventsT, Policies<EventsT>>() && ...);
    }(Events{}));
    static_assert([]<typename... EventsT>(TypeList<EventsT...>) {
        return (detail::validate_resolution<EventsT, Events>() && ...);
    }(Events{}));
    static_assert(
        []<typename... ProcessorsT>(TypeList<ProcessorsT...>) {
            return (
                (processor_traits<ProcessorsT>::valid &&
                 RecordProcessor<typename processor_traits<ProcessorsT>::ObserverType> &&
                 requires { typename processor_traits<ProcessorsT>::ObserverType::EventRole; } &&
                 std::is_same_v<typename processor_traits<ProcessorsT>::ObserverType::EventRole,
                                InfrastructureObserver>) &&
                ...);
        }(Processors{}),
        "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_HANDLER: processor requires an "
        "InfrastructureObserver EventRole and static process(RecordView)");
    static_assert(
        []<typename... ProcessorsT>(TypeList<ProcessorsT...>) {
            return (contains_v<typename processor_traits<ProcessorsT>::EventType, Events> && ...);
        }(Processors{}),
        "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_UNREGISTERED: processor references an "
        "unregistered event");
    static constexpr std::size_t critical_reserved_slots =
        []<typename... EventsT>(TypeList<EventsT...>) {
            return (detail::RetentionTraits<typename Policies<EventsT>::Retention>::reserved_slots +
                    ... + std::size_t{0});
        }(Events{});
    static_assert(critical_reserved_slots <= CONFIG_SOLAR_EVENTS_MAX_CRITICAL_RESERVED_SLOTS,
                  "SOLAR_DIAGNOSTIC_EVENT_CRITICAL_CEILING: critical reservations exceed "
                  "CONFIG_SOLAR_EVENTS_MAX_CRITICAL_RESERVED_SLOTS");

    static constexpr bool demanded = list_size_v<Events> != 0 || list_size_v<Processors> != 0 ||
                                     list_size_v<ConfigurationPolicies> != 0;
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    using EventTypes = typename Architecture::Events;
    using ProcessorTypes = typename Architecture::Processors;
    using Configuration = typename Architecture::ConfigurationPolicies;
    using ProcessorTarget = typename Architecture::ProcessorTarget;
    using ProcessorStopPolicy = typename detail::ConfiguredStop<Configuration>::type;
    using Dependencies =
        typename detail::AsDependencies<typename Architecture::BootstrapDependencies>::type;

    static constexpr component::Descriptor descriptor{
        .name = "solar.events",
        .description = "Structured observability events",
    };

    template <typename EventT> using Policies = typename Architecture::template Policies<EventT>;
    using ProcessorRegistration = detail::ProcessorRegistration<Facility>;
    using Tasks = execution::Tasks<ProcessorRegistration>;

    template <typename EventT>
    inline static detail::EventState<EventT, Policies<EventT>,
                                     list_size_v<typename Architecture::ComponentTypes> + 2>
        event_state{};
    template <typename Processor> inline static ProcessorRecord processor_record_state{};

    inline static detail::FixedRing<CONFIG_SOLAR_EVENTS_INGRESS_DEPTH> thread_ingress{};
    inline static detail::FixedRing<CONFIG_SOLAR_EVENTS_ISR_INGRESS_DEPTH> isr_ingress{};
    inline static detail::CompactHistory history{};
    inline static kernel::SpinLock lock{};
    inline static FacilityRecord facility_record{};
    inline static Sequence next_sequence{1};
    inline static std::atomic_bool ready{};
    inline static std::atomic_bool accepting{};
    inline static std::atomic_bool processor_pending{};
    using ScheduleProcessor = Result<void> (*)(bool) noexcept;
    using ProcessRecord = Result<void> (*)(const detail::StoredRecord&) noexcept;
    inline static ScheduleProcessor schedule_processor{};
    inline static ProcessRecord process_record{};

    [[nodiscard]] static Result<void> init() noexcept;
    [[nodiscard]] static Result<void> start() noexcept;
    [[nodiscard]] static Result<void> stop() noexcept;
    [[nodiscard]] static Result<void> deinit() noexcept;
    [[nodiscard]] static Result<void> run_processor() noexcept;

    template <typename System> static void activate_runtime() noexcept;
};

#endif

} // namespace solar::events

template <typename Architecture> struct solar::builtin_traits<solar::events::Facility<Architecture>>
{
    static constexpr bool enabled = solar::events::enabled;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<>;

    template <typename> static constexpr bool demanded = Architecture::demanded;
};

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_EVENTS)
template <typename Component, typename Architecture, typename AllComponents>
struct solar::generated_component_dependency<Component, solar::events::Facility<Architecture>,
                                             AllComponents>
    : std::bool_constant<
          !std::is_same_v<Component, solar::events::Facility<Architecture>> &&
          solar::contains_v<Component, typename Architecture::ComponentTypes> &&
          !solar::contains_v<Component, typename Architecture::BootstrapDependencies>>
{};
#endif
