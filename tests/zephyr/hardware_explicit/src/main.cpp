#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include <solar/hardware/gpio.hpp>

inline constexpr auto LedSpec =
    solar::hardware::dt::gpio(GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios));
using Led = solar::hardware::gpio::Output<LedSpec>;

ZTEST(hardware_explicit, test_explicit_descriptor_without_generator)
{
    zassert_true(Led::ready());
    zassert_true(Led::configure().has_value());
    zassert_true(Led::activate().has_value());
    zassert_equal(gpio_emul_output_get_dt(&Led::native_handle()), 1);
}

ZTEST_SUITE(hardware_explicit, nullptr, nullptr, nullptr, nullptr, nullptr);
