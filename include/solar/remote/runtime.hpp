#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "solar/remote/codec.hpp"
#include "solar/remote/declaration.hpp"
#include "solar/remote/facility.hpp"
#include "solar/remote/manifest.hpp"
#include "solar/remote/packed.hpp"
#include "solar/remote/protocol.hpp"

#if defined(CONFIG_SOLAR_LOG)
#include "solar/log/api.hpp"
#endif

#if defined(CONFIG_SOLAR_INSPECTION_REMOTE)
#include "solar/inspection/api.hpp"
#include "solar/inspection/cbor.hpp"
#endif

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
#include "solar/execution/runtime.hpp"
#include "solar/kernel/semaphore.hpp"
#include "solar/kernel/spinlock.hpp"
#include "solar/remote/service.hpp"
#endif

namespace solar::remote::detail
{

template <typename System> void pong_responded() noexcept
{
#if defined(CONFIG_SOLAR_LOG)
    (void)log::info<typename System::RemoteService, log::domain::Transport>("pong responded");
#endif
}

template <typename T> struct IsPushOutStream : std::false_type
{};

template <typename... Policies>
struct IsPushOutStream<OutStream<Push, Policies...>> : std::true_type
{};

template <typename CapabilitiesT> struct HasPush;

template <typename... Entries>
struct HasPush<Capabilities<Entries...>>
    : std::bool_constant<(IsPushOutStream<Entries>::value || ...)>
{};

template <typename DataT>
inline constexpr bool has_push_v = HasPush<typename DataT::Capabilities>::value;

template <typename CapabilitiesT> struct PushCapability;

template <> struct PushCapability<Capabilities<>>
{
    using type = void;
};

template <typename Head, typename... Tail> struct PushCapability<Capabilities<Head, Tail...>>
{
    using type = std::conditional_t<IsPushOutStream<Head>::value, Head,
                                    typename PushCapability<Capabilities<Tail...>>::type>;
};

template <typename T> struct IsQueuePolicy : std::false_type
{
    using type = Queue<1, DropOldest>;
};

template <std::size_t Depth, typename Overflow>
struct IsQueuePolicy<Queue<Depth, Overflow>> : std::true_type
{
    using type = Queue<Depth, Overflow>;
};

template <typename... Policies> struct SelectedQueuePolicy
{
    using type = Queue<1, DropOldest>;
};

template <typename Head, typename... Tail> struct SelectedQueuePolicy<Head, Tail...>
{
  private:
    using Remaining = typename SelectedQueuePolicy<Tail...>::type;

  public:
    static_assert((static_cast<std::size_t>(IsQueuePolicy<Head>::value) +
                   (static_cast<std::size_t>(IsQueuePolicy<Tail>::value) + ... + 0U)) <= 1,
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_QUEUE: Push OutStream declares Queue more "
                  "than once");
    using type = std::conditional_t<IsQueuePolicy<Head>::value, typename IsQueuePolicy<Head>::type,
                                    Remaining>;
};

template <typename T> struct PushStoragePolicy;

template <typename T> struct IsBatchPolicy : std::false_type
{};

template <std::size_t Count> struct IsBatchPolicy<Batch<Count>> : std::true_type
{
    static constexpr std::size_t count = Count;
};

template <typename... Policies> consteval std::size_t selected_batch_count()
{
    static_assert((static_cast<std::size_t>(IsBatchPolicy<Policies>::value) + ... + 0U) <= 1,
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_BATCH: Push OutStream declares Batch more "
                  "than once");
    std::size_t count{1};
    (([&] {
         if constexpr (IsBatchPolicy<Policies>::value) {
             count = IsBatchPolicy<Policies>::count;
         }
     }()),
     ...);
    return count;
}

template <typename... Policies> struct PushStoragePolicy<OutStream<Push, Policies...>>
{
    using QueuePolicy = typename SelectedQueuePolicy<Policies...>::type;
    static constexpr std::size_t depth = QueuePolicy::depth;
    static constexpr std::size_t batch_count = selected_batch_count<Policies...>();
    using Overflow = typename QueuePolicy::OverflowPolicy;
};

template <typename T> struct IsOutStream : std::false_type
{};

template <typename Acquisition, typename... Policies>
struct IsOutStream<OutStream<Acquisition, Policies...>> : std::true_type
{};

template <typename CapabilitiesT> struct HasOutStream;

template <typename... Entries>
struct HasOutStream<Capabilities<Entries...>>
    : std::bool_constant<(IsOutStream<Entries>::value || ...)>
{};

template <typename DataT>
inline constexpr bool has_out_stream_v = HasOutStream<typename DataT::Capabilities>::value;

template <typename T> struct IsLoanedOutStream : std::false_type
{};

template <typename Pool, typename... Policies>
struct IsLoanedOutStream<OutStream<Loaned<Pool>, Policies...>> : std::true_type
{
    using PoolType = Pool;
};

template <typename CapabilitiesT> struct LoanedCapability;

template <> struct LoanedCapability<Capabilities<>>
{
    static constexpr bool present = false;
    using type = void;
};

template <typename Head, typename... Tail> struct LoanedCapability<Capabilities<Head, Tail...>>
{
  private:
    using Remaining = LoanedCapability<Capabilities<Tail...>>;

  public:
    static constexpr bool present = IsLoanedOutStream<Head>::value || Remaining::present;
    static_assert(!(IsLoanedOutStream<Head>::value && Remaining::present),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_LOANED: Data declares more than one Loaned "
                  "OutStream");
    using type = std::conditional_t<IsLoanedOutStream<Head>::value, Head, typename Remaining::type>;
};

template <typename DataT>
inline constexpr bool has_loaned_v = LoanedCapability<typename DataT::Capabilities>::present;

template <typename T> struct IsWatch : std::false_type
{};

template <typename... Policies> struct IsWatch<Watch<Policies...>> : std::true_type
{
    using PolicyTypes = TypeList<Policies...>;
};

template <typename CapabilitiesT> struct WatchCapability;

template <> struct WatchCapability<Capabilities<>>
{
    static constexpr bool present = false;
    using type = void;
};

template <typename Head, typename... Tail> struct WatchCapability<Capabilities<Head, Tail...>>
{
  private:
    using Remaining = WatchCapability<Capabilities<Tail...>>;

  public:
    static constexpr bool present = IsWatch<Head>::value || Remaining::present;
    static_assert(!(IsWatch<Head>::value && Remaining::present),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_WATCH: Data declares more than one Watch "
                  "capability");
    using type = std::conditional_t<IsWatch<Head>::value, Head, typename Remaining::type>;
};

template <typename DataT>
inline constexpr bool has_watch_v = WatchCapability<typename DataT::Capabilities>::present;

template <typename TopicT, typename = void> struct TopicPublication
{
    using type = Watch<Latest, MultipleProducers>;
};

template <typename TopicT>
struct TopicPublication<TopicT, std::void_t<typename TopicT::Publication>>
{
    using type = typename TopicT::Publication;
    static_assert(IsWatch<type>::value,
                  "SOLAR_DIAGNOSTIC_REMOTE_TOPIC_PUBLICATION: Topic::Publication must be a "
                  "Watch<...> policy declaration");
};

template <typename PolicyTypes> struct DiscreteStoragePolicy;

template <typename... Policies> struct DiscreteStoragePolicy<TypeList<Policies...>>
{
    using QueuePolicy = typename SelectedQueuePolicy<Policies...>::type;
    static constexpr std::size_t depth = QueuePolicy::depth;
    using Overflow = typename QueuePolicy::OverflowPolicy;
};

template <typename T> struct IsInStream : std::false_type
{};

template <auto Consumer, typename... Policies>
struct IsInStream<InStream<Consumer, Policies...>> : std::true_type
{
    static constexpr auto consumer = Consumer;
    using PolicyTypes = TypeList<Policies...>;
};

template <typename CapabilitiesT> struct InStreamCapability;

template <> struct InStreamCapability<Capabilities<>>
{
    static constexpr bool present = false;
    using type = void;
};

template <typename Head, typename... Tail> struct InStreamCapability<Capabilities<Head, Tail...>>
{
  private:
    using Remaining = InStreamCapability<Capabilities<Tail...>>;

  public:
    static constexpr bool present = IsInStream<Head>::value || Remaining::present;
    static_assert(!(IsInStream<Head>::value && Remaining::present),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM: Data declares more than one "
                  "InStream capability");
    using type = std::conditional_t<IsInStream<Head>::value, Head, typename Remaining::type>;
};

template <typename DataT>
inline constexpr bool has_in_stream_v = InStreamCapability<typename DataT::Capabilities>::present;

template <typename T> struct IsReliableWindow : std::false_type
{};

template <std::size_t Count> struct IsReliableWindow<ReliableWindow<Count>> : std::true_type
{
    static constexpr std::size_t count = Count;
};

template <typename PolicyTypes> struct InboundWindow;

template <typename... Policies> struct InboundWindow<TypeList<Policies...>>
{
    static_assert((static_cast<std::size_t>(IsReliableWindow<Policies>::value) + ... + 0U) <= 1,
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_RELIABLE_WINDOW: InStream declares more "
                  "than one ReliableWindow");
    static constexpr std::size_t value = [] {
        std::size_t count{1};
        (([&] {
             if constexpr (IsReliableWindow<Policies>::value) {
                 count = IsReliableWindow<Policies>::count;
             }
         }()),
         ...);
        return count;
    }();
};

template <typename PolicyTypes> struct InStreamExecutionPolicy;

template <> struct InStreamExecutionPolicy<TypeList<>>
{
    using type = void;
};

template <typename Head, typename... Tail> struct InStreamExecutionPolicy<TypeList<Head, Tail...>>
{
  private:
    using Remaining = typename InStreamExecutionPolicy<TypeList<Tail...>>::type;
    static constexpr bool selected = std::same_as<Head, Inline> || IsOn<Head>::value;

  public:
    static_assert(!(selected && !std::is_void_v<Remaining>),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM_EXECUTION: InStream declares "
                  "more than one execution policy");
    using type = std::conditional_t<selected, Head, Remaining>;
};

template <typename PolicyTypes> struct InStreamLifecycle;

template <typename... Policies> struct InStreamLifecycle<TypeList<Policies...>>
{
    static constexpr std::size_t open_count =
        (static_cast<std::size_t>(remote::detail::IsOnOpen<Policies>::value) + ... + 0U);
    static constexpr std::size_t close_count =
        (static_cast<std::size_t>(remote::detail::IsOnClose<Policies>::value) + ... + 0U);
    static constexpr std::size_t exclusive_count =
        (static_cast<std::size_t>(remote::detail::IsExclusive<Policies>::value) + ... + 0U);
    static_assert(open_count <= 1,
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM_ON_OPEN: InStream declares more "
                  "than one OnOpen callback");
    static_assert(close_count <= 1,
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM_ON_CLOSE: InStream declares more "
                  "than one OnClose callback");
    static_assert(exclusive_count <= 1,
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM_EXCLUSIVE: InStream declares more "
                  "than one Exclusive policy");

    static constexpr std::uint32_t maximum_rate_hz = [] {
        std::uint32_t rate{};
        (([]<typename Policy>(std::uint32_t& value) {
             if constexpr (requires { Policy::hertz; }) {
                 value = Policy::hertz;
             }
         }.template operator()<Policies>(rate)),
         ...);
        return rate;
    }();
};

template <typename PolicyTypes> struct FirstInStreamExclusive;

template <> struct FirstInStreamExclusive<TypeList<>>
{
    using Group = void;
    using Behavior = void;
};

template <typename Head, typename... Tail>
struct FirstInStreamExclusive<TypeList<Head, Tail...>>
{
  private:
    using Remaining = FirstInStreamExclusive<TypeList<Tail...>>;

