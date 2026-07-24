#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "solar/remote/facility.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
#include "solar/execution/contribution.hpp"
#include "solar/execution/policy.hpp"
#include "solar/execution/registration.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/message_queue.hpp"
#include "solar/kernel/stop.hpp"
#include "solar/remote/frame.hpp"
#endif

namespace solar::remote
{

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)

namespace detail
{

struct ServiceEvent
{
    enum class Kind : std::uint8_t
    {
        Link,
        Publication,
        Output,
        Stop,
    } kind{Kind::Link};
    std::uint16_t subject{};
    LinkEvent event{};
};

static_assert(std::is_trivially_copyable_v<ServiceEvent>);

struct OutboundSlot
{
    protocol::Kind kind{protocol::Kind::Keepalive};
    protocol::Flags flags{protocol::Flags::None};
    std::uint32_t request_id{};
    std::uint32_t target{};
    std::uint32_t reserved{};
    std::uint16_t size{};
    std::uint16_t fragment_id{};
    std::uint8_t fragment_index{};
    std::uint8_t fragment_count{1};
    std::array<std::byte, CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES> payload{};
};

struct OutboundMessage
{
    bool active{};
    OutputLane lane{OutputLane::Bulk};
    protocol::Kind kind{protocol::Kind::Data};
    protocol::Flags flags{protocol::Flags::None};
    std::uint32_t request_id{};
    std::uint32_t target{};
    std::uint32_t reserved{};
    std::uint32_t size{};
    std::uint32_t offset{};
    std::uint16_t fragment_id{};
    std::uint8_t fragment_index{};
    std::uint8_t fragment_count{};
    std::array<std::byte, CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES> payload{};
};

struct OutboundLane
{
    std::array<OutboundSlot, CONFIG_SOLAR_REMOTE_OUTPUT_LANE_DEPTH> slots{};
    std::uint16_t head{};
    std::uint16_t size{};
    LaneRecord record{};
};

struct ResponseSlot
{
    bool reserved{};
    bool valid{};
    bool staged{};
    bool error{};
    std::uint16_t size{};
    std::uint32_t request{};
    std::uint32_t target{};
    std::array<std::byte, CONFIG_SOLAR_REMOTE_RESPONSE_CACHE_BYTES> payload{};
};

struct SubscriptionSlot
{
    bool active{};
    std::uint32_t minimum_interval_us{};
    kernel::Tick next_delivery{};
    std::uint32_t delivered{};
    std::uint32_t skipped{};
    std::uint32_t dropped{};
};

struct ReassemblySlot
{
    bool active{};
    protocol::Envelope envelope{};
    std::uint8_t next_fragment{};
    std::uint32_t size{};
    kernel::Tick deadline{};
    std::array<std::byte, CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES> payload{};
};

[[nodiscard]] constexpr OutputLane lane_for(protocol::Kind kind, std::uint32_t request_id) noexcept
{
    switch (kind) {
    case protocol::Kind::ClientHello:
    case protocol::Kind::ServerHello:
    case protocol::Kind::Keepalive:
    case protocol::Kind::SessionReset:
    case protocol::Kind::Credit:
        return OutputLane::Control;
    case protocol::Kind::Response:
        return OutputLane::Response;
    case protocol::Kind::Error:
        return request_id == 0 ? OutputLane::Control : OutputLane::Response;
    case protocol::Kind::Data:
        return OutputLane::Telemetry;
    default:
        return OutputLane::Important;
    }
}

template <typename T> struct IsDeferredDataCapability : std::false_type
{};

template <auto Reader> struct IsDeferredDataCapability<Query<Reader>> : std::true_type
{};

template <auto Writer> struct IsDeferredDataCapability<Update<Writer>> : std::true_type
{};

template <typename CapabilitiesT> struct HasDeferredDataCapability;

template <typename... CapabilityTypes>
struct HasDeferredDataCapability<Capabilities<CapabilityTypes...>>
    : std::bool_constant<(IsDeferredDataCapability<CapabilityTypes>::value || ...)>
{};

template <typename T, typename = void> struct ExplicitInline : std::false_type
{};

template <typename T>
struct ExplicitInline<T, std::void_t<typename T::Execution>>
    : std::is_same<typename T::Execution, Inline>
{};

template <typename ListT> struct NeedsRequestDispatcher;

template <typename... ActionTypes>
struct NeedsRequestDispatcher<TypeList<ActionTypes...>>
    : std::bool_constant<(!ExplicitInline<ActionTypes>::value || ...)>
{};

template <typename ListT> struct DataNeedsRequestDispatcher;

template <typename... DataTypes>
struct DataNeedsRequestDispatcher<TypeList<DataTypes...>>
    : std::bool_constant<((HasDeferredDataCapability<typename DataTypes::Capabilities>::value &&
                           !ExplicitInline<DataTypes>::value) ||
                          ...)>
{};

template <typename ServiceT, typename LinkT, std::uint16_t Index> struct LinkState
{
    static void notify(void*, LinkEvent event) noexcept
    {
        ServiceT::post(
            ServiceEvent{.kind = ServiceEvent::Kind::Link, .subject = Index, .event = event});
    }

    inline static frame::StreamDecoder<CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES + 32,
                                       CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES>
        decoder{};
    inline static std::atomic_bool connected{};
    inline static std::atomic_uint32_t accepted_frames{};
    inline static std::atomic_uint32_t received_bytes{};
    inline static std::atomic_uint32_t rejected_frames{};
    inline static std::atomic_uint32_t transmitted_frames{};
    inline static std::atomic_uint32_t transmitted_bytes{};
    inline static std::atomic_uint32_t protocol_errors{};
    inline static std::atomic_uint32_t connections{};
    inline static std::atomic_uint32_t epoch{};
    inline static std::atomic_uint32_t tx_sequence{};
    inline static std::atomic<SessionState> session{SessionState::Disconnected};
    inline static std::atomic_bool tx_in_flight{};
    inline static std::atomic_uint16_t tx_generation{};
    inline static std::atomic_uint8_t grants{};
    inline static std::atomic_uint32_t last_request_id{};
    inline static std::atomic_uint32_t duplicate_requests{};
    inline static std::atomic_uint32_t completed_requests{};
    inline static kernel::SpinLock response_lock{};
    inline static std::array<ResponseSlot, CONFIG_SOLAR_REMOTE_MAX_REQUESTS> responses{};
    inline static kernel::SpinLock output_lock{};
    inline static std::array<OutboundLane, CONFIG_SOLAR_REMOTE_OUTPUT_LANES> lanes{};
    inline static std::array<OutboundMessage, CONFIG_SOLAR_REMOTE_OUTBOUND_MESSAGE_SLOTS>
        outbound_messages{};
    inline static std::uint16_t next_fragment_id{};
    inline static std::optional<OutputLane> transmitting_lane{};
    inline static LeaseHandle transmitting_handle{};
    inline static std::array<SubscriptionSlot, CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS> subscriptions{};
    inline static std::atomic_uint32_t subscription_count{};
    inline static std::array<ReassemblySlot, CONFIG_SOLAR_REMOTE_REASSEMBLY_SLOTS> reassembly{};
    inline static std::array<std::byte, CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES> tx_scratch{};
    inline static std::array<std::byte,
                             frame::max_encoded_size(CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES)>
        tx_encoded{};
};

template <typename T> struct IsPollOutStream : std::false_type
{};

template <typename Acquisition, typename... Policies>
struct IsPollOutStream<OutStream<Acquisition, Policies...>>
    : std::bool_constant<remote::detail::PollAcquisition<Acquisition>::valid>
{
    using AcquisitionType = Acquisition;
};

template <typename CapabilitiesT> struct PollCapability;

template <> struct PollCapability<Capabilities<>>
{
    static constexpr bool present = false;
    using type = void;
};

template <typename Head, typename... Tail> struct PollCapability<Capabilities<Head, Tail...>>
{
  private:
    using Remaining = PollCapability<Capabilities<Tail...>>;

  public:
    static constexpr bool present = IsPollOutStream<Head>::value || Remaining::present;
    static_assert(!(IsPollOutStream<Head>::value && Remaining::present),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_POLL: Data declares more than one Poll "
                  "OutStream");
    using type = std::conditional_t<IsPollOutStream<Head>::value, Head, typename Remaining::type>;
};

template <typename DataT>
inline constexpr bool has_poll_v = PollCapability<typename DataT::Capabilities>::present;

template <typename DataT> consteval auto poll_registration_name()
{
    FixedString result{"solar.remote.poll.00000000"};
    constexpr char digits[] = "0123456789abcdef";
    auto value = DataT::descriptor.id.value;
    for (std::size_t offset{}; offset < 8; ++offset) {
        result.value[25 - offset] = digits[value & 0x0FU];
        value >>= 4U;
    }
    return result;
}

template <typename ActionT> consteval auto action_registration_name()
{
    FixedString result{"solar.remote.action.00000000"};
    constexpr char digits[] = "0123456789abcdef";
    auto value = ActionT::descriptor.id.value;
    for (std::size_t offset{}; offset < 8; ++offset) {
        result.value[result.size() - 1U - offset] = digits[value & 0x0FU];
        value >>= 4U;
    }
    return result;
}

template <typename DataT> consteval auto data_registration_name()
{
    FixedString result{"solar.remote.data.00000000"};
    constexpr char digits[] = "0123456789abcdef";
    auto value = DataT::descriptor.id.value;
    for (std::size_t offset{}; offset < 8; ++offset) {
        result.value[result.size() - 1U - offset] = digits[value & 0x0FU];
        value >>= 4U;
    }
    return result;
}

template <typename T> struct ServiceIsInStream : std::false_type
{
    using PolicyTypes = TypeList<>;
};

template <auto Consumer, typename... Policies>
struct ServiceIsInStream<InStream<Consumer, Policies...>> : std::true_type
{
    using PolicyTypes = TypeList<Policies...>;
};

template <typename CapabilitiesT> struct ServiceInStreamCapability;

template <> struct ServiceInStreamCapability<Capabilities<>>
{
    static constexpr bool present = false;
    using type = void;
};

template <typename Head, typename... Tail>
struct ServiceInStreamCapability<Capabilities<Head, Tail...>>
{
  private:
    using Remaining = ServiceInStreamCapability<Capabilities<Tail...>>;

