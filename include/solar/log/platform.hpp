#pragma once

#include <cstdint>

#include "solar/log/types.hpp"

namespace solar::log::platform
{

struct Record
{
    CaptureRequest request{};
    std::uint16_t source{};
    std::uint8_t domain{};
};

struct Bridge
{
    using Capture = void (*)(const Record&) noexcept;
    using Panic = void (*)() noexcept;

    Capture capture{};
    Panic panic{};
};

void install(Bridge bridge) noexcept;
void reset_for_test() noexcept;
void panic_for_test() noexcept;

} // namespace solar::log::platform

namespace solar::log::detail
{

struct PlatformPayloadHeader
{
    std::uint16_t package_size{};
    std::uint16_t data_size{};
    std::uint32_t reserved{};
};

} // namespace solar::log::detail