  public:
    using Group = std::conditional_t<remote::detail::IsExclusive<Head>::value,
                                     typename remote::detail::IsExclusive<Head>::Group,
                                     typename Remaining::Group>;
    using Behavior = std::conditional_t<remote::detail::IsExclusive<Head>::value,
                                        typename remote::detail::IsExclusive<Head>::Behavior,
                                        typename Remaining::Behavior>;
};

template <typename DataT> struct InStreamTraits
{
    using Capability = typename InStreamCapability<typename DataT::Capabilities>::type;
    using Policies = typename IsInStream<Capability>::PolicyTypes;
    static constexpr std::size_t window = InboundWindow<Policies>::value;
    using AuthoredExecution = typename InStreamExecutionPolicy<Policies>::type;
    using DataExecution = action_execution_t<DataT>;
    using Execution =
        std::conditional_t<!std::is_void_v<AuthoredExecution>, AuthoredExecution, DataExecution>;
    using Lifecycle = InStreamLifecycle<Policies>;
    using ExclusivePolicy = FirstInStreamExclusive<Policies>;
    using ExclusiveGroup = typename ExclusivePolicy::Group;
    using ExclusiveBehavior = typename ExclusivePolicy::Behavior;
    static constexpr bool exclusive = !std::is_void_v<ExclusiveGroup>;
    static constexpr std::uint32_t minimum_interval_us =
        Lifecycle::maximum_rate_hz == 0 ? 0 : 1'000'000U / Lifecycle::maximum_rate_hz;
};

template <typename T> struct OutStreamMaxRate : std::integral_constant<std::uint32_t, 0>
{};

template <typename Acquisition, typename... Policies>
struct OutStreamMaxRate<OutStream<Acquisition, Policies...>>
    : std::integral_constant<std::uint32_t, [] {
        std::uint32_t rate{};
        (([]<typename Policy>(std::uint32_t& value) {
             if constexpr (requires { Policy::hertz; }) {
                 value = Policy::hertz;
             }
         }.template operator()<Policies>(rate)),
         ...);
        return rate;
    }()>
{};

template <typename CapabilitiesT> struct DeclaredOutStreamMaxRate;

template <typename... CapabilityTypes>
struct DeclaredOutStreamMaxRate<Capabilities<CapabilityTypes...>>
    : std::integral_constant<std::uint32_t,
                             (std::max)({OutStreamMaxRate<CapabilityTypes>::value...})>
{};

template <typename T> struct IsQuery : std::false_type
{};

template <auto Reader> struct IsQuery<Query<Reader>> : std::true_type
{
    static constexpr auto reader = Reader;
};

template <typename CapabilitiesT> struct QueryCapability;

struct NoQuery
{};

template <> struct QueryCapability<Capabilities<>>
{
    static constexpr bool present = false;
    using type = NoQuery;
};

template <typename Head, typename... Tail> struct QueryCapability<Capabilities<Head, Tail...>>
{
  private:
    using Remaining = QueryCapability<Capabilities<Tail...>>;

  public:
    static constexpr bool present = IsQuery<Head>::value || Remaining::present;
    static_assert(!(IsQuery<Head>::value && Remaining::present),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_QUERY: Data declares Query more than once");
    using type = std::conditional_t<IsQuery<Head>::value, Head, typename Remaining::type>;
};

template <typename DataT>
inline constexpr bool has_query_v = QueryCapability<typename DataT::Capabilities>::present;

template <typename T> struct IsUpdate : std::false_type
{};

template <auto Writer> struct IsUpdate<Update<Writer>> : std::true_type
{
    static constexpr auto writer = Writer;
};

template <typename CapabilitiesT> struct UpdateCapability;

struct NoUpdate
{};

template <> struct UpdateCapability<Capabilities<>>
{
    static constexpr bool present = false;
    using type = NoUpdate;
};

template <typename Head, typename... Tail> struct UpdateCapability<Capabilities<Head, Tail...>>
{
  private:
    using Remaining = UpdateCapability<Capabilities<Tail...>>;

  public:
    static constexpr bool present = IsUpdate<Head>::value || Remaining::present;
    static_assert(!(IsUpdate<Head>::value && Remaining::present),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_UPDATE: Data declares Update more than once");
    using type = std::conditional_t<IsUpdate<Head>::value, Head, typename Remaining::type>;
};

template <typename DataT>
inline constexpr bool has_update_v = UpdateCapability<typename DataT::Capabilities>::present;

template <typename Return, typename Value>
concept ExpectedValue = requires {
    typename Return::value_type;
    typename Return::error_type;
} && std::same_as<typename Return::value_type, Value>;

template <typename Return>
concept ExpectedVoid = requires {
    typename Return::value_type;
    typename Return::error_type;
} && std::same_as<typename Return::value_type, void>;

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)

struct PushStateKey
{};

template <typename DataT> struct PushState
{
    using Value = typename DataT::Value;
    using Stream = typename PushCapability<typename DataT::Capabilities>::type;
    using Storage = PushStoragePolicy<Stream>;

    kernel::SpinLock lock{};
    std::array<std::optional<Value>, Storage::depth> values{};
    std::size_t head{};
    std::size_t size{};
    std::uint64_t sequence{};
    std::uint64_t replaced{};
    std::atomic_bool wake_pending{};
    std::atomic_uint32_t interested_sessions{};
};

template <typename System, typename DataT> [[nodiscard]] auto& push_state() noexcept
{
    return System::template StateSlot<DataT, PushStateKey, PushState<DataT>>::value;
}

template <typename System, Data DataT, bool FromIsr = false>
[[nodiscard]] Result<WriteReceipt, Error> write_data(typename DataT::Value value) noexcept
{
    static_assert(System::RemoteDataCatalog::template contains<DataT>,
                  "SOLAR_DIAGNOSTIC_REMOTE_DATA_NOT_REGISTERED: written Data is absent from the "
                  "bound Remote catalog");
    static_assert(has_push_v<DataT>,
                  "SOLAR_DIAGNOSTIC_REMOTE_WRITE_REQUIRES_PUSH: remote::write requires an "
                  "OutStream<Push, ...> capability");
    if constexpr (FromIsr) {
        static_assert(std::is_trivially_copyable_v<typename DataT::Value> &&
                          std::is_trivially_destructible_v<typename DataT::Value>,
                      "SOLAR_DIAGNOSTIC_REMOTE_ISR_VALUE: ISR publication requires a trivially "
                      "copyable and trivially destructible Value");
    }
    if (kernel::in_isr() != FromIsr) {
        return fail<Error>({Status::Invalid, Reason::InvalidContext, Operation::Publish});
    }
    using FacilityT = typename System::RemoteFacility;
    if (!FacilityT::ready.load(std::memory_order_acquire) ||
        !FacilityT::accepting.load(std::memory_order_acquire)) {
        return fail<Error>({Status::NotReady, Reason::NotReady, Operation::Publish});
    }

    auto& state = push_state<System, DataT>();
    WriteDisposition disposition{WriteDisposition::Accepted};
    std::uint64_t sequence{};
    {
        auto guard = state.lock.acquire();
        using Storage = typename std::remove_reference_t<decltype(state)>::Storage;
        if (state.size == state.values.size()) {
            if constexpr (std::same_as<typename Storage::Overflow, Reject>) {
                return fail<Error>({Status::NoSpace, Reason::NoCapacity, Operation::Publish});
            } else if constexpr (std::same_as<typename Storage::Overflow, DropNewest>) {
                ++state.replaced;
                sequence = ++state.sequence;
                return WriteReceipt{.disposition = WriteDisposition::DroppedNewest,
                                    .sequence = sequence,
                                    .wake_queued =
                                        state.wake_pending.load(std::memory_order_acquire)};
            } else {
                state.values[state.head].reset();
                state.head = (state.head + 1U) % state.values.size();
                --state.size;
                disposition = WriteDisposition::ReplacedOlder;
                ++state.replaced;
            }
        }
        const auto tail = (state.head + state.size) % state.values.size();
        state.values[tail] = std::move(value);
        ++state.size;
        sequence = ++state.sequence;
    }

    const bool already_pending = state.wake_pending.exchange(true, std::memory_order_acq_rel);
    if (state.interested_sessions.load(std::memory_order_acquire) == 0) {
        state.wake_pending.store(false, std::memory_order_release);
        return WriteReceipt{.disposition = WriteDisposition::NoSubscribers,
                            .sequence = sequence,
                            .wake_queued = false};
    }
    bool wake_queued = already_pending;
    if (!already_pending) {
        constexpr auto endpoint = System::RemoteDataCatalog::template Entry<DataT>::local_id.value;
        const auto status = System::RemoteService::notify_publication(endpoint);
        wake_queued = status.has_value();
        if (!wake_queued) {
            state.wake_pending.store(false, std::memory_order_release);
        }
    } else {
        disposition = WriteDisposition::Coalesced;
    }
    return WriteReceipt{
        .disposition = disposition, .sequence = sequence, .wake_queued = wake_queued};
}

template <typename System, Data DataT> [[nodiscard]] bool interested_in_data() noexcept
{
    static_assert(System::RemoteDataCatalog::template contains<DataT>,
                  "SOLAR_DIAGNOSTIC_REMOTE_DATA_NOT_REGISTERED: interested Data is absent from "
                  "the bound Remote catalog");
    static_assert(has_push_v<DataT>,
                  "SOLAR_DIAGNOSTIC_REMOTE_INTEREST_REQUIRES_PUSH: remote::interested requires "
                  "an OutStream<Push, ...> capability");
    return push_state<System, DataT>().interested_sessions.load(std::memory_order_acquire) != 0;
}

template <typename System, Data DataT>
[[nodiscard]] std::optional<typename DataT::Value> take_next() noexcept
{
    auto& state = push_state<System, DataT>();
    auto guard = state.lock.acquire();
    if (state.size == 0) {
        state.wake_pending.store(false, std::memory_order_release);
        return std::nullopt;
    }
    auto value = std::move(state.values[state.head]);
    state.values[state.head].reset();
    state.head = (state.head + 1U) % state.values.size();
    --state.size;
    state.wake_pending.store(false, std::memory_order_release);
    return value;
}

template <typename System, Data DataT> [[nodiscard]] bool push_pending() noexcept
{
    auto& state = push_state<System, DataT>();
    auto guard = state.lock.acquire();
    return state.size != 0;
}

template <typename System, Data DataT> void rearm_push() noexcept
{
    auto& state = push_state<System, DataT>();
    if (!push_pending<System, DataT>() ||
        state.wake_pending.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    constexpr auto endpoint = System::RemoteDataCatalog::template Entry<DataT>::local_id.value;
    if (!System::RemoteService::notify_publication(endpoint)) {
        state.wake_pending.store(false, std::memory_order_release);
    }
}

enum class LoanSlotState : std::uint8_t
{
    Free,
    Loaned,
    Ready,
};

struct LoanSlot
{
    LoanSlotState state{LoanSlotState::Free};
    std::uint16_t generation{};
    std::uint16_t size{};
};

template <typename DataT> struct LoanState
{
    using Capability = typename LoanedCapability<typename DataT::Capabilities>::type;
    using Pool = typename IsLoanedOutStream<Capability>::PoolType;

    kernel::SpinLock lock{};
    alignas(Pool::alignment) std::array<std::array<std::byte, Pool::bytes>, Pool::slots> bytes{};
    std::array<LoanSlot, Pool::slots> slots{};
    std::atomic_bool wake_pending{};
    std::atomic_uint32_t interested_sessions{};
    std::uint64_t sequence{};
    std::uint32_t abandoned{};
    std::uint32_t committed{};
    std::uint32_t released{};
};

template <typename DataT> struct LoanStateKey
{};

template <typename System, typename DataT> [[nodiscard]] auto& loan_state() noexcept
{
    return System::template StateSlot<DataT, LoanStateKey<DataT>, LoanState<DataT>>::value;
}

template <typename System, typename DataT>
void abandon_loan(std::uint16_t slot, std::uint16_t generation) noexcept
{
    auto& state = loan_state<System, DataT>();
    auto guard = state.lock.acquire();
    if (slot < state.slots.size() && state.slots[slot].state == LoanSlotState::Loaned &&
        state.slots[slot].generation == generation) {
        state.slots[slot].state = LoanSlotState::Free;
        state.slots[slot].size = 0;
        ++state.abandoned;
    }
}

template <typename System, typename DataT>
void release_ready_loan(std::uint16_t slot, std::uint16_t generation) noexcept
{
    auto& state = loan_state<System, DataT>();
    auto guard = state.lock.acquire();
    if (slot < state.slots.size() && state.slots[slot].state == LoanSlotState::Ready &&
        state.slots[slot].generation == generation) {
        state.slots[slot].state = LoanSlotState::Free;
        state.slots[slot].size = 0;
        ++state.released;
    }
}

template <typename System, typename DataT>
[[nodiscard]] Result<Loan<DataT>, Error> try_loan_data() noexcept
{
    static_assert(has_loaned_v<DataT>);
    static_assert(Schema<typename DataT::Value>::codec == Codec::Packed,
                  "SOLAR_DIAGNOSTIC_REMOTE_LOAN_CODEC: byte Loaned streams require a Packed "
                  "schema");
    using Pool = typename LoanState<DataT>::Pool;
    static_assert(Pool::bytes >= Schema<typename DataT::Value>::max_encoded_size,
                  "SOLAR_DIAGNOSTIC_REMOTE_LOAN_SIZE: LoanedPool bytes must fit the declared "
                  "schema ceiling");
    if (kernel::in_isr()) {
        return fail<Error>({Status::Invalid, Reason::InvalidContext, Operation::Publish});
    }
    using FacilityT = typename System::RemoteFacility;
    if (!FacilityT::ready.load(std::memory_order_acquire) ||
        !FacilityT::accepting.load(std::memory_order_acquire)) {
        return fail<Error>({Status::NotReady, Reason::NotReady, Operation::Publish});
    }
    auto& state = loan_state<System, DataT>();
    auto guard = state.lock.acquire();
    for (std::uint16_t slot{}; slot < state.slots.size(); ++slot) {
        auto& record = state.slots[slot];
        if (record.state != LoanSlotState::Free) {
            continue;
        }
        record.state = LoanSlotState::Loaned;
        if (++record.generation == 0) {
            ++record.generation;
        }
        return Loan<DataT>::make(state.bytes[slot], slot, record.generation,
                                 &abandon_loan<System, DataT>);
    }
    return fail<Error>({Status::NoBuffer, Reason::NoCapacity, Operation::Publish});
}

template <typename System, typename DataT>
[[nodiscard]] Result<WriteReceipt, Error> commit_loan_data(Loan<DataT>&& loan,
                                                           std::size_t size) noexcept
{
    static_assert(has_loaned_v<DataT>);
    if (kernel::in_isr()) {
        return fail<Error>({Status::Invalid, Reason::InvalidContext, Operation::Publish});
    }
    if (!loan || size > loan.capacity() || size > (std::numeric_limits<std::uint16_t>::max)()) {
        return fail<Error>({Status::Invalid, Reason::InvalidValue, Operation::Publish});
    }
    using FacilityT = typename System::RemoteFacility;
    if (!FacilityT::accepting.load(std::memory_order_acquire)) {
        return fail<Error>({Status::NotReady, Reason::NotReady, Operation::Publish});
    }
    auto& state = loan_state<System, DataT>();
    std::uint64_t sequence{};
    {
        auto guard = state.lock.acquire();
        if (loan.slot() >= state.slots.size()) {
            return fail<Error>({Status::Invalid, Reason::InvalidValue, Operation::Publish});
        }
        auto& record = state.slots[loan.slot()];
        if (record.state != LoanSlotState::Loaned || record.generation != loan.generation()) {
            return fail<Error>({Status::Invalid, Reason::InvalidValue, Operation::Publish});
        }
        record.state = LoanSlotState::Ready;
        record.size = static_cast<std::uint16_t>(size);
        ++state.committed;
        sequence = ++state.sequence;
    }
    loan.commit_ownership();
    if (state.interested_sessions.load(std::memory_order_acquire) == 0) {
        release_ready_loan<System, DataT>(loan.slot(), loan.generation());
        return WriteReceipt{.disposition = WriteDisposition::NoSubscribers,
                            .sequence = sequence,
                            .wake_queued = false};
    }
    const bool pending = state.wake_pending.exchange(true, std::memory_order_acq_rel);
    bool queued = pending;
    if (!pending) {
        constexpr auto endpoint = System::RemoteDataCatalog::template Entry<DataT>::local_id.value;
        queued = System::RemoteService::notify_publication(endpoint).has_value();
        if (!queued) {
            state.wake_pending.store(false, std::memory_order_release);
        }
    }
    return WriteReceipt{.disposition =
                            pending ? WriteDisposition::Coalesced : WriteDisposition::Accepted,
                        .sequence = sequence,
                        .wake_queued = queued};
}

template <typename System, typename DataT>
[[nodiscard]] consteval std::uint16_t data_stream_subscription_slot()
{
    return System::RemoteDataCatalog::template Entry<DataT>::local_id.value;
}

template <typename System, typename DataT>
[[nodiscard]] consteval std::uint16_t data_watch_subscription_slot()
{
    return static_cast<std::uint16_t>(
        System::RemoteDataCatalog::size +
        System::RemoteDataCatalog::template Entry<DataT>::local_id.value);
}

template <typename System, typename TopicT>
[[nodiscard]] consteval std::uint16_t topic_subscription_slot()
{
    return static_cast<std::uint16_t>(
        System::RemoteDataCatalog::size * 2 +
        System::RemoteTopicCatalog::template Entry<TopicT>::local_id.value);
}

template <typename DeclarationT, typename PublicationT> struct DiscreteState
{
    using Policies = typename IsWatch<PublicationT>::PolicyTypes;
    using Storage = DiscreteStoragePolicy<Policies>;

    kernel::SpinLock lock{};
    std::array<std::optional<typename DeclarationT::Value>, Storage::depth> values{};
    std::size_t head{};
    std::size_t size{};
    std::atomic_bool wake_pending{};
    std::atomic_uint32_t interested_sessions{};
    std::uint64_t sequence{};
    std::uint32_t replaced{};
};

template <typename DataT> struct WatchStateKey
{};
template <typename TopicT> struct TopicStateKey
{};

template <typename System, typename DataT> [[nodiscard]] auto& watch_state() noexcept
{
    using Publication = typename WatchCapability<typename DataT::Capabilities>::type;
    using State = DiscreteState<DataT, Publication>;
    return System::template StateSlot<DataT, WatchStateKey<DataT>, State>::value;
}

template <typename System, typename TopicT> [[nodiscard]] auto& topic_state() noexcept
{
    using Publication = typename TopicPublication<TopicT>::type;
    using State = DiscreteState<TopicT, Publication>;
    return System::template StateSlot<TopicT, TopicStateKey<TopicT>, State>::value;
}

template <typename System, typename DeclarationT, typename StateT>
[[nodiscard]] Result<WriteReceipt, Error>
write_discrete(StateT& state, std::uint16_t subscription_slot,
               typename DeclarationT::Value value) noexcept
{
    if (kernel::in_isr()) {
        return fail<Error>({Status::Invalid, Reason::InvalidContext, Operation::Publish});
    }
    using FacilityT = typename System::RemoteFacility;
    if (!FacilityT::ready.load(std::memory_order_acquire) ||
        !FacilityT::accepting.load(std::memory_order_acquire)) {
        return fail<Error>({Status::NotReady, Reason::NotReady, Operation::Publish});
    }

    WriteDisposition disposition{WriteDisposition::Accepted};
    std::uint64_t sequence{};
    {
        auto guard = state.lock.acquire();
        using Storage = typename StateT::Storage;
        if (state.size == state.values.size()) {
            if constexpr (std::same_as<typename Storage::Overflow, Reject>) {
                return fail<Error>({Status::NoSpace, Reason::NoCapacity, Operation::Publish});
            } else if constexpr (std::same_as<typename Storage::Overflow, DropNewest>) {
                ++state.replaced;
                sequence = ++state.sequence;
                return WriteReceipt{.disposition = WriteDisposition::DroppedNewest,
                                    .sequence = sequence,
                                    .wake_queued =
                                        state.wake_pending.load(std::memory_order_acquire)};
            } else {
                state.values[state.head].reset();
                state.head = (state.head + 1U) % state.values.size();
                --state.size;
                disposition = WriteDisposition::ReplacedOlder;
                ++state.replaced;
            }
        }
        const auto tail = (state.head + state.size) % state.values.size();
        state.values[tail] = std::move(value);
        ++state.size;
        sequence = ++state.sequence;
    }

    const bool pending = state.wake_pending.exchange(true, std::memory_order_acq_rel);
    if (state.interested_sessions.load(std::memory_order_acquire) == 0) {
        state.wake_pending.store(false, std::memory_order_release);
        return WriteReceipt{.disposition = WriteDisposition::NoSubscribers,
                            .sequence = sequence,
                            .wake_queued = false};
    }
    bool queued = pending;
    if (!pending) {
        queued = System::RemoteService::notify_publication(subscription_slot).has_value();
        if (!queued) {
            state.wake_pending.store(false, std::memory_order_release);
        }
    } else {
        disposition = WriteDisposition::Coalesced;
    }
    return WriteReceipt{.disposition = disposition, .sequence = sequence, .wake_queued = queued};
}

template <typename System, typename DataT>
[[nodiscard]] Result<WriteReceipt, Error> publish_watch(typename DataT::Value value) noexcept
{
    static_assert(has_watch_v<DataT>);
    auto& state = watch_state<System, DataT>();
    return write_discrete<System, DataT>(state, data_watch_subscription_slot<System, DataT>(),
                                         std::move(value));
}

template <typename System, typename TopicT>
[[nodiscard]] Result<WriteReceipt, Error> publish_topic(typename TopicT::Value value) noexcept
{
    auto& state = topic_state<System, TopicT>();
    return write_discrete<System, TopicT>(state, topic_subscription_slot<System, TopicT>(),
                                          std::move(value));
}

template <typename DeclarationT, typename PublicationT>
[[nodiscard]] std::optional<typename DeclarationT::Value>
take_discrete(DiscreteState<DeclarationT, PublicationT>& state) noexcept
{
    auto guard = state.lock.acquire();
    if (state.size == 0) {
        state.wake_pending.store(false, std::memory_order_release);
        return std::nullopt;
    }
    auto value = std::move(state.values[state.head]);
    state.values[state.head].reset();
    state.head = (state.head + 1U) % state.values.size();
    --state.size;
    state.wake_pending.store(false, std::memory_order_release);
    return value;
}

template <typename StateT> [[nodiscard]] bool discrete_pending(StateT& state) noexcept
{
    auto guard = state.lock.acquire();
    return state.size != 0;
}

template <typename System, typename StateT>
void rearm_discrete(StateT& state, std::uint16_t subscription_slot) noexcept
{
    if (!discrete_pending(state) || state.wake_pending.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (!System::RemoteService::notify_publication(subscription_slot)) {
        state.wake_pending.store(false, std::memory_order_release);
    }
}

template <typename System, typename DataT> struct PublicationBuffer
{
    inline static std::array<std::byte, Schema<typename DataT::Value>::max_encoded_size> bytes{};
};

template <typename System>
void publish_subscription_payload(std::span<const std::byte> payload,
                                  std::uint16_t subscription_slot, std::uint32_t target,
                                  protocol::SubscriptionKind subscription_kind,
                                  protocol::Flags flags = protocol::Flags::None) noexcept
{
    using ServiceT = typename System::RemoteService;
    []<typename... LinkTypes, std::size_t... Indices>(
        std::span<const std::byte> encoded, std::uint16_t slot, std::uint32_t endpoint_target,
        protocol::SubscriptionKind kind, protocol::Flags payload_flags, TypeList<LinkTypes...>,
        std::index_sequence<Indices...>) {
        (([&] {
             using State =
                 detail::LinkState<ServiceT, LinkTypes, static_cast<std::uint16_t>(Indices)>;
             bool subscribed{};
             bool rate_selected{};
             const auto now = kernel::now_ticks();
             {
                 auto guard = State::output_lock.acquire();
                 auto& subscription = State::subscriptions[slot];
                 subscribed = subscription.active;
                 if (subscribed) {
                     if (subscription.next_delivery <= now) {
                         rate_selected = true;
                         subscription.next_delivery =
                             now + kernel::to_ticks_ceil(
                                       std::chrono::microseconds{subscription.minimum_interval_us});
                     } else {
                         ++subscription.skipped;
                     }
                 }
             }
             if (subscribed && rate_selected &&
                 State::session.load(std::memory_order_acquire) == SessionState::Active) {
                 const auto transmitted =
                     ServiceT::template transmit<LinkTypes, static_cast<std::uint16_t>(Indices)>(
                         protocol::Kind::Data, encoded, 0, endpoint_target, payload_flags,
                         static_cast<std::uint8_t>(kind));
                 auto guard = State::output_lock.acquire();
                 auto& subscription = State::subscriptions[slot];
                 if (transmitted) {
                     ++subscription.delivered;
                 } else {
                     ++subscription.dropped;
                 }
             }
         }()),
         ...);
    }(payload, subscription_slot, target, subscription_kind, flags,
      typename System::RemoteArchitecture::Links{},
      std::make_index_sequence<list_size_v<typename System::RemoteArchitecture::Links>>{});
}

template <typename System, typename DataT>
void publish_data_payload(std::span<const std::byte> payload,
                          protocol::Flags flags = protocol::Flags::None) noexcept
{
    publish_subscription_payload<System>(payload, data_stream_subscription_slot<System, DataT>(),
                                         DataT::descriptor.id.value,
                                         protocol::SubscriptionKind::DataStream, flags);
}

template <typename System, typename DataT>
void publish_data_value(typename DataT::Value value) noexcept
{
    using Value = typename DataT::Value;
    auto& buffer = PublicationBuffer<System, DataT>::bytes;
    Result<std::size_t, Error> encoded = [&]() -> Result<std::size_t, Error> {
        if constexpr (Schema<Value>::codec == Codec::Cbor) {
            return cbor::encode(value, buffer);
        } else {
            return packed::encode(value, buffer);
        }
    }();
    if (!encoded) {
        return;
    }
    constexpr auto flags = Schema<Value>::codec == Codec::Packed ? protocol::Flags::PackedPayload
                                                                 : protocol::Flags::None;
    publish_data_payload<System, DataT>(std::span{buffer}.first(*encoded), flags);
}

template <typename System, typename DeclarationT, typename StateT>
void publish_discrete_value(StateT& state, std::uint16_t subscription_slot,
                            std::uint32_t target) noexcept
{
    using Value = typename DeclarationT::Value;
    auto value = take_discrete(state);
    if (!value) {
        return;
    }
    auto& buffer = PublicationBuffer<System, DeclarationT>::bytes;
    Result<std::size_t, Error> encoded = [&]() -> Result<std::size_t, Error> {
        if constexpr (Schema<Value>::codec == Codec::Cbor) {
            return cbor::encode(*value, buffer);
        } else {
            return packed::encode(*value, buffer);
        }
    }();
    if (encoded) {
        constexpr auto flags = Schema<Value>::codec == Codec::Packed
                                   ? protocol::Flags::PackedPayload
                                   : protocol::Flags::None;
        publish_subscription_payload<System>(std::span{buffer}.first(*encoded), subscription_slot,
                                             target,
                                             subscription_slot < System::RemoteDataCatalog::size * 2
                                                 ? protocol::SubscriptionKind::DataWatch
                                                 : protocol::SubscriptionKind::Topic,
                                             flags);
    }
    rearm_discrete<System>(state, subscription_slot);
}

template <typename System, typename DataT> struct BatchBuffer
{
    using Storage = typename PushState<DataT>::Storage;
    static constexpr std::size_t capacity =
        protocol::batch_header_size +
        Storage::batch_count *
            (sizeof(std::uint16_t) + Schema<typename DataT::Value>::max_encoded_size);
    static_assert(capacity <= CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES,
                  "SOLAR_DIAGNOSTIC_REMOTE_BATCH_MESSAGE: Batch payload exceeds the logical "
                  "message ceiling");
    inline static std::array<std::byte, capacity> bytes{};
};

template <typename System, typename DataT> void publish_push_batch() noexcept
{
    using Value = typename DataT::Value;
    using Storage = typename PushState<DataT>::Storage;
    auto& output = BatchBuffer<System, DataT>::bytes;
    std::size_t offset = protocol::batch_header_size;
    std::uint16_t count{};
    while (count < Storage::batch_count) {
        auto value = take_next<System, DataT>();
        if (!value) {
            break;
        }
        auto destination = std::span{output}.subspan(offset + sizeof(std::uint16_t));
        Result<std::size_t, Error> encoded = [&]() -> Result<std::size_t, Error> {
            if constexpr (Schema<Value>::codec == Codec::Cbor) {
                return cbor::encode(*value, destination);
            } else {
                return packed::encode(*value, destination);
            }
        }();
        if (!encoded) {
            break;
        }
        protocol::detail::put_u16(output, offset, static_cast<std::uint16_t>(*encoded));
        offset += sizeof(std::uint16_t) + *encoded;
        ++count;
    }
    if (count == 0) {
        return;
    }
    const auto header = protocol::encode(protocol::BatchHeader{
        .count = count,
        .codec = Schema<Value>::codec,
    });
    std::copy(header.begin(), header.end(), output.begin());
    constexpr auto flags = Schema<Value>::codec == Codec::Packed ? protocol::Flags::PackedPayload
                                                                 : protocol::Flags::None;
    publish_data_payload<System, DataT>(std::span{output}.first(offset), flags);
}

struct ReadyLoan
{
    std::uint16_t slot{};
    std::uint16_t generation{};
    std::span<const std::byte> bytes{};
};

template <typename System, typename DataT>
[[nodiscard]] std::optional<ReadyLoan> take_ready_loan() noexcept
{
    auto& state = loan_state<System, DataT>();
    auto guard = state.lock.acquire();
    for (std::uint16_t slot{}; slot < state.slots.size(); ++slot) {
        const auto& record = state.slots[slot];
        if (record.state == LoanSlotState::Ready) {
            state.wake_pending.store(false, std::memory_order_release);
            return ReadyLoan{
                .slot = slot,
                .generation = record.generation,
                .bytes = std::span{state.bytes[slot]}.first(record.size),
            };
        }
    }
    state.wake_pending.store(false, std::memory_order_release);
    return std::nullopt;
}

template <typename System, typename DataT> [[nodiscard]] bool loan_ready() noexcept
{
    auto& state = loan_state<System, DataT>();
    auto guard = state.lock.acquire();
    return std::ranges::any_of(state.slots,
                               [](const auto& slot) { return slot.state == LoanSlotState::Ready; });
}

template <typename System, typename DataT> void rearm_loan() noexcept
{
    auto& state = loan_state<System, DataT>();
    if (!loan_ready<System, DataT>() ||
        state.wake_pending.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    constexpr auto endpoint = System::RemoteDataCatalog::template Entry<DataT>::local_id.value;
    if (!System::RemoteService::notify_publication(endpoint)) {
        state.wake_pending.store(false, std::memory_order_release);
    }
}

template <typename System, typename DataT> void publish_ready_loan() noexcept
{
    auto ready = take_ready_loan<System, DataT>();
    if (!ready) {
        return;
    }
    const auto flags = Schema<typename DataT::Value>::codec == Codec::Packed
                           ? protocol::Flags::PackedPayload
                           : protocol::Flags::None;
    publish_data_payload<System, DataT>(ready->bytes, flags);
    release_ready_loan<System, DataT>(ready->slot, ready->generation);
    rearm_loan<System, DataT>();
}

template <typename System, typename... DataTypes>
void process_stream_publication_for(std::uint16_t endpoint, TypeList<DataTypes...>) noexcept
{
    std::uint16_t index{};
    ((endpoint == index++
          ? (
                [&] {
                    if constexpr (has_push_v<DataTypes>) {
                        using Storage = typename PushState<DataTypes>::Storage;
                        if constexpr (Storage::batch_count > 1) {
                            publish_push_batch<System, DataTypes>();
                        } else {
                            auto value = take_next<System, DataTypes>();
                            if (value) {
                                publish_data_value<System, DataTypes>(std::move(*value));
                            }
                        }
                        rearm_push<System, DataTypes>();
                    } else if constexpr (has_loaned_v<DataTypes>) {
                        publish_ready_loan<System, DataTypes>();
                    }
                }(),
                void())
          : void()),
     ...);
}

template <typename System, typename... DataTypes>
void process_watch_publication_for(std::uint16_t endpoint, TypeList<DataTypes...>) noexcept
{
    std::uint16_t index{};
    ((endpoint == index++ ? (
                                [&] {
                                    if constexpr (has_watch_v<DataTypes>) {
                                        auto& state = watch_state<System, DataTypes>();
                                        publish_discrete_value<System, DataTypes>(
                                            state,
                                            data_watch_subscription_slot<System, DataTypes>(),
                                            DataTypes::descriptor.id.value);
                                    }
                                }(),
                                void())
                          : void()),
     ...);
}

template <typename System, typename... TopicTypes>
void process_topic_publication_for(std::uint16_t endpoint, TypeList<TopicTypes...>) noexcept
{
    std::uint16_t index{};
    ((endpoint == index++ ? (
                                [&] {
                                    auto& state = topic_state<System, TopicTypes>();
                                    publish_discrete_value<System, TopicTypes>(
                                        state, topic_subscription_slot<System, TopicTypes>(),
                                        TopicTypes::descriptor.id.value);
                                }(),
                                void())
                          : void()),
     ...);
}

template <typename System> void process_publication(std::uint16_t endpoint) noexcept
{
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    using TopicTypes = declarations_of_t<typename System::RemoteTopicCatalog::EntryTypes>;
    constexpr auto data_count = System::RemoteDataCatalog::size;
    if (endpoint < data_count) {
        process_stream_publication_for<System>(endpoint, DataTypes{});
    } else if (endpoint < data_count * 2) {
        process_watch_publication_for<System>(static_cast<std::uint16_t>(endpoint - data_count),
                                              DataTypes{});
    } else {
        process_topic_publication_for<System>(static_cast<std::uint16_t>(endpoint - data_count * 2),
                                              TopicTypes{});
    }
}

template <typename DataT> struct PollStateKey
{};

struct PollState
{
    std::atomic_bool in_flight{};
    kernel::Tick next_release{};
    std::atomic_uint32_t releases{};
    std::atomic_uint32_t skipped{};
    std::atomic_uint32_t failures{};
};

template <typename System, typename DataT> [[nodiscard]] auto& poll_state() noexcept
{
    return System::template StateSlot<DataT, PollStateKey<DataT>, PollState>::value;
}

template <ErrorType ErrorT> [[nodiscard]] Status poll_error_status(const ErrorT& error) noexcept
{
    return status_of(error);
}

template <typename System, typename DataT>
[[nodiscard]] Result<typename DataT::Value> acquire_poll_value() noexcept
{
    using Value = typename DataT::Value;
    using Capability = typename PollCapability<typename DataT::Capabilities>::type;
    using Acquisition = typename Capability::AcquisitionType;
    constexpr auto reader = remote::detail::PollAcquisition<Acquisition>::reader;
    if constexpr (std::is_invocable_v<decltype(reader)>) {
        using Return = std::invoke_result_t<decltype(reader)>;
        static_assert(!std::is_reference_v<Return> && !std::is_pointer_v<Return>,
                      "SOLAR_DIAGNOSTIC_REMOTE_POLL_BORROWED_RETURN: Poll must return an owned "
                      "Value or expected<Value, Error>");
        if constexpr (std::same_as<Return, Value>) {
            return reader();
        } else if constexpr (ExpectedValue<Return, Value>) {
            auto result = reader();
            return result ? Result<Value>{std::move(*result)}
                          : Result<Value>{
                                fail<solar::Error>({.status = poll_error_status(result.error())})};
        } else {
            static_assert(solar::detail::dependent_false_v<Return>,
                          "SOLAR_DIAGNOSTIC_REMOTE_POLL_RETURN: Poll reader must return Value or "
                          "expected<Value, Error>");
        }
    } else if constexpr (std::is_invocable_v<decltype(reader), Value&>) {
        using Return = std::invoke_result_t<decltype(reader), Value&>;
        Value value{};
        if constexpr (std::same_as<Return, void>) {
            reader(value);
            return value;
        } else if constexpr (ExpectedVoid<Return>) {
            auto result = reader(value);
            return result ? Result<Value>{std::move(value)}
                          : Result<Value>{
                                fail<solar::Error>({.status = poll_error_status(result.error())})};
        } else {
            static_assert(solar::detail::dependent_false_v<Return>,
                          "SOLAR_DIAGNOSTIC_REMOTE_POLL_DESTINATION_RETURN: destination Poll "
                          "reader must return void or expected<void, Error>");
        }
    } else {
        static_assert(solar::detail::dependent_false_v<DataT>,
                      "SOLAR_DIAGNOSTIC_REMOTE_POLL_READER: Poll reader must accept no arguments "
                      "or one Value destination");
    }
}

template <typename System, typename DataT> [[nodiscard]] Result<void> execute_poll() noexcept
{
    auto& state = poll_state<System, DataT>();
    auto value = acquire_poll_value<System, DataT>();
    if (value) {
        publish_data_value<System, DataT>(std::move(*value));
    } else {
        state.failures.fetch_add(1, std::memory_order_relaxed);
    }
    state.in_flight.store(false, std::memory_order_release);
    return value ? Result<void>{} : Result<void>{fail<solar::Error>(value.error())};
}

template <typename System, typename... DataTypes>
[[nodiscard]] Result<void> process_poll_work_for(std::uint32_t target,
                                                 TypeList<DataTypes...>) noexcept
{
    bool matched{};
    Result<void> result{fail<solar::Error>({.status = solar::Status::NotFound})};
    (([&] {
         if constexpr (has_poll_v<DataTypes>) {
             if (!matched && target == DataTypes::descriptor.id.value) {
                 matched = true;
                 result = execute_poll<System, DataTypes>();
             }
         }
     }()),
     ...);
    return matched ? result : Result<void>{fail<solar::Error>({.status = solar::Status::NotFound})};
}

template <typename System>
[[nodiscard]] Result<void> process_poll_work(std::uint32_t target) noexcept
{
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    return process_poll_work_for<System>(target, DataTypes{});
}

template <typename System, typename DataT, typename... LinkTypes, std::size_t... Indices>
[[nodiscard]] std::optional<std::uint32_t>
effective_poll_interval(TypeList<LinkTypes...>, std::index_sequence<Indices...>) noexcept
{
    constexpr auto endpoint = System::RemoteDataCatalog::template Entry<DataT>::local_id.value;
    std::optional<std::uint32_t> interval;
    (([&] {
         using State = LinkState<typename System::RemoteService, LinkTypes,
                                 static_cast<std::uint16_t>(Indices)>;
         auto guard = State::output_lock.acquire();
         const auto& subscription = State::subscriptions[endpoint];
         if (subscription.active &&
             State::session.load(std::memory_order_acquire) == SessionState::Active) {
             interval = interval ? (std::min)(*interval, subscription.minimum_interval_us)
                                 : subscription.minimum_interval_us;
         }
     }()),
     ...);
    return interval;
}

template <typename System, typename DataT>
[[nodiscard]] kernel::Tick process_poll_release() noexcept
{
    constexpr auto maintenance = kernel::to_ticks_ceil(std::chrono::milliseconds{50});
    if constexpr (!has_poll_v<DataT>) {
        return maintenance;
    } else {
        using Links = typename System::RemoteArchitecture::Links;
        const auto interval_us = effective_poll_interval<System, DataT>(
            Links{}, std::make_index_sequence<list_size_v<Links>>{});
        auto& state = poll_state<System, DataT>();
        if (!interval_us) {
            state.next_release = 0;
            return maintenance;
        }
        const auto interval = kernel::to_ticks_ceil(std::chrono::microseconds{*interval_us});
        const auto now = kernel::now_ticks();
        if (state.next_release == 0) {
            state.next_release = now;
        }
        if (state.next_release <= now) {
            state.next_release = now + interval;
            if (state.in_flight.exchange(true, std::memory_order_acq_rel)) {
                state.skipped.fetch_add(1, std::memory_order_relaxed);
            } else {
                using Registration =
                    typename System::RemoteService::template PollRegistration<DataT>;
                auto submitted =
                    execution::detail::submit_registration<System, Registration>(false);
                if (submitted) {
                    state.releases.fetch_add(1, std::memory_order_relaxed);
                } else {
                    state.in_flight.store(false, std::memory_order_release);
                    state.failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        return (std::max)(kernel::Tick{1}, state.next_release - now);
    }
}

template <typename System, typename... DataTypes>
[[nodiscard]] kernel::Tick process_poll_releases_for(TypeList<DataTypes...>) noexcept
{
    constexpr auto maintenance = kernel::to_ticks_ceil(std::chrono::milliseconds{50});
    kernel::Tick next = maintenance;
    ((next = (std::min)(next, process_poll_release<System, DataTypes>())), ...);
    return next;
}

template <typename System> [[nodiscard]] std::int64_t process_poll_releases() noexcept
{
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    return process_poll_releases_for<System>(DataTypes{});
}

template <typename DataT> [[nodiscard]] consteval std::uint32_t minimum_stream_interval_us()
{
    constexpr auto declared = DeclaredOutStreamMaxRate<typename DataT::Capabilities>::value;
    constexpr auto maximum =
        declared == 0
            ? static_cast<std::uint32_t>(CONFIG_SOLAR_REMOTE_MAX_STREAM_RATE_HZ)
            : (std::min)(declared,
                         static_cast<std::uint32_t>(CONFIG_SOLAR_REMOTE_MAX_STREAM_RATE_HZ));
    return (1'000'000U + maximum - 1U) / maximum;
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename DataT>
Result<bool, protocol::ErrorCode>
update_data_subscription(std::uint32_t target, bool enable, protocol::SubscriptionKind kind,
                         const protocol::SubscriptionRequest& request,
                         protocol::SubscriptionPolicy& effective) noexcept
{
    if (target != DataT::descriptor.id.value) {
        return false;
    }
    const bool stream = kind == protocol::SubscriptionKind::DataStream;
    const bool watch = kind == protocol::SubscriptionKind::DataWatch;
    if (!stream && !watch) {
        return false;
    }
    if ((stream && !has_out_stream_v<DataT>) || (watch && !has_watch_v<DataT>)) {
        return fail<protocol::ErrorCode>(protocol::ErrorCode::UnsupportedOperation);
    } else {
        constexpr auto codec = Schema<typename DataT::Value>::codec;
        if (request.flags != 0 ||
            (request.codec != 0 && request.codec != static_cast<std::uint8_t>(codec))) {
            return fail<protocol::ErrorCode>(protocol::ErrorCode::UnsupportedCapability);
        }
        const auto endpoint_minimum = stream ? minimum_stream_interval_us<DataT>() : 0U;
        effective = {
            .minimum_interval_us = (std::max)(request.minimum_interval_us, endpoint_minimum),
            .batch_size = 1,
            .codec = codec,
            .flags = 0,
        };
        using ServiceT = typename System::RemoteService;
        using State = LinkState<ServiceT, LinkT, LinkIndex>;
        const auto endpoint = stream ? data_stream_subscription_slot<System, DataT>()
                                     : data_watch_subscription_slot<System, DataT>();
        bool changed{};
        {
            auto guard = State::output_lock.acquire();
            auto& subscription = State::subscriptions[endpoint];
            changed = subscription.active != enable;
            if (enable) {
                subscription.active = true;
                subscription.minimum_interval_us = effective.minimum_interval_us;
                subscription.next_delivery = 0;
            } else if (changed) {
                subscription = {};
            }
            if (changed) {
                if (enable) {
                    ++State::subscription_count;
                } else {
                    --State::subscription_count;
                }
            }
        }
        if (stream) {
            if constexpr (has_push_v<DataT>) {
                if (changed) {
                    auto& interested = push_state<System, DataT>().interested_sessions;
                    if (enable) {
                        interested.fetch_add(1, std::memory_order_acq_rel);
                    } else {
                        interested.fetch_sub(1, std::memory_order_acq_rel);
                    }
                }
            } else if constexpr (has_loaned_v<DataT>) {
                if (changed) {
                    auto& interested = loan_state<System, DataT>().interested_sessions;
                    if (enable) {
                        interested.fetch_add(1, std::memory_order_acq_rel);
                    } else {
                        interested.fetch_sub(1, std::memory_order_acq_rel);
                    }
                }
            }
        } else if constexpr (has_watch_v<DataT>) {
            if (changed) {
                auto& state = watch_state<System, DataT>();
                if (enable) {
                    state.interested_sessions.fetch_add(1, std::memory_order_acq_rel);
                } else {
                    state.interested_sessions.fetch_sub(1, std::memory_order_acq_rel);
                }
            }
        }
        return true;
    }
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename TopicT>
Result<bool, protocol::ErrorCode>
update_topic_subscription(std::uint32_t target, bool enable, protocol::SubscriptionKind kind,
                          const protocol::SubscriptionRequest& request,
                          protocol::SubscriptionPolicy& effective) noexcept
{
    if (kind != protocol::SubscriptionKind::Topic || target != TopicT::descriptor.id.value) {
        return false;
    }
    constexpr auto codec = Schema<typename TopicT::Value>::codec;
    if (request.flags != 0 ||
        (request.codec != 0 && request.codec != static_cast<std::uint8_t>(codec))) {
        return fail<protocol::ErrorCode>(protocol::ErrorCode::UnsupportedCapability);
    }
    effective = {
        .minimum_interval_us = request.minimum_interval_us,
        .batch_size = 1,
        .codec = codec,
        .flags = 0,
    };
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    constexpr auto endpoint = topic_subscription_slot<System, TopicT>();
    bool changed{};
    {
        auto guard = State::output_lock.acquire();
        auto& subscription = State::subscriptions[endpoint];
        changed = subscription.active != enable;
        if (enable) {
            subscription.active = true;
            subscription.minimum_interval_us = effective.minimum_interval_us;
            subscription.next_delivery = 0;
        } else if (changed) {
            subscription = {};
        }
        if (changed) {
            if (enable) {
                ++State::subscription_count;
            } else {
                --State::subscription_count;
            }
        }
    }
    if (changed) {
        auto& state = topic_state<System, TopicT>();
        if (enable) {
            state.interested_sessions.fetch_add(1, std::memory_order_acq_rel);
        } else {
            state.interested_sessions.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
    return true;
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename... DataTypes>
Result<bool, protocol::ErrorCode>
update_subscription(std::uint32_t target, bool enable, protocol::SubscriptionKind kind,
                    const protocol::SubscriptionRequest& request,
                    protocol::SubscriptionPolicy& effective, TypeList<DataTypes...>,
                    auto topic_types) noexcept
{
    Result<bool, protocol::ErrorCode> result{false};
    ((result&& !* result ? result = update_data_subscription<System, LinkT, LinkIndex, DataTypes>(
                               target, enable, kind, request, effective)
                         : result),
     ...);
    [&]<typename... TopicTypes>(TypeList<TopicTypes...>) {
        ((result&& !* result
              ? result = update_topic_subscription<System, LinkT, LinkIndex, TopicTypes>(
                    target, enable, kind, request, effective)
              : result),
         ...);
    }(topic_types);
    return result;
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename... DataTypes>
void reset_link_subscriptions(TypeList<DataTypes...>) noexcept
{
    protocol::SubscriptionRequest request{};
    protocol::SubscriptionPolicy effective{};
    (static_cast<void>(update_data_subscription<System, LinkT, LinkIndex, DataTypes>(
         DataTypes::descriptor.id.value, false, protocol::SubscriptionKind::DataStream, request,
         effective)),
     ...);
    (static_cast<void>(update_data_subscription<System, LinkT, LinkIndex, DataTypes>(
         DataTypes::descriptor.id.value, false, protocol::SubscriptionKind::DataWatch, request,
         effective)),
     ...);
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename... TopicTypes>
void reset_link_topic_subscriptions(TypeList<TopicTypes...>) noexcept
{
    protocol::SubscriptionRequest request{};
    protocol::SubscriptionPolicy effective{};
    (static_cast<void>(update_topic_subscription<System, LinkT, LinkIndex, TopicTypes>(
         TopicTypes::descriptor.id.value, false, protocol::SubscriptionKind::Topic, request,
         effective)),
     ...);
}

template <typename System, typename... LinkTypes, std::size_t... Indices>
void reset_session_link(std::uint16_t link, TypeList<LinkTypes...>,
                        std::index_sequence<Indices...>) noexcept
{
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    using TopicTypes = declarations_of_t<typename System::RemoteTopicCatalog::EntryTypes>;
    ((link == Indices
          ? (reset_link_subscriptions<System, LinkTypes, static_cast<std::uint16_t>(Indices)>(
                 DataTypes{}),
             reset_link_topic_subscriptions<System, LinkTypes, static_cast<std::uint16_t>(Indices)>(
                 TopicTypes{}),
             void())
          : void()),
     ...);
}

enum class InboundSlotState : std::uint8_t
{
    Free,
    Pending,
    Running,
};

enum class InStreamLifecycleOperation : std::uint8_t
{
    None,
    Open,
    Close,
};

template <typename DataT> struct InboundSlot
{
    std::optional<typename DataT::Value> value{};
    protocol::Envelope envelope{};
    std::uint32_t token{};
    std::uint32_t generation{};
    InboundSlotState state{InboundSlotState::Free};
};

template <typename System, typename DataT> struct InStreamState
{
    using Traits = InStreamTraits<DataT>;
    static constexpr std::size_t window = Traits::window;
    static constexpr std::size_t link_count =
        list_size_v<typename System::RemoteArchitecture::Links>;
    static_assert(window <= CONFIG_SOLAR_REMOTE_MAX_INBOUND_WINDOW,
                  "SOLAR_DIAGNOSTIC_REMOTE_INBOUND_WINDOW_CEILING: ReliableWindow exceeds "
                  "CONFIG_SOLAR_REMOTE_MAX_INBOUND_WINDOW");

    kernel::SpinLock lock{};
    std::array<InboundSlot<DataT>, window * link_count> slots{};
    std::array<bool, link_count> active{};
    std::array<std::uint32_t, link_count> token{};
    std::array<std::uint32_t, link_count> generation{};
    std::array<std::uint16_t, link_count> credits{};
    std::array<std::uint32_t, link_count> last_sequence{};
    std::array<kernel::Tick, link_count> last_admitted{};
    std::array<std::uint32_t, link_count> minimum_interval_us{};
    kernel::BinarySemaphore lifecycle_done{};
    InStreamLifecycleOperation lifecycle_operation{InStreamLifecycleOperation::None};
    InStreamOpenContext open_context{};
    InStreamCloseContext close_context{};
    bool lifecycle_result{};
    std::uint32_t admitted{};
    std::uint32_t completed{};
    std::uint32_t rejected{};
    std::uint32_t consumer_failures{};
};

template <typename DataT> struct InStreamStateKey
{};

struct InStreamTokenState
{
    std::atomic<std::uint32_t> next{1};
};

struct InStreamTokenStateKey
{};

template <typename System> [[nodiscard]] std::uint32_t next_in_stream_token() noexcept
{
    auto& state =
        System::template StateSlot<InStreamTokenStateKey, InStreamTokenStateKey,
                                   InStreamTokenState>::value;
    auto token = state.next.fetch_add(1, std::memory_order_relaxed);
    if (token == 0) {
        token = state.next.fetch_add(1, std::memory_order_relaxed);
    }
    return token;
}

template <typename System, typename DataT> [[nodiscard]] auto& in_stream_state() noexcept
{
    using State = InStreamState<System, DataT>;
    return System::template StateSlot<DataT, InStreamStateKey<DataT>, State>::value;
}

template <typename System, typename DataT> void initialize_in_stream_state() noexcept
{
    if constexpr (has_in_stream_v<DataT>) {
        auto& state = in_stream_state<System, DataT>();
        // State slots are zero-initialized before catalog activation. Initialize
        // the kernel object explicitly here as well: native_sim does not
        // reliably run constructors for every weak inline template instance.
        (void)k_sem_init(state.lifecycle_done.native_handle(), 0, 1);
    }
}

template <typename System, typename... DataTypes>
void initialize_in_stream_states(TypeList<DataTypes...>) noexcept
{
    (initialize_in_stream_state<System, DataTypes>(), ...);
}

template <typename System> void initialize_in_stream_runtime() noexcept
{
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    initialize_in_stream_states<System>(DataTypes{});
}

template <typename System, typename DataT, typename LinkT, std::uint16_t LinkIndex>
void send_in_stream_credit(std::uint16_t credits, std::uint32_t correlation = 0) noexcept
{
    if (credits == 0) {
        return;
    }
    constexpr auto window = InStreamTraits<DataT>::window;
    auto& state = in_stream_state<System, DataT>();
    std::uint32_t token{};
    {
        auto guard = state.lock.acquire();
        if (!state.active[LinkIndex]) {
            return;
        }
        token = state.token[LinkIndex];
    }
    const auto payload = protocol::encode(protocol::CreditGrant{
        .token = token,
        .credits = credits,
        .window = static_cast<std::uint16_t>(window),
    });
    (void)System::RemoteService::template transmit<LinkT, LinkIndex>(
        protocol::Kind::Credit, payload, correlation, DataT::descriptor.id.value,
        protocol::Flags::None,
        static_cast<std::uint8_t>(protocol::OperationKind::InStream));
}

template <typename System, typename DataT, typename LinkT, std::uint16_t LinkIndex>
void restore_in_stream_credit(std::uint32_t token, std::uint32_t generation) noexcept
{
    auto& state = in_stream_state<System, DataT>();
    {
        auto guard = state.lock.acquire();
        if (!state.active[LinkIndex] || state.token[LinkIndex] != token ||
            state.generation[LinkIndex] != generation) {
            return;
        }
    }

    // A sender spends credit before transmitting. Rate rejection does not
    // consume the corresponding firmware slot, so re-advertise that one
    // credit without changing the firmware's available-credit count.
    constexpr auto window = InStreamTraits<DataT>::window;
    const auto payload = protocol::encode(protocol::CreditGrant{
        .token = token,
        .credits = 1,
        .window = static_cast<std::uint16_t>(window),
    });
    (void)System::RemoteService::template transmit<LinkT, LinkIndex>(
        protocol::Kind::Credit, payload, 0, DataT::descriptor.id.value, protocol::Flags::None,
        static_cast<std::uint8_t>(protocol::OperationKind::InStream));
}

template <typename System, typename DataT, typename... LinkTypes, std::size_t... Indices>
void send_in_stream_credit_on_link(std::uint16_t link, std::uint16_t credits,
                                   TypeList<LinkTypes...>, std::index_sequence<Indices...>) noexcept
{
    ((link == Indices
          ? (send_in_stream_credit<System, DataT, LinkTypes, static_cast<std::uint16_t>(Indices)>(
                 credits),
             void())
          : void()),
     ...);
}

template <typename Policy>
[[nodiscard]] bool invoke_in_stream_open_policy(const InStreamOpenContext& context) noexcept
{
    if constexpr (!remote::detail::IsOnOpen<Policy>::value) {
        return true;
    } else {
        constexpr auto callback = remote::detail::IsOnOpen<Policy>::callback;
        if constexpr (std::is_invocable_v<decltype(callback), const InStreamOpenContext&>) {
            using Return =
                std::invoke_result_t<decltype(callback), const InStreamOpenContext&>;
            if constexpr (std::same_as<Return, void>) {
                callback(context);
                return true;
            } else if constexpr (ExpectedVoid<Return>) {
                return static_cast<bool>(callback(context));
            }
        } else if constexpr (std::is_invocable_v<decltype(callback)>) {
            using Return = std::invoke_result_t<decltype(callback)>;
            if constexpr (std::same_as<Return, void>) {
                callback();
                return true;
            } else if constexpr (ExpectedVoid<Return>) {
                return static_cast<bool>(callback());
            }
        } else {
            static_assert(solar::detail::dependent_false_v<Policy>,
                          "SOLAR_DIAGNOSTIC_REMOTE_IN_STREAM_ON_OPEN_SIGNATURE: OnOpen callback "
                          "must accept InStreamOpenContext or no arguments");
        }
    }
}

template <typename... Policies>
[[nodiscard]] bool invoke_in_stream_open(TypeList<Policies...>,
                                         const InStreamOpenContext& context) noexcept
{
    return (invoke_in_stream_open_policy<Policies>(context) && ...);
}

template <typename Policy>
void invoke_in_stream_close_policy(const InStreamCloseContext& context) noexcept
{
    if constexpr (remote::detail::IsOnClose<Policy>::value) {
        constexpr auto callback = remote::detail::IsOnClose<Policy>::callback;
        if constexpr (std::is_invocable_v<decltype(callback), const InStreamCloseContext&>) {
            callback(context);
        } else if constexpr (std::is_invocable_v<decltype(callback)>) {
            callback();
        } else {
            static_assert(solar::detail::dependent_false_v<Policy>,
                          "SOLAR_DIAGNOSTIC_REMOTE_IN_STREAM_ON_CLOSE_SIGNATURE: OnClose callback "
                          "must accept InStreamCloseContext or no arguments");
        }
    }
}

template <typename... Policies>
void invoke_in_stream_close(TypeList<Policies...>,
                            const InStreamCloseContext& context) noexcept
{
    (invoke_in_stream_close_policy<Policies>(context), ...);
}

template <typename System, typename DataT>
[[nodiscard]] bool run_in_stream_lifecycle() noexcept
{
    if constexpr (!has_in_stream_v<DataT>) {
        return false;
    } else {
        auto& state = in_stream_state<System, DataT>();
        InStreamLifecycleOperation operation{};
        InStreamOpenContext open_context{};
        InStreamCloseContext close_context{};
        {
            auto guard = state.lock.acquire();
            operation = state.lifecycle_operation;
            if (operation == InStreamLifecycleOperation::None) {
                return false;
            }
            open_context = state.open_context;
            close_context = state.close_context;
            state.lifecycle_operation = InStreamLifecycleOperation::None;
        }
        bool result{true};
        if (operation == InStreamLifecycleOperation::Open) {
            result = invoke_in_stream_open(
                typename InStreamTraits<DataT>::Policies{}, open_context);
        } else {
            invoke_in_stream_close(
                typename InStreamTraits<DataT>::Policies{}, close_context);
        }
        {
            auto guard = state.lock.acquire();
            state.lifecycle_result = result;
        }
        state.lifecycle_done.give();
        return true;
    }
}

template <typename System, typename DataT>
[[nodiscard]] bool execute_in_stream_open_lifecycle(
    const InStreamOpenContext& context) noexcept
{
    using Traits = InStreamTraits<DataT>;
    if constexpr (Traits::Lifecycle::open_count == 0) {
        return true;
    } else if constexpr (std::same_as<typename Traits::Execution, Inline>) {
        return invoke_in_stream_open(typename Traits::Policies{}, context);
    } else {
        auto& state = in_stream_state<System, DataT>();
        state.lifecycle_done.reset();
        {
            auto guard = state.lock.acquire();
            if (state.lifecycle_operation != InStreamLifecycleOperation::None) {
                return false;
            }
            state.open_context = context;
            state.lifecycle_operation = InStreamLifecycleOperation::Open;
        }
        auto submission = execution::detail::submit_registration<
            System,
            typename System::RemoteService::template InStreamRegistration<DataT>>(false);
        if (!submission || !state.lifecycle_done.take()) {
            auto guard = state.lock.acquire();
            state.lifecycle_operation = InStreamLifecycleOperation::None;
            return false;
        }
        auto guard = state.lock.acquire();
        return state.lifecycle_result;
    }
}

template <typename System, typename DataT>
void execute_in_stream_close_lifecycle(const InStreamCloseContext& context) noexcept
{
    using Traits = InStreamTraits<DataT>;
    if constexpr (Traits::Lifecycle::close_count == 0) {
        return;
    } else if constexpr (std::same_as<typename Traits::Execution, Inline>) {
        invoke_in_stream_close(typename Traits::Policies{}, context);
    } else {
        auto& state = in_stream_state<System, DataT>();
        state.lifecycle_done.reset();
        {
            auto guard = state.lock.acquire();
            if (state.lifecycle_operation != InStreamLifecycleOperation::None) {
                return;
            }
            state.close_context = context;
            state.lifecycle_operation = InStreamLifecycleOperation::Close;
        }
        auto submission = execution::detail::submit_registration<
            System,
            typename System::RemoteService::template InStreamRegistration<DataT>>(false);
        if (!submission || !state.lifecycle_done.take()) {
            auto guard = state.lock.acquire();
            state.lifecycle_operation = InStreamLifecycleOperation::None;
        }
    }
}

template <typename System, typename DataT, typename LinkT, std::uint16_t LinkIndex>
void send_in_stream_closed(std::uint32_t token, InStreamCloseReason reason) noexcept
{
    const auto payload = protocol::encode(protocol::InStreamClosed{
        .token = token,
        .reason = reason,
    });
    (void)System::RemoteService::template transmit<LinkT, LinkIndex>(
        protocol::Kind::InStreamClosed, payload, 0, DataT::descriptor.id.value,
        protocol::Flags::None, static_cast<std::uint8_t>(protocol::OperationKind::InStream));
}

template <typename System, typename DataT, typename... LinkTypes, std::size_t... Indices>
void send_in_stream_closed_on_link(std::uint16_t link, std::uint32_t token,
                                   InStreamCloseReason reason, TypeList<LinkTypes...>,
                                   std::index_sequence<Indices...>) noexcept
{
    ((link == Indices
          ? (send_in_stream_closed<System, DataT, LinkTypes,
                                   static_cast<std::uint16_t>(Indices)>(token, reason),
             void())
          : void()),
     ...);
}

template <typename System, typename DataT>
[[nodiscard]] std::uint32_t close_in_stream_link(std::uint16_t link,
                                                 InStreamCloseReason reason,
                                                 bool notify) noexcept
{
    if constexpr (has_in_stream_v<DataT>) {
        auto& state = in_stream_state<System, DataT>();
        std::uint32_t token{};
        {
            auto guard = state.lock.acquire();
            if (link >= state.link_count || !state.active[link]) {
                return 0;
            }
            token = state.token[link];
            state.active[link] = false;
            state.token[link] = 0;
            state.credits[link] = 0;
            state.last_sequence[link] = 0;
            state.last_admitted[link] = 0;
            ++state.generation[link];
            const auto begin = link * state.window;
            for (std::size_t offset{}; offset < state.window; ++offset) {
                auto& slot = state.slots[begin + offset];
                if (slot.state == InboundSlotState::Pending) {
                    slot = {};
                }
            }
        }
        execute_in_stream_close_lifecycle<System, DataT>(
            InStreamCloseContext{
                .endpoint = DataT::descriptor.id,
                .link = link,
                .token = token,
                .reason = reason,
            });
        if (notify) {
            using Links = typename System::RemoteArchitecture::Links;
            send_in_stream_closed_on_link<System, DataT>(
                link, token, reason, Links{},
                std::make_index_sequence<list_size_v<Links>>{});
        }
        return token;
    } else {
        return 0;
    }
}

template <typename System, typename DataT>
void reset_in_stream_link(std::uint16_t link, InStreamCloseReason reason) noexcept
{
    (void)close_in_stream_link<System, DataT>(link, reason, false);
}

template <typename System> void open_session(std::uint16_t) noexcept {}

template <typename System> void cancel_session_requests(std::uint16_t link) noexcept;

template <typename System>
void reset_session(std::uint16_t link, InStreamCloseReason reason) noexcept
{
    cancel_session_requests<System>(link);
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    []<typename... Types>(std::uint16_t session_link, InStreamCloseReason close_reason,
                          TypeList<Types...>) {
        (reset_in_stream_link<System, Types>(session_link, close_reason), ...);
    }(link, reason, DataTypes{});
    using Links = typename System::RemoteArchitecture::Links;
    reset_session_link<System>(link, Links{}, std::make_index_sequence<list_size_v<Links>>{});
}

template <typename ActionT> decltype(auto) call_action(const action_request_t<ActionT>& request)
{
    if constexpr (std::is_same_v<action_request_t<ActionT>, Empty> &&
                  requires { ActionT::execute(); }) {
        return ActionT::execute();
    } else {
        return ActionT::execute(request);
    }
}

template <typename ActionT>
inline constexpr bool asynchronous_action_v = [] {
    using Request = action_request_t<ActionT>;
    if constexpr (std::is_same_v<Request, Empty> && requires(Responder<ActionT> responder) {
                      { ActionT::execute(std::move(responder)) } -> std::same_as<void>;
                  }) {
        return true;
    } else {
        return requires(const Request& request, Responder<ActionT> responder) {
            { ActionT::execute(request, std::move(responder)) } -> std::same_as<void>;
        };
    }
}();

template <typename ActionT>
void call_asynchronous_action(const action_request_t<ActionT>& request,
                              Responder<ActionT> responder) noexcept
{
    if constexpr (std::is_same_v<action_request_t<ActionT>, Empty> &&
                  requires { ActionT::execute(std::move(responder)); }) {
        ActionT::execute(std::move(responder));
    } else {
        ActionT::execute(request, std::move(responder));
    }
}

template <typename ActionT>
[[nodiscard]] Result<action_response_t<ActionT>, action_error_t<ActionT>>
invoke_action_handler(const action_request_t<ActionT>& request)
{
    static_assert(!asynchronous_action_v<ActionT>,
                  "SOLAR_DIAGNOSTIC_REMOTE_ASYNC_ACTION_PATH: asynchronous Actions require a "
                  "Responder and cannot use the synchronous invocation path");
    using Response = action_response_t<ActionT>;
    using DomainError = action_error_t<ActionT>;
    using Return = decltype(call_action<ActionT>(request));
    if constexpr (std::same_as<Return, void>) {
        static_assert(std::is_same_v<Response, Empty>,
                      "SOLAR_DIAGNOSTIC_REMOTE_ACTION_VOID_RESPONSE: void Action handlers require "
                      "the default Empty Response");
        call_action<ActionT>(request);
        return Empty{};
    } else if constexpr (std::same_as<Return, Response>) {
        return call_action<ActionT>(request);
    } else if constexpr (std::same_as<Return, Result<Response, DomainError>>) {
        return call_action<ActionT>(request);
    } else if constexpr (std::same_as<Return, Result<void, DomainError>>) {
        static_assert(std::is_same_v<Response, Empty>,
                      "SOLAR_DIAGNOSTIC_REMOTE_ACTION_VOID_RESULT: Result<void, Error> Action "
                      "handlers require the default Empty Response");
        auto result = call_action<ActionT>(request);
        if (!result) {
            return fail<DomainError>(std::move(result.error()));
        }
        return Empty{};
    } else {
        static_assert(solar::detail::dependent_false_v<Return>,
                      "SOLAR_DIAGNOSTIC_REMOTE_ACTION_RETURN: Action handler must return Response, "
                      "Result<Response, Error>, void, or Result<void, Error> for Empty Response");
    }
}

template <typename System, typename ActionT> struct ActionBuffers
{
    using Request = action_request_t<ActionT>;
    using Response = action_response_t<ActionT>;
    using DomainError = action_error_t<ActionT>;
    static constexpr auto capacity = [] {
        auto size = Schema<Response>::max_encoded_size;
        if (Schema<DomainError>::max_encoded_size > size) {
            size = Schema<DomainError>::max_encoded_size;
        }
        return size;
    }();
    inline static std::array<std::byte, capacity> output{};
};

template <typename System, typename DataT> struct QueryBuffer
{
    inline static std::array<std::byte, Schema<typename DataT::Value>::max_encoded_size> output{};
};

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename DataT>
void send_query_value(const typename DataT::Value& value,
                      const protocol::Envelope& envelope) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    if (State::session.load(std::memory_order_acquire) != SessionState::Active ||
        State::epoch.load(std::memory_order_acquire) != envelope.session_epoch) {
        return;
    }
    auto& output = QueryBuffer<System, DataT>::output;
    auto encoded = cbor::encode(value, output);
    if (!encoded) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            envelope.request_id, envelope.target, protocol::ErrorCode::InternalFailure);
        return;
    }
    (void)ServiceT::template respond<LinkT, LinkIndex>(envelope.request_id, envelope.target,
                                                       std::span{output}.first(*encoded));
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename ErrorT>
void send_query_error(const ErrorT& error, const protocol::Envelope& envelope) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    if (State::session.load(std::memory_order_acquire) != SessionState::Active ||
        State::epoch.load(std::memory_order_acquire) != envelope.session_epoch) {
        return;
    }
    static_assert(SchemaType<ErrorT>,
                  "SOLAR_DIAGNOSTIC_REMOTE_QUERY_ERROR_SCHEMA: Query error requires a Schema");
    std::array<std::byte, Schema<ErrorT>::max_encoded_size> output{};
    auto encoded = cbor::encode(error, output);
    if (!encoded) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            envelope.request_id, envelope.target, protocol::ErrorCode::InternalFailure);
        return;
    }
    (void)ServiceT::template respond<LinkT, LinkIndex>(envelope.request_id, envelope.target,
                                                       std::span{output}.first(*encoded), true);
}

template <typename System, typename LinkT, std::uint16_t LinkIndex>
void send_empty_response(const protocol::Envelope& envelope) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    if (State::session.load(std::memory_order_acquire) != SessionState::Active ||
        State::epoch.load(std::memory_order_acquire) != envelope.session_epoch) {
        return;
    }
    std::array<std::byte, Schema<Empty>::max_encoded_size> output{};
    auto encoded = cbor::encode(Empty{}, output);
    if (!encoded) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            envelope.request_id, envelope.target, protocol::ErrorCode::InternalFailure);
        return;
    }
    (void)ServiceT::template respond<LinkT, LinkIndex>(envelope.request_id, envelope.target,
                                                       std::span{output}.first(*encoded));
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename DataT>
void execute_query(const protocol::Envelope& envelope) noexcept
{
    using State = LinkState<typename System::RemoteService, LinkT, LinkIndex>;
    if (State::session.load(std::memory_order_acquire) != SessionState::Active ||
        State::epoch.load(std::memory_order_acquire) != envelope.session_epoch) {
        return;
    }
    using Value = typename DataT::Value;
    using QueryT = typename QueryCapability<typename DataT::Capabilities>::type;
    constexpr auto reader = IsQuery<QueryT>::reader;
    if constexpr (std::is_invocable_v<decltype(reader)>) {
        using Return = std::invoke_result_t<decltype(reader)>;
        static_assert(!std::is_reference_v<Return> && !std::is_pointer_v<Return>,
                      "SOLAR_DIAGNOSTIC_REMOTE_QUERY_BORROWED_RETURN: Query must return an owned "
                      "Value or expected<Value, Error>");
        if constexpr (std::same_as<Return, Value>) {
            send_query_value<System, LinkT, LinkIndex, DataT>(reader(), envelope);
        } else if constexpr (ExpectedValue<Return, Value>) {
            auto result = reader();
            if (result) {
                send_query_value<System, LinkT, LinkIndex, DataT>(*result, envelope);
            } else {
                send_query_error<System, LinkT, LinkIndex>(result.error(), envelope);
            }
        } else {
            static_assert(solar::detail::dependent_false_v<Return>,
                          "SOLAR_DIAGNOSTIC_REMOTE_QUERY_RETURN: Query reader must return Value "
                          "or expected<Value, Error>");
        }
    } else if constexpr (std::is_invocable_v<decltype(reader), Value&>) {
        using Return = std::invoke_result_t<decltype(reader), Value&>;
        Value value{};
        if constexpr (std::same_as<Return, void>) {
            reader(value);
            send_query_value<System, LinkT, LinkIndex, DataT>(value, envelope);
        } else if constexpr (ExpectedVoid<Return>) {
            auto result = reader(value);
            if (result) {
                send_query_value<System, LinkT, LinkIndex, DataT>(value, envelope);
            } else {
                send_query_error<System, LinkT, LinkIndex>(result.error(), envelope);
            }
        } else {
            static_assert(solar::detail::dependent_false_v<Return>,
                          "SOLAR_DIAGNOSTIC_REMOTE_QUERY_DESTINATION_RETURN: destination Query "
                          "reader must return void or expected<void, Error>");
        }
    } else {
        static_assert(solar::detail::dependent_false_v<DataT>,
                      "SOLAR_DIAGNOSTIC_REMOTE_QUERY_READER: Query reader must accept no "
                      "arguments or one Value destination");
    }
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename DataT>
void execute_update(const typename DataT::Value& value, const protocol::Envelope& envelope) noexcept
{
    using State = LinkState<typename System::RemoteService, LinkT, LinkIndex>;
    if (State::session.load(std::memory_order_acquire) != SessionState::Active ||
        State::epoch.load(std::memory_order_acquire) != envelope.session_epoch) {
        return;
    }
    using Value = typename DataT::Value;
    using UpdateT = typename UpdateCapability<typename DataT::Capabilities>::type;
    constexpr auto writer = IsUpdate<UpdateT>::writer;
    static_assert(std::is_invocable_v<decltype(writer), const Value&>,
                  "SOLAR_DIAGNOSTIC_REMOTE_UPDATE_WRITER: Update writer must accept the Data "
                  "Value by value or const reference");
    using Return = std::invoke_result_t<decltype(writer), const Value&>;
    if constexpr (std::same_as<Return, void>) {
        writer(value);
        send_empty_response<System, LinkT, LinkIndex>(envelope);
    } else if constexpr (ExpectedVoid<Return>) {
        auto result = writer(value);
        if (result) {
            send_empty_response<System, LinkT, LinkIndex>(envelope);
        } else {
            send_query_error<System, LinkT, LinkIndex>(result.error(), envelope);
        }
    } else {
        static_assert(solar::detail::dependent_false_v<Return>,
                      "SOLAR_DIAGNOSTIC_REMOTE_UPDATE_RETURN: Update writer must return void or "
                      "Result<void, ErrorType>");
    }
}

template <typename DataT>
[[nodiscard]] Result<void> invoke_in_stream_consumer(const typename DataT::Value& value) noexcept
{
    using Capability = typename InStreamTraits<DataT>::Capability;
    constexpr auto consumer = IsInStream<Capability>::consumer;
    static_assert(std::is_invocable_v<decltype(consumer), const typename DataT::Value&>,
                  "SOLAR_DIAGNOSTIC_REMOTE_IN_STREAM_CONSUMER: InStream consumer must accept "
                  "the owned Value by value or const reference");
    using Return = std::invoke_result_t<decltype(consumer), const typename DataT::Value&>;
    if constexpr (std::same_as<Return, void>) {
        consumer(value);
        return {};
    } else if constexpr (ExpectedVoid<Return>) {
        auto result = consumer(value);
        return result ? Result<void>{}
                      : Result<void>{
                            fail<solar::Error>({.status = poll_error_status(result.error())})};
    } else {
        static_assert(solar::detail::dependent_false_v<Return>,
                      "SOLAR_DIAGNOSTIC_REMOTE_IN_STREAM_RETURN: InStream consumer must return "
                      "void or Result<void, ErrorType>");
    }
}

template <typename System, typename DataT, typename LinkT, std::uint16_t LinkIndex>
void return_in_stream_credit(std::uint32_t token, std::uint32_t generation) noexcept
{
    using LinkStateT = LinkState<typename System::RemoteService, LinkT, LinkIndex>;
    if (LinkStateT::session.load(std::memory_order_acquire) != SessionState::Active) {
        return;
    }
    auto& state = in_stream_state<System, DataT>();
    bool returned{};
    {
        auto guard = state.lock.acquire();
        if (state.active[LinkIndex] && state.token[LinkIndex] == token &&
            state.generation[LinkIndex] == generation) {
            ++state.credits[LinkIndex];
            returned = true;
        }
    }
    if (returned) {
        send_in_stream_credit<System, DataT, LinkT, LinkIndex>(1);
    }
}

template <typename System, typename DataT, typename... LinkTypes, std::size_t... Indices>
void return_in_stream_credit_on_link(std::uint16_t link, std::uint32_t token,
                                     std::uint32_t generation, TypeList<LinkTypes...>,
                                     std::index_sequence<Indices...>) noexcept
{
    ((link == Indices ? (return_in_stream_credit<System, DataT, LinkTypes,
                                                 static_cast<std::uint16_t>(Indices)>(
                              token, generation),
                         void())
                      : void()),
     ...);
}

template <typename System, typename DataT> bool run_pending_in_stream() noexcept
{
    if constexpr (!has_in_stream_v<DataT>) {
        return false;
    } else {
        auto& state = in_stream_state<System, DataT>();
        bool ran{};
        while (true) {
            std::optional<typename DataT::Value> value;
            std::size_t selected{};
            std::uint16_t link{};
            std::uint32_t token{};
            std::uint32_t generation{};
            {
                auto guard = state.lock.acquire();
                auto found =
                    std::find_if(state.slots.begin(), state.slots.end(), [](const auto& slot) {
                        return slot.state == InboundSlotState::Pending;
                    });
                if (found == state.slots.end()) {
                    break;
                }
                selected = static_cast<std::size_t>(found - state.slots.begin());
                link = static_cast<std::uint16_t>(selected / state.window);
                token = found->token;
                generation = found->generation;
                value = std::move(found->value);
                found->value.reset();
                found->state = InboundSlotState::Running;
            }
            bool current{};
            {
                auto guard = state.lock.acquire();
                current = state.active[link] && state.token[link] == token &&
                          state.generation[link] == generation &&
                          state.slots[selected].state == InboundSlotState::Running;
            }
            const auto consumed =
                current ? invoke_in_stream_consumer<DataT>(*value) : Result<void>{};
            {
                auto guard = state.lock.acquire();
                state.slots[selected] = {};
                if (current) {
                    ++state.completed;
                } else {
                    ++state.rejected;
                }
                if (current && !consumed) {
                    ++state.consumer_failures;
                }
            }
            using Links = typename System::RemoteArchitecture::Links;
            return_in_stream_credit_on_link<System, DataT>(
                link, token, generation, Links{},
                std::make_index_sequence<list_size_v<Links>>{});
            ran = true;
        }
        return ran;
    }
}

template <typename ActionT> struct PendingAction
{
    using Request = action_request_t<ActionT>;
    kernel::SpinLock lock{};
    std::optional<Request> request{};
    protocol::Envelope envelope{};
    std::uint16_t link{};
};

template <typename ActionT> struct AsynchronousActionState
{
    kernel::SpinLock lock{};
    bool active{};
    protocol::Envelope envelope{};
    std::uint16_t link{};
};

template <typename DataT> struct PendingQuery
{
    kernel::SpinLock lock{};
    bool pending{};
    protocol::Envelope envelope{};
    std::uint16_t link{};
};

template <typename DataT> struct PendingUpdate
{
    using Value = typename DataT::Value;
    kernel::SpinLock lock{};
    std::optional<Value> value{};
    protocol::Envelope envelope{};
    std::uint16_t link{};
};

template <typename DataT> struct PendingUpdateKey
{};

template <typename System, typename DataT> [[nodiscard]] auto& pending_update() noexcept
{
    return System::template StateSlot<DataT, PendingUpdateKey<DataT>, PendingUpdate<DataT>>::value;
}

template <typename DataT> struct PendingQueryKey
{};

template <typename System, typename DataT> [[nodiscard]] auto& pending_query() noexcept
{
    return System::template StateSlot<DataT, PendingQueryKey<DataT>, PendingQuery<DataT>>::value;
}

template <typename ActionT> struct PendingActionKey
{};

template <typename ActionT> struct AsynchronousActionKey
{};

template <typename System, typename ActionT> [[nodiscard]] auto& pending_action() noexcept
{
    return System::template StateSlot<ActionT, PendingActionKey<ActionT>,
                                      PendingAction<ActionT>>::value;
}

template <typename System, typename ActionT> [[nodiscard]] auto& asynchronous_action() noexcept
{
    return System::template StateSlot<ActionT, AsynchronousActionKey<ActionT>,
                                      AsynchronousActionState<ActionT>>::value;
}

template <typename System, typename ActionT>
[[nodiscard]] bool claim_asynchronous_action(ResponderToken token) noexcept
{
    auto& state = asynchronous_action<System, ActionT>();
    auto guard = state.lock.acquire();
    if (!state.active || state.link != token.link || state.envelope.request_id != token.request ||
        state.envelope.session_epoch != token.epoch) {
        return false;
    }
    state.active = false;
    return true;
}

template <typename System, typename ActionT>
[[nodiscard]] bool asynchronous_action_cancelled(ResponderToken token) noexcept
{
    auto& state = asynchronous_action<System, ActionT>();
    auto guard = state.lock.acquire();
    return !state.active || state.link != token.link ||
           state.envelope.request_id != token.request ||
           state.envelope.session_epoch != token.epoch;
}

template <typename System, typename ActionT>
bool cancel_pending_action(std::uint16_t link, std::uint32_t request) noexcept
{
    auto& pending = pending_action<System, ActionT>();
    {
        auto guard = pending.lock.acquire();
        if (pending.request && pending.link == link && pending.envelope.request_id == request) {
            pending.request.reset();
            System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
            return true;
        }
    }
    if constexpr (asynchronous_action_v<ActionT>) {
        auto& state = asynchronous_action<System, ActionT>();
        auto guard = state.lock.acquire();
        if (state.active && state.link == link && state.envelope.request_id == request) {
            state.active = false;
            System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
            return true;
        }
    }
    return false;
}

template <typename System, typename DataT>
bool cancel_pending_query(std::uint16_t link, std::uint32_t request) noexcept
{
    if constexpr (!has_query_v<DataT>) {
        return false;
    } else {
        auto& pending = pending_query<System, DataT>();
        auto guard = pending.lock.acquire();
        if (!pending.pending || pending.link != link || pending.envelope.request_id != request) {
            return false;
        }
        pending.pending = false;
        System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }
}

template <typename System, typename DataT>
bool cancel_pending_update(std::uint16_t link, std::uint32_t request) noexcept
{
    if constexpr (!has_update_v<DataT>) {
        return false;
    } else {
        auto& pending = pending_update<System, DataT>();
        auto guard = pending.lock.acquire();
        if (!pending.value || pending.link != link || pending.envelope.request_id != request) {
            return false;
        }
        pending.value.reset();
        System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }
}

template <typename System, typename... ActionTypes, typename... DataTypes>
bool cancel_pending_request(std::uint16_t link, std::uint32_t request, TypeList<ActionTypes...>,
                            TypeList<DataTypes...>) noexcept
{
    const bool action = (cancel_pending_action<System, ActionTypes>(link, request) || ...);
    if (action) {
        return true;
    }
    const bool query = (cancel_pending_query<System, DataTypes>(link, request) || ...);
    if (query) {
        return true;
    }
    return (cancel_pending_update<System, DataTypes>(link, request) || ...);
}

template <typename System, typename ActionT> void cancel_link_action(std::uint16_t link) noexcept
{
    auto& pending = pending_action<System, ActionT>();
    {
        auto guard = pending.lock.acquire();
        if (pending.request && pending.link == link) {
            pending.request.reset();
            System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
    if constexpr (asynchronous_action_v<ActionT>) {
        auto& state = asynchronous_action<System, ActionT>();
        auto guard = state.lock.acquire();
        if (state.active && state.link == link) {
            state.active = false;
            System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
}

template <typename System, typename DataT> void cancel_link_data(std::uint16_t link) noexcept
{
    if constexpr (has_query_v<DataT>) {
        auto& query = pending_query<System, DataT>();
        auto guard = query.lock.acquire();
        if (query.pending && query.link == link) {
            query.pending = false;
            System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
    if constexpr (has_update_v<DataT>) {
        auto& update = pending_update<System, DataT>();
        auto guard = update.lock.acquire();
        if (update.value && update.link == link) {
            update.value.reset();
            System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
}

template <typename System, typename... ActionTypes, typename... DataTypes>
void cancel_link_requests(std::uint16_t link, TypeList<ActionTypes...>,
                          TypeList<DataTypes...>) noexcept
{
    (cancel_link_action<System, ActionTypes>(link), ...);
    (cancel_link_data<System, DataTypes>(link), ...);
}

template <typename System> void cancel_session_requests(std::uint16_t link) noexcept
{
    using Actions = declarations_of_t<typename System::RemoteActionCatalog::EntryTypes>;
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    cancel_link_requests<System>(link, Actions{}, DataTypes{});
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename ActionT,
          typename Value>
[[nodiscard]] Result<void> send_asynchronous_action_result(ResponderToken token, Value&& value,
                                                           bool domain_error) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    if (State::session.load(std::memory_order_acquire) != SessionState::Active ||
        State::epoch.load(std::memory_order_acquire) != token.epoch) {
        return fail<solar::Error>({.status = solar::Status::NotReady});
    }
    auto& output = ActionBuffers<System, ActionT>::output;
    auto encoded = cbor::encode(value, output);
    if (!encoded) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            token.request, ActionT::descriptor.id.value, protocol::ErrorCode::InternalFailure);
        return fail<solar::Error>({.status = encoded.error().status});
    }
    auto sent = ServiceT::template respond<LinkT, LinkIndex>(
        token.request, ActionT::descriptor.id.value, std::span{output}.first(*encoded),
        domain_error);
    if (!sent) {
        return fail<solar::Error>({.status = sent.error().status});
    }
    State::completed_requests.fetch_add(1, std::memory_order_relaxed);
    return {};
}

template <typename System, typename ActionT, typename Value, typename... LinkTypes,
          std::size_t... Indices>
[[nodiscard]] Result<void>
send_asynchronous_action_result_on_link(ResponderToken token, Value&& value, bool domain_error,
                                        TypeList<LinkTypes...>,
                                        std::index_sequence<Indices...>) noexcept
{
    Result<void> result = fail<solar::Error>({.status = solar::Status::NotFound});
    ((token.link == Indices
          ? (result = send_asynchronous_action_result<System, LinkTypes,
                                                      static_cast<std::uint16_t>(Indices), ActionT>(
                 token, std::forward<Value>(value), domain_error),
             true)
          : false) ||
     ...);
    return result;
}

template <typename System, typename ActionT, typename Value>
[[nodiscard]] Result<void> complete_asynchronous_action(ResponderToken token, Value&& value,
                                                        bool domain_error) noexcept
{
    if (!claim_asynchronous_action<System, ActionT>(token)) {
        return fail<solar::Error>({.status = solar::Status::NotReady});
    }
    using Links = typename System::RemoteArchitecture::Links;
    auto result = send_asynchronous_action_result_on_link<System, ActionT>(
        token, std::forward<Value>(value), domain_error, Links{},
        std::make_index_sequence<list_size_v<Links>>{});
    System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

template <typename System, typename ActionT>
[[nodiscard]] Result<void>
complete_asynchronous_action_success(ResponderToken token,
                                     action_response_t<ActionT>&& response) noexcept
{
    return complete_asynchronous_action<System, ActionT>(token, std::move(response), false);
}

template <typename System, typename ActionT>
[[nodiscard]] Result<void>
complete_asynchronous_action_failure(ResponderToken token, action_error_t<ActionT>&& error) noexcept
{
    return complete_asynchronous_action<System, ActionT>(token, std::move(error), true);
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename ActionT>
void abandon_asynchronous_action_on_link(ResponderToken token) noexcept
{
    using ServiceT = typename System::RemoteService;
    (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
        token.request, ActionT::descriptor.id.value, protocol::ErrorCode::InternalFailure);
}

template <typename System, typename ActionT, typename... LinkTypes, std::size_t... Indices>
void abandon_asynchronous_action_on_link(ResponderToken token, TypeList<LinkTypes...>,
                                         std::index_sequence<Indices...>) noexcept
{
    ((token.link == Indices
          ? (abandon_asynchronous_action_on_link<
                 System, LinkTypes, static_cast<std::uint16_t>(Indices), ActionT>(token),
             void())
          : void()),
     ...);
}

template <typename System, typename ActionT>
void abandon_asynchronous_action(ResponderToken token) noexcept
{
    if (!claim_asynchronous_action<System, ActionT>(token)) {
        return;
    }
    using Links = typename System::RemoteArchitecture::Links;
    abandon_asynchronous_action_on_link<System, ActionT>(
        token, Links{}, std::make_index_sequence<list_size_v<Links>>{});
    System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename ActionT>
[[nodiscard]] bool execute_owned_action(const action_request_t<ActionT>& request,
                                        const protocol::Envelope& envelope) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    if (State::session.load(std::memory_order_acquire) != SessionState::Active ||
        State::epoch.load(std::memory_order_acquire) != envelope.session_epoch) {
        return false;
    }
    if constexpr (asynchronous_action_v<ActionT>) {
        auto& state = asynchronous_action<System, ActionT>();
        {
            auto guard = state.lock.acquire();
            if (state.active) {
                (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                    envelope.request_id, envelope.target, protocol::ErrorCode::Busy);
                return false;
            }
            state.active = true;
            state.envelope = envelope;
            state.link = LinkIndex;
        }
        const ResponderToken token{
            .link = LinkIndex,
            .request = envelope.request_id,
            .epoch = envelope.session_epoch,
        };
        auto responder = ResponderFactory<ActionT>::make(
            token, &complete_asynchronous_action_success<System, ActionT>,
            &complete_asynchronous_action_failure<System, ActionT>,
            &abandon_asynchronous_action<System, ActionT>,
            &asynchronous_action_cancelled<System, ActionT>);
        call_asynchronous_action<ActionT>(request, std::move(responder));
        return true;
    } else {
        auto result = invoke_action_handler<ActionT>(request);
        if (State::session.load(std::memory_order_acquire) != SessionState::Active ||
            State::epoch.load(std::memory_order_acquire) != envelope.session_epoch) {
            return false;
        }
        auto& output = ActionBuffers<System, ActionT>::output;
        if (result) {
            auto encoded = cbor::encode(*result, output);
            if (!encoded) {
                (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                    envelope.request_id, envelope.target, protocol::ErrorCode::InternalFailure);
                return false;
            }
            (void)ServiceT::template respond<LinkT, LinkIndex>(envelope.request_id, envelope.target,
                                                               std::span{output}.first(*encoded));
        } else {
            auto encoded = cbor::encode(result.error(), output);
            if (!encoded) {
                (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                    envelope.request_id, envelope.target, protocol::ErrorCode::InternalFailure);
                return false;
            }
            (void)ServiceT::template respond<LinkT, LinkIndex>(
                envelope.request_id, envelope.target, std::span{output}.first(*encoded), true);
        }
        State::completed_requests.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

template <typename System, typename ActionT, typename... LinkTypes, std::size_t... Indices>
[[nodiscard]] bool
execute_action_on_link(std::uint16_t link, const action_request_t<ActionT>& request,
                       const protocol::Envelope& envelope, TypeList<LinkTypes...>,
                       std::index_sequence<Indices...>) noexcept
{
    bool retained{};
    ((link == Indices
          ? (retained = execute_owned_action<System, LinkTypes, static_cast<std::uint16_t>(Indices),
                                             ActionT>(request, envelope),
             true)
          : false) ||
     ...);
    return retained;
}

template <typename System, typename ActionT> bool run_pending_action() noexcept
{
    auto& pending = pending_action<System, ActionT>();
    std::optional<action_request_t<ActionT>> request;
    protocol::Envelope envelope;
    std::uint16_t link{};
    {
        auto guard = pending.lock.acquire();
        if (!pending.request) {
            return false;
        }
        request = std::move(pending.request);
        pending.request.reset();
        envelope = pending.envelope;
        link = pending.link;
    }
    using Links = typename System::RemoteArchitecture::Links;
    const bool retained = execute_action_on_link<System, ActionT>(
        link, *request, envelope, Links{}, std::make_index_sequence<list_size_v<Links>>{});
    if (!retained) {
        System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
    }
    return true;
}

template <typename System, typename DataT, typename... LinkTypes, std::size_t... Indices>
void execute_query_on_link(std::uint16_t link, const protocol::Envelope& envelope,
                           TypeList<LinkTypes...>, std::index_sequence<Indices...>) noexcept
{
    ((link == Indices
          ? (execute_query<System, LinkTypes, static_cast<std::uint16_t>(Indices), DataT>(envelope),
             void())
          : void()),
     ...);
}

template <typename System, typename DataT> bool run_pending_query() noexcept
{
    if constexpr (!has_query_v<DataT>) {
        return false;
    } else {
        auto& pending = pending_query<System, DataT>();
        protocol::Envelope envelope;
        std::uint16_t link{};
        {
            auto guard = pending.lock.acquire();
            if (!pending.pending) {
                return false;
            }
            pending.pending = false;
            envelope = pending.envelope;
            link = pending.link;
        }
        using Links = typename System::RemoteArchitecture::Links;
        execute_query_on_link<System, DataT>(link, envelope, Links{},
                                             std::make_index_sequence<list_size_v<Links>>{});
        System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }
}

template <typename System, typename DataT, typename... LinkTypes, std::size_t... Indices>
void execute_update_on_link(std::uint16_t link, const typename DataT::Value& value,
                            const protocol::Envelope& envelope, TypeList<LinkTypes...>,
                            std::index_sequence<Indices...>) noexcept
{
    ((link == Indices
          ? (execute_update<System, LinkTypes, static_cast<std::uint16_t>(Indices), DataT>(
                 value, envelope),
             void())
          : void()),
     ...);
}

template <typename System, typename DataT> bool run_pending_update() noexcept
{
    if constexpr (!has_update_v<DataT>) {
        return false;
    } else {
        auto& pending = pending_update<System, DataT>();
        std::optional<typename DataT::Value> value;
        protocol::Envelope envelope;
        std::uint16_t link{};
        {
            auto guard = pending.lock.acquire();
            if (!pending.value) {
                return false;
            }
            value = std::move(pending.value);
            pending.value.reset();
            envelope = pending.envelope;
            link = pending.link;
        }
        using Links = typename System::RemoteArchitecture::Links;
        execute_update_on_link<System, DataT>(link, *value, envelope, Links{},
                                              std::make_index_sequence<list_size_v<Links>>{});
        System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }
}

template <typename System, typename... ActionTypes>
[[nodiscard]] Result<void> process_action_work_for(TypeList<ActionTypes...>) noexcept
{
    (static_cast<void>(run_pending_action<System, ActionTypes>()), ...);
    return {};
}

template <typename System>
[[nodiscard]] Result<void> process_action_work(std::uint32_t target, bool action) noexcept
{
    if (action) {
        using Actions = declarations_of_t<typename System::RemoteActionCatalog::EntryTypes>;
        bool found{};
        []<typename... ActionTypes>(std::uint32_t selected, bool& matched,
                                    TypeList<ActionTypes...>) {
            ((selected == ActionTypes::descriptor.id.value
                  ? (static_cast<void>(run_pending_action<System, ActionTypes>()), matched = true)
                  : false),
             ...);
        }(target, found, Actions{});
        return found ? Result<void>{}
                     : Result<void>{fail<solar::Error>({.status = solar::Status::NotFound})};
    }
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    bool found{};
    []<typename... Types>(std::uint32_t selected, bool& matched, TypeList<Types...>) {
        ((selected == Types::descriptor.id.value
              ? (static_cast<void>(run_pending_query<System, Types>()),
                 static_cast<void>(run_pending_update<System, Types>()), matched = true)
              : false),
         ...);
    }(target, found, DataTypes{});
    return found ? Result<void>{}
                 : Result<void>{fail<solar::Error>({.status = solar::Status::NotFound})};
}

template <typename System, typename... DataTypes>
[[nodiscard]] Result<void> process_in_stream_work_for(std::uint32_t target,
                                                      TypeList<DataTypes...>) noexcept
{
    bool found{};
    ((target == DataTypes::descriptor.id.value
          ? (static_cast<void>(run_in_stream_lifecycle<System, DataTypes>()),
             static_cast<void>(run_pending_in_stream<System, DataTypes>()),
             static_cast<void>(run_in_stream_lifecycle<System, DataTypes>()), found = true)
          : false),
     ...);
    return found ? Result<void>{}
                 : Result<void>{fail<solar::Error>({.status = solar::Status::NotFound})};
}

template <typename System>
[[nodiscard]] Result<void> process_in_stream_work(std::uint32_t target) noexcept
{
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    return process_in_stream_work_for<System>(target, DataTypes{});
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename ActionT>
void execute_action_request(const frame::Decoded& decoded) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    using Request = action_request_t<ActionT>;
    using Access = action_access_t<ActionT>;
    constexpr auto required = PermissionMask<Access>::value;
    if ((State::grants.load(std::memory_order_acquire) & required) != required) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::Unauthorized);
        return;
    }
    auto request = cbor::decode<Request>(decoded.payload);
    if (!request) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::DecodeFailure);
        return;
    }
    if (!ServiceT::template reserve_response<LinkT, LinkIndex>(decoded.envelope.request_id,
                                                               decoded.envelope.target)) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target, protocol::ErrorCode::NoCapacity);
        return;
    }
    using Execution = action_execution_t<ActionT>;
    if constexpr (std::is_same_v<Execution, Inline>) {
        if constexpr (asynchronous_action_v<ActionT>) {
            auto active = System::RemoteFacility::active_requests.load(std::memory_order_acquire);
            while (true) {
                if (active >= CONFIG_SOLAR_REMOTE_MAX_REQUESTS) {
                    ServiceT::template release_response<LinkT, LinkIndex>(
                        decoded.envelope.request_id);
                    (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                        decoded.envelope.request_id, decoded.envelope.target,
                        protocol::ErrorCode::NoCapacity);
                    return;
                }
                if (System::RemoteFacility::active_requests.compare_exchange_weak(
                        active, active + 1, std::memory_order_acq_rel)) {
                    break;
                }
            }
            const bool retained =
                execute_owned_action<System, LinkT, LinkIndex, ActionT>(*request, decoded.envelope);
            if (!retained) {
                System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
            }
        } else {
            (void)execute_owned_action<System, LinkT, LinkIndex, ActionT>(*request,
                                                                          decoded.envelope);
        }
    } else {
        auto active = System::RemoteFacility::active_requests.load(std::memory_order_acquire);
        while (true) {
            if (active >= CONFIG_SOLAR_REMOTE_MAX_REQUESTS) {
                ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
                (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                    decoded.envelope.request_id, decoded.envelope.target,
                    protocol::ErrorCode::NoCapacity);
                return;
            }
            if (System::RemoteFacility::active_requests.compare_exchange_weak(
                    active, active + 1, std::memory_order_acq_rel)) {
                break;
            }
        }
        auto& pending = pending_action<System, ActionT>();
        {
            auto guard = pending.lock.acquire();
            if (pending.request) {
                System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
                ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
                (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                    decoded.envelope.request_id, decoded.envelope.target,
                    protocol::ErrorCode::Busy);
                return;
            }
            pending.request = std::move(*request);
            pending.envelope = decoded.envelope;
            pending.link = LinkIndex;
        }
        auto submission = execution::detail::submit_registration<
            System, typename ServiceT::template ActionRegistration<ActionT>>(false);
        if (!submission) {
            auto guard = pending.lock.acquire();
            pending.request.reset();
            System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
            ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target, protocol::ErrorCode::Busy);
        }
    }
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename DataT>
void execute_query_request(const frame::Decoded& decoded) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    constexpr auto observe = PermissionMask<Requires<permission::Observe>>::value;
    if ((State::grants.load(std::memory_order_acquire) & observe) != observe) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::Unauthorized);
        return;
    }
    if constexpr (!has_query_v<DataT>) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::UnsupportedOperation);
    } else {
        if (!ServiceT::template reserve_response<LinkT, LinkIndex>(decoded.envelope.request_id,
                                                                   decoded.envelope.target)) {
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::NoCapacity);
            return;
        }
        using Execution = action_execution_t<DataT>;
        if constexpr (std::is_same_v<Execution, Inline>) {
            execute_query<System, LinkT, LinkIndex, DataT>(decoded.envelope);
        } else {
            auto active = System::RemoteFacility::active_requests.load(std::memory_order_acquire);
            while (true) {
                if (active >= CONFIG_SOLAR_REMOTE_MAX_REQUESTS) {
                    ServiceT::template release_response<LinkT, LinkIndex>(
                        decoded.envelope.request_id);
                    (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                        decoded.envelope.request_id, decoded.envelope.target,
                        protocol::ErrorCode::NoCapacity);
                    return;
                }
                if (System::RemoteFacility::active_requests.compare_exchange_weak(
                        active, active + 1, std::memory_order_acq_rel)) {
                    break;
                }
            }
            auto& pending = pending_query<System, DataT>();
            {
                auto guard = pending.lock.acquire();
                if (pending.pending) {
                    System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
                    ServiceT::template release_response<LinkT, LinkIndex>(
                        decoded.envelope.request_id);
                    (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                        decoded.envelope.request_id, decoded.envelope.target,
                        protocol::ErrorCode::Busy);
                    return;
                }
                pending.pending = true;
                pending.envelope = decoded.envelope;
                pending.link = LinkIndex;
            }
            auto submission = execution::detail::submit_registration<
                System, typename ServiceT::template DataRegistration<DataT>>(false);
            if (!submission) {
                auto guard = pending.lock.acquire();
                pending.pending = false;
                System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
                ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
                (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                    decoded.envelope.request_id, decoded.envelope.target,
                    protocol::ErrorCode::Busy);
            }
        }
    }
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename DataT>
void execute_update_request(const frame::Decoded& decoded) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    constexpr auto configure = PermissionMask<Requires<permission::Configure>>::value;
    if ((State::grants.load(std::memory_order_acquire) & configure) != configure) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::Unauthorized);
        return;
    }
    if constexpr (!has_update_v<DataT>) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::UnsupportedOperation);
    } else {
        auto value = cbor::decode<typename DataT::Value>(decoded.payload);
        if (!value) {
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::DecodeFailure);
            return;
        }
        if (!ServiceT::template reserve_response<LinkT, LinkIndex>(decoded.envelope.request_id,
                                                                   decoded.envelope.target)) {
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::NoCapacity);
            return;
        }
        using Execution = action_execution_t<DataT>;
        if constexpr (std::is_same_v<Execution, Inline>) {
            execute_update<System, LinkT, LinkIndex, DataT>(*value, decoded.envelope);
        } else {
            auto active = System::RemoteFacility::active_requests.load(std::memory_order_acquire);
            while (true) {
                if (active >= CONFIG_SOLAR_REMOTE_MAX_REQUESTS) {
                    ServiceT::template release_response<LinkT, LinkIndex>(
                        decoded.envelope.request_id);
                    (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                        decoded.envelope.request_id, decoded.envelope.target,
                        protocol::ErrorCode::NoCapacity);
                    return;
                }
                if (System::RemoteFacility::active_requests.compare_exchange_weak(
                        active, active + 1, std::memory_order_acq_rel)) {
                    break;
                }
            }
            auto& pending = pending_update<System, DataT>();
            {
                auto guard = pending.lock.acquire();
                if (pending.value) {
                    System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
                    ServiceT::template release_response<LinkT, LinkIndex>(
                        decoded.envelope.request_id);
                    (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                        decoded.envelope.request_id, decoded.envelope.target,
                        protocol::ErrorCode::Busy);
                    return;
                }
                pending.value = std::move(*value);
                pending.envelope = decoded.envelope;
                pending.link = LinkIndex;
            }
            auto submission = execution::detail::submit_registration<
                System, typename ServiceT::template DataRegistration<DataT>>(false);
            if (!submission) {
                auto guard = pending.lock.acquire();
                pending.value.reset();
                System::RemoteFacility::active_requests.fetch_sub(1, std::memory_order_acq_rel);
                ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
                (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                    decoded.envelope.request_id, decoded.envelope.target,
                    protocol::ErrorCode::Busy);
            }
        }
    }
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename DataT>
void execute_in_stream_frame(const frame::Decoded& decoded) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    constexpr auto control = PermissionMask<Requires<permission::Control>>::value;
    if ((State::grants.load(std::memory_order_acquire) & control) != control) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            0, decoded.envelope.target, protocol::ErrorCode::Unauthorized);
        return;
    }
    if constexpr (!has_in_stream_v<DataT>) {
        ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            0, decoded.envelope.target, protocol::ErrorCode::UnsupportedOperation);
    } else {
        using Value = typename DataT::Value;
        Result<Value, Error> decoded_value = [&]() -> Result<Value, Error> {
            if constexpr (Schema<Value>::codec == Codec::Cbor) {
                return cbor::decode<Value>(decoded.payload);
            } else {
                return packed::decode<Value>(decoded.payload);
            }
        }();
        if (!decoded_value) {
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                0, decoded.envelope.target, protocol::ErrorCode::DecodeFailure);
            return;
        }

        auto& state = in_stream_state<System, DataT>();
        bool credit_violation{};
        bool sequence_violation{};
        bool token_violation{};
        bool rate_violation{};
        bool admitted{};
        std::uint32_t admitted_generation{};
        std::uint32_t rejected_generation{};
        {
            auto guard = state.lock.acquire();
            const auto token = decoded.envelope.request_id;
            const auto now = kernel::now_ticks();
            const auto interval = kernel::to_ticks_ceil(
                std::chrono::microseconds{state.minimum_interval_us[LinkIndex]});
            if (!state.active[LinkIndex] || token == 0 ||
                state.token[LinkIndex] != token) {
                token_violation = true;
                ++state.rejected;
            } else if (state.credits[LinkIndex] == 0) {
                credit_violation = true;
                ++state.rejected;
            } else if (decoded.envelope.frame_sequence <= state.last_sequence[LinkIndex]) {
                sequence_violation = true;
                ++state.rejected;
            } else if (state.last_admitted[LinkIndex] != 0 && interval != 0 &&
                       now - state.last_admitted[LinkIndex] < interval) {
                rate_violation = true;
                rejected_generation = state.generation[LinkIndex];
                ++state.rejected;
            } else {
                const auto begin = LinkIndex * state.window;
                auto slot = std::find_if(state.slots.begin() + begin,
                                         state.slots.begin() + begin + state.window,
                                         [](const auto& candidate) {
                                             return candidate.state == InboundSlotState::Free;
                                         });
                if (slot != state.slots.begin() + begin + state.window) {
                    --state.credits[LinkIndex];
                    state.last_sequence[LinkIndex] = decoded.envelope.frame_sequence;
                    state.last_admitted[LinkIndex] = now;
                    slot->value = std::move(*decoded_value);
                    slot->envelope = decoded.envelope;
                    slot->token = token;
                    slot->generation = state.generation[LinkIndex];
                    slot->state = InboundSlotState::Pending;
                    admitted_generation = state.generation[LinkIndex];
                    ++state.admitted;
                    admitted = true;
                } else {
                    credit_violation = true;
                    ++state.rejected;
                }
            }
        }
        if (!admitted) {
            if (rate_violation) {
                restore_in_stream_credit<System, DataT, LinkT, LinkIndex>(
                    decoded.envelope.request_id, rejected_generation);
            }
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                0, decoded.envelope.target,
                rate_violation
                    ? protocol::ErrorCode::RateRejected
                    : ((sequence_violation || token_violation)
                           ? protocol::ErrorCode::RequestExpired
                           : protocol::ErrorCode::CreditViolation));
            return;
        }

        using Execution = typename InStreamTraits<DataT>::Execution;
        if constexpr (std::same_as<Execution, Inline>) {
            (void)run_pending_in_stream<System, DataT>();
        } else {
            auto submission = execution::detail::submit_registration<
                System, typename ServiceT::template InStreamRegistration<DataT>>(false);
            if (!submission) {
                auto& ingress = in_stream_state<System, DataT>();
                {
                    auto guard = ingress.lock.acquire();
                    const auto begin = LinkIndex * ingress.window;
                    auto slot = std::find_if(
                        ingress.slots.begin() + begin,
                        ingress.slots.begin() + begin + ingress.window, [&](const auto& candidate) {
                            return candidate.state == InboundSlotState::Pending &&
                                   candidate.envelope.frame_sequence ==
                                       decoded.envelope.frame_sequence;
                        });
                    if (slot != ingress.slots.begin() + begin + ingress.window) {
                        *slot = {};
                    }
                }
                return_in_stream_credit<System, DataT, LinkT, LinkIndex>(
                    decoded.envelope.request_id, admitted_generation);
                (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                    0, decoded.envelope.target, protocol::ErrorCode::Busy);
            }
        }
    }
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename... ActionTypes>
bool dispatch_action(const frame::Decoded& decoded, TypeList<ActionTypes...>) noexcept
{
    return ((decoded.envelope.target == ActionTypes::descriptor.id.value
                 ? (execute_action_request<System, LinkT, LinkIndex, ActionTypes>(decoded), true)
                 : false) ||
            ...);
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename... DataTypes>
bool dispatch_query(const frame::Decoded& decoded, TypeList<DataTypes...>) noexcept
{
    return ((decoded.envelope.target == DataTypes::descriptor.id.value
                 ? (execute_query_request<System, LinkT, LinkIndex, DataTypes>(decoded), true)
                 : false) ||
            ...);
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename... DataTypes>
bool dispatch_update(const frame::Decoded& decoded, TypeList<DataTypes...>) noexcept
{
    return ((decoded.envelope.target == DataTypes::descriptor.id.value
                 ? (execute_update_request<System, LinkT, LinkIndex, DataTypes>(decoded), true)
                 : false) ||
            ...);
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename... DataTypes>
bool dispatch_in_stream(const frame::Decoded& decoded, TypeList<DataTypes...>) noexcept
{
    return ((decoded.envelope.target == DataTypes::descriptor.id.value
                 ? (execute_in_stream_frame<System, LinkT, LinkIndex, DataTypes>(decoded), true)
                 : false) ||
            ...);
}

template <typename System, typename DataT> [[nodiscard]] bool in_stream_is_active() noexcept
{
    if constexpr (!has_in_stream_v<DataT>) {
        return false;
    } else {
        auto& state = in_stream_state<System, DataT>();
        auto guard = state.lock.acquire();
        return std::any_of(state.active.begin(), state.active.end(),
                           [](bool active) { return active; });
    }
}

template <typename System, typename Group, typename DataT>
[[nodiscard]] bool in_stream_group_is_active_for() noexcept
{
    if constexpr (!has_in_stream_v<DataT>) {
        return false;
    } else if constexpr (InStreamTraits<DataT>::exclusive &&
                         std::same_as<typename InStreamTraits<DataT>::ExclusiveGroup, Group>) {
        return in_stream_is_active<System, DataT>();
    } else {
        return false;
    }
}

template <typename System, typename Group, typename... DataTypes>
[[nodiscard]] bool in_stream_group_is_active(TypeList<DataTypes...>) noexcept
{
    return (in_stream_group_is_active_for<System, Group, DataTypes>() || ...);
}

template <typename System, typename Group, typename DataT>
void close_in_stream_group_for() noexcept
{
    if constexpr (has_in_stream_v<DataT>) {
        if constexpr (InStreamTraits<DataT>::exclusive &&
                      std::same_as<typename InStreamTraits<DataT>::ExclusiveGroup, Group>) {
            auto& state = in_stream_state<System, DataT>();
            for (std::uint16_t link{}; link < state.link_count; ++link) {
                (void)close_in_stream_link<System, DataT>(
                    link, InStreamCloseReason::Replaced, true);
            }
        }
    }
}

template <typename System, typename Group, typename... DataTypes>
void close_in_stream_group(TypeList<DataTypes...>) noexcept
{
    (close_in_stream_group_for<System, Group, DataTypes>(), ...);
}

template <typename System, typename DataT>
[[nodiscard]] Result<protocol::InStreamOpenResponse, protocol::ErrorCode>
open_in_stream(std::uint16_t link, const protocol::SubscriptionRequest& request) noexcept
{
    constexpr auto codec = Schema<typename DataT::Value>::codec;
    if (request.flags != 0 || request.batch_size > 1 ||
        (request.codec != 0 && request.codec != static_cast<std::uint8_t>(codec))) {
        return fail<protocol::ErrorCode>(protocol::ErrorCode::UnsupportedCapability);
    }

    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    if constexpr (InStreamTraits<DataT>::exclusive) {
        using Group = typename InStreamTraits<DataT>::ExclusiveGroup;
        if constexpr (std::same_as<typename InStreamTraits<DataT>::ExclusiveBehavior,
                                   RejectExisting>) {
            if (in_stream_group_is_active<System, Group>(DataTypes{})) {
                return fail<protocol::ErrorCode>(protocol::ErrorCode::Busy);
            }
        } else {
            close_in_stream_group<System, Group>(DataTypes{});
        }
    } else {
        (void)close_in_stream_link<System, DataT>(
            link, InStreamCloseReason::Replaced, true);
    }

    auto& state = in_stream_state<System, DataT>();
    const auto token = next_in_stream_token<System>();
    const auto minimum_interval =
        (std::max)(request.minimum_interval_us, InStreamTraits<DataT>::minimum_interval_us);
    std::uint16_t credits{};
    std::uint32_t generation{};
    {
        auto guard = state.lock.acquire();
        if (link >= state.link_count) {
            return fail<protocol::ErrorCode>(protocol::ErrorCode::InternalFailure);
        }
        generation = ++state.generation[link];
        state.active[link] = true;
        state.token[link] = token;
        state.last_sequence[link] = 0;
        state.last_admitted[link] = 0;
        state.minimum_interval_us[link] = minimum_interval;
        const auto begin = link * state.window;
        for (std::size_t offset{}; offset < state.window; ++offset) {
            auto& slot = state.slots[begin + offset];
            if (slot.state == InboundSlotState::Free) {
                ++credits;
            }
        }
        state.credits[link] = credits;
    }

    if (!execute_in_stream_open_lifecycle<System, DataT>(
            InStreamOpenContext{
                .endpoint = DataT::descriptor.id,
                .link = link,
                .token = token,
                .minimum_interval_us = minimum_interval,
                .window = static_cast<std::uint16_t>(state.window),
            })) {
        (void)generation;
        (void)close_in_stream_link<System, DataT>(
            link, InStreamCloseReason::ConfigurationFailed, true);
        return fail<protocol::ErrorCode>(protocol::ErrorCode::InternalFailure);
    }

    return protocol::InStreamOpenResponse{
        .policy =
            {
                .minimum_interval_us = minimum_interval,
                .batch_size = 1,
                .codec = codec,
                .flags = 0,
            },
        .token = token,
    };
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename DataT>
bool process_in_stream_subscription_for(const frame::Decoded& decoded, bool enable) noexcept
{
    if (decoded.envelope.target != DataT::descriptor.id.value) {
        return false;
    }
    using ServiceT = typename System::RemoteService;
    if constexpr (!has_in_stream_v<DataT>) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::UnsupportedOperation);
        return true;
    } else if (enable) {
        protocol::SubscriptionRequest request{};
        if (!decoded.payload.empty()) {
            auto decoded_request = protocol::decode_subscription_request(decoded.payload);
            if (!decoded_request) {
                ServiceT::template release_response<LinkT, LinkIndex>(
                    decoded.envelope.request_id);
                (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                    decoded.envelope.request_id, decoded.envelope.target,
                    protocol::ErrorCode::DecodeFailure);
                return true;
            }
            request = *decoded_request;
        }
        auto opened = open_in_stream<System, DataT>(LinkIndex, request);
        if (!opened) {
            ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target, opened.error());
            return true;
        }
        const auto payload = protocol::encode(*opened);
        auto response = ServiceT::template respond<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target, payload);
        if (!response) {
            (void)close_in_stream_link<System, DataT>(
                LinkIndex, InStreamCloseReason::Fault, true);
            ServiceT::template release_response<LinkT, LinkIndex>(
                decoded.envelope.request_id);
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::InternalFailure);
            return true;
        }
        ServiceT::template stage_response<LinkT, LinkIndex>();
        auto& state = in_stream_state<System, DataT>();
        std::uint16_t credits{};
        {
            auto guard = state.lock.acquire();
            credits = state.credits[LinkIndex];
        }
        send_in_stream_credit<System, DataT, LinkT, LinkIndex>(
            credits, decoded.envelope.request_id);
        return true;
    } else {
        auto request = protocol::decode_in_stream_close_request(decoded.payload);
        if (!request) {
            ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::DecodeFailure);
            return true;
        }
        auto& state = in_stream_state<System, DataT>();
        bool token_matches{};
        {
            auto guard = state.lock.acquire();
            token_matches =
                state.active[LinkIndex] && state.token[LinkIndex] == request->token;
        }
        if (!token_matches) {
            ServiceT::template release_response<LinkT, LinkIndex>(
                decoded.envelope.request_id);
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::RequestExpired);
            return true;
        }
        (void)close_in_stream_link<System, DataT>(
            LinkIndex, InStreamCloseReason::Closed, false);
        (void)ServiceT::template respond<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            std::span<const std::byte>{});
        return true;
    }
}

