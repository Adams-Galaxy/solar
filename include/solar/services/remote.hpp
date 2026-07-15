#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "solar/core.hpp"
#include "solar/log/source.hpp"
#include "solar/remote/generated/core.hpp"
#include "solar/remote/protocol.hpp"
#include "solar/remote/schema.hpp"
#include "solar/kernel/this_thread.hpp"
#include "solar/service.hpp"

namespace solar::services
{

template <typename TransportT>
/**
 * @brief Transport concept required by `services::Remote`.
 */
concept RemoteTransport = requires(const std::uint8_t *tx, std::uint8_t *rx, std::size_t len) {
    { TransportT::write(tx, len) } -> std::same_as<std::size_t>;
    { TransportT::read(rx, len) } -> std::same_as<std::size_t>;
    { TransportT::read() } -> std::convertible_to<int>;
    { TransportT::available() } -> std::convertible_to<int>;
    { TransportT::flush() } -> std::same_as<void>;
};

namespace detail
{

template <typename T>
inline constexpr bool UsesSystemRemoteCatalogV = std::is_same_v<T, remote::UseSystemCatalog>;

template <typename MethodListT>
struct MethodDescriptors;

template <typename... Methods>
struct MethodDescriptors<remote::Methods<Methods...>>
{
    static constexpr auto make()
    {
        return std::array<remote::MethodDescriptor, sizeof...(Methods)>{{
            {Methods::id,
             Methods::Name::c_str(),
             Methods::Request::Type,
             Methods::Response::Type,
             Methods::version}...}};
    }
};

template <typename TypeListT>
struct TypeDescriptors;

template <typename... Types>
struct TypeDescriptors<remote::Types<Types...>>
{
    static constexpr auto make()
    {
        return std::array<remote::TypeDescriptor, sizeof...(Types)>{{
            {Types::Type,
             Types::Name::c_str(),
             Types::Version,
             Types::MaxSize}...}};
    }
};

template <typename TopicListT>
struct TopicDescriptors;

template <typename... Topics>
struct TopicDescriptors<remote::Topics<Topics...>>
{
    static constexpr auto make()
    {
        return std::array<remote::TopicDescriptor, sizeof...(Topics)>{{
            {Topics::id,
             Topics::Name::c_str(),
             Topics::Payload::Type,
             Topics::direction,
             Topics::policy,
             Topics::version}...}};
    }
};

template <typename ObservableListT>
struct ObservableDescriptors;

template <typename... Observables>
struct ObservableDescriptors<remote::Observables<Observables...>>
{
    static constexpr auto make()
    {
        return std::array<remote::ObservableDescriptor, sizeof...(Observables)>{{
            {Observables::id,
             Observables::Name::c_str(),
             Observables::Payload::Type,
             Observables::mode,
             Observables::policy,
             Observables::min_interval_ms,
             Observables::max_interval_ms,
             Observables::version}...}};
    }
};

template <typename ContextT, typename FallbackT>
using EffectiveRemoteMethodsT = std::conditional_t<UsesSystemRemoteCatalogV<FallbackT>,
                                                   typename ContextT::SystemType::RemoteMethodsCatalog,
                                                   FallbackT>;

template <typename ContextT, typename FallbackT>
using EffectiveRemoteTypesT = std::conditional_t<UsesSystemRemoteCatalogV<FallbackT>,
                                                 typename ContextT::SystemType::RemoteTypesCatalog,
                                                 FallbackT>;

template <typename ContextT, typename FallbackT>
using EffectiveRemoteTopicsT = std::conditional_t<UsesSystemRemoteCatalogV<FallbackT>,
                                                  typename ContextT::SystemType::RemoteTopicsCatalog,
                                                  FallbackT>;

template <typename ContextT, typename FallbackT>
using EffectiveRemoteObservablesT = std::conditional_t<UsesSystemRemoteCatalogV<FallbackT>,
                                                       typename ContextT::SystemType::RemoteObservablesCatalog,
                                                       FallbackT>;

} // namespace detail

template <RemoteTransport TransportT,
          typename MethodListT = remote::UseSystemCatalog,
          typename TopicListT = remote::UseSystemCatalog,
          typename ObservableListT = remote::UseSystemCatalog,
          typename TypeListT = remote::UseSystemCatalog>
/**
 * @brief Active Remote service for binary host/device control and inspection.
 *
 * The service runs on its own Solar service thread. By default it exposes the
 * Remote catalogs collected by `System`, plus Solar's generated core methods.
 */
class Remote
{
public:
    using Name = solar::Name<"remote">;
    using Thread = solar::ServiceSpec<Name, 2048, kernel::Priority::Normal>;
    using Transport = TransportT;
    using Methods = MethodListT;
    using Topics = TopicListT;
    using Observables = ObservableListT;
    using Types = TypeListT;