  public:
    static constexpr bool present = ServiceIsInStream<Head>::value || Remaining::present;
    using type = std::conditional_t<ServiceIsInStream<Head>::value, Head, typename Remaining::type>;
};

template <typename PolicyTypes> struct ServiceInStreamExecution;

template <> struct ServiceInStreamExecution<TypeList<>>
{
    using type = void;
};

template <typename Head, typename... Tail> struct ServiceInStreamExecution<TypeList<Head, Tail...>>
{
  private:
    using Remaining = typename ServiceInStreamExecution<TypeList<Tail...>>::type;

  public:
    using type =
        std::conditional_t<std::same_as<Head, Inline> || IsOn<Head>::value, Head, Remaining>;
};

template <typename DataT> struct ServiceInStreamTraits
{
    using Capability = typename ServiceInStreamCapability<typename DataT::Capabilities>::type;
    static constexpr bool present =
        ServiceInStreamCapability<typename DataT::Capabilities>::present;
    using Policies =
        std::conditional_t<present, typename ServiceIsInStream<Capability>::PolicyTypes,
                           TypeList<>>;
    using Authored = typename ServiceInStreamExecution<Policies>::type;
    using DataExecution = action_execution_t<DataT>;
    using Execution = std::conditional_t<!std::is_void_v<Authored>, Authored, DataExecution>;
    static constexpr bool is_inline = std::same_as<Execution, Inline>;
};

template <typename DataT> consteval auto in_stream_registration_name()
{
    FixedString result{"solar.remote.in.00000000"};
    constexpr char digits[] = "0123456789abcdef";
    auto value = DataT::descriptor.id.value;
    for (std::size_t offset{}; offset < 8; ++offset) {
        result.value[result.size() - 1U - offset] = digits[value & 0x0FU];
        value >>= 4U;
    }
    return result;
}

} // namespace detail