template <typename System, typename LinkT, std::uint16_t LinkIndex, typename... DataTypes>
bool process_in_stream_subscription_target(const frame::Decoded& decoded, bool enable,
                                           TypeList<DataTypes...>) noexcept
{
    return (process_in_stream_subscription_for<System, LinkT, LinkIndex, DataTypes>(
                decoded, enable) ||
            ...);
}

template <typename System, typename LinkT, std::uint16_t LinkIndex>
bool admit_request_id(const frame::Decoded& decoded) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    if (decoded.envelope.request_id == 0) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            0, decoded.envelope.target, protocol::ErrorCode::DecodeFailure);
        return false;
    }
    const auto previous = State::last_request_id.load(std::memory_order_acquire);
    if (decoded.envelope.request_id == previous) {
        State::duplicate_requests.fetch_add(1, std::memory_order_relaxed);
        auto replay = ServiceT::template replay_response<LinkT, LinkIndex>(previous);
        if (!replay) {
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                previous, decoded.envelope.target,
                replay.error().status == Status::Busy
                    ? protocol::ErrorCode::Busy
                    : protocol::ErrorCode::DuplicateResponseExpired);
        }
        return false;
    }
    if (decoded.envelope.request_id < previous) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::RequestExpired);
        return false;
    }
    State::last_request_id.store(decoded.envelope.request_id, std::memory_order_release);
    return true;
}

