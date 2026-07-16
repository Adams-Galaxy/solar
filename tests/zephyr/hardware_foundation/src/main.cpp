#include <atomic>

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include <solar/hardware.hpp>

namespace fixture
{

using BoardLed = solar::hardware::gpio::Output<solar::hardware::dt::alias<"led0">>;
using Console = solar::hardware::Endpoint<solar::hardware::dt::chosen<"zephyr,console">>;
#if DT_NODE_EXISTS(DT_ALIAS(solar_output))
using Output = solar::hardware::gpio::Output<
    solar::hardware::dt::alias<"solar-output">, solar::hardware::gpio::Initial::Inactive>;
using Input = solar::hardware::gpio::Input<solar::hardware::dt::alias<"solar-input">>;
using Interrupt = solar::hardware::gpio::Interrupt<
    solar::hardware::dt::alias<"solar-input">, solar::hardware::gpio::Trigger::Rising>;
using InterruptAlias = solar::hardware::gpio::Interrupt<
    solar::hardware::dt::alias<"solar-input-copy">,
    solar::hardware::gpio::Trigger::Rising>;
inline std::atomic<unsigned> interrupts{};

void on_interrupt(solar::hardware::gpio::Event event) noexcept
{
    if ((event.pins & BIT(Input::pin())) != 0) {
        interrupts.fetch_add(1, std::memory_order_relaxed);
    }
}
#endif

} // namespace fixture

static_assert(fixture::BoardLed::descriptor().identity.endpoint_kind ==
              solar::hardware::EndpointKind::Gpio);
static_assert(fixture::Console::descriptor().identity.endpoint_kind ==
              solar::hardware::EndpointKind::Uart);
#if DT_NODE_EXISTS(DT_ALIAS(solar_output))
static_assert(solar::hardware::dt::alias<"solar-input">.identity.stable_id ==
              solar::hardware::dt::alias<"solar-input-copy">.identity.stable_id);
#endif
static_assert(solar::hardware::dt::generated::inventory.size() > 3);

#if DT_NODE_EXISTS(DT_ALIAS(solar_output))
ZTEST(hardware_foundation, test_generated_readiness_and_logical_gpio)
{
    zassert_true(fixture::BoardLed::ready());
    zassert_false(fixture::Console::ready());
    auto console_ready = fixture::Console::require_ready();
    zassert_false(console_ready.has_value());
    zassert_equal(console_ready.error().reason, solar::hardware::Reason::Unsupported);
    zassert_true(fixture::Output::configure().has_value());

    zassert_equal(gpio_emul_output_get_dt(&fixture::Output::native_handle()), 1);
    zassert_true(fixture::Output::activate().has_value());
    zassert_equal(gpio_emul_output_get_dt(&fixture::Output::native_handle()), 0);
    zassert_true(fixture::Output::deactivate().has_value());
    zassert_equal(gpio_emul_output_get_dt(&fixture::Output::native_handle()), 1);
}

ZTEST(hardware_foundation, test_input_raw_and_logical_reads)
{
    zassert_true(fixture::Input::configure().has_value());
    zassert_equal(gpio_emul_input_set_dt(&fixture::Input::native_handle(), 1), 0);
    auto logical = fixture::Input::read();
    auto raw = fixture::Input::read_raw();
    zassert_true(logical.has_value() && *logical);
    zassert_true(raw.has_value() && *raw);
}

ZTEST(hardware_foundation, test_interrupt_aliases_share_physical_ownership)
{
    fixture::interrupts.store(0, std::memory_order_relaxed);
    zassert_equal(gpio_emul_input_set_dt(&fixture::Input::native_handle(), 0), 0);
    zassert_true(fixture::Interrupt::start(&fixture::on_interrupt).has_value());

    auto duplicate = fixture::InterruptAlias::install(&fixture::on_interrupt);
    zassert_false(duplicate.has_value());
    zassert_equal(duplicate.error().reason, solar::hardware::Reason::AlreadyOwned);

    zassert_equal(gpio_emul_input_set_dt(&fixture::Input::native_handle(), 1), 0);
    zassert_equal(fixture::interrupts.load(std::memory_order_relaxed), 1);
    zassert_true(fixture::InterruptAlias::stop().has_value());
    zassert_false(fixture::Interrupt::installed());
}
#else
ZTEST(hardware_foundation, test_generated_board_aliases_compile)
{
    zassert_true(fixture::BoardLed::descriptor().identity.okay);
    zassert_true(fixture::Console::descriptor().identity.okay);
}
#endif

ZTEST(hardware_foundation, test_caller_owned_async_generation_gate)
{
    solar::hardware::async::Gate gate;
    auto first = gate.begin();
    zassert_true(first.has_value());
    zassert_true(gate.active(*first));

    auto busy = gate.begin();
    zassert_false(busy.has_value());
    zassert_equal(busy.error().reason, solar::hardware::Reason::Busy);

    zassert_true(gate.complete(*first).has_value());
    auto stale = gate.complete(*first);
    zassert_false(stale.has_value());
    zassert_equal(stale.error().reason, solar::hardware::Reason::StaleCompletion);

    auto second = gate.begin();
    zassert_true(second.has_value());
    zassert_not_equal(second->generation, first->generation);
    zassert_true(gate.cancel(*second).has_value());
}

ZTEST_SUITE(hardware_foundation, nullptr, nullptr, nullptr, nullptr, nullptr);
