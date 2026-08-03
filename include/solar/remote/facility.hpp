#pragma once

#include <atomic>
#include <cstddef>
#include <span>

#include "solar/component.hpp"
#include "solar/core/type_list.hpp"
#include "solar/remote/link.hpp"
#include "solar/remote/protocol.hpp"

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
template <typename Architecture, typename RuntimeContext = void> struct Service;

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
template <typename System> void pong_responded() noexcept;
template <typename System>
[[nodiscard]] protocol::IntrospectionSummary introspection_summary() noexcept;
template <typename System> [[nodiscard]] protocol::ServerInformation server_information() noexcept;
template <typename System>
[[nodiscard]] Result<std::size_t, Error> manifest_chunk(std::span<const std::byte> request,
                                                        std::span<std::byte> output) noexcept;
template <typename Entries> struct DeclarationsOf;

template <typename... Entries> struct DeclarationsOf<TypeList<Entries...>>
{
    using type = TypeList<typename Entries::Declaration...>;
};

template <typename Entries> using declarations_of_t = typename DeclarationsOf<Entries>::type;

} // namespace detail

/** Default scheduler for Remote work; applications may supply an explicit adapter. */
struct InlineScheduler
{
    template <typename Registration> [[nodiscard]] static Result<void> submit() noexcept
    {
        return Registration::BehaviorType::execute();
    }
};

template <typename SchemasT, typename DataT, typename ActionsT, typename TopicsT, typename StreamsT,
          typename LinksT, typename ComponentsT, typename ConfigurationT,
          typename SchedulerT = InlineScheduler>
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
    using Scheduler = SchedulerT;

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
    [[nodiscard]] static Result<void> init() noexcept
    {
        accepting.store(false, std::memory_order_relaxed);
        active_requests.store(0, std::memory_order_relaxed);
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
        return {};
    }
};

} // namespace solar::remote