template <typename System, typename LinkT, std::uint16_t LinkIndex>
void process_subscription(const frame::Decoded& decoded, bool enable) noexcept
{
    using ServiceT = typename System::RemoteService;
    using State = LinkState<ServiceT, LinkT, LinkIndex>;
    if (decoded.envelope.subscription() == protocol::SubscriptionKind::DataInStream) {
        constexpr auto control = PermissionMask<Requires<permission::Control>>::value;
        if ((State::grants.load(std::memory_order_acquire) & control) != control) {
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::Unauthorized);
            return;
        }
        if (!admit_request_id<System, LinkT, LinkIndex>(decoded)) {
            return;
        }
        if (!ServiceT::template reserve_response<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target)) {
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::NoCapacity);
            return;
        }
        using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
        if (!process_in_stream_subscription_target<System, LinkT, LinkIndex>(
                decoded, enable, DataTypes{})) {
            ServiceT::template release_response<LinkT, LinkIndex>(
                decoded.envelope.request_id);
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::UnknownTarget);
        }
        return;
    }
    constexpr auto observe = PermissionMask<Requires<permission::Observe>>::value;
    if ((State::grants.load(std::memory_order_acquire) & observe) != observe) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::Unauthorized);
        return;
    }
    if (!admit_request_id<System, LinkT, LinkIndex>(decoded)) {
        return;
    }
    if (!ServiceT::template reserve_response<LinkT, LinkIndex>(decoded.envelope.request_id,
                                                               decoded.envelope.target)) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target, protocol::ErrorCode::NoCapacity);
        return;
    }
    protocol::SubscriptionRequest request{};
    if (enable && !decoded.payload.empty()) {
        auto decoded_request = protocol::decode_subscription_request(decoded.payload);
        if (!decoded_request) {
            ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                decoded.envelope.request_id, decoded.envelope.target,
                protocol::ErrorCode::DecodeFailure);
            return;
        }
        request = *decoded_request;
    } else if (!enable && !decoded.payload.empty()) {
        ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::DecodeFailure);
        return;
    }
    protocol::SubscriptionPolicy effective{};
    using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
    using TopicTypes = declarations_of_t<typename System::RemoteTopicCatalog::EntryTypes>;
    auto updated = update_subscription<System, LinkT, LinkIndex>(
        decoded.envelope.target, enable, decoded.envelope.subscription(), request, effective,
        DataTypes{}, TopicTypes{});
    if (!updated || !*updated) {
        ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            updated ? protocol::ErrorCode::UnknownTarget : updated.error());
        return;
    }
    const auto payload = protocol::encode(effective);
    (void)ServiceT::template respond<LinkT, LinkIndex>(decoded.envelope.request_id,
                                                       decoded.envelope.target, payload);
}

