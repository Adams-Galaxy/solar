#pragma once

#include <cstddef>
#include <utility>

#include "solar/catalog/catalog.hpp"
#include "solar/core/type_list.hpp"
#include "solar/remote/catalog.hpp"
#include "solar/remote/facility.hpp"
#include "solar/remote/service.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
#include <atomic>
#include <chrono>

#include "solar/kernel/priority.hpp"
#include "solar/kernel/stop.hpp"
#include "solar/kernel/thread.hpp"
#include "solar/remote/runtime.hpp"
#endif

namespace solar::remote
{
namespace runtime_detail
{

template <typename Tag, typename Declarations> struct CatalogFrom;

template <typename Tag, typename... Declarations> struct CatalogFrom<Tag, TypeList<Declarations...>>
{
  private:
    template <std::size_t... Indices>
    static auto make(std::index_sequence<Indices...>)
        -> Catalog<Tag,
                   CatalogEntry<Tag, Declarations, ApplicationOwner, origin::Direct, Indices>...>;

  public:
    using type = decltype(make(std::index_sequence_for<Declarations...>{}));
};

template <typename Tag, typename Declarations>
using catalog_from_t = typename CatalogFrom<Tag, Declarations>::type;

} // namespace runtime_detail

/**
 * System-independent context required by the bounded Remote byte engine.
 *
 * The existing framing implementation is intentionally reused, but catalogs,
 * state, links, and lifecycle now belong to this Remote module rather than a
 * synthetic application System.
 */
template <typename ArchitectureT, typename PollDispatchT = void> struct RuntimeContext
{
    static constexpr bool standalone_byte_runtime = true;
    using RemoteArchitecture = ArchitectureT;
    /// The plain, non-Remote `system::Dispatch<Contract, Components>` used to
    /// pull a value from a `direction: out` Stream's `Output<Endpoint,
    /// Publisher>` binding on the scheduler's poll tick. `void` (the
    /// default) when no such Dispatch is available -- e.g. host tests
    /// exercising this Context directly, without a generated Application.
    using RemotePollDispatch = PollDispatchT;
    using RemoteSchemaCatalog =
        runtime_detail::catalog_from_t<SchemaTag, typename ArchitectureT::Schemas>;
    using RemoteDataCatalog = runtime_detail::catalog_from_t<DataTag, typename ArchitectureT::Data>;
    using RemoteActionCatalog =
        runtime_detail::catalog_from_t<ActionTag, typename ArchitectureT::Actions>;
    using RemoteTopicCatalog =
        runtime_detail::catalog_from_t<TopicTag, typename ArchitectureT::Topics>;
    using RemoteStreamCatalog =
        runtime_detail::catalog_from_t<StreamTag, typename ArchitectureT::Streams>;
    using RemoteLinkCatalog =
        runtime_detail::catalog_from_t<LinkTag, typename ArchitectureT::Links>;
    using RemoteFacility = Facility<ArchitectureT>;
    using RemoteService = Service<ArchitectureT, RuntimeContext<ArchitectureT, PollDispatchT>>;

    template <typename Owner, typename Key, typename State> struct StateSlot
    {
        inline static State value{};
    };
};

/**
 * Owns a Remote byte engine, its links, and (on Zephyr) its worker thread.
 * It can be placed directly in `Own<...>` and has no application binding.
 */
template <typename Application, typename ArchitectureT, typename DependenciesT = TypeList<>,
          typename PollDispatchT = void>
struct ByteRuntime
{
    using Context = RuntimeContext<ArchitectureT, PollDispatchT>;
    using FacilityType = typename Context::RemoteFacility;
    using ServiceType = typename Context::RemoteService;
    using Dependencies = DependenciesT;

    [[nodiscard]] static Result<void> initialize()
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        run_status_.store(Status::NotReady, std::memory_order_relaxed);
        if (auto result = stop_source_.reset(); !result) {
            return result;
        }
        if (auto result = FacilityType::init(); !result) {
            return result;
        }
        if (auto result = ServiceType::init(); !result) {
            (void)FacilityType::deinit();
            return result;
        }
        detail::initialize_in_stream_runtime<Context>();
        auto prepared = thread_.prepare(
            &thread_entry, nullptr,
            kernel::ThreadConfiguration{
                .priority =
                    kernel::Priority::template preemptive<CONFIG_SOLAR_REMOTE_SERVICE_PRIORITY>(),
                .name = nullptr,
            });
        if (!prepared) {
            (void)ServiceType::deinit();
            (void)FacilityType::deinit();
        }
        return prepared;
#else
        return {};
#endif
    }

    [[nodiscard]] static Result<void> start()
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        if (auto result = FacilityType::start(); !result) {
            return result;
        }
        if (auto result = ServiceType::start(); !result) {
            (void)FacilityType::stop();
            return result;
        }
        if (auto result = thread_.start(); !result) {
            (void)ServiceType::stop();
            (void)FacilityType::stop();
            return result;
        }
#endif
        return {};
    }

    [[nodiscard]] static Result<void> stop()
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        auto requested = stop_source_.request_stop();
        ServiceType::notify_stop();
        auto service = ServiceType::stop();
        auto joined = thread_.join(kernel::Timeout::after(std::chrono::seconds{2}));
        auto facility = FacilityType::stop();
        if (!requested && status_of(requested.error()) != Status::Already) {
            return fail<solar::Error>(requested.error());
        }
        if (!service)
            return service;
        if (!joined)
            return joined;
        if (!facility)
            return facility;
        const auto status = run_status_.load(std::memory_order_acquire);
        if (status != Status::Ok)
            return fail<solar::Error>({.status = status});
#endif
        return {};
    }

    [[nodiscard]] static Result<void> deinitialize()
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        auto service = ServiceType::deinit();
        auto facility = FacilityType::deinit();
        return service ? facility : service;
#else
        return {};
#endif
    }

    /** Publish one producer-paced value to subscribers of a genuine Stream endpoint. */
    template <typename StreamT>
    [[nodiscard]] static Result<WriteReceipt, Error> publish(typename StreamT::Value value)
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        static_assert(Context::RemoteStreamCatalog::template contains<StreamT>,
                      "SOLAR_REMOTE_STREAM_NOT_DECLARED: published Stream is absent from the "
                      "Remote architecture");
        return detail::publish_stream<Context, StreamT>(std::move(value));
#else
        (void)value;
        return fail<Error>({Status::NotReady, Reason::NotReady, Operation::Publish});
#endif
    }

    template <typename StreamT> [[nodiscard]] static bool interested()
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        static_assert(Context::RemoteStreamCatalog::template contains<StreamT>);
        return detail::stream_state<Context, StreamT>().interested_sessions.load(
                   std::memory_order_acquire) != 0;
#else
        return false;
#endif
    }

  private:
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
    static void thread_entry(void*) noexcept
    {
        const auto result = ServiceType::run(stop_source_.token());
        run_status_.store(result ? Status::Ok : status_of(result.error()),
                          std::memory_order_release);
    }

    inline static kernel::Thread<CONFIG_SOLAR_REMOTE_SERVICE_STACK_SIZE> thread_{};
    inline static kernel::StopSource stop_source_{};
    inline static std::atomic<Status> run_status_{Status::NotReady};
#endif
};

} // namespace solar::remote