template <typename ArchitectureT> struct Service
{
    static_assert(CONFIG_SOLAR_REMOTE_OUTPUT_LANES == 5,
                  "SOLAR_DIAGNOSTIC_REMOTE_OUTPUT_LANES: the initial scheduler requires five "
                  "output lanes");
    static constexpr std::size_t max_frame_payload =
        CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES - protocol::envelope_size - protocol::crc_size;
    static_assert(max_frame_payload > 0);
    using Architecture = ArchitectureT;
    using FacilityType = Facility<Architecture>;
    using Links = typename Architecture::Links;
    using Dependencies = solar::Dependencies<FacilityType>;
    using Execution =
        execution::Service<execution::StackSize<CONFIG_SOLAR_REMOTE_SERVICE_STACK_SIZE>,
                           execution::Priority<CONFIG_SOLAR_REMOTE_SERVICE_PRIORITY>>;

    template <typename DataT> struct DataWork
    {
        [[nodiscard]] static Result<void> execute() noexcept
        {
            return FacilityType::process_action_work != nullptr
                       ? FacilityType::process_action_work(DataT::descriptor.id.value, false)
                       : Result<void>{fail<solar::Error>({.status = solar::Status::NotReady})};
        }
    };

    template <typename ActionT> struct ActionWork
    {
        [[nodiscard]] static Result<void> execute() noexcept
        {
            return FacilityType::process_action_work != nullptr
                       ? FacilityType::process_action_work(ActionT::descriptor.id.value, true)
                       : Result<void>{fail<solar::Error>({.status = solar::Status::NotReady})};
        }
    };

    template <typename ActionT> struct ActionTarget
    {
      private:
        using Authored = detail::action_execution_t<ActionT>;
        using Selected = typename detail::IsOn<Authored>::type;

      public:
        static_assert(std::is_void_v<Authored> || detail::IsOn<Authored>::value ||
                          std::same_as<Authored, Inline>,
                      "SOLAR_DIAGNOSTIC_REMOTE_ACTION_EXECUTION_TARGET: Action execution must "
                      "be Inline, On<Target>, or omitted");
        using type =
            std::conditional_t<std::is_void_v<Selected>, execution::SystemWorkQueue, Selected>;
    };

    template <typename ActionT>
    using ActionRegistration =
        execution::OnDemand<detail::action_registration_name<ActionT>(), ActionWork<ActionT>,
                            typename ActionTarget<ActionT>::type, execution::NativeCoalescing,
                            execution::stop::Drain, execution::failure::RecordAndContinue>;

    template <typename DataT> struct DataTarget
    {
      private:
        using Authored = detail::action_execution_t<DataT>;
        using Selected = typename detail::IsOn<Authored>::type;

      public:
        static_assert(std::is_void_v<Authored> || detail::IsOn<Authored>::value ||
                          std::same_as<Authored, Inline>,
                      "SOLAR_DIAGNOSTIC_REMOTE_DATA_EXECUTION_TARGET: Data execution must be "
                      "Inline, On<Target>, or omitted");
        using type =
            std::conditional_t<std::is_void_v<Selected>, execution::SystemWorkQueue, Selected>;
    };

    template <typename DataT>
    using DataRegistration =
        execution::OnDemand<detail::data_registration_name<DataT>(), DataWork<DataT>,
                            typename DataTarget<DataT>::type, execution::NativeCoalescing,
                            execution::stop::Drain, execution::failure::RecordAndContinue>;

    template <typename DataT> struct PollWork
    {
        [[nodiscard]] static Result<void> execute() noexcept
        {
            return FacilityType::process_poll_work != nullptr
                       ? FacilityType::process_poll_work(DataT::descriptor.id.value)
                       : Result<void>{fail<solar::Error>({.status = solar::Status::NotReady})};
        }
    };

    template <typename DataT> struct InStreamWork
    {
        [[nodiscard]] static Result<void> execute() noexcept
        {
            return FacilityType::process_in_stream_work != nullptr
                       ? FacilityType::process_in_stream_work(DataT::descriptor.id.value)
                       : Result<void>{fail<solar::Error>({.status = solar::Status::NotReady})};
        }
    };

    template <typename DataT> struct PollTarget
    {
      private:
        using Capability = typename detail::PollCapability<typename DataT::Capabilities>::type;
        using Acquisition = typename Capability::AcquisitionType;
        using Authored = typename remote::detail::PollAcquisition<Acquisition>::Target;

      public:
        using type =
            std::conditional_t<std::is_void_v<Authored>, execution::SystemWorkQueue, Authored>;
    };

    template <typename DataT>
    using PollRegistration =
        execution::OnDemand<detail::poll_registration_name<DataT>(), PollWork<DataT>,
                            typename PollTarget<DataT>::type, execution::NativeCoalescing,
                            execution::stop::Drain, execution::failure::RecordAndContinue>;

    template <typename DataT> struct InStreamTarget
    {
      private:
        using Execution = typename detail::ServiceInStreamTraits<DataT>::Execution;
        using Authored = typename detail::IsOn<Execution>::type;

      public:
        using type =
            std::conditional_t<std::is_void_v<Authored>, execution::SystemWorkQueue, Authored>;
    };

    template <typename DataT>
    using InStreamRegistration =
        execution::OnDemand<detail::in_stream_registration_name<DataT>(), InStreamWork<DataT>,
                            typename InStreamTarget<DataT>::type, execution::NativeCoalescing,
                            execution::stop::Drain, execution::failure::RecordAndContinue>;

    template <typename DataT, bool = detail::has_poll_v<DataT>> struct PollRegistrationFor
    {
        using type = TypeList<>;
    };

    template <typename DataT> struct PollRegistrationFor<DataT, true>
    {
        using type = TypeList<PollRegistration<DataT>>;
    };

    template <typename ListT> struct PollRegistrations;

    template <typename... DataTypes> struct PollRegistrations<TypeList<DataTypes...>>
    {
        using type = concat_t<typename PollRegistrationFor<DataTypes>::type...>;
    };

    template <typename DataT, bool = detail::ServiceInStreamTraits<DataT>::present &&
                                     !detail::ServiceInStreamTraits<DataT>::is_inline>
    struct InStreamRegistrationFor
    {
        using type = TypeList<>;
    };

    template <typename DataT> struct InStreamRegistrationFor<DataT, true>
    {
        using type = TypeList<InStreamRegistration<DataT>>;
    };

    template <typename ListT> struct InStreamRegistrations;

    template <typename... DataTypes> struct InStreamRegistrations<TypeList<DataTypes...>>
    {
        using type = concat_t<typename InStreamRegistrationFor<DataTypes>::type...>;
    };

    template <typename ListT> struct AsTasks;

    template <typename... Registrations> struct AsTasks<TypeList<Registrations...>>
    {
        using type = execution::Tasks<Registrations...>;
    };

    template <typename ActionT, bool = !std::same_as<detail::action_execution_t<ActionT>, Inline>>
    struct ActionRegistrationFor
    {
        using type = TypeList<>;
    };

    template <typename ActionT> struct ActionRegistrationFor<ActionT, true>
    {
        using type = TypeList<ActionRegistration<ActionT>>;
    };

    template <typename ListT> struct ActionRegistrations;

    template <typename... ActionTypes> struct ActionRegistrations<TypeList<ActionTypes...>>
    {
        using type = concat_t<typename ActionRegistrationFor<ActionTypes>::type...>;
    };

    template <typename DataT,
              bool = detail::HasDeferredDataCapability<typename DataT::Capabilities>::value &&
                     !detail::ExplicitInline<DataT>::value>
    struct DataRegistrationFor
    {
        using type = TypeList<>;
    };

    template <typename DataT> struct DataRegistrationFor<DataT, true>
    {
        using type = TypeList<DataRegistration<DataT>>;
    };

    template <typename ListT> struct DataRegistrations;

    template <typename... DataTypes> struct DataRegistrations<TypeList<DataTypes...>>
    {
        using type = concat_t<typename DataRegistrationFor<DataTypes>::type...>;
    };

    using ActionRegistrationTypes =
        typename ActionRegistrations<typename Architecture::Actions>::type;
    using DataRegistrationTypes = typename DataRegistrations<typename Architecture::Data>::type;
    using PollRegistrationTypes = typename PollRegistrations<typename Architecture::Data>::type;
    using InStreamRegistrationTypes =
        typename InStreamRegistrations<typename Architecture::Data>::type;
    using Tasks =
        typename AsTasks<concat_t<ActionRegistrationTypes, DataRegistrationTypes,
                                  PollRegistrationTypes, InStreamRegistrationTypes>>::type;

    static constexpr component::Descriptor descriptor{
        .name = "solar.remote.service",
        .description = "Bounded asynchronous Remote protocol engine",
    };

    inline static kernel::MessageQueue<detail::ServiceEvent, CONFIG_SOLAR_REMOTE_EVENT_QUEUE_DEPTH>
        events{};
    inline static std::atomic_bool open{};
    inline static std::atomic_uint32_t dropped_events{};
    inline static std::atomic_uint32_t processed_events{};
    inline static std::atomic_uint32_t event_high_water{};

    static void update_event_high_water() noexcept
    {
        auto observed = event_high_water.load(std::memory_order_relaxed);
        const auto current = static_cast<std::uint32_t>(events.size());
        while (observed < current &&
               !event_high_water.compare_exchange_weak(observed, current, std::memory_order_relaxed,
                                                       std::memory_order_relaxed)) {
        }
    }

    static void post(detail::ServiceEvent event) noexcept
    {
        const auto posted = kernel::in_isr() ? events.try_send_isr(event) : events.try_send(event);
        if (!posted) {
            dropped_events.fetch_add(1, std::memory_order_relaxed);
        } else {
            update_event_high_water();
        }
    }

    static void notify_stop() noexcept
    {
        (void)events.try_send_front(detail::ServiceEvent{.kind = detail::ServiceEvent::Kind::Stop});
    }

    [[nodiscard]] static Result<void> notify_publication(std::uint16_t endpoint) noexcept
    {
        const auto status = events.try_send(detail::ServiceEvent{
            .kind = detail::ServiceEvent::Kind::Publication,
            .subject = endpoint,
        });
        if (!status) {
            dropped_events.fetch_add(1, std::memory_order_relaxed);
        } else {
            update_event_high_water();
        }
        return status;
    }

    template <typename LinkT, std::uint16_t Index> static Result<void> open_link() noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        auto result = LinkT::open(LinkEventSink{.notify_function = &State::notify});
        if (!result) {
            return fail<solar::Error>({.status = result.error().status});
        }
        return {};
    }

    template <typename LinkT, std::uint16_t Index>
    [[nodiscard]] static Result<void, Error>
    transmit(protocol::Kind kind, std::span<const std::byte> payload = {},
             std::uint32_t request_id = 0, std::uint32_t target = 0,
             protocol::Flags flags = protocol::Flags::None, std::uint32_t reserved = 0) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        if (payload.size() > CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES) {
            return fail<Error>({Status::MessageTooLarge, Reason::Oversized, Operation::Transmit});
        }
        const auto lane = detail::lane_for(kind, request_id);
        if (payload.size() > max_frame_payload) {
            const auto count = (payload.size() + max_frame_payload - 1U) / max_frame_payload;
            if (count > UINT8_MAX) {
                return fail<Error>(
                    {Status::MessageTooLarge, Reason::Oversized, Operation::Transmit});
            }
            {
                auto guard = State::output_lock.acquire();
                auto message =
                    std::find_if(State::outbound_messages.begin(), State::outbound_messages.end(),
                                 [](const auto& candidate) { return !candidate.active; });
                if (message == State::outbound_messages.end()) {
                    if (lane != OutputLane::Telemetry) {
                        return fail<Error>({Status::Busy, Reason::NoCapacity, Operation::Transmit});
                    }
                    message = std::find_if(
                        State::outbound_messages.begin(), State::outbound_messages.end(),
                        [](const auto& candidate) {
                            return candidate.active && candidate.kind == protocol::Kind::Data;
                        });
                    if (message == State::outbound_messages.end()) {
                        return fail<Error>({Status::Busy, Reason::NoCapacity, Operation::Transmit});
                    }
                    ++State::lanes[static_cast<std::size_t>(OutputLane::Bulk)].record.replaced;
                }
                *message = {};
                message->active = true;
                message->lane = OutputLane::Bulk;
                message->kind = kind;
                message->flags = flags;
                message->request_id = request_id;
                message->target = target;
                message->reserved = reserved;
                message->size = static_cast<std::uint32_t>(payload.size());
                if (++State::next_fragment_id == 0) {
                    ++State::next_fragment_id;
                }
                message->fragment_id = State::next_fragment_id;
                message->fragment_count = static_cast<std::uint8_t>(count);
                std::copy(payload.begin(), payload.end(), message->payload.begin());
            }
            post(
                detail::ServiceEvent{.kind = detail::ServiceEvent::Kind::Output, .subject = Index});
            return {};
        }
        {
            auto guard = State::output_lock.acquire();
            auto& output = State::lanes[static_cast<std::size_t>(lane)];
            std::size_t slot_index{};
            if (output.size == output.slots.size()) {
                if (lane != OutputLane::Telemetry) {
                    ++output.record.dropped;
                    return fail<Error>({Status::Busy, Reason::NoCapacity, Operation::Transmit});
                }
                slot_index = (output.head + output.size - 1U) % output.slots.size();
                ++output.record.replaced;
            } else {
                slot_index = (output.head + output.size) % output.slots.size();
                ++output.size;
                ++output.record.admitted;
                output.record.high_water =
                    (std::max)(output.record.high_water, static_cast<std::uint16_t>(output.size));
            }
            auto& slot = output.slots[slot_index];
            std::copy(payload.begin(), payload.end(), slot.payload.begin());
            slot.kind = kind;
            slot.flags = flags;
            slot.request_id = request_id;
            slot.target = target;
            slot.reserved = reserved;
            slot.size = static_cast<std::uint16_t>(payload.size());
            output.record.occupied = true;
        }
        post(detail::ServiceEvent{.kind = detail::ServiceEvent::Kind::Output, .subject = Index});
        return {};
    }

    template <typename LinkT, std::uint16_t Index> static void stage_fragment() noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        auto guard = State::output_lock.acquire();
        auto message =
            std::find_if(State::outbound_messages.begin(), State::outbound_messages.end(),
                         [](const auto& candidate) { return candidate.active; });
        if (message == State::outbound_messages.end()) {
            return;
        }
        auto& lane = State::lanes[static_cast<std::size_t>(message->lane)];
        if (lane.size == lane.slots.size()) {
            return;
        }
        const auto slot_index = (lane.head + lane.size) % lane.slots.size();
        auto& slot = lane.slots[slot_index];
        const auto remaining = message->size - message->offset;
        const auto size = (std::min)(remaining, static_cast<std::uint32_t>(max_frame_payload));
        std::copy_n(message->payload.begin() + message->offset, size, slot.payload.begin());
        slot.kind = message->kind;
        slot.flags = message->flags | protocol::Flags::Fragmented;
        slot.request_id = message->request_id;
        slot.target = message->target;
        slot.reserved = message->reserved;
        slot.size = static_cast<std::uint16_t>(size);
        slot.fragment_id = message->fragment_id;
        slot.fragment_index = message->fragment_index;
        slot.fragment_count = message->fragment_count;
        ++lane.size;
        ++lane.record.admitted;
        lane.record.high_water =
            (std::max)(lane.record.high_water, static_cast<std::uint16_t>(lane.size));
        lane.record.occupied = true;
        message->offset += size;
        ++message->fragment_index;
        if (message->offset == message->size) {
            message->active = false;
        }
    }

    template <typename LinkT, std::uint16_t Index> static void try_start_transmit() noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        stage_response<LinkT, Index>();
        stage_fragment<LinkT, Index>();
        std::size_t encoded_size{};
        LeaseHandle handle{};
        OutputLane lane{};
        {
            auto guard = State::output_lock.acquire();
            if (State::tx_in_flight.load(std::memory_order_acquire) ||
                !State::connected.load(std::memory_order_acquire)) {
                return;
            }
            auto selected = State::lanes.end();
            for (auto current = State::lanes.begin(); current != State::lanes.end(); ++current) {
                if (current->size != 0) {
                    selected = current;
                    break;
                }
            }
            if (selected == State::lanes.end()) {
                return;
            }
            lane = static_cast<OutputLane>(selected - State::lanes.begin());
            auto& slot = selected->slots[selected->head];
            auto envelope_flags =
                static_cast<protocol::Flags>(static_cast<std::uint8_t>(slot.flags) &
                                             ~static_cast<std::uint8_t>(protocol::Flags::Final));
            if (slot.fragment_count == 1 || slot.fragment_index + 1U == slot.fragment_count) {
                envelope_flags = envelope_flags | protocol::Flags::Final;
            }
            protocol::Envelope envelope{
                .kind = slot.kind,
                .flags = envelope_flags,
                .session_epoch = State::epoch.load(std::memory_order_acquire),
                .frame_sequence = State::tx_sequence.fetch_add(1, std::memory_order_relaxed) + 1,
                .target = slot.target,
                .request_id = slot.request_id,
                .fragment_id = slot.fragment_id,
                .fragment_index = slot.fragment_index,
                .fragment_count = slot.fragment_count,
                .reserved = slot.reserved,
            };
            auto size = frame::encode(envelope, std::span{slot.payload}.first(slot.size),
                                      State::tx_scratch, State::tx_encoded);
            if (!size) {
                selected->head = (selected->head + 1U) % selected->slots.size();
                --selected->size;
                selected->record.occupied = selected->size != 0;
                ++selected->record.dropped;
                return;
            }
            encoded_size = *size;
            handle = LeaseHandle{
                .slot = static_cast<std::uint16_t>(lane),
                .generation = static_cast<std::uint16_t>(
                    State::tx_generation.fetch_add(1, std::memory_order_relaxed) + 1),
            };
            selected->record.transmitting = true;
            State::transmitting_lane = lane;
            State::transmitting_handle = handle;
            State::tx_in_flight.store(true, std::memory_order_release);
        }

        auto submitted =
            LinkT::try_transmit(TxLease{std::span{State::tx_encoded}.first(encoded_size), handle});
        if (submitted && *submitted == TxDisposition::Accepted) {
            State::transmitted_frames.fetch_add(1, std::memory_order_relaxed);
            auto guard = State::output_lock.acquire();
            auto& output = State::lanes[static_cast<std::size_t>(lane)];
            output.slots[output.head] = {};
            output.head = (output.head + 1U) % output.slots.size();
            --output.size;
            output.record.occupied = output.size != 0;
            return;
        }

        auto guard = State::output_lock.acquire();
        auto& output = State::lanes[static_cast<std::size_t>(lane)];
        if (submitted && *submitted == TxDisposition::Busy) {
            output.record.transmitting = false;
        } else {
            output.slots[output.head] = {};
            output.head = (output.head + 1U) % output.slots.size();
            --output.size;
            output.record.occupied = output.size != 0;
            output.record.transmitting = false;
            ++output.record.dropped;
        }
        State::transmitting_lane.reset();
        State::transmitting_handle = {};
        State::tx_in_flight.store(false, std::memory_order_release);
    }

    template <typename LinkT, std::uint16_t Index>
    [[nodiscard]] static bool reserve_response(std::uint32_t request, std::uint32_t target) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        auto guard = State::response_lock.acquire();
        auto slot = std::find_if(
            State::responses.begin(), State::responses.end(), [request](const auto& candidate) {
                return (candidate.reserved || candidate.valid) && candidate.request == request;
            });
        if (slot != State::responses.end()) {
            return true;
        }
        slot = std::find_if(
            State::responses.begin(), State::responses.end(),
            [](const auto& candidate) { return !candidate.reserved && !candidate.valid; });
        if (slot == State::responses.end()) {
            return false;
        }
        slot->reserved = true;
        slot->request = request;
        slot->target = target;
        return true;
    }

    template <typename LinkT, std::uint16_t Index>
    static void release_response(std::uint32_t request) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        auto guard = State::response_lock.acquire();
        const auto slot = std::find_if(
            State::responses.begin(), State::responses.end(), [request](const auto& candidate) {
                return (candidate.reserved || candidate.valid) && candidate.request == request;
            });
        if (slot != State::responses.end()) {
            *slot = {};
        }
    }

    template <typename LinkT, std::uint16_t Index>
    [[nodiscard]] static Result<void, Error> respond(std::uint32_t request, std::uint32_t target,
                                                     std::span<const std::byte> payload,
                                                     bool domain_error = false) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        if (payload.size() > CONFIG_SOLAR_REMOTE_RESPONSE_CACHE_BYTES) {
            return fail<Error>({Status::NoSpace, Reason::NoCapacity, Operation::Dispatch});
        }
        {
            auto guard = State::response_lock.acquire();
            auto slot = std::find_if(
                State::responses.begin(), State::responses.end(), [request](const auto& candidate) {
                    return (candidate.reserved || candidate.valid) && candidate.request == request;
                });
            if (slot == State::responses.end()) {
                slot = std::find_if(State::responses.begin(), State::responses.end(),
                                    [](const auto& candidate) { return !candidate.valid; });
            }
            if (slot == State::responses.end()) {
                return fail<Error>({Status::NoSpace, Reason::NoCapacity, Operation::Dispatch});
            }
            std::copy(payload.begin(), payload.end(), slot->payload.begin());
            slot->size = static_cast<std::uint16_t>(payload.size());
            slot->request = request;
            slot->target = target;
            slot->error = domain_error;
            slot->reserved = false;
            slot->valid = true;
            slot->staged = false;
        }
        post(detail::ServiceEvent{.kind = detail::ServiceEvent::Kind::Output, .subject = Index});
        return {};
    }

    template <typename LinkT, std::uint16_t Index>
    [[nodiscard]] static Result<void, Error> replay_response(std::uint32_t request) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        auto guard = State::response_lock.acquire();
        const auto slot = std::find_if(State::responses.begin(), State::responses.end(),
                                       [request](const auto& candidate) {
                                           return candidate.valid && candidate.request == request;
                                       });
        if (slot == State::responses.end()) {
            const auto pending = std::find_if(
                State::responses.begin(), State::responses.end(), [request](const auto& candidate) {
                    return candidate.reserved && candidate.request == request;
                });
            if (pending != State::responses.end()) {
                return fail<Error>({Status::Busy, Reason::Busy, Operation::Dispatch});
            }
            return fail<Error>({Status::NotFound, Reason::NoCapacity, Operation::Dispatch});
        }
        slot->staged = false;
        post(detail::ServiceEvent{.kind = detail::ServiceEvent::Kind::Output, .subject = Index});
        return {};
    }

    template <typename LinkT, std::uint16_t Index> static void stage_response() noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        auto guard = State::response_lock.acquire();
        const auto slot = std::find_if(
            State::responses.begin(), State::responses.end(),
            [](const auto& candidate) { return candidate.valid && !candidate.staged; });
        if (slot == State::responses.end()) {
            return;
        }
        auto queued = transmit<LinkT, Index>(
            protocol::Kind::Response, std::span{slot->payload}.first(slot->size), slot->request,
            slot->target, slot->error ? protocol::Flags::ErrorPayload : protocol::Flags::None);
        if (queued) {
            slot->staged = true;
        }
    }

    template <typename LinkT, std::uint16_t Index>
    static void acknowledge_response(std::uint32_t request) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        auto guard = State::response_lock.acquire();
        const auto slot = std::find_if(State::responses.begin(), State::responses.end(),
                                       [request](const auto& candidate) {
                                           return candidate.valid && candidate.request == request;
                                       });
        if (slot != State::responses.end()) {
            *slot = {};
        }
    }

    template <typename LinkT, std::uint16_t Index>
    [[nodiscard]] static bool response_cached(std::uint32_t request) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        auto guard = State::response_lock.acquire();
        return std::any_of(State::responses.begin(), State::responses.end(),
                           [request](const auto& candidate) {
                               return candidate.valid && candidate.request == request;
                           });
    }

    template <typename LinkT, std::uint16_t Index>
    [[nodiscard]] static Result<void, Error>
    protocol_error(std::uint32_t request, std::uint32_t target, protocol::ErrorCode code) noexcept
    {
        std::array<std::byte, 2> payload{};
        protocol::detail::put_u16(payload, 0, static_cast<std::uint16_t>(code));
        return transmit<LinkT, Index>(protocol::Kind::Error, payload, request, target,
                                      protocol::Flags::ErrorPayload);
    }

    [[nodiscard]] static constexpr std::array<std::byte, 16> hello_payload() noexcept
    {
        std::array<std::byte, 16> payload{};
        payload[0] = static_cast<std::byte>(protocol::major_version);
        payload[1] = static_cast<std::byte>(protocol::minor_version);
        protocol::detail::put_u32(payload, 4, CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES);
        protocol::detail::put_u32(payload, 8, CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES);
        protocol::detail::put_u32(payload, 12,
                                  0x0FU
#if defined(CONFIG_SOLAR_REMOTE_RUNTIME_INTROSPECTION)
                                      | 0x10U
#if defined(CONFIG_SOLAR_REMOTE_MANIFEST_RETRIEVAL)
                                      | 0x40U
#endif
#endif
#if defined(CONFIG_SOLAR_INSPECTION_REMOTE)
                                      | 0x20U
#endif
        );
        return payload;
    }

    [[nodiscard]] static auto introspection_summary_payload() noexcept
    {
        return protocol::encode(FacilityType::introspection_summary != nullptr
                                    ? FacilityType::introspection_summary()
                                    : protocol::IntrospectionSummary{});
    }

    [[nodiscard]] static auto server_information_payload() noexcept
    {
        return protocol::encode(FacilityType::server_information != nullptr
                                    ? FacilityType::server_information()
                                    : protocol::ServerInformation{});
    }

    template <typename LinkT, std::uint16_t Index>
    static void process_introspection(const frame::Decoded& decoded) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        constexpr auto observe =
            remote::detail::PermissionMask<Requires<permission::Observe>>::value;
        if ((State::grants.load(std::memory_order_acquire) & observe) != observe) {
            (void)protocol_error<LinkT, Index>(decoded.envelope.request_id, decoded.envelope.target,
                                               protocol::ErrorCode::Unauthorized);
            return;
        }
        if (decoded.envelope.request_id == 0) {
            (void)protocol_error<LinkT, Index>(decoded.envelope.request_id, decoded.envelope.target,
                                               protocol::ErrorCode::DecodeFailure);
            return;
        }