template <typename System, typename LinkT, std::uint16_t LinkIndex>
void process_link_application_frame(const frame::Decoded& decoded) noexcept
{
    using ServiceT = typename System::RemoteService;
    if (decoded.envelope.kind == protocol::Kind::ResponseAck) {
        ServiceT::template acknowledge_response<LinkT, LinkIndex>(decoded.envelope.request_id);
        return;
    }
    if (decoded.envelope.kind == protocol::Kind::Cancel) {
        using Actions = declarations_of_t<typename System::RemoteActionCatalog::EntryTypes>;
        using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
        const bool cancelled = cancel_pending_request<System>(
            LinkIndex, decoded.envelope.request_id, Actions{}, DataTypes{});
        if (cancelled) {
            ServiceT::template release_response<LinkT, LinkIndex>(decoded.envelope.request_id);
        }
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            cancelled ? protocol::ErrorCode::Cancelled : protocol::ErrorCode::RequestExpired);
        return;
    }
    if (decoded.envelope.kind == protocol::Kind::Subscribe ||
        decoded.envelope.kind == protocol::Kind::Unsubscribe) {
        process_subscription<System, LinkT, LinkIndex>(decoded, decoded.envelope.kind ==
                                                                    protocol::Kind::Subscribe);
        return;
    }
    if (decoded.envelope.kind == protocol::Kind::Data) {
        if (decoded.envelope.operation() != protocol::OperationKind::InStream) {
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                0, decoded.envelope.target, protocol::ErrorCode::UnsupportedOperation);
            return;
        }
        using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
        if (!dispatch_in_stream<System, LinkT, LinkIndex>(decoded, DataTypes{})) {
            (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
                0, decoded.envelope.target, protocol::ErrorCode::UnknownTarget);
        }
        return;
    }
    if (decoded.envelope.kind != protocol::Kind::Request || decoded.envelope.request_id == 0) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::UnsupportedOperation);
        return;
    }
    if (!admit_request_id<System, LinkT, LinkIndex>(decoded)) {
        return;
    }
    bool dispatched{};
    if (decoded.envelope.operation() == protocol::OperationKind::Action) {
        using Actions = declarations_of_t<typename System::RemoteActionCatalog::EntryTypes>;
        dispatched = dispatch_action<System, LinkT, LinkIndex>(decoded, Actions{});
    } else if (decoded.envelope.operation() == protocol::OperationKind::Query) {
        using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
        dispatched = dispatch_query<System, LinkT, LinkIndex>(decoded, DataTypes{});
    } else if (decoded.envelope.operation() == protocol::OperationKind::Update) {
        using DataTypes = declarations_of_t<typename System::RemoteDataCatalog::EntryTypes>;
        dispatched = dispatch_update<System, LinkT, LinkIndex>(decoded, DataTypes{});
    } else {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::UnsupportedOperation);
        return;
    }
    if (!dispatched) {
        (void)ServiceT::template protocol_error<LinkT, LinkIndex>(
            decoded.envelope.request_id, decoded.envelope.target,
            protocol::ErrorCode::UnknownTarget);
    }
}

