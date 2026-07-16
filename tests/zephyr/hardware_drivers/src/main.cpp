#include <array>
#include <atomic>
#include <chrono>

#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/ztest.h>

#include <solar/hardware.hpp>

RTIO_DEFINE(fixture_rtio, 1, 1);
RTIO_DEFINE(fixture_bus_rtio, 4, 4);

namespace fixture
{

inline constexpr auto SpiSpec = solar::hardware::dt::spi(spi_dt_spec{
    .bus = DEVICE_DT_GET(DT_NODELABEL(spi0)),
    .config =
        {
            .frequency = 1'000'000,
            .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
            .slave = 0,
            .cs = {},
            .word_delay = 0,
        },
});
inline constexpr auto I2cSpec = solar::hardware::dt::i2c(i2c_dt_spec{
    .bus = DEVICE_DT_GET(DT_NODELABEL(i2c0)),
    .addr = 0x52,
});

using Spi = solar::hardware::spi::Endpoint<SpiSpec>;
using SpiController = solar::hardware::spi::Controller<solar::hardware::dt::node_label<"spi0">>;
using I2c = solar::hardware::i2c::Endpoint<I2cSpec>;
using I2cController = solar::hardware::i2c::Controller<solar::hardware::dt::node_label<"i2c0">>;
using Adc = solar::hardware::adc::Channel<solar::hardware::dt::alias<"solar-adc">>;
using AdcSequence = solar::hardware::adc::Sequence<Adc>;
struct AdcStreamConfiguration
{
    inline static constexpr std::array channels{Adc::descriptor().native};
    inline static constexpr std::array triggers{
        adc_stream_trigger{ADC_TRIG_DATA_READY, ADC_STREAM_DATA_INCLUDE}};
};
using AdcStream = solar::hardware::adc::Stream<AdcStreamConfiguration>;
using Uart = solar::hardware::uart::Polling<solar::hardware::dt::alias<"solar-uart">>;
using UartAsync = solar::hardware::uart::Async<solar::hardware::dt::alias<"solar-uart">>;
using UartInterrupt =
    solar::hardware::uart::InterruptDriven<solar::hardware::dt::alias<"solar-uart">>;
using Counter = solar::hardware::counter::Counter<solar::hardware::dt::alias<"solar-counter">>;
using Alarm = solar::hardware::counter::Alarm<Counter, 0>;
using Top = solar::hardware::counter::Top<Counter>;

inline std::atomic<unsigned> alarms{};
inline std::atomic<unsigned> completions{};
inline std::atomic<unsigned> rtio_callbacks{};
inline int rtio_user_data{};

void alarm(std::uint32_t) noexcept
{
    alarms.fetch_add(1, std::memory_order_relaxed);
}

void completed(solar::Result<void, solar::hardware::Error>) noexcept
{
    completions.fetch_add(1, std::memory_order_relaxed);
}

void rtio_callback(::rtio*, const ::rtio_sqe*, int, void*) noexcept
{
    rtio_callbacks.fetch_add(1, std::memory_order_relaxed);
}

void uart_async(const uart_event&) noexcept {}

void uart_interrupt() noexcept {}

} // namespace fixture

static_assert(solar::hardware::dt::SpiDescriptorType<decltype(fixture::SpiSpec)>);
static_assert(solar::hardware::dt::I2cDescriptorType<decltype(fixture::I2cSpec)>);
static_assert(
    solar::hardware::dt::AdcDescriptorType<decltype(solar::hardware::dt::alias<"solar-adc">)>);
static_assert(fixture::I2c::address() == 0x52);
static_assert(sizeof(fixture::Spi::Operation) > 0);
static_assert(sizeof(fixture::I2c::Operation) > 0);
static_assert(fixture::AdcStream::native_handle() != nullptr);

