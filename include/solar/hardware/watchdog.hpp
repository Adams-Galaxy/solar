#pragma once

#include <chrono>
#include <cstdint>
#include <limits>

#include <zephyr/drivers/watchdog.h>

#include "solar/hardware/endpoint.hpp"

namespace solar::hardware::watchdog
{

inline constexpr bool available =
#if defined(CONFIG_SOLAR_HARDWARE_WATCHDOG)
    true;
#else
    false;
#endif

struct Timeout
{
    wdt_timeout_cfg native{};
    bool valid{};

    template <typename MinRep, typename MinPeriod, typename MaxRep, typename MaxPeriod>
    [[nodiscard]] static constexpr Timeout window(std::chrono::duration<MinRep, MinPeriod> minimum,
                                                  std::chrono::duration<MaxRep, MaxPeriod> maximum,
                                                  wdt_callback_t callback = nullptr,
                                                  std::uint8_t flags = WDT_FLAG_RESET_SOC) noexcept
    {
        const auto min_ms = std::chrono::duration_cast<std::chrono::milliseconds>(minimum).count();
        const auto max_ms = std::chrono::duration_cast<std::chrono::milliseconds>(maximum).count();
        const auto in_range = min_ms >= 0 && max_ms > 0 && min_ms <= max_ms &&
                              static_cast<std::uint64_t>(max_ms) <= UINT32_MAX;
        return Timeout{.native =
                           {
                               .window = {.min = static_cast<std::uint32_t>(min_ms),
                                          .max = static_cast<std::uint32_t>(max_ms)},
                               .callback = callback,
#if defined(CONFIG_WDT_MULTISTAGE)
                               .next = nullptr,
#endif
                               .flags = flags,
                           },
                       .valid = in_range};
    }
};

template <auto Spec> class Channel
{
  public:
    Channel() = delete;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    constexpr Channel(Channel&& other) noexcept : id_(other.id_)
    {
        other.id_ = -1;
    }
    constexpr Channel& operator=(Channel&& other) noexcept
    {
        if (this != &other) {
            id_ = other.id_;
            other.id_ = -1;
        }
        return *this;
    }

    [[nodiscard]] constexpr int id() const noexcept
    {
        return id_;
    }

    [[nodiscard]] Result<void, Error> feed() const noexcept
    {
        if (id_ < 0) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = Operation::Feed,
                              .native = -EINVAL,
                              .endpoint = Spec.identity.path.view()});
        }
        return hardware::detail::native_result(wdt_feed(Spec.native, id_), Operation::Feed,
                                               Spec.identity.path.view());
    }

    [[nodiscard]] static constexpr const device* native_device() noexcept
    {
        return Spec.native;
    }

  private:
    explicit constexpr Channel(int id) noexcept : id_(id) {}
    int id_{-1};

    template <auto> friend struct Device;
};

template <auto Spec> struct Device : hardware::Endpoint<Spec>
{
    static_assert(available,
                  "SOLAR_DIAGNOSTIC_HARDWARE_WATCHDOG_DISABLED: Watchdog wrappers require "
                  "CONFIG_SOLAR_HARDWARE_WATCHDOG");
    static_assert(dt::DeviceDescriptorType<decltype(Spec)> &&
                      Spec.identity.endpoint_kind == EndpointKind::Watchdog,
                  "SOLAR_DIAGNOSTIC_HARDWARE_WATCHDOG_DESCRIPTOR_REQUIRED: Watchdog Device "
                  "requires a Watchdog device descriptor");

    using Base = hardware::Endpoint<Spec>;

    [[nodiscard]] static Result<Channel<Spec>, Error> install(const Timeout& timeout) noexcept
    {
        if (!timeout.valid || timeout.native.window.max == 0U ||
            timeout.native.window.min > timeout.native.window.max) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = Operation::Install,
                              .native = -EINVAL,
                              .endpoint = Base::path()});
        }
        const auto result = wdt_install_timeout(Base::native_device(), &timeout.native);
        if (result < 0) {
            return fail(hardware::detail::native_error(result, Operation::Install, Base::path()));
        }
        return Channel<Spec>{result};
    }

    [[nodiscard]] static Result<void, Error> setup(std::uint8_t options = 0) noexcept
    {
        return hardware::detail::native_result(wdt_setup(Base::native_device(), options),
                                               Operation::Configure, Base::path());
    }

    [[nodiscard]] static Result<void, Error> disable() noexcept
    {
        return hardware::detail::native_result(wdt_disable(Base::native_device()),
                                               Operation::Disable, Base::path());
    }
};

} // namespace solar::hardware::watchdog
