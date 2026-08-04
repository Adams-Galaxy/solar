#include <solar/log/zephyr_bridge.hpp>

#if defined(CONFIG_SOLAR_LOG_ZEPHYR_BRIDGE)

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_output.h>

namespace solar::log::bridge
{

namespace
{

std::atomic<Sink> installed_sink{nullptr};

[[nodiscard]] constexpr Level level_from_zephyr(std::uint8_t level) noexcept
{
    switch (level) {
    case LOG_LEVEL_ERR:
        return Level::Error;
    case LOG_LEVEL_WRN:
        return Level::Warning;
    case LOG_LEVEL_INF:
        return Level::Info;
    case LOG_LEVEL_DBG:
        return Level::Debug;
    default:
        return Level::Info;
    }
}

// Rendered line only, no timestamp/level/color prefix -- those already live
// as typed fields on Solar's RecordHeader; only the source-prefixed message
// text is worth carrying across the bridge.
constexpr std::uint32_t render_flags = 0;
constexpr std::size_t scratch_bytes = 160;

struct Accumulator
{
    std::array<char, scratch_bytes> text{};
    std::size_t size{};
};

int accumulate(std::uint8_t* data, std::size_t length, void* context) noexcept
{
    auto& accumulator = *static_cast<Accumulator*>(context);
    const auto remaining = accumulator.text.size() - std::min(accumulator.size, accumulator.text.size());
    const auto copied = std::min(length, remaining);
    if (copied != 0) {
        std::memcpy(accumulator.text.data() + accumulator.size, data, copied);
        accumulator.size += copied;
    }
    // Zephyr requires the full byte count to be reported as consumed even
    // when this scratch buffer is saturated; excess bytes are dropped.
    return static_cast<int>(length);
}

std::array<std::uint8_t, scratch_bytes> render_scratch{};
LOG_OUTPUT_DEFINE(bridge_output, accumulate, render_scratch.data(), render_scratch.size());

// process() already runs on the log core's own processing thread and calls
// all the way into StaticLogger::capture(), which stacks two full
// max_record_payload_bytes buffers of its own (CaptureRequest, then Record).
// That thread's stack was sized for plain backend rendering, not for also
// carrying a Solar capture chain, so this accumulator is static rather than
// a stack local to avoid adding to an already tight budget. Log processing
// is single-threaded per log core (deferred: one dedicated thread; immediate:
// serialized by the emitting call itself), so this is not shared across
// concurrent processing the way it would be if backends ran in parallel.
Accumulator accumulator{};

void process(const log_backend* const, union log_msg_generic* msg) noexcept
{
    if (installed_sink.load(std::memory_order_acquire) == nullptr) {
        return;
    }
    accumulator = {};
    log_output_ctx_set(&bridge_output, &accumulator);
    log_output_msg_process(&bridge_output, &msg->log, render_flags);
    if (accumulator.size == 0) {
        return;
    }
    // Trailing newline is Zephyr's console convention; Solar's sinks append
    // their own line ending on delivery.
    auto size = accumulator.size;
    if (accumulator.text[size - 1] == '\n') {
        --size;
    }
    const auto level = level_from_zephyr(log_msg_get_level(&msg->log));
    forward(level, std::string_view{accumulator.text.data(), size});
}

void init(const log_backend* const) noexcept {}

int is_ready(const log_backend* const) noexcept
{
    return 0;
}

void panic(const log_backend* const) noexcept {}

void dropped(const log_backend* const, std::uint32_t) noexcept {}

constexpr log_backend_api api{
    .process = process,
    .dropped = dropped,
    .panic = panic,
    .init = init,
    .is_ready = is_ready,
    .format_set = nullptr,
    .notify = nullptr,
};

} // namespace

[[nodiscard]] Result<void> install(Sink sink) noexcept
{
    if (sink == nullptr) {
        return fail<solar::Error>({.status = Status::Invalid});
    }
    Sink expected = nullptr;
    if (installed_sink.compare_exchange_strong(expected, sink, std::memory_order_acq_rel)) {
        return {};
    }
    return fail<solar::Error>({.status = expected == sink ? Status::Already : Status::Busy});
}

void forward(Level level, std::string_view text) noexcept
{
    if (const auto sink = installed_sink.load(std::memory_order_acquire); sink != nullptr) {
        (void)sink(level, text);
    }
}

} // namespace solar::log::bridge

LOG_BACKEND_DEFINE(solar_zephyr_bridge, solar::log::bridge::api, true);

#endif // CONFIG_SOLAR_LOG_ZEPHYR_BRIDGE