ZTEST(hardware_drivers, test_generated_bus_endpoints_and_native_escape_hatches)
{
    zassert_true(fixture::Spi::ready());
    zassert_true(fixture::SpiController::ready());
    zassert_true(fixture::I2c::ready());
    zassert_true(fixture::I2cController::ready());
    zassert_equal(fixture::Spi::native_device(), DEVICE_DT_GET(DT_NODELABEL(spi0)));
    zassert_equal(fixture::I2c::native_device(), DEVICE_DT_GET(DT_NODELABEL(i2c0)));

    std::array<std::byte, 2> bytes{};
    auto spi = fixture::Spi::transceive(bytes, bytes);
    zassert_false(spi.has_value());
    zassert_not_equal(spi.error().native, 0);

    auto i2c = fixture::I2c::read(bytes);
    zassert_false(i2c.has_value());
    zassert_not_equal(i2c.error().native, 0);
}

ZTEST(hardware_drivers, test_adc_setup_sample_and_conversion)
{
    zassert_true(fixture::Adc::ready());
    zassert_true(fixture::Adc::setup().has_value());
    zassert_equal(
        adc_emul_const_raw_value_set(fixture::Adc::native_device(), fixture::Adc::channel(), 1234),
        0);
    auto sample = fixture::Adc::sample();
    zassert_true(sample.has_value());
    zassert_equal(*sample, 1234);
    auto millivolts = fixture::Adc::to_millivolts(*sample);
    zassert_true(millivolts.has_value());

    std::array<std::byte, 1> too_small{};
    auto invalid_sequence = fixture::AdcSequence::native(too_small);
    zassert_false(invalid_sequence.has_value());
    zassert_equal(invalid_sequence.error().status, solar::Status::NoBuffer);

    std::array<std::byte, sizeof(std::int16_t)> sequence_buffer{};
    zassert_true(fixture::AdcSequence::setup().has_value());
    zassert_true(fixture::AdcSequence::read(sequence_buffer).has_value());
}

ZTEST(hardware_drivers, test_uart_and_counter_roles)
{
    zassert_true(fixture::Uart::ready());
    auto configuration = fixture::Uart::configuration();
    zassert_false(configuration.has_value());
    zassert_equal(configuration.error().reason, solar::hardware::Reason::Unsupported);

    fixture::alarms.store(0, std::memory_order_relaxed);
    zassert_true(fixture::Counter::ready());
    zassert_true(fixture::Counter::start().has_value());
    k_sleep(K_MSEC(2));
    auto value = fixture::Counter::value();
    zassert_true(value.has_value());
    zassert_true(*value > 0);
    auto value64 = fixture::Counter::value64();
    zassert_false(value64.has_value());
    zassert_equal(value64.error().reason, solar::hardware::Reason::Unsupported);
    zassert_true(fixture::Top::value() > 0);
    zassert_true(fixture::Alarm::set(1, false, &fixture::alarm).has_value());
    k_sleep(K_MSEC(2));
    zassert_equal(fixture::alarms.load(std::memory_order_relaxed), 1);
    zassert_true(fixture::Counter::stop().has_value());
}

ZTEST(hardware_drivers, test_uart_callback_roles_share_endpoint_ownership)
{
    zassert_true(fixture::UartAsync::install(&fixture::uart_async).has_value());
    zassert_true(fixture::UartAsync::installed());
    auto interrupt_conflict = fixture::UartInterrupt::install(&fixture::uart_interrupt);
    zassert_false(interrupt_conflict.has_value());
    zassert_equal(interrupt_conflict.error().reason, solar::hardware::Reason::AlreadyOwned);
    zassert_true(fixture::UartAsync::uninstall().has_value());

    zassert_true(fixture::UartInterrupt::install(&fixture::uart_interrupt).has_value());
    zassert_true(fixture::UartInterrupt::installed());
    auto async_conflict = fixture::UartAsync::install(&fixture::uart_async);
    zassert_false(async_conflict.has_value());
    zassert_equal(async_conflict.error().reason, solar::hardware::Reason::AlreadyOwned);
    zassert_true(fixture::UartInterrupt::uninstall().has_value());
}

