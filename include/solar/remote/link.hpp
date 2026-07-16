#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

#include "solar/remote/types.hpp"

namespace solar::remote
{

enum class LinkEventKind : std::uint8_t
{
    Connected,
    Disconnected,
    RxReady,
    TxComplete,
    Fault,
};

struct LeaseHandle
{
    std::uint16_t slot{};
    std::uint16_t generation{};

    constexpr bool operator==(const LeaseHandle&) const = default;
};

struct LinkEvent
{
    LinkEventKind kind{};
    LeaseHandle lease{};
    std::uint32_t size{};
    Status status{Status::Ok};
};

struct LinkEventSink
{
    using Notify = void (*)(void*, LinkEvent) noexcept;

    void* context{};
    Notify notify_function{};

    void notify(LinkEvent event) const noexcept
    {
        if (notify_function != nullptr) {
            notify_function(context, event);
        }
    }
};

enum class TxDisposition : std::uint8_t
{
    Accepted,
    Busy,
};

struct LinkError
{
    Status status{Status::Error};
    std::int32_t native_error{};
};

class TxLease
{
  public:
    TxLease(std::span<const std::byte> bytes, LeaseHandle handle) noexcept
        : bytes_(bytes), handle_(handle)
    {}

    TxLease(const TxLease&) = delete;
    TxLease& operator=(const TxLease&) = delete;
    TxLease(TxLease&&) noexcept = default;
    TxLease& operator=(TxLease&&) noexcept = default;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
    {
        return bytes_;
    }

    [[nodiscard]] LeaseHandle handle() const noexcept
    {
        return handle_;
    }

  private:
    std::span<const std::byte> bytes_{};
    LeaseHandle handle_{};
};

template <typename T>
concept Link = requires(LinkEventSink sink, LeaseHandle rx, TxLease tx) {
    { T::descriptor } -> std::convertible_to<LinkDescriptor>;
    { T::open(sink) } -> std::same_as<Result<void, LinkError>>;
    { T::rx_bytes(rx) } -> std::same_as<Result<std::span<const std::byte>, LinkError>>;
    { T::release_rx(rx) } -> std::same_as<void>;
    { T::try_transmit(std::move(tx)) } ->
        std::same_as<Result<TxDisposition, LinkError>>;
    { T::close() } -> std::same_as<void>;
};

} // namespace solar::remote