    template <typename ContextT>
    Status init(ContextT &)
    {
        rx_size_ = 0;
        sequence_ = 1;
        return Status::Ok;
    }

    template <typename ContextT>
    Status start(ContextT &)
    {
        return Status::Ok;
    }

    template <typename ContextT>
    void run(ContextT &ctx, StopToken stop)
    {
        while (!stop.stop_requested())
        {
            (void)drain(ctx);
            kernel::ThisThread::sleep_for(Milliseconds{1});
        }
    }

private:
    template <typename ContextT>
    Status drain(ContextT &ctx)
    {
        while (TransportT::available() > 0)
        {
            const int value = TransportT::read();
            if (value < 0)
            {
                break;
            }
            if (rx_size_ >= FrameBufferBytes)
            {
                rx_size_ = 0;
                return Status::Invalid;
            }
            rx_frame_[rx_size_++] = static_cast<std::uint8_t>(value);
            if (value == 0)
            {
                handle_frame(ctx, rx_frame_, rx_size_);
                rx_size_ = 0;
            }
        }
        return Status::Ok;
    }

    static constexpr std::size_t PayloadBytes = 1024;
    static constexpr std::size_t FrameBufferBytes = 1152;
    static constexpr std::size_t CoreMethodCount = sizeof(remote::generated::CoreMethods) / sizeof(remote::generated::CoreMethods[0]);
    static constexpr std::size_t CoreTypeCount = sizeof(remote::generated::CoreTypes) / sizeof(remote::generated::CoreTypes[0]);
    static constexpr remote::MethodId HelloId = remote::Id<"solar.hello">::value;
    static constexpr remote::MethodId SummaryId = remote::Id<"solar.remote.summary">::value;
    static constexpr remote::MethodId MethodsListId = remote::Id<"solar.remote.methods.list">::value;
    static constexpr remote::MethodId TopicsListId = remote::Id<"solar.remote.topics.list">::value;
    static constexpr remote::MethodId ObservablesListId = remote::Id<"solar.remote.observables.list">::value;
    static constexpr remote::MethodId TypesListId = remote::Id<"solar.remote.types.list">::value;
    static constexpr remote::MethodId ComponentsListId = remote::Id<"solar.graph.components.list">::value;
    static constexpr remote::MethodId BootReportId = remote::Id<"solar.boot.report">::value;

    template <typename ContextT>
    static constexpr auto method_descriptors()
    {
        using Effective = detail::EffectiveRemoteMethodsT<ContextT, MethodListT>;
        return detail::MethodDescriptors<Effective>::make();
    }

    template <typename ContextT>
    static constexpr auto topic_descriptors()
    {
        using Effective = detail::EffectiveRemoteTopicsT<ContextT, TopicListT>;
        return detail::TopicDescriptors<Effective>::make();
    }

    template <typename ContextT>
    static constexpr auto observable_descriptors()
    {
        using Effective = detail::EffectiveRemoteObservablesT<ContextT, ObservableListT>;
        return detail::ObservableDescriptors<Effective>::make();
    }

    template <typename ContextT>
    static constexpr auto type_descriptors()
    {
        using Effective = detail::EffectiveRemoteTypesT<ContextT, TypeListT>;
        return detail::TypeDescriptors<Effective>::make();
    }