ZTEST(hardware_drivers, test_caller_owned_async_operations)
{
    std::array<std::byte, 1> storage{};
    i2c_msg message{
        .buf = reinterpret_cast<std::uint8_t*>(storage.data()),
        .len = static_cast<std::uint32_t>(storage.size()),
        .flags = I2C_MSG_READ | I2C_MSG_STOP,
    };
    fixture::I2c::Operation i2c_operation;
    auto i2c_submission =
        i2c_operation.submit(std::span<i2c_msg>{&message, 1}, &fixture::completed);
    zassert_false(i2c_submission.has_value());
    zassert_equal(i2c_submission.error().reason, solar::hardware::Reason::Unsupported);
    zassert_false(i2c_operation.active());

    std::int16_t value{};
    adc_sequence sequence{};
    zassert_equal(adc_sequence_init_dt(&fixture::Adc::descriptor().native, &sequence), 0);
    sequence.buffer = &value;
    sequence.buffer_size = sizeof(value);
    zassert_equal(
        adc_emul_const_raw_value_set(fixture::Adc::native_device(), fixture::Adc::channel(), 77),
        0);
    solar::hardware::adc::Operation<fixture::Adc> adc_operation;
    auto adc_submission = adc_operation.submit(sequence);
    zassert_true(adc_submission.has_value());
    zassert_true(adc_operation.wait(K_SECONDS(1)).has_value());
    zassert_false(adc_operation.active());
    zassert_equal(value, 77);
}

ZTEST(hardware_drivers, test_caller_owned_rtio_context_and_completion)
{
    solar::hardware::rtio::Context context{fixture_rtio};
    auto empty = context.consume();
    zassert_false(empty.has_value());
    zassert_equal(empty.error().status, solar::Status::WouldBlock);

    fixture::rtio_callbacks.store(0, std::memory_order_relaxed);
    auto acquired = context.acquire();
    zassert_true(acquired.has_value());
    rtio_sqe_prep_callback(*acquired, &fixture::rtio_callback, nullptr, &fixture::rtio_user_data);
    zassert_true(context.submit(1).has_value());
    {
        auto completion = context.consume();
        zassert_true(completion.has_value());
        zassert_equal(completion->result(), 0);
        zassert_equal(completion->user_data(), &fixture::rtio_user_data);
    }
    zassert_equal(fixture::rtio_callbacks.load(std::memory_order_relaxed), 1);

    // The completion's destructor returned the sole CQE to the caller-owned pool.
    acquired = context.acquire();
    zassert_true(acquired.has_value());
}

ZTEST(hardware_drivers, test_driver_rtio_iodevs_and_submission_copy)
{
    zassert_true(fixture::Spi::Rtio::ready());
    zassert_true(fixture::I2c::Rtio::ready());
    zassert_true(fixture::AdcStream::ready());

    solar::hardware::rtio::Context context{fixture_bus_rtio};
    std::array<std::byte, 2> storage{};
    spi_buf buffer{.buf = storage.data(), .len = storage.size()};
    spi_buf_set buffers{.buffers = &buffer, .count = 1};
    rtio_sqe* spi_last{};
    auto spi_count = fixture::Spi::Rtio::copy(context, &buffers, &buffers, spi_last);
    zassert_true(spi_count.has_value());
    zassert_equal(*spi_count, 1);
    zassert_not_null(spi_last);

    i2c_msg message{
        .buf = reinterpret_cast<std::uint8_t*>(storage.data()),
        .len = static_cast<std::uint32_t>(storage.size()),
        .flags = I2C_MSG_READ | I2C_MSG_STOP,
    };
    auto i2c_last = fixture::I2c::Rtio::copy(context, std::span<const i2c_msg>{&message, 1});
    zassert_true(i2c_last.has_value());
    zassert_not_null(*i2c_last);

    auto invalid_i2c = fixture::I2c::Rtio::copy(context, std::span<const i2c_msg>{});
    zassert_false(invalid_i2c.has_value());
    zassert_equal(invalid_i2c.error().status, solar::Status::Invalid);
}

ZTEST_SUITE(hardware_drivers, nullptr, nullptr, nullptr, nullptr, nullptr);