#if defined(CONFIG_SOLAR_REMOTE_RUNTIME_INTROSPECTION)
        if (decoded.envelope.target ==
            static_cast<std::uint32_t>(protocol::IntrospectionTarget::ProtocolSummary)) {
            if (!decoded.payload.empty()) {
                (void)protocol_error<LinkT, Index>(decoded.envelope.request_id,
                                                   decoded.envelope.target,
                                                   protocol::ErrorCode::DecodeFailure);
                return;
            }
            const auto summary = introspection_summary_payload();
            (void)transmit<LinkT, Index>(protocol::Kind::Introspection, summary,
                                         decoded.envelope.request_id, decoded.envelope.target);
            return;
        }
#endif
        if (decoded.envelope.target ==
            static_cast<std::uint32_t>(protocol::IntrospectionTarget::ServerInformation)) {
            if (!decoded.payload.empty()) {
                (void)protocol_error<LinkT, Index>(decoded.envelope.request_id,
                                                   decoded.envelope.target,
                                                   protocol::ErrorCode::DecodeFailure);
                return;
            }
            const auto information = server_information_payload();
            (void)transmit<LinkT, Index>(protocol::Kind::Introspection, information,
                                         decoded.envelope.request_id, decoded.envelope.target);
            return;
        }
#if defined(CONFIG_SOLAR_REMOTE_MANIFEST_RETRIEVAL)
        if (decoded.envelope.target ==
                static_cast<std::uint32_t>(protocol::IntrospectionTarget::Manifest) &&
            FacilityType::manifest_chunk != nullptr) {
            static std::array<std::byte, CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES> manifest_payload{};
            auto encoded = FacilityType::manifest_chunk(decoded.payload, manifest_payload);
            if (!encoded) {
                (void)protocol_error<LinkT, Index>(
                    decoded.envelope.request_id, decoded.envelope.target,
                    encoded.error().reason == Reason::Malformed ? protocol::ErrorCode::DecodeFailure
                                                                : protocol::ErrorCode::NoCapacity);
                return;
            }
            (void)transmit<LinkT, Index>(protocol::Kind::Introspection,
                                         std::span{manifest_payload}.first(*encoded),
                                         decoded.envelope.request_id, decoded.envelope.target);
            return;
        }