    template <typename ContextT>
    void handle_frame(ContextT &ctx, const std::uint8_t *frame_data, std::size_t frame_size)
    {
        remote::Frame frame{};
        if (!remote::decode_frame(frame_data, frame_size, rx_payload_, payload_limit<ContextT>(), frame))
        {
            log_warn(ctx, "malformed frame");
            send_error(0, 0, 0, remote::ErrorCode::MalformedFrame, Status::Invalid, "malformed frame");
            return;
        }

        if (frame.kind == remote::FrameKind::Hello || frame.target_id == HelloId)
        {
            send_hello(frame, ctx);
            return;
        }

        if (frame.kind == remote::FrameKind::Heartbeat)
        {
            remote::generated::Empty empty{};
            send_response(frame, empty);
            return;
        }

        if (frame.kind == remote::FrameKind::Subscribe)
        {
            send_error(frame.sequence, frame.correlation, frame.target_id, remote::ErrorCode::UnknownTarget, Status::NotFound, "subscriptions are not active yet");
            return;
        }

        if (frame.kind != remote::FrameKind::Request)
        {
            log_warn(ctx, "unsupported frame kind=%u", static_cast<unsigned>(frame.kind));
            send_error(frame.sequence, frame.correlation, frame.target_id, remote::ErrorCode::UnknownTarget, Status::Invalid, "unsupported frame kind");
            return;
        }

        if (frame.target_id == SummaryId)
        {
            constexpr auto methods = method_descriptors<ContextT>();
            constexpr auto topics = topic_descriptors<ContextT>();
            constexpr auto observables = observable_descriptors<ContextT>();
            constexpr auto types = type_descriptors<ContextT>();
            remote::generated::RemoteSummary response{};
            response.methods = static_cast<std::uint16_t>(CoreMethodCount + methods.size());
            response.topics = static_cast<std::uint16_t>(topics.size());
            response.observables = static_cast<std::uint16_t>(observables.size());
            response.types = static_cast<std::uint16_t>(CoreTypeCount + types.size());
            response.components = static_cast<std::uint16_t>(
                ContextT::SystemType::graph::components().size());
            send_response(frame, response);
            return;
        }

        if (frame.target_id == MethodsListId)
        {
            remote::generated::ListRequest request{};
            if (!decode_request(frame, request))
            {
                return;
            }
            send_methods<ContextT>(frame, request);
            return;
        }

        if (frame.target_id == TopicsListId)
        {
            remote::generated::ListRequest request{};
            if (!decode_request(frame, request))
            {
                return;
            }
            send_topics<ContextT>(frame, request);
            return;
        }

        if (frame.target_id == ObservablesListId)
        {
            remote::generated::ListRequest request{};
            if (!decode_request(frame, request))
            {
                return;
            }
            send_observables<ContextT>(frame, request);
            return;
        }

        if (frame.target_id == TypesListId)
        {
            remote::generated::ListRequest request{};
            if (!decode_request(frame, request))
            {
                return;
            }
            send_types<ContextT>(frame, request);
            return;
        }

        if (frame.target_id == ComponentsListId)
        {
            remote::generated::ListRequest request{};
            if (!decode_request(frame, request))
            {
                return;
            }
            send_components<typename ContextT::SystemType>(frame, request);
            return;
        }

        if (frame.target_id == BootReportId)
        {
            send_boot_report(frame, ContextT::SystemType::boot_report());
            return;
        }

        log_warn(ctx, "unknown target=%lu", static_cast<unsigned long>(frame.target_id));
        send_error(frame.sequence, frame.correlation, frame.target_id, remote::ErrorCode::UnknownTarget, Status::NotFound, "unknown Remote target");
    }

    template <typename ContextT, typename... Args>
    static void log_warn(ContextT &, const char *format, Args... args)
    {
        if constexpr (IS_ENABLED(CONFIG_SOLAR_REMOTE_LOGS))
        {
            using RemoteLog = typename ContextT::SystemType::template Log<
                solar::Name<"solar">,
                solar::log::Categories<solar::Name<"remote">>>;
            RemoteLog::template warn<solar::Name<"remote">>(format, args...);
        }
    }

    template <typename T>
    bool decode_request(remote::Frame const &frame, T &request)
    {
        if (!remote::decode(frame.payload, frame.payload_size, request))
        {
            send_error(frame.sequence, frame.correlation, frame.target_id, remote::ErrorCode::DecodeFailure, Status::Invalid, "decode failed");
            return false;
        }
        return true;
    }

