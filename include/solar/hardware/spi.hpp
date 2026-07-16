#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/drivers/spi.h>
#if defined(CONFIG_SOLAR_HARDWARE_RTIO) && defined(CONFIG_SPI_RTIO)
#include "solar/hardware/rtio.hpp"
#include <zephyr/drivers/spi/rtio.h>
#endif

#include "solar/hardware/async.hpp"
#include "solar/hardware/endpoint.hpp"

namespace solar::hardware::spi
{

template <typename EndpointT> class AsyncTransfer;
template <typename EndpointT> struct RtioEndpoint;

inline constexpr bool available =
#if defined(CONFIG_SOLAR_HARDWARE_SPI)
    true;
#else
    false;
#endif

template <auto Spec> struct Endpoint : hardware::Endpoint<Spec>
{
    static_assert(available, "SOLAR_DIAGNOSTIC_HARDWARE_SPI_DISABLED: SPI wrappers require "
                             "CONFIG_SOLAR_HARDWARE_SPI");
    static_assert(dt::SpiDescriptorType<decltype(Spec)>,
                  "SOLAR_DIAGNOSTIC_HARDWARE_SPI_DESCRIPTOR_REQUIRED: SPI Endpoint requires "
                  "an addressed SPI devicetree descriptor");

    using Base = hardware::Endpoint<Spec>;
    using Operation = AsyncTransfer<Endpoint>;
#if defined(CONFIG_SOLAR_HARDWARE_RTIO) && defined(CONFIG_SPI_RTIO)
    using Rtio = RtioEndpoint<Endpoint>;
#endif

    [[nodiscard]] static Result<void, Error> transceive(const spi_buf_set* transmit,
                                                        const spi_buf_set* receive) noexcept
    {
        if (auto ready = Base::require_ready(); !ready) {
            return ready;
        }
        return hardware::detail::native_result(
            spi_transceive_dt(&Base::descriptor_value.native, transmit, receive),
            hardware::Operation::Transceive, Base::path());
    }

    [[nodiscard]] static Result<void, Error> transceive(std::span<const std::byte> transmit,
                                                        std::span<std::byte> receive) noexcept
    {
        spi_buf tx_buffer{.buf = const_cast<std::byte*>(transmit.data()), .len = transmit.size()};
        spi_buf rx_buffer{.buf = receive.data(), .len = receive.size()};
        spi_buf_set tx_set{.buffers = &tx_buffer, .count = 1};
        spi_buf_set rx_set{.buffers = &rx_buffer, .count = 1};
        return transceive(transmit.empty() ? nullptr : &tx_set,
                          receive.empty() ? nullptr : &rx_set);
    }

    [[nodiscard]] static Result<void, Error> write(std::span<const std::byte> bytes) noexcept
    {
        spi_buf buffer{.buf = const_cast<std::byte*>(bytes.data()), .len = bytes.size()};
        spi_buf_set set{.buffers = &buffer, .count = 1};
        return transceive(bytes.empty() ? nullptr : &set, nullptr);
    }

    [[nodiscard]] static Result<void, Error> read(std::span<std::byte> bytes) noexcept
    {
        spi_buf buffer{.buf = bytes.data(), .len = bytes.size()};
        spi_buf_set set{.buffers = &buffer, .count = 1};
        return transceive(nullptr, bytes.empty() ? nullptr : &set);
    }

    [[nodiscard]] static Result<void, Error> release() noexcept
    {
        return hardware::detail::native_result(spi_release_dt(&Base::descriptor_value.native),
                                               hardware::Operation::Release, Base::path());
    }
};

#if defined(CONFIG_SOLAR_HARDWARE_RTIO) && defined(CONFIG_SPI_RTIO)
template <typename EndpointT> struct RtioEndpoint
{
    inline static constinit spi_dt_spec native_spec = EndpointT::descriptor().native;
    inline static constinit ::rtio_iodev native_iodev{
        .api = &spi_iodev_api,
        .data = &native_spec,
    };

    [[nodiscard]] static bool ready() noexcept
    {
        return spi_is_ready_iodev(&native_iodev);
    }

    [[nodiscard]] static Result<std::uint32_t, Error> copy(rtio::Context& context,
                                                           const spi_buf_set* transmit,
                                                           const spi_buf_set* receive,
                                                           ::rtio_sqe*& last) noexcept
    {
        const auto count =
            spi_rtio_copy(context.native_handle(), &native_iodev, transmit, receive, &last);
        if (count < 0) {
            return fail(hardware::detail::native_error(count, hardware::Operation::Submit,
                                                       EndpointT::path()));
        }
        return static_cast<std::uint32_t>(count);
    }

    [[nodiscard]] static constexpr ::rtio_iodev* native_handle() noexcept
    {
        return &native_iodev;
    }
};
#endif

using Completion = void (*)(Result<void, Error>) noexcept;

#if defined(CONFIG_SOLAR_HARDWARE_SPI_ASYNC)
template <typename EndpointT> class AsyncTransfer
{
  public:
    [[nodiscard]] Result<async::Token, Error> submit(std::span<const std::byte> transmit,
                                                     std::span<std::byte> receive,
                                                     Completion completion) noexcept
    {
        if (completion == nullptr) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = hardware::Operation::Submit,
                              .native = -EINVAL,
                              .endpoint = EndpointT::path()});
        }
        auto admitted = gate_.begin();
        if (!admitted) {
            return fail(admitted.error());
        }
        token_ = *admitted;
        completion_ = completion;
        tx_buffer_ = {.buf = const_cast<std::byte*>(transmit.data()), .len = transmit.size()};
        rx_buffer_ = {.buf = receive.data(), .len = receive.size()};
        tx_set_ = {.buffers = &tx_buffer_, .count = 1};
        rx_set_ = {.buffers = &rx_buffer_, .count = 1};
        const auto result =
            spi_transceive_cb(EndpointT::native_device(), &EndpointT::descriptor().native.config,
                              transmit.empty() ? nullptr : &tx_set_,
                              receive.empty() ? nullptr : &rx_set_, &trampoline, this);
        if (result < 0) {
            (void)gate_.complete(token_);
            completion_ = nullptr;
            return fail(hardware::detail::native_error(result, hardware::Operation::Submit,
                                                       EndpointT::path()));
        }
        return token_;
    }

    [[nodiscard]] bool active() const noexcept
    {
        return gate_.active(token_);
    }

    [[nodiscard]] Result<void, Error> cancel() noexcept
    {
        return fail(Error{.status = Status::NotSupported,
                          .reason = Reason::Unsupported,
                          .operation = hardware::Operation::Cancel,
                          .native = -ENOTSUP,
                          .endpoint = EndpointT::path()});
    }

  private:
    static void trampoline(const device*, int result, void* context) noexcept
    {
        static_cast<AsyncTransfer*>(context)->finish(result);
    }

    void finish(int result) noexcept
    {
        if (!gate_.complete(token_)) {
            return;
        }
        const auto completion = completion_;
        completion_ = nullptr;
        if (completion != nullptr) {
            if (result < 0) {
                completion(fail(hardware::detail::native_error(
                    result, hardware::Operation::Complete, EndpointT::path())));
            } else {
                completion({});
            }
        }
    }

    async::Gate gate_{};
    async::Token token_{};
    Completion completion_{};
    spi_buf tx_buffer_{};
    spi_buf rx_buffer_{};
    spi_buf_set tx_set_{};
    spi_buf_set rx_set_{};
};
#else
template <typename EndpointT> class AsyncTransfer
{
    static_assert(sizeof(EndpointT) == 0,
                  "SOLAR_DIAGNOSTIC_HARDWARE_SPI_ASYNC_DISABLED: SPI Operation requires "
                  "CONFIG_SOLAR_HARDWARE_SPI_ASYNC");
};
#endif