#endif
#if defined(CONFIG_SOLAR_INSPECTION_REMOTE)
        static std::array<std::byte, CONFIG_SOLAR_INSPECTION_REMOTE_RESPONSE_BYTES>
            inspection_payload{};
        if (decoded.envelope.target ==
                static_cast<std::uint32_t>(protocol::IntrospectionTarget::Collections) &&
            FacilityType::inspection_collections != nullptr) {
            auto encoded =
                FacilityType::inspection_collections(decoded.payload, inspection_payload);
            if (!encoded) {
                const auto code = encoded.error().reason == Reason::Malformed
                                      ? protocol::ErrorCode::DecodeFailure
                                      : protocol::ErrorCode::NoCapacity;
                (void)protocol_error<LinkT, Index>(decoded.envelope.request_id,
                                                   decoded.envelope.target, code);
                return;
            }
            (void)transmit<LinkT, Index>(protocol::Kind::Introspection,
                                         std::span{inspection_payload}.first(*encoded),
                                         decoded.envelope.request_id, decoded.envelope.target);
            return;
        }
        if (decoded.envelope.target ==
                static_cast<std::uint32_t>(protocol::IntrospectionTarget::CollectionQuery) &&
            FacilityType::inspection_query != nullptr) {
            auto encoded = FacilityType::inspection_query(decoded.payload, inspection_payload);
            if (!encoded) {
                auto code = protocol::ErrorCode::InternalFailure;
                switch (encoded.error().status) {
                case Status::NotFound:
                    code = protocol::ErrorCode::UnknownTarget;
                    break;
                case Status::NotReady:
                    code = protocol::ErrorCode::NotReady;
                    break;
                case Status::Busy:
                case Status::WouldBlock:
                    code = protocol::ErrorCode::Busy;
                    break;
                case Status::NoSpace:
                case Status::NoBuffer:
                    code = protocol::ErrorCode::NoCapacity;
                    break;
                case Status::Invalid:
                case Status::ProtocolError:
                    code = protocol::ErrorCode::DecodeFailure;
                    break;
                default:
                    break;
                }
                (void)protocol_error<LinkT, Index>(decoded.envelope.request_id,
                                                   decoded.envelope.target, code);
                return;
            }
            (void)transmit<LinkT, Index>(protocol::Kind::Introspection,
                                         std::span{inspection_payload}.first(*encoded),
                                         decoded.envelope.request_id, decoded.envelope.target);
            return;
        }
