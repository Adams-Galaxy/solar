#include <solar/hardware.hpp>

#if SOLAR_FAIL_CASE == 1
using Invalid = decltype(solar::hardware::dt::alias<"not-present">);
static_assert(sizeof(Invalid) != 0);
#elif SOLAR_FAIL_CASE == 2
using Invalid = solar::hardware::gpio::Output<solar::hardware::dt::chosen<"zephyr,console">>;
static_assert(sizeof(Invalid) != 0);
#elif SOLAR_FAIL_CASE == 3
using Invalid = solar::hardware::Endpoint<42>;
static_assert(sizeof(Invalid) != 0);
#elif SOLAR_FAIL_CASE == 4
using Invalid = solar::hardware::uart::Port<solar::hardware::dt::alias<"led0">>;
static_assert(sizeof(Invalid) != 0);
#elif SOLAR_FAIL_CASE == 5
inline constexpr auto UartSpec = solar::hardware::dt::device(
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console)), solar::hardware::EndpointKind::Uart);
using Invalid = solar::hardware::uart::InterruptDriven<UartSpec>;
static_assert(sizeof(Invalid) != 0);
#elif SOLAR_FAIL_CASE == 6
using Invalid = solar::hardware::spi::Endpoint<solar::hardware::dt::alias<"led0">>;
static_assert(sizeof(Invalid) != 0);
#elif SOLAR_FAIL_CASE == 7
inline constexpr auto SpiSpec = solar::hardware::dt::spi(spi_dt_spec{});
using Spi = solar::hardware::spi::Endpoint<SpiSpec>;
using Invalid = Spi::Operation;
static_assert(sizeof(Invalid) != 0);
#elif SOLAR_FAIL_CASE == 8
inline constexpr auto PwmSpec = solar::hardware::dt::pwm(pwm_dt_spec{});
using Invalid = solar::hardware::pwm::Capture<PwmSpec>;
static_assert(sizeof(Invalid) != 0);
#elif SOLAR_FAIL_CASE == 9
inline constexpr auto AdcSpecA = solar::hardware::dt::adc(adc_dt_spec{
    .dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console)),
    .channel_id = 0,
    .resolution = 12,
});
inline constexpr auto AdcSpecB = solar::hardware::dt::adc(adc_dt_spec{
    .dev = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
    .channel_id = 1,
    .resolution = 12,
});
using AdcA = solar::hardware::adc::Channel<AdcSpecA>;
using AdcB = solar::hardware::adc::Channel<AdcSpecB>;
using Invalid = solar::hardware::adc::Sequence<AdcA, AdcB>;
static_assert(sizeof(Invalid) != 0);
#elif SOLAR_FAIL_CASE == 10
inline constexpr auto I2cSpec = solar::hardware::dt::i2c(i2c_dt_spec{});
using I2c = solar::hardware::i2c::Endpoint<I2cSpec>;
using Invalid = I2c::Operation;
static_assert(sizeof(Invalid) != 0);
#else
using Valid = solar::hardware::gpio::Output<solar::hardware::dt::alias<"led0">>;
static_assert(Valid::descriptor().identity.endpoint_kind == solar::hardware::EndpointKind::Gpio);
#endif

int main()
{
    return 0;
}
