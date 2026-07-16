#pragma once

#include <concepts>
#include <type_traits>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/spi.h>

#include "solar/hardware/types.hpp"

namespace solar::hardware::dt
{

template <typename IdentityT> struct NodeDescriptor
{
    IdentityT identity;
    constexpr bool operator==(const NodeDescriptor&) const = default;
};

template <typename IdentityT> struct DeviceDescriptor
{
    const device* native{};
    IdentityT identity;
    constexpr bool operator==(const DeviceDescriptor&) const = default;
};

template <typename IdentityT> struct GpioDescriptor
{
    gpio_dt_spec native{};
    IdentityT identity;
    constexpr bool operator==(const GpioDescriptor&) const = default;
};

template <typename IdentityT> struct SpiDescriptor
{
    spi_dt_spec native{};
    IdentityT identity;
    constexpr bool operator==(const SpiDescriptor&) const = default;
};

template <typename IdentityT> struct I2cDescriptor
{
    i2c_dt_spec native{};
    IdentityT identity;
    constexpr bool operator==(const I2cDescriptor&) const = default;
};

template <typename IdentityT> struct AdcDescriptor
{
    adc_dt_spec native{};
    IdentityT identity;
    constexpr bool operator==(const AdcDescriptor&) const = default;
};

template <typename IdentityT> struct PwmDescriptor
{
    pwm_dt_spec native{};
    IdentityT identity;
    constexpr bool operator==(const PwmDescriptor&) const = default;
};

template <typename IdentityT>
[[nodiscard]] consteval auto node(IdentityT identity_value)
{
    return NodeDescriptor<IdentityT>{.identity = identity_value};
}

template <typename IdentityT>
[[nodiscard]] consteval auto device(const ::device* native, IdentityT identity_value)
{
    return DeviceDescriptor<IdentityT>{.native = native, .identity = identity_value};
}

template <typename IdentityT>
[[nodiscard]] consteval auto gpio(gpio_dt_spec native, IdentityT identity_value)
{
    return GpioDescriptor<IdentityT>{.native = native, .identity = identity_value};
}

template <typename IdentityT>
[[nodiscard]] consteval auto spi(spi_dt_spec native, IdentityT identity_value)
{
    return SpiDescriptor<IdentityT>{.native = native, .identity = identity_value};
}

template <typename IdentityT>
[[nodiscard]] consteval auto i2c(i2c_dt_spec native, IdentityT identity_value)
{
    return I2cDescriptor<IdentityT>{.native = native, .identity = identity_value};
}

template <typename IdentityT>
[[nodiscard]] consteval auto adc(adc_dt_spec native, IdentityT identity_value)
{
    return AdcDescriptor<IdentityT>{.native = native, .identity = identity_value};
}

template <typename IdentityT>
[[nodiscard]] consteval auto pwm(pwm_dt_spec native, IdentityT identity_value)
{
    return PwmDescriptor<IdentityT>{.native = native, .identity = identity_value};
}

[[nodiscard]] consteval auto gpio(gpio_dt_spec native)
{
    return gpio(native, hardware::identity(SelectorKind::Explicit, "", "", "", 0,
                                           EndpointKind::Gpio,
                                           Capability::Metadata | Capability::Ready |
                                               Capability::NativeHandle | Capability::Read |
                                               Capability::Write | Capability::Interrupt,
                                           true));
}

[[nodiscard]] consteval auto device(const ::device* native, EndpointKind kind)
{
    return device(native, hardware::identity(SelectorKind::Explicit, "", "", "", 0, kind,
                                             Capability::Metadata | Capability::Ready |
                                                 Capability::NativeHandle,
                                             true));
}

[[nodiscard]] consteval auto spi(spi_dt_spec native)
{
    return spi(native, hardware::identity(SelectorKind::Explicit, "", "", "", 0,
                                          EndpointKind::Spi,
                                          Capability::Metadata | Capability::Ready |
                                              Capability::NativeHandle | Capability::Read |
                                              Capability::Write | Capability::Async,
                                          true));
}

[[nodiscard]] consteval auto i2c(i2c_dt_spec native)
{
    return i2c(native, hardware::identity(SelectorKind::Explicit, "", "", "", 0,
                                          EndpointKind::I2c,
                                          Capability::Metadata | Capability::Ready |
                                              Capability::NativeHandle | Capability::Read |
                                              Capability::Write | Capability::Async,
                                          true));
}

[[nodiscard]] consteval auto adc(adc_dt_spec native)
{
    return adc(native, hardware::identity(SelectorKind::Explicit, "", "", "", 0,
                                          EndpointKind::Adc,
                                          Capability::Metadata | Capability::Ready |
                                              Capability::NativeHandle | Capability::Read |
                                              Capability::Async,
                                          true));
}

[[nodiscard]] consteval auto pwm(pwm_dt_spec native)
{
    return pwm(native, hardware::identity(SelectorKind::Explicit, "", "", "", 0,
                                          EndpointKind::Pwm,
                                          Capability::Metadata | Capability::Ready |
                                              Capability::NativeHandle | Capability::Read |
                                              Capability::Write | Capability::Async,
                                          true));
}

template <typename T> struct IsGpioDescriptor : std::false_type
{};

template <typename IdentityT>
struct IsGpioDescriptor<GpioDescriptor<IdentityT>> : std::true_type
{};

template <typename T> struct IsDeviceDescriptor : std::false_type
{};

template <typename IdentityT>
struct IsDeviceDescriptor<DeviceDescriptor<IdentityT>> : std::true_type
{};

template <typename T> struct IsSpiDescriptor : std::false_type
{};

template <typename IdentityT>
struct IsSpiDescriptor<SpiDescriptor<IdentityT>> : std::true_type
{};

template <typename T> struct IsI2cDescriptor : std::false_type
{};

template <typename IdentityT>
struct IsI2cDescriptor<I2cDescriptor<IdentityT>> : std::true_type
{};

template <typename T> struct IsAdcDescriptor : std::false_type
{};

template <typename IdentityT>
struct IsAdcDescriptor<AdcDescriptor<IdentityT>> : std::true_type
{};

template <typename T> struct IsPwmDescriptor : std::false_type
{};

template <typename IdentityT>
struct IsPwmDescriptor<PwmDescriptor<IdentityT>> : std::true_type
{};

template <typename T>
inline constexpr bool is_gpio_descriptor_v = IsGpioDescriptor<std::remove_cv_t<T>>::value;

template <typename T>
inline constexpr bool is_device_descriptor_v = IsDeviceDescriptor<std::remove_cv_t<T>>::value;

template <typename T>
inline constexpr bool is_spi_descriptor_v = IsSpiDescriptor<std::remove_cv_t<T>>::value;

template <typename T>
inline constexpr bool is_i2c_descriptor_v = IsI2cDescriptor<std::remove_cv_t<T>>::value;

template <typename T>
inline constexpr bool is_adc_descriptor_v = IsAdcDescriptor<std::remove_cv_t<T>>::value;

template <typename T>
inline constexpr bool is_pwm_descriptor_v = IsPwmDescriptor<std::remove_cv_t<T>>::value;

template <typename T>
concept Descriptor = requires(const T& value) {
    value.identity;
    { value.identity.stable_id } -> std::convertible_to<std::uint32_t>;
};

template <typename T>
concept GpioDescriptorType = Descriptor<T> && is_gpio_descriptor_v<T>;

template <typename T>
concept DeviceDescriptorType = Descriptor<T> && is_device_descriptor_v<T>;

template <typename T>
concept SpiDescriptorType = Descriptor<T> && is_spi_descriptor_v<T>;

template <typename T>
concept I2cDescriptorType = Descriptor<T> && is_i2c_descriptor_v<T>;

template <typename T>
concept AdcDescriptorType = Descriptor<T> && is_adc_descriptor_v<T>;

template <typename T>
concept PwmDescriptorType = Descriptor<T> && is_pwm_descriptor_v<T>;

namespace generated
{

template <FixedString Name> inline constexpr bool resolved = false;

template <FixedString Name> struct Alias
{
    static_assert(resolved<Name>,
                  "SOLAR_DIAGNOSTIC_HARDWARE_ALIAS_NOT_GENERATED: alias is absent from the "
                  "resolved devicetree or generation is disabled");
};

template <FixedString Name> struct Chosen
{
    static_assert(resolved<Name>,
                  "SOLAR_DIAGNOSTIC_HARDWARE_CHOSEN_NOT_GENERATED: chosen node is absent from "
                  "the resolved devicetree or generation is disabled");
};

template <FixedString Name> struct NodeLabel
{
    static_assert(resolved<Name>,
                  "SOLAR_DIAGNOSTIC_HARDWARE_NODELABEL_NOT_GENERATED: node label is absent from "
                  "the resolved devicetree or generation is disabled");
};

} // namespace generated

template <FixedString Name> inline constexpr auto alias = generated::Alias<Name>::value;
template <FixedString Name> inline constexpr auto chosen = generated::Chosen<Name>::value;
template <FixedString Name> inline constexpr auto node_label = generated::NodeLabel<Name>::value;

} // namespace solar::hardware::dt

#if defined(CONFIG_SOLAR_HARDWARE_GENERATE_DEVICETREE)
#include <solar/hardware/generated/devicetree.hpp>
#endif
