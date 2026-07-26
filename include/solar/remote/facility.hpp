#pragma once

#include <atomic>
#include <cstddef>
#include <span>

#include "solar/component.hpp"
#include "solar/core/type_list.hpp"
#include "solar/remote/link.hpp"
#include "solar/remote/protocol.hpp"
#include "solar/system/sections.hpp"

namespace solar::remote
{

namespace frame
{
struct Decoded;
}

namespace protocol
{
struct IntrospectionSummary;
struct ServerInformation;
} // namespace protocol

template <typename Architecture> struct Facility;
template <typename Architecture> struct Service;

namespace detail
{

template <typename System> void process_publication(std::uint16_t endpoint) noexcept;
template <typename System>
void process_application_frame(std::uint16_t link, const frame::Decoded& decoded) noexcept;
template <typename System>
[[nodiscard]] Result<void> process_action_work(std::uint32_t target, bool action) noexcept;
template <typename System>
[[nodiscard]] Result<void> process_poll_work(std::uint32_t target) noexcept;
template <typename System>
[[nodiscard]] Result<void> process_in_stream_work(std::uint32_t target) noexcept;
template <typename System> void initialize_in_stream_runtime() noexcept;
template <typename System> [[nodiscard]] std::int64_t process_poll_releases() noexcept;
template <typename System>
void reset_session(std::uint16_t link, InStreamCloseReason reason) noexcept;
template <typename System> void open_session(std::uint16_t link) noexcept;
template <typename System>
[[nodiscard]] protocol::IntrospectionSummary introspection_summary() noexcept;
template <typename System> [[nodiscard]] protocol::ServerInformation server_information() noexcept;
template <typename System>
[[nodiscard]] Result<std::size_t, Error> manifest_chunk(std::span<const std::byte> request,
                                                        std::span<std::byte> output) noexcept;
template <typename System>
[[nodiscard]] Result<std::size_t, Error>
inspection_collections(std::span<const std::byte> request, std::span<std::byte> output) noexcept;
template <typename System>
[[nodiscard]] Result<std::size_t, Error> inspection_query(std::span<const std::byte> request,
                                                          std::span<std::byte> output) noexcept;

template <typename Entries> struct DeclarationsOf;

template <typename... Entries> struct DeclarationsOf<TypeList<Entries...>>
{
    using type = TypeList<typename Entries::Declaration...>;
};

template <typename Entries> using declarations_of_t = typename DeclarationsOf<Entries>::type;

} // namespace detail

template <typename SchemasT, typename DataT, typename ActionsT, typename TopicsT, typename StreamsT,
          typename LinksT, typename ComponentsT, typename ConfigurationT>
struct Architecture
{
    using Schemas = SchemasT;
    using Data = DataT;
    using Actions = ActionsT;
    using Topics = TopicsT;
    using Streams = StreamsT;
    using Links = LinksT;
    using ComponentTypes = ComponentsT;
    using ConfigurationPolicies = ConfigurationT;

