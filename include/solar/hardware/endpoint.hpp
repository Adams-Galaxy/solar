#pragma once

#include <zephyr/device.h>

#include "solar/hardware/dt.hpp"
#include "solar/hardware/error.hpp"

namespace solar::hardware
{

template <auto Spec> struct Endpoint
{
    static_assert(dt::Descriptor<decltype(Spec)>,
                  "SOLAR_DIAGNOSTIC_INVALID_HARDWARE_ENDPOINT: endpoint requires a structural "
                  "Solar devicetree descriptor");

    inline static constexpr auto descriptor_value = Spec;

    [[nodiscard]] static constexpr const auto& descriptor() noexcept
    {
        return descriptor_value;
    }

    [[nodiscard]] static constexpr std::string_view path() noexcept
    {
        return descriptor_value.identity.path.view();
    }

    [[nodiscard]] static bool ready() noexcept
    {
        if constexpr (dt::GpioDescriptorType<decltype(Spec)>) {
            return gpio_is_ready_dt(&descriptor_value.native);
        } else if constexpr (dt::SpiDescriptorType<decltype(Spec)>) {
            return spi_is_ready_dt(&descriptor_value.native);
        } else if constexpr (dt::I2cDescriptorType<decltype(Spec)>) {
            return i2c_is_ready_dt(&descriptor_value.native);
        } else if constexpr (dt::AdcDescriptorType<decltype(Spec)>) {
            return adc_is_ready_dt(&descriptor_value.native);
        } else if constexpr (dt::PwmDescriptorType<decltype(Spec)>) {
            return pwm_is_ready_dt(&descriptor_value.native);
        } else if constexpr (requires { descriptor_value.native; }) {
            return device_is_ready(descriptor_value.native);
        } else {
            return false;
        }
    }

    [[nodiscard]] static Result<void, Error> require_ready() noexcept
    {
        if constexpr (!requires { descriptor_value.native; }) {
            return fail<Error>({.status = solar::Status::NotSupported,
                                .reason = Reason::Unsupported,
                                .operation = Operation::RequireReady,
                                .native = -ENOTSUP,
                                .endpoint = path()});
        } else if (!ready()) {
            return fail<Error>({.status = solar::Status::NotReady,
                                .reason = Reason::NotReady,
                                .operation = Operation::RequireReady,
                                .native = -ENODEV,
                                .endpoint = path()});
        }
        return {};
    }

    [[nodiscard]] static constexpr const auto& native_handle() noexcept
        requires requires { descriptor_value.native; }
    {
        return descriptor_value.native;
    }

    [[nodiscard]] static constexpr const device* native_device() noexcept
        requires requires { descriptor_value.native; }
    {
        if constexpr (dt::GpioDescriptorType<decltype(Spec)>) {
            return descriptor_value.native.port;
        } else if constexpr (dt::SpiDescriptorType<decltype(Spec)> ||
                             dt::I2cDescriptorType<decltype(Spec)>) {
            return descriptor_value.native.bus;
        } else if constexpr (dt::AdcDescriptorType<decltype(Spec)> ||
                             dt::PwmDescriptorType<decltype(Spec)>) {
            return descriptor_value.native.dev;
        } else {
            return descriptor_value.native;
        }
    }
};

} // namespace solar::hardware