template <typename System, typename... LinkTypes, std::size_t... Indices>
void dispatch_application_link(std::uint16_t link, const frame::Decoded& decoded,
                               TypeList<LinkTypes...>, std::index_sequence<Indices...>) noexcept
{
    ((link == Indices
          ? (process_link_application_frame<System, LinkTypes, static_cast<std::uint16_t>(Indices)>(
                 decoded),
             void())
          : void()),
     ...);
}

template <typename System>
void process_application_frame(std::uint16_t link, const frame::Decoded& decoded) noexcept
{
    using Links = typename System::RemoteArchitecture::Links;
    dispatch_application_link<System>(link, decoded, Links{},
                                      std::make_index_sequence<list_size_v<Links>>{});
}

template <typename System>
[[nodiscard]] protocol::IntrospectionSummary introspection_summary() noexcept
{
    return {
        .schemas = static_cast<std::uint16_t>(manifest::Image<System>::schema_count),
        .data = static_cast<std::uint16_t>(System::RemoteDataCatalog::size),
        .actions = static_cast<std::uint16_t>(System::RemoteActionCatalog::size),
        .topics = static_cast<std::uint16_t>(System::RemoteTopicCatalog::size),
        .streams = static_cast<std::uint16_t>(System::RemoteStreamCatalog::size),
        .links = static_cast<std::uint16_t>(System::RemoteLinkCatalog::size),
        .maximum_frame_bytes = CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES,
        .maximum_message_bytes = CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES,
    };
}