template <auto Spec> struct Controller : hardware::Endpoint<Spec>
{
    static_assert(dt::DeviceDescriptorType<decltype(Spec)> &&
                      Spec.identity.endpoint_kind == EndpointKind::Spi,
                  "SOLAR_DIAGNOSTIC_HARDWARE_SPI_CONTROLLER_REQUIRED: SPI Controller requires "
                  "a controller device descriptor");
};

template <typename EndpointT> class Session
{
  public:
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Session(Session&& other) noexcept : owns_(other.owns_)
    {
        other.owns_ = false;
    }

    ~Session()
    {
        if (owns_) {
            (void)EndpointT::release();
        }
    }

    [[nodiscard]] static Result<Session, Error> begin() noexcept
    {
        if ((EndpointT::descriptor().native.config.operation & SPI_LOCK_ON) == 0U) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = hardware::Operation::Start,
                              .native = -EINVAL,
                              .endpoint = EndpointT::path()});
        }
        return Session{};
    }

    [[nodiscard]] Result<void, Error> release() noexcept
    {
        if (!owns_) {
            return {};
        }
        auto result = EndpointT::release();
        if (result) {
            owns_ = false;
        }
        return result;
    }

  private:
    Session() = default;
    bool owns_{true};
};

} // namespace solar::hardware::spi