    static constexpr bool demanded = list_size_v<Links> != 0;
    static_assert([]<typename... LinkTypes>(
                      TypeList<LinkTypes...>) { return (remote::Link<LinkTypes> && ...); }(Links{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_INVALID_LINK: RemoteLinks entries must implement the "
                  "asynchronous link contract");
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    using Links = typename Architecture::Links;

    static constexpr component::Descriptor descriptor{
        .name = "solar.remote",
        .description = "Remote semantic facility",
    };

    inline static std::atomic_bool ready{};
    inline static std::atomic_bool accepting{};
    inline static std::atomic_uint32_t active_requests{};
    using ProcessPublication = void (*)(std::uint16_t) noexcept;
    using ProcessApplicationFrame = void (*)(std::uint16_t, const frame::Decoded&) noexcept;
    using ProcessActionWork = Result<void> (*)(std::uint32_t, bool) noexcept;
    using ProcessPollWork = Result<void> (*)(std::uint32_t) noexcept;
    using ProcessInStreamWork = Result<void> (*)(std::uint32_t) noexcept;
    using ProcessPollReleases = std::int64_t (*)() noexcept;
    using ResetSession = void (*)(std::uint16_t, InStreamCloseReason) noexcept;
    using OpenSession = void (*)(std::uint16_t) noexcept;
    using IntrospectionSummary = protocol::IntrospectionSummary (*)() noexcept;
    using ServerInformation = protocol::ServerInformation (*)() noexcept;
    using InspectionCollections = Result<std::size_t, Error> (*)(std::span<const std::byte>,
                                                                 std::span<std::byte>) noexcept;
    using InspectionQuery = InspectionCollections;
    inline static ProcessPublication process_publication{};
    inline static ProcessApplicationFrame process_application_frame{};
    inline static ProcessActionWork process_action_work{};
    inline static ProcessPollWork process_poll_work{};
    inline static ProcessInStreamWork process_in_stream_work{};
    inline static ProcessPollReleases process_poll_releases{};
    inline static ResetSession reset_session{};
    inline static OpenSession open_session{};
    inline static IntrospectionSummary introspection_summary{};
    inline static ServerInformation server_information{};
    inline static InspectionCollections manifest_chunk{};
    inline static InspectionCollections inspection_collections{};
    inline static InspectionQuery inspection_query{};

    [[nodiscard]] static Result<void> init() noexcept
    {
        accepting.store(false, std::memory_order_relaxed);
        active_requests.store(0, std::memory_order_relaxed);
        process_publication = nullptr;
        process_application_frame = nullptr;
        process_action_work = nullptr;
        process_poll_work = nullptr;
        process_in_stream_work = nullptr;
        process_poll_releases = nullptr;
        reset_session = nullptr;
        open_session = nullptr;
        introspection_summary = nullptr;
        server_information = nullptr;
        manifest_chunk = nullptr;
        inspection_collections = nullptr;
        inspection_query = nullptr;
        ready.store(true, std::memory_order_release);
        return {};
    }

    [[nodiscard]] static Result<void> start() noexcept
    {
        accepting.store(true, std::memory_order_release);
        return {};
    }

    [[nodiscard]] static Result<void> stop() noexcept
    {
        accepting.store(false, std::memory_order_release);
        return {};
    }

    [[nodiscard]] static Result<void> deinit() noexcept
    {
        ready.store(false, std::memory_order_release);
        process_publication = nullptr;
        process_application_frame = nullptr;
        process_action_work = nullptr;
        process_poll_work = nullptr;
        process_in_stream_work = nullptr;
        process_poll_releases = nullptr;
        reset_session = nullptr;
        open_session = nullptr;
        introspection_summary = nullptr;
        server_information = nullptr;
        manifest_chunk = nullptr;
        inspection_collections = nullptr;
        inspection_query = nullptr;
        return {};
    }

    template <typename System> static void activate_runtime() noexcept
    {
        detail::initialize_in_stream_runtime<System>();
        process_publication = &detail::process_publication<System>;
        process_application_frame = &detail::process_application_frame<System>;
        process_action_work = &detail::process_action_work<System>;
        process_poll_work = &detail::process_poll_work<System>;
        process_in_stream_work = &detail::process_in_stream_work<System>;
        process_poll_releases = &detail::process_poll_releases<System>;
        reset_session = &detail::reset_session<System>;
        open_session = &detail::open_session<System>;
        introspection_summary = &detail::introspection_summary<System>;
        server_information = &detail::server_information<System>;
        manifest_chunk = &detail::manifest_chunk<System>;
#if defined(CONFIG_SOLAR_INSPECTION_REMOTE)
        inspection_collections = &detail::inspection_collections<System>;
        inspection_query = &detail::inspection_query<System>;
#endif
    }
};

} // namespace solar::remote

template <typename Architecture> struct solar::builtin_traits<solar::remote::Facility<Architecture>>
{
    static constexpr bool enabled = solar::remote::available;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<>;

    template <typename> static constexpr bool demanded = Architecture::demanded;
};
