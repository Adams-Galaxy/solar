#include "solar/log/platform.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>

#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
extern "C" {
#include <zephyr/logging/log_frontend.h>
}
#include <zephyr/sys/cbprintf.h>

namespace solar::log::platform
{
namespace
{

std::atomic<Bridge::Capture> capture_callback{};
std::atomic<Bridge::Panic> panic_callback{};
k_spinlock early_lock{};
std::array<Record, CONFIG_SOLAR_LOG_EARLY_RECORDS> early_records{};
std::size_t early_head{};
std::size_t early_size{};
bool early_loss{};
bool early_panic{};

void submit(const Record& record) noexcept
{
    if (const auto capture = capture_callback.load(std::memory_order_acquire); capture != nullptr) {
        capture(record);
        return;
    }

    const auto key = k_spin_lock(&early_lock);
    const auto capture = capture_callback.load(std::memory_order_relaxed);
    if (capture != nullptr) {
        k_spin_unlock(&early_lock, key);
        capture(record);
        return;
    }
    if (early_size < early_records.size()) {
        auto retained = record;
        if (early_loss) {
            retained.request.flags |= flag(RecordFlag::PrecedingLoss);
            early_loss = false;
        }
        early_records[(early_head + early_size) % early_records.size()] = retained;
        ++early_size;
    } else {
        early_loss = true;
    }
    k_spin_unlock(&early_lock, key);
}

[[nodiscard]] Level map_level(std::uint32_t level) noexcept
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

} // namespace

void install(Bridge bridge) noexcept
{
    panic_callback.store(bridge.panic, std::memory_order_release);
    capture_callback.store(bridge.capture, std::memory_order_release);

    while (true) {
        Record record{};
        const auto key = k_spin_lock(&early_lock);
        if (early_size == 0) {
            const bool panic = early_panic;
            early_panic = false;
            k_spin_unlock(&early_lock, key);
            if (panic && bridge.panic != nullptr) {
                bridge.panic();
            }
            break;
        }
        record = early_records[early_head];
        early_head = (early_head + 1U) % early_records.size();
        --early_size;
        k_spin_unlock(&early_lock, key);
        if (bridge.capture != nullptr) {
            bridge.capture(record);
        }
    }
}

void reset_for_test() noexcept
{
    capture_callback.store(nullptr, std::memory_order_release);
    panic_callback.store(nullptr, std::memory_order_release);
    const auto key = k_spin_lock(&early_lock);
    early_head = 0;
    early_size = 0;
    early_loss = false;
    early_panic = false;
    k_spin_unlock(&early_lock, key);
}

void panic_for_test() noexcept
{
    if (const auto panic = panic_callback.load(std::memory_order_acquire); panic != nullptr) {
        panic();
    }
}

} // namespace solar::log::platform

extern "C" void log_frontend_init(void)
{}

extern "C" void log_frontend_msg(const void* source, const struct log_msg_desc desc,
                                 std::uint8_t* package, const void* data)
{
    using namespace solar::log;
    platform::Record record{};
    record.request.level = platform::map_level(desc.level);
    record.request.context = k_is_in_isr() ? ContextKind::Isr : ContextKind::EarlyBoot;
    record.request.origin = Origin::Zephyr;
    record.request.encoding = Encoding::ZephyrCbprintf;
    record.source = source == nullptr ? 0 : static_cast<std::uint16_t>(log_source_id(source));
    record.domain = static_cast<std::uint8_t>(desc.domain);

    detail::PlatformPayloadHeader payload_header{
        .data_size = static_cast<std::uint16_t>(
            std::min<std::size_t>(desc.data_len, CONFIG_SOLAR_LOG_MAX_HEXDUMP_BYTES)),
    };
    constexpr std::size_t header_size = sizeof(payload_header);
    auto* output_package = record.request.payload.data() + header_size;
    const auto package_capacity = record.request.payload.size() - header_size - payload_header.data_size;
    std::array<std::uint16_t, 8> string_lengths{};
    const std::uint32_t flags =
        CBPRINTF_PACKAGE_CONVERT_RW_STR |
        (IS_ENABLED(CONFIG_LOG_MSG_APPEND_RO_STRING_LOC) ? CBPRINTF_PACKAGE_CONVERT_KEEP_RO_STR : 0) |
        (IS_ENABLED(CONFIG_LOG_FMT_SECTION_STRIP) ? 0 : CBPRINTF_PACKAGE_CONVERT_PTR_CHECK);
    int package_size{};
    if (desc.package_len != 0) {
        package_size = cbprintf_package_copy(package, desc.package_len, nullptr, 0, flags,
                                             string_lengths.data(), string_lengths.size());
        if (package_size < 0 || static_cast<std::size_t>(package_size) > package_capacity) {
            return;
        }
        package_size = cbprintf_package_copy(package, desc.package_len, output_package,
                                             package_capacity, flags, string_lengths.data(),
                                             string_lengths.size());
        if (package_size < 0) {
            return;
        }
    }
    payload_header.package_size = static_cast<std::uint16_t>(package_size);
    std::memcpy(record.request.payload.data(), &payload_header, sizeof(payload_header));
    if (payload_header.data_size != 0 && data != nullptr) {
        std::memcpy(record.request.payload.data() + header_size + payload_header.package_size, data,
                    payload_header.data_size);
        if (payload_header.data_size != desc.data_len) {
            record.request.flags |= flag(RecordFlag::Truncated);
        }
    }
    record.request.payload_size = static_cast<std::uint16_t>(
        header_size + payload_header.package_size + payload_header.data_size);
    platform::submit(record);
}

extern "C" void log_frontend_panic(void)
{
    using namespace solar::log::platform;
    if (const auto panic = panic_callback.load(std::memory_order_acquire); panic != nullptr) {
        panic();
        return;
    }
    const auto key = k_spin_lock(&early_lock);
    early_panic = true;
    k_spin_unlock(&early_lock, key);
}
