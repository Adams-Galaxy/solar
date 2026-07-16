#include <array>
#include <chrono>
#include <concepts>

#include <solar/hardware.hpp>

using Pwm = solar::hardware::pwm::Output<solar::hardware::dt::alias<"solar-pwm">>;
using PwmCapture = solar::hardware::pwm::Capture<solar::hardware::dt::alias<"solar-pwm">>;
using Spi = solar::hardware::spi::Endpoint<solar::hardware::dt::alias<"solar-spi">>;
using I2c = solar::hardware::i2c::Endpoint<solar::hardware::dt::alias<"solar-i2c">>;
using Watchdog = solar::hardware::watchdog::Device<solar::hardware::dt::alias<"solar-watchdog">>;
using Console =
    solar::hardware::uart::InterruptDriven<solar::hardware::dt::chosen<"zephyr,console">>;

static_assert(
    solar::hardware::dt::PwmDescriptorType<decltype(solar::hardware::dt::alias<"solar-pwm">)>);
static_assert(Pwm::descriptor().native.period == 20000);
static_assert(sizeof(PwmCapture) > 0);
static_assert(solar::hardware::pwm::DutyCycle::percent(101).parts_per_million > 1'000'000);
static_assert(
    solar::hardware::dt::SpiDescriptorType<decltype(solar::hardware::dt::alias<"solar-spi">)>);
static_assert(
    solar::hardware::dt::I2cDescriptorType<decltype(solar::hardware::dt::alias<"solar-i2c">)>);
static_assert(Spi::descriptor().native.config.frequency == 1'000'000);
static_assert(I2c::address() == 0x52);
static_assert(sizeof(Spi::Operation) > 0);
static_assert(Watchdog::descriptor().identity.endpoint_kind ==
              solar::hardware::EndpointKind::Watchdog);
static_assert(Console::descriptor().identity.endpoint_kind == solar::hardware::EndpointKind::Uart);
static_assert(!std::copy_constructible<
              solar::hardware::watchdog::Channel<solar::hardware::dt::alias<"solar-watchdog">>>);

[[maybe_unused]] solar::Result<void, solar::hardware::Error> exercise_pwm()
{
    using namespace std::chrono_literals;
    return Pwm::set(20us, 5us);
}

[[maybe_unused]] solar::Result<void, solar::hardware::Error> exercise_buses()
{
    std::array<std::byte, 1> bytes{};
    if (auto spi = Spi::transceive(bytes, bytes); !spi) {
        return spi;
    }
    return I2c::write(bytes);
}

[[maybe_unused]] solar::Result<
    solar::hardware::watchdog::Channel<solar::hardware::dt::alias<"solar-watchdog">>,
    solar::hardware::Error>
exercise_watchdog()
{
    using namespace std::chrono_literals;
    return Watchdog::install(solar::hardware::watchdog::Timeout::window(0ms, 1000ms));
}

int main()
{
    return Pwm::ready() && Watchdog::ready() && Console::ready() ? 0 : -1;
}