    template <typename ContextT>
    void send_hello(remote::Frame const &request, ContextT &ctx)
    {
        remote::generated::HelloResponse response{};
        constexpr auto methods = method_descriptors<ContextT>();
        constexpr auto topics = topic_descriptors<ContextT>();
        constexpr auto observables = observable_descriptors<ContextT>();
        constexpr auto types = type_descriptors<ContextT>();
        response.method_count = static_cast<std::uint16_t>(CoreMethodCount + methods.size());
        response.topic_count = static_cast<std::uint16_t>(topics.size());
        response.observable_count = static_cast<std::uint16_t>(observables.size());
        response.type_count = static_cast<std::uint16_t>(CoreTypeCount + types.size());
        response.frame_max_bytes = static_cast<std::uint16_t>(payload_limit<ContextT>());
        response.heartbeat_ms = CONFIG_SOLAR_REMOTE_HEARTBEAT_MS;
        response.session_timeout_ms = CONFIG_SOLAR_REMOTE_SESSION_TIMEOUT_MS;
        (void)ctx;
        send_payload(request.sequence, request.correlation, request.target_id, remote::FrameKind::HelloAck, response);
    }

    template <typename ContextT>
    static constexpr std::size_t payload_limit()
    {
        constexpr std::size_t configured = CONFIG_SOLAR_REMOTE_PAYLOAD_BYTES;
        return configured < PayloadBytes ? configured : PayloadBytes;
    }

    template <typename ContextT>
    void send_methods(remote::Frame const &request, remote::generated::ListRequest const &list)
    {
        constexpr auto user_descriptors = method_descriptors<ContextT>();
        remote::generated::MethodListResponse response{};
        append_page(response.methods, list, remote::generated::CoreMethods, CoreMethodCount, user_descriptors, [](auto const &descriptor, auto &out) {
            out.id = descriptor.id;
            out.request_type = descriptor.request_type;
            out.response_type = descriptor.response_type;
            out.version = descriptor.version;
            return out.name.assign(descriptor.name);
        });
        send_response(request, response);
    }

    template <typename ContextT>
    void send_topics(remote::Frame const &request, remote::generated::ListRequest const &list)
    {
        constexpr auto user_descriptors = topic_descriptors<ContextT>();
        remote::generated::TopicListResponse response{};
        append_page(response.topics, list, static_cast<const remote::TopicDescriptor *>(nullptr), 0, user_descriptors, [](auto const &descriptor, auto &out) {
            out.id = descriptor.id;
            out.payload_type = descriptor.payload_type;
            out.direction = static_cast<std::uint8_t>(descriptor.direction);
            out.policy = static_cast<std::uint8_t>(descriptor.policy);
            out.version = descriptor.version;
            return out.name.assign(descriptor.name);
        });
        send_response(request, response);
    }

    template <typename ContextT>
    void send_types(remote::Frame const &request, remote::generated::ListRequest const &list)
    {
        constexpr auto user_descriptors = type_descriptors<ContextT>();
        remote::generated::TypeListResponse response{};
        append_page(response.types, list, remote::generated::CoreTypes, CoreTypeCount, user_descriptors, [](auto const &descriptor, auto &out) {
            out.id = descriptor.id;
            out.version = descriptor.version;
            out.max_size = descriptor.max_size;
            return out.name.assign(descriptor.name);
        });
        send_response(request, response);
    }

    template <typename ContextT>
    void send_observables(remote::Frame const &request, remote::generated::ListRequest const &list)
    {
        constexpr auto user_descriptors = observable_descriptors<ContextT>();
        remote::generated::ObservableListResponse response{};
        append_page(response.observables, list, static_cast<const remote::ObservableDescriptor *>(nullptr), 0, user_descriptors, [](auto const &descriptor, auto &out) {
            out.id = descriptor.id;
            out.payload_type = descriptor.payload_type;
            out.mode = static_cast<std::uint8_t>(descriptor.mode);
            out.policy = static_cast<std::uint8_t>(descriptor.policy);
            out.min_interval_ms = descriptor.min_interval_ms;
            out.max_interval_ms = descriptor.max_interval_ms;
            out.version = descriptor.version;
            return out.name.assign(descriptor.name);
        });
        send_response(request, response);
    }