template <typename System> [[nodiscard]] protocol::ServerInformation server_information() noexcept
{
    return {
        .maximum_frame_bytes = CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES,
        .maximum_message_bytes = CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES,
#if defined(CONFIG_SOLAR_REMOTE_BUILD_ID)
        .build_id = CONFIG_SOLAR_REMOTE_BUILD_ID,
#else
        .build_id = 0,
#endif
        .manifest_digest = manifest::Image<System>::digest,
        .manifest_size = static_cast<std::uint32_t>(manifest::Image<System>::bytes.size()),
        .feature_flags = 0U
#if defined(CONFIG_SOLAR_REMOTE_MANIFEST_RETRIEVAL)
                         | 0x01U
#endif
#if defined(CONFIG_SOLAR_REMOTE_RUNTIME_INTROSPECTION)
                         | 0x02U
#endif
#if defined(CONFIG_SOLAR_INSPECTION_REMOTE)
                         | 0x04U
#endif
        ,
    };
}

template <typename System>
[[nodiscard]] Result<std::size_t, Error> manifest_chunk(std::span<const std::byte> request_bytes,
                                                        std::span<std::byte> output) noexcept
{
    auto request = protocol::decode_manifest_request(request_bytes);
    constexpr auto& image = manifest::Image<System>::bytes;
    if (!request || request->limit == 0 || request->offset > image.size()) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    const auto count =
        (std::min)({static_cast<std::size_t>(request->limit), image.size() - request->offset,
                    output.size() >= protocol::manifest_chunk_header_size
                        ? output.size() - protocol::manifest_chunk_header_size
                        : std::size_t{}});
    if (output.size() < protocol::manifest_chunk_header_size || count == 0) {
        return fail<Error>({Status::NoSpace, Reason::NoSpace, Operation::Encode});
    }
    protocol::detail::put_u32(output, 0, request->offset);
    protocol::detail::put_u32(output, 4, static_cast<std::uint32_t>(image.size()));
    std::copy_n(image.begin() + request->offset, count,
                output.begin() + protocol::manifest_chunk_header_size);
    return protocol::manifest_chunk_header_size + count;
}

