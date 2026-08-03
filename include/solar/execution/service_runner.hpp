#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

#if defined(__ZEPHYR__)
#include "solar/kernel/priority.hpp"
#include "solar/kernel/stop.hpp"
#include "solar/kernel/thread.hpp"
#endif

namespace solar::execution
{

/**
 * Owns the execution lifecycle for one static service with `run(StopToken)`.
 *
 * Service behavior stays an ordinary application type; the Zephyr thread,
 * cancellation source, join deadline, and error propagation live here.
 */
template <typename Application, typename Service, std::size_t StackBytes, auto PriorityValue,
          typename DependenciesT = TypeList<>>
struct ServiceRunner
{
    static_assert(StackBytes > 0);
    using Dependencies = DependenciesT;
    static constexpr std::string_view name = [] {
        if constexpr (requires { Service::name; }) {
            return std::string_view{Service::name};
        }
        return std::string_view{};
    }();

    [[nodiscard]] static Result<void> initialize() noexcept
    {
        if constexpr (requires {
                          { Service::initialize() } -> std::same_as<Result<void>>;
                      }) {
            if (auto result = Service::initialize(); !result) {
                return result;
            }
        }
#if defined(__ZEPHYR__)
        run_status_.store(Status::NotReady, std::memory_order_relaxed);
        if (auto result = stop_source_.reset(); !result) {
            return result;
        }
        auto prepared = thread_.prepare(
            &entry, nullptr,
            kernel::ThreadConfiguration{.priority = configured_priority(), .name = nullptr});
        if (!prepared) {
            if constexpr (requires {
                              { Service::deinitialize() } -> std::same_as<Result<void>>;
                          }) {
                (void)Service::deinitialize();
            }
        }
        return prepared;
#else
        return {};
#endif
    }

    [[nodiscard]] static Result<void> start() noexcept
    {
#if defined(__ZEPHYR__)
        return thread_.start();
#else
        return {};
#endif
    }

    [[nodiscard]] static Result<void> stop() noexcept
    {
#if defined(__ZEPHYR__)
        auto requested = stop_source_.request_stop();
        auto joined = thread_.join(kernel::Timeout::after(std::chrono::seconds{2}));
        if (!requested && status_of(requested.error()) != Status::Already) {
            return fail<solar::Error>(requested.error());
        }
        if (!joined) {
            return joined;
        }
        const auto status = run_status_.load(std::memory_order_acquire);
        if (status != Status::Ok) {
            return fail<solar::Error>({.status = status});
        }
#endif
        return {};
    }

    [[nodiscard]] static Result<void> deinitialize() noexcept
    {
        if constexpr (requires {
                          { Service::deinitialize() } -> std::same_as<Result<void>>;
                      }) {
            return Service::deinitialize();
        }
        return {};
    }

  private:
#if defined(__ZEPHYR__)
    [[nodiscard]] static consteval kernel::Priority configured_priority()
    {
        using Value = std::remove_cv_t<decltype(PriorityValue)>;
        if constexpr (std::is_same_v<Value, kernel::PriorityLevel>) {
            return kernel::Priority::template semantic<PriorityValue>();
        } else {
            return kernel::Priority::template preemptive<static_cast<std::uint32_t>(
                PriorityValue)>();
        }
    }

    static void entry(void*) noexcept
    {
        const auto result = Service::run(stop_source_.token());
        run_status_.store(result ? Status::Ok : status_of(result.error()),
                          std::memory_order_release);
    }

    inline static kernel::Thread<StackBytes> thread_{};
    inline static kernel::StopSource stop_source_{};
    inline static std::atomic<Status> run_status_{Status::NotReady};
#endif
};

} // namespace solar::execution