    template <typename SystemT>
    void send_components(remote::Frame const &request, remote::generated::ListRequest const &list)
    {
        remote::generated::ComponentListResponse response{};
        const auto records = SystemT::lifecycle::components();
        if (!records)
        {
            send_error(request.sequence,
                       request.correlation,
                       request.target_id,
                       remote::ErrorCode::InternalError,
                       records.status(),
                       "lifecycle unavailable");
            return;
        }

        const auto &components = records.value();
        const std::size_t begin = std::min<std::size_t>(list.offset, components.size());
        const std::size_t max_count = std::min<std::size_t>(list.limit, response.components.data.size());
        const std::size_t end = std::min<std::size_t>(components.size(), begin + max_count);

        for (std::size_t i = begin; i < end; ++i)
        {
            auto &out = response.components.data[response.components.size++];
            (void)out.name.assign(components[i].component.name);
            (void)out.kind.assign(component_kind_name(components[i].component.kind));
            out.state = static_cast<std::uint8_t>(components[i].state);
        }
        send_response(request, response);
    }

    void send_boot_report(remote::Frame const &request, BootReport const &report)
    {
        remote::generated::BootReportResponse response{};
        response.status = static_cast<std::uint8_t>(report.status);
        if (report.status != Status::Ok)
        {
            response.phase = static_cast<std::uint8_t>(report.failure.phase);
            (void)response.component.assign(report.failure.component.name);
        }
        send_response(request, response);
    }

    template <typename ArrayT, typename ListT, typename DescriptorT, typename UserArrayT, typename FillT>
    void append_page(ArrayT &out,
                     ListT const &list,
                     const DescriptorT *core_descriptors,
                     std::size_t core_count,
                     UserArrayT const &user_descriptors,
                     FillT fill)
    {
        const std::size_t descriptor_count = core_count + user_descriptors.size();
        const std::size_t begin = std::min<std::size_t>(list.offset, descriptor_count);
        const std::size_t max_count = std::min<std::size_t>(list.limit, out.data.size());
        const std::size_t end = std::min<std::size_t>(descriptor_count, begin + max_count);
        for (std::size_t i = begin; i < end; ++i)
        {
            const auto &descriptor = i < core_count ? core_descriptors[i] : user_descriptors[i - core_count];
            if (fill(descriptor, out.data[out.size]))
            {
                ++out.size;
            }
        }
    }

    template <typename T>
    void send_response(remote::Frame const &request, T const &response)
    {
        send_payload(request.sequence, request.correlation, request.target_id, remote::FrameKind::Response, response);
    }

    template <typename T>
    void send_payload(std::uint16_t request_sequence,
                      std::uint16_t correlation,
                      std::uint32_t target_id,
                      remote::FrameKind kind,
                      T const &response)
    {
        std::size_t payload_size = 0;
        if (!remote::encode(response, tx_payload_, sizeof(tx_payload_), payload_size))
        {
            send_error(request_sequence, correlation, target_id, remote::ErrorCode::InternalError, Status::NoMemory, "encode failed");
            return;
        }

        remote::Frame frame{};
        frame.kind = kind;
        frame.sequence = sequence_++;
        frame.correlation = correlation;
        frame.target_id = target_id;
        frame.payload = tx_payload_;
        frame.payload_size = static_cast<std::uint16_t>(payload_size);

        std::size_t frame_size = 0;
        if (remote::encode_frame(frame, tx_frame_, sizeof(tx_frame_), frame_size))
        {
            (void)TransportT::write(tx_frame_, frame_size);
            TransportT::flush();
        }
    }

    void send_error(std::uint16_t request_sequence,
                    std::uint16_t correlation,
                    std::uint32_t target_id,
                    remote::ErrorCode error,
                    Status status,
                    const char *message)
    {
        remote::generated::ErrorResponse response{};
        response.error_code = static_cast<std::uint16_t>(error);
        response.status = static_cast<std::uint8_t>(status);
        response.target_id = target_id;
        (void)response.message.assign(message);
        send_payload(request_sequence, correlation, target_id, remote::FrameKind::Error, response);
    }

    std::uint8_t rx_frame_[FrameBufferBytes]{};
    std::uint8_t rx_payload_[PayloadBytes]{};
    std::uint8_t tx_payload_[PayloadBytes]{};
    std::uint8_t tx_frame_[FrameBufferBytes]{};
    std::size_t rx_size_ = 0;
    std::uint16_t sequence_ = 1;
};

} // namespace solar::services