#if defined(CONFIG_SOLAR_INSPECTION_REMOTE)
template <typename System>
[[nodiscard]] Result<std::size_t, Error>
inspection_collections(std::span<const std::byte> request_bytes,
                       std::span<std::byte> output) noexcept
{
    auto request = protocol::decode_collection_request(request_bytes);
    if (!request || request->limit == 0) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    constexpr auto descriptors = System::InspectionCatalog::descriptors();
    if (request->offset > descriptors.size()) {
        return fail<Error>({Status::Invalid, Reason::InvalidValue, Operation::Decode});
    }
    const auto limit =
        (std::min)(static_cast<std::size_t>(request->limit),
                   static_cast<std::size_t>(CONFIG_SOLAR_INSPECTION_REMOTE_MAX_PAGE_RECORDS));
    const auto count = (std::min)(limit, descriptors.size() - request->offset);
    if (output.size() < protocol::collection_page_header_size) {
        return fail<Error>({Status::NoSpace, Reason::NoSpace, Operation::Encode});
    }
    output[0] = std::byte{1};
    output[1] = static_cast<std::byte>(count);
    protocol::detail::put_u16(output, 2, static_cast<std::uint16_t>(descriptors.size()));
    protocol::detail::put_u16(output, 4, static_cast<std::uint16_t>(request->offset + count));
    output[6] = static_cast<std::byte>(request->offset + count < descriptors.size());
    output[7] = std::byte{};
    std::size_t written = protocol::collection_page_header_size;
    for (std::size_t index{}; index < count; ++index) {
        const auto& view = descriptors[request->offset + index];
        const auto& descriptor = view.descriptor;
        const auto name_size = (std::min)(descriptor.name.size(), std::size_t{UINT8_MAX});
        const auto required = protocol::collection_descriptor_header_size + name_size;
        if (written + required > output.size()) {
            return fail<Error>({Status::NoSpace, Reason::NoSpace, Operation::Encode});
        }
        protocol::detail::put_u16(output, written, view.local_id.value);
        protocol::detail::put_u32(output, written + 2, descriptor.stable_id.value);
        protocol::detail::put_u16(output, written + 6, descriptor.version);
        output[written + 8] = static_cast<std::byte>(descriptor.subsystem);
        output[written + 9] = static_cast<std::byte>(descriptor.capabilities);
        output[written + 10] = static_cast<std::byte>(descriptor.consistency_modes);
        output[written + 11] = static_cast<std::byte>(descriptor.synchronization);
        output[written + 12] = static_cast<std::byte>(descriptor.context);
        output[written + 13] = static_cast<std::byte>(descriptor.cost);
        protocol::detail::put_u16(output, written + 14, descriptor.maximum_page);
        protocol::detail::put_u16(output, written + 16, descriptor.record_size);
        protocol::detail::put_u16(output, written + 18, descriptor.query_size);
        output[written + 20] = static_cast<std::byte>((descriptor.may_block ? 1U : 0U) |
                                                      (descriptor.expensive ? 2U : 0U) |
                                                      (descriptor.values_may_be_stale ? 4U : 0U));
        output[written + 21] = static_cast<std::byte>(name_size);
        std::copy_n(reinterpret_cast<const std::byte*>(descriptor.name.data()), name_size,
                    output.begin() + written + protocol::collection_descriptor_header_size);
        written += required;
    }
    return written;
}

template <typename System, inspection::CollectionType Collection>
inline static std::array<typename Collection::Record,
                         CONFIG_SOLAR_INSPECTION_REMOTE_MAX_PAGE_RECORDS>
    inspection_remote_records{};

template <typename System, inspection::CollectionType Collection>
[[nodiscard]] Result<std::size_t, Error>
encode_inspection_query(const protocol::CollectionQueryRequest& request,
                        std::span<std::byte> output) noexcept
{
    if constexpr (!inspection::CborEncodable<typename Collection::Record> ||
                  !std::is_same_v<typename Collection::Query, inspection::BasicQuery>) {
        return fail<Error>({Status::NotSupported, Reason::UnsupportedOperation, Operation::Encode});
    } else {
        if (request.limit > Collection::descriptor.maximum_page) {
            return fail<Error>({Status::Invalid, Reason::InvalidValue, Operation::Query});
        }
        constexpr auto collection = System::InspectionCatalog::template Entry<Collection>::local_id;
        auto& records = inspection_remote_records<System, Collection>;
        const auto limit =
            (std::min)(static_cast<std::size_t>(request.limit),
                       static_cast<std::size_t>(CONFIG_SOLAR_INSPECTION_REMOTE_MAX_PAGE_RECORDS));
        typename Collection::Query query{.page = {.cursor = {.collection = collection,
                                                             .offset = request.offset,
                                                             .revision = request.revision},
                                                  .limit = limit}};
        auto page = inspection::detail::query_provider<System, Collection>(
            query, std::span{records}.first(limit), collection);
        if (!page) {
            return fail<Error>({page.error().status, Reason::InternalInvariant, Operation::Query});
        }
        inspection::CborWriter writer{output};
        bool encoded = writer.map(10) && writer.unsigned_integer(0) &&
                       writer.unsigned_integer(Collection::descriptor.stable_id.value) &&
                       writer.unsigned_integer(1) && writer.unsigned_integer(page->written) &&
                       writer.unsigned_integer(2) && writer.unsigned_integer(page->next.offset) &&
                       writer.unsigned_integer(3) && writer.boolean(page->has_more) &&
                       writer.unsigned_integer(4) && writer.unsigned_integer(page->revision) &&
                       writer.unsigned_integer(5) &&
                       writer.unsigned_integer(static_cast<std::uint8_t>(page->consistency)) &&
                       writer.unsigned_integer(6) &&
                       writer.unsigned_integer(static_cast<std::uint8_t>(page->freshness)) &&
                       writer.unsigned_integer(7) && writer.unsigned_integer(page->loss.count) &&
                       writer.unsigned_integer(8) && writer.array(page->written) &&
                       writer.unsigned_integer(9) && writer.boolean(page->loss.known);
        for (std::size_t index{}; encoded && index < page->written; ++index) {
            encoded = inspection::CborEncoder<typename Collection::Record>::encode(records[index],
                                                                                   writer);
        }
        if (!encoded || !writer.good()) {
            return fail<Error>({Status::NoSpace, Reason::NoSpace, Operation::Encode});
        }
        return writer.size();
    }
}

template <typename System>
[[nodiscard]] Result<std::size_t, Error> inspection_query(std::span<const std::byte> request_bytes,
                                                          std::span<std::byte> output) noexcept
{
    auto request = protocol::decode_collection_query_request(request_bytes);
    if (!request || request->limit == 0 ||
        request->limit > CONFIG_SOLAR_INSPECTION_REMOTE_MAX_PAGE_RECORDS) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    const auto descriptors = System::InspectionCatalog::descriptors();
    const auto found = std::find_if(descriptors.begin(), descriptors.end(), [&](const auto& value) {
        return value.descriptor.stable_id.value == request->stable_id &&
               (value.descriptor.capabilities &
                inspection::capability(inspection::OperationCapability::Remote)) != 0;
    });
    if (found == descriptors.end()) {
        return fail<Error>({Status::NotFound, Reason::NotRegistered, Operation::Query});
    }
    Result<std::size_t, Error> result =
        fail<Error>({Status::NotFound, Reason::NotRegistered, Operation::Query});
    auto visited = inspection::detail::visit_entry<System>(
        found->local_id,
        [&](auto identity) {
            using Collection = typename decltype(identity)::type;
            result = encode_inspection_query<System, Collection>(*request, output);
        },
        typename System::InspectionCatalog::EntryTypes{});
    if (!visited) {
        return fail<Error>({Status::NotFound, Reason::NotRegistered, Operation::Query});
    }
    return result;
}
#endif

#endif

} // namespace solar::remote::detail