#endif
        {
            (void)protocol_error<LinkT, Index>(decoded.envelope.request_id, decoded.envelope.target,
                                               protocol::ErrorCode::UnknownTarget);
        }
    }

    [[nodiscard]] static constexpr bool has_flag(protocol::Flags flags,
                                                 protocol::Flags flag) noexcept
    {
        return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(flag)) != 0;
    }

    template <typename LinkT, std::uint16_t Index>
    [[nodiscard]] static Result<std::optional<frame::Decoded>, Error>
    reassemble(const frame::Decoded& decoded) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        const bool fragmented = has_flag(decoded.envelope.flags, protocol::Flags::Fragmented);
        if (!fragmented && decoded.envelope.fragment_count == 1) {
            return std::optional<frame::Decoded>{decoded};
        }
        if (!fragmented || decoded.envelope.fragment_count <= 1 ||
            decoded.envelope.fragment_id == 0) {
            return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Receive});
        }

        const bool final = has_flag(decoded.envelope.flags, protocol::Flags::Final);
        const bool last = decoded.envelope.fragment_index + 1U == decoded.envelope.fragment_count;
        if (final != last) {
            return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Receive});
        }

        const auto now = kernel::now_ticks();
        for (auto& candidate : State::reassembly) {
            if (candidate.active && candidate.deadline <= now) {
                candidate = {};
                State::rejected_frames.fetch_add(1, std::memory_order_relaxed);
            }
        }

        auto slot = std::find_if(State::reassembly.begin(), State::reassembly.end(),
                                 [&](const auto& candidate) {
                                     return candidate.active && candidate.envelope.fragment_id ==
                                                                    decoded.envelope.fragment_id;
                                 });
        if (decoded.envelope.fragment_index == 0) {
            if (slot == State::reassembly.end()) {
                slot = std::find_if(State::reassembly.begin(), State::reassembly.end(),
                                    [](const auto& candidate) { return !candidate.active; });
            }
            if (slot == State::reassembly.end()) {
                return fail<Error>({Status::NoSpace, Reason::NoCapacity, Operation::Receive});
            }
            *slot = {};
            slot->active = true;
            slot->envelope = decoded.envelope;
            slot->deadline = now + kernel::to_ticks_ceil(std::chrono::milliseconds{
                                       CONFIG_SOLAR_REMOTE_REASSEMBLY_TIMEOUT_MS});
        } else if (slot == State::reassembly.end()) {
            return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Receive});
        }

        const auto& authored = slot->envelope;
        const auto stable_flags = [](protocol::Flags flags) {
            return static_cast<std::uint8_t>(flags) &
                   ~(static_cast<std::uint8_t>(protocol::Flags::Final));
        };
        if (decoded.envelope.fragment_index != slot->next_fragment ||
            decoded.envelope.fragment_count != authored.fragment_count ||
            decoded.envelope.session_epoch != authored.session_epoch ||
            decoded.envelope.kind != authored.kind || decoded.envelope.target != authored.target ||
            decoded.envelope.request_id != authored.request_id ||
            decoded.envelope.reserved != authored.reserved ||
            stable_flags(decoded.envelope.flags) != stable_flags(authored.flags) ||
            slot->size + decoded.payload.size() > slot->payload.size()) {
            *slot = {};
            return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Receive});
        }

        std::copy(decoded.payload.begin(), decoded.payload.end(),
                  slot->payload.begin() + slot->size);
        slot->size += static_cast<std::uint32_t>(decoded.payload.size());
        ++slot->next_fragment;
        if (!last) {
            return std::optional<frame::Decoded>{};
        }

        auto envelope = slot->envelope;
        envelope.flags =
            static_cast<protocol::Flags>((static_cast<std::uint8_t>(envelope.flags) &
                                          ~static_cast<std::uint8_t>(protocol::Flags::Fragmented)) |
                                         static_cast<std::uint8_t>(protocol::Flags::Final));
        envelope.payload_size = 0;
        envelope.fragment_id = 0;
        envelope.fragment_index = 0;
        envelope.fragment_count = 1;
        const auto payload = std::span<const std::byte>{slot->payload}.first(slot->size);
        slot->active = false;
        return std::optional<frame::Decoded>{frame::Decoded{
            .envelope = envelope,
            .payload = payload,
        }};
    }

    template <typename LinkT, std::uint16_t Index>
    static void handle_frame(const frame::Decoded& decoded) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        State::accepted_frames.fetch_add(1, std::memory_order_relaxed);
        auto assembled = reassemble<LinkT, Index>(decoded);
        if (!assembled) {
            State::protocol_errors.fetch_add(1, std::memory_order_relaxed);
            (void)protocol_error<LinkT, Index>(decoded.envelope.request_id, decoded.envelope.target,
                                               protocol::ErrorCode::FragmentRejected);
            return;
        }
        if (!*assembled) {
            return;
        }
        const auto complete = **assembled;
        switch (complete.envelope.kind) {
        case protocol::Kind::ClientHello: {
            if (complete.payload.size() != hello_payload().size() ||
                std::to_integer<std::uint8_t>(complete.payload[0]) != protocol::major_version) {
                State::protocol_errors.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            State::session.store(SessionState::Active, std::memory_order_release);
            const auto hello = hello_payload();
            (void)transmit<LinkT, Index>(protocol::Kind::ServerHello, hello);
            if (FacilityType::open_session != nullptr) {
                FacilityType::open_session(Index);
            }
            break;
        }
        case protocol::Kind::Keepalive:
            if (State::session.load(std::memory_order_acquire) != SessionState::Active) {
                State::protocol_errors.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            (void)transmit<LinkT, Index>(protocol::Kind::Keepalive, complete.payload);
            break;
        case protocol::Kind::Introspection:
            if (State::session.load(std::memory_order_acquire) != SessionState::Active) {
                State::protocol_errors.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            process_introspection<LinkT, Index>(complete);
            break;
        case protocol::Kind::SessionReset: {
            if (FacilityType::reset_session != nullptr) {
                FacilityType::reset_session(Index);
            }
            {
                auto guard = State::response_lock.acquire();
                State::responses = {};
            }
            State::last_request_id.store(0, std::memory_order_release);
            State::reassembly = {};
            State::session.store(SessionState::Negotiating, std::memory_order_release);
            State::epoch.fetch_add(1, std::memory_order_acq_rel);
            const auto hello = hello_payload();
            (void)transmit<LinkT, Index>(protocol::Kind::ServerHello, hello);
            break;
        }
        case protocol::Kind::Request:
        case protocol::Kind::ResponseAck:
        case protocol::Kind::Cancel:
        case protocol::Kind::Subscribe:
        case protocol::Kind::Unsubscribe:
        case protocol::Kind::Credit:
        case protocol::Kind::Data:
            if (State::session.load(std::memory_order_acquire) != SessionState::Active ||
                FacilityType::process_application_frame == nullptr) {
                State::protocol_errors.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            FacilityType::process_application_frame(Index, complete);
            break;
        default:
            State::protocol_errors.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }

    template <typename... LinkTypes, std::size_t... Indices>
    static Result<void> open_links(TypeList<LinkTypes...>, std::index_sequence<Indices...>) noexcept
    {
        Result<void> result{};
        ((result ? result = open_link<LinkTypes, static_cast<std::uint16_t>(Indices)>() : result),
         ...);
        return result;
    }

    [[nodiscard]] static Result<void> init() noexcept
    {
        events.purge();
        dropped_events.store(0, std::memory_order_relaxed);
        processed_events.store(0, std::memory_order_relaxed);
        event_high_water.store(0, std::memory_order_relaxed);
        open.store(false, std::memory_order_relaxed);
        return {};
    }

    [[nodiscard]] static Result<void> start() noexcept
    {
        auto result = open_links(Links{}, std::make_index_sequence<list_size_v<Links>>{});
        if (!result) {
            close_links(Links{});
            return result;
        }
        open.store(true, std::memory_order_release);
        return {};
    }

    template <typename... LinkTypes> static void close_links(TypeList<LinkTypes...>) noexcept
    {
        (LinkTypes::close(), ...);
    }

    template <std::size_t... Indices>
    static void reset_sessions(std::index_sequence<Indices...>) noexcept
    {
        if (FacilityType::reset_session != nullptr) {
            (FacilityType::reset_session(static_cast<std::uint16_t>(Indices)), ...);
        }
    }

    [[nodiscard]] static Result<void> stop() noexcept
    {
        open.store(false, std::memory_order_release);
        reset_sessions(std::make_index_sequence<list_size_v<Links>>{});
        close_links(Links{});
        return {};
    }

    [[nodiscard]] static Result<void> deinit() noexcept
    {
        events.purge();
        return {};
    }

    template <typename LinkT, std::uint16_t Index>
    static void process_link_event(const LinkEvent& event) noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        switch (event.kind) {
        case LinkEventKind::Connected:
            if (FacilityType::reset_session != nullptr) {
                FacilityType::reset_session(Index);
            }
            {
                auto guard = State::output_lock.acquire();
                State::lanes = {};
                State::outbound_messages = {};
                State::transmitting_lane.reset();
                State::transmitting_handle = {};
                State::subscriptions = {};
                State::subscription_count = 0;
            }
            {
                auto guard = State::response_lock.acquire();
                State::responses = {};
            }
            State::last_request_id.store(0, std::memory_order_release);
            State::reassembly = {};
            State::connected.store(true, std::memory_order_release);
            State::connections.fetch_add(1, std::memory_order_relaxed);
            State::session.store(SessionState::Negotiating, std::memory_order_release);
            State::epoch.fetch_add(1, std::memory_order_acq_rel);
            State::grants.store(
                [] {
                    if constexpr (requires { typename LinkT::Grants; }) {
                        return remote::detail::PermissionMask<typename LinkT::Grants>::value;
                    }
                    return std::uint8_t{};
                }(),
                std::memory_order_release);
            {
                const auto hello = hello_payload();
                (void)transmit<LinkT, Index>(protocol::Kind::ServerHello, hello);
            }
            break;
        case LinkEventKind::Disconnected:
        case LinkEventKind::Fault:
            if (FacilityType::reset_session != nullptr) {
                FacilityType::reset_session(Index);
            }
            State::connected.store(false, std::memory_order_release);
            State::session.store(event.kind == LinkEventKind::Fault ? SessionState::Faulted
                                                                    : SessionState::Disconnected,
                                 std::memory_order_release);
            State::tx_in_flight.store(false, std::memory_order_release);
            {
                auto guard = State::response_lock.acquire();
                State::responses = {};
            }
            State::last_request_id.store(0, std::memory_order_release);
            State::reassembly = {};
            {
                auto guard = State::output_lock.acquire();
                State::lanes = {};
                State::outbound_messages = {};
                State::transmitting_lane.reset();
                State::transmitting_handle = {};
            }
            break;
        case LinkEventKind::RxReady: {
            State::received_bytes.fetch_add(event.size, std::memory_order_relaxed);
            auto bytes = LinkT::rx_bytes(event.lease);
            if (!bytes) {
                State::rejected_frames.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            const auto feed =
                State::decoder.feed(*bytes, [](const frame::Decoded& decoded) noexcept {
                    handle_frame<LinkT, Index>(decoded);
                });
            State::rejected_frames.fetch_add(
                static_cast<std::uint32_t>(feed.rejected + feed.overflowed),
                std::memory_order_relaxed);
            LinkT::release_rx(event.lease);
            break;
        }
        case LinkEventKind::TxComplete:
            State::transmitted_bytes.fetch_add(event.size, std::memory_order_relaxed);
            {
                auto guard = State::output_lock.acquire();
                if (State::transmitting_lane && event.lease == State::transmitting_handle) {
                    auto& slot = State::lanes[static_cast<std::size_t>(*State::transmitting_lane)];
                    slot.record.transmitting = false;
                    State::transmitting_lane.reset();
                    State::transmitting_handle = {};
                    State::tx_in_flight.store(false, std::memory_order_release);
                }
            }
            break;
        }
        try_start_transmit<LinkT, Index>();
    }

    [[nodiscard]] static ServiceRecord record() noexcept
    {
        return {
            .ready = FacilityType::ready.load(std::memory_order_acquire),
            .accepting = FacilityType::accepting.load(std::memory_order_acquire),
            .queued_events = static_cast<std::uint32_t>(events.size()),
            .event_high_water = event_high_water.load(std::memory_order_acquire),
            .processed_events = processed_events.load(std::memory_order_acquire),
            .dropped_events = dropped_events.load(std::memory_order_acquire),
        };
    }

    template <typename LinkT, std::uint16_t Index>
    [[nodiscard]] static LinkRecord link_record() noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        return {
            .id = LinkT::descriptor.id,
            .session = State::session.load(std::memory_order_acquire),
            .epoch = State::epoch.load(std::memory_order_acquire),
            .received_frames = State::accepted_frames.load(std::memory_order_acquire),
            .received_bytes = State::received_bytes.load(std::memory_order_acquire),
            .rejected_frames = State::rejected_frames.load(std::memory_order_acquire),
            .transmitted_frames = State::transmitted_frames.load(std::memory_order_acquire),
            .transmitted_bytes = State::transmitted_bytes.load(std::memory_order_acquire),
            .protocol_errors = State::protocol_errors.load(std::memory_order_acquire),
            .connections = State::connections.load(std::memory_order_acquire),
            .duplicate_requests = State::duplicate_requests.load(std::memory_order_acquire),
            .completed_requests = State::completed_requests.load(std::memory_order_acquire),
            .subscriptions = State::subscription_count.load(std::memory_order_acquire),
            .lanes =
                [&] {
                    std::array<LaneRecord, 5> records{};
                    auto guard = State::output_lock.acquire();
                    for (std::size_t lane{}; lane < records.size(); ++lane) {
                        records[lane] = State::lanes[lane].record;
                        records[lane].depth = State::lanes[lane].size;
                    }
                    return records;
                }(),
            .connected = State::connected.load(std::memory_order_acquire),
            .tx_in_flight = State::tx_in_flight.load(std::memory_order_acquire),
        };
    }

    template <typename... LinkTypes, std::size_t... Indices>
    static void dispatch(detail::ServiceEvent event, TypeList<LinkTypes...>,
                         std::index_sequence<Indices...>) noexcept
    {
        if (event.kind == detail::ServiceEvent::Kind::Output) {
            ((event.subject == Indices
                  ? (try_start_transmit<LinkTypes, static_cast<std::uint16_t>(Indices)>(), void())
                  : void()),
             ...);
            return;
        }
        if (event.kind != detail::ServiceEvent::Kind::Link) {
            if (event.kind == detail::ServiceEvent::Kind::Publication &&
                FacilityType::process_publication != nullptr) {
                FacilityType::process_publication(event.subject);
            }
            return;
        }
        ((event.subject == Indices
              ? (process_link_event<LinkTypes, static_cast<std::uint16_t>(Indices)>(event.event),
                 void())
              : void()),
         ...);
    }

    template <typename... LinkTypes, std::size_t... Indices>
    static void drain_outputs(TypeList<LinkTypes...>, std::index_sequence<Indices...>) noexcept
    {
        (try_start_transmit<LinkTypes, static_cast<std::uint16_t>(Indices)>(), ...);
    }

    template <typename LinkT> static void poll_link() noexcept
    {
        if constexpr (requires { LinkT::poll(); }) {
            LinkT::poll();
        }
    }

    template <typename... LinkTypes> static void poll_links(TypeList<LinkTypes...>) noexcept
    {
        (poll_link<LinkTypes>(), ...);
    }

    template <typename LinkT, std::uint16_t Index> static void expire_reassembly() noexcept
    {
        using State = detail::LinkState<Service, LinkT, Index>;
        const auto now = kernel::now_ticks();
        for (auto& slot : State::reassembly) {
            if (slot.active && slot.deadline <= now) {
                slot = {};
                State::rejected_frames.fetch_add(1, std::memory_order_relaxed);
                State::protocol_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    template <typename... LinkTypes, std::size_t... Indices>
    static void expire_reassemblies(TypeList<LinkTypes...>,
                                    std::index_sequence<Indices...>) noexcept
    {
        (expire_reassembly<LinkTypes, static_cast<std::uint16_t>(Indices)>(), ...);
    }

    [[nodiscard]] static Result<void> pump_once(kernel::Timeout timeout) noexcept
    {
        auto event = events.receive(timeout);
        if (!event) {
            const auto status = status_of(event.error());
            return status == Status::WouldBlock || status == Status::Empty ||
                           status == Status::Timeout
                       ? Result<void>{}
                       : Result<void>{fail<solar::Error>(event.error())};
        }
        dispatch(*event, Links{}, std::make_index_sequence<list_size_v<Links>>{});
        processed_events.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    [[nodiscard]] static Result<void> run(kernel::StopToken stop) noexcept
    {
        const auto maintenance = kernel::to_ticks_ceil(std::chrono::milliseconds{50});
        auto wait_ticks = maintenance;
        while (!stop.stop_requested()) {
            auto result = pump_once(kernel::Timeout::after_ticks(wait_ticks));
            if (!result) {
                return result;
            }
            poll_links(Links{});
            drain_outputs(Links{}, std::make_index_sequence<list_size_v<Links>>{});
            expire_reassemblies(Links{}, std::make_index_sequence<list_size_v<Links>>{});
            wait_ticks = maintenance;
            if (FacilityType::process_poll_releases != nullptr) {
                const auto requested = FacilityType::process_poll_releases();
                wait_ticks = (std::max)(kernel::Tick{1}, (std::min)(maintenance, requested));
            }
        }
        return {};
    }
};

#else

template <typename ArchitectureT> struct Service
{
    using Architecture = ArchitectureT;
    using FacilityType = Facility<Architecture>;
    using Dependencies = solar::Dependencies<FacilityType>;

    static constexpr component::Descriptor descriptor{
        .name = "solar.remote.service",
        .description = "Bounded asynchronous Remote protocol engine",
    };
};

#endif

} // namespace solar::remote
