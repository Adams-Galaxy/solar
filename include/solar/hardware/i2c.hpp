#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/drivers/i2c.h>
#if defined(CONFIG_SOLAR_HARDWARE_RTIO) && defined(CONFIG_I2C_RTIO)
#include "solar/hardware/rtio.hpp"
#include <zephyr/drivers/i2c/rtio.h>
#endif

#include "solar/hardware/async.hpp"
#include "solar/hardware/endpoint.hpp"

namespace solar::hardware::i2c
{

template <typename EndpointT> class AsyncTransfer;
template <typename EndpointT> struct RtioEndpoint;

inline constexpr bool available =
#if defined(CONFIG_SOLAR_HARDWARE_I2C)
    true;
#else
    false;
#endif

template <auto Spec> struct Endpoint : hardware::Endpoint<Spec>
{
    static_assert(available, "SOLAR_DIAGNOSTIC_HARDWARE_I2C_DISABLED: I2C wrappers require "
                             "CONFIG_SOLAR_HARDWARE_I2C");
    static_assert(dt::I2cDescriptorType<decltype(Spec)>,
                  "SOLAR_DIAGNOSTIC_HARDWARE_I2C_DESCRIPTOR_REQUIRED: I2C Endpoint requires "
                  "an addressed I2C devicetree descriptor");

    using Base = hardware::Endpoint<Spec>;
    using Operation = AsyncTransfer<Endpoint>;
#if defined(CONFIG_SOLAR_HARDWARE_RTIO) && defined(CONFIG_I2C_RTIO)
    using Rtio = RtioEndpoint<Endpoint>;
#endif

    [[nodiscard]] static Result<void, Error> transfer(std::span<i2c_msg> messages) noexcept
    {
        if (auto ready = Base::require_ready(); !ready) {
            return ready;
        }
        return hardware::detail::native_result(
            i2c_transfer_dt(&Base::descriptor_value.native, messages.data(), messages.size()),
            hardware::Operation::Transceive, Base::path());
    }

    [[nodiscard]] static Result<void, Error> write(std::span<const std::byte> bytes) noexcept
    {
        return hardware::detail::native_result(
            i2c_write_dt(&Base::descriptor_value.native,
                         reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()),
            hardware::Operation::Write, Base::path());
    }

    [[nodiscard]] static Result<void, Error> read(std::span<std::byte> bytes) noexcept
    {
        return hardware::detail::native_result(
            i2c_read_dt(&Base::descriptor_value.native,
                        reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size()),
            hardware::Operation::Read, Base::path());
    }

    [[nodiscard]] static Result<void, Error> write_read(std::span<const std::byte> write_bytes,
                                                        std::span<std::byte> read_bytes) noexcept
    {
        return hardware::detail::native_result(
            i2c_write_read_dt(
                &Base::descriptor_value.native,
                reinterpret_cast<const std::uint8_t*>(write_bytes.data()), write_bytes.size(),
                reinterpret_cast<std::uint8_t*>(read_bytes.data()), read_bytes.size()),
            hardware::Operation::Transceive, Base::path());
    }

    [[nodiscard]] static Result<std::uint8_t, Error> read_register(std::uint8_t address) noexcept
    {
        std::uint8_t value{};
        const auto result = i2c_reg_read_byte_dt(&Base::descriptor_value.native, address, &value);
        if (result != 0) {
            return fail(
                hardware::detail::native_error(result, hardware::Operation::Read, Base::path()));
        }
        return value;
    }

    [[nodiscard]] static Result<void, Error> write_register(std::uint8_t address,
                                                            std::uint8_t value) noexcept
    {
        return hardware::detail::native_result(
            i2c_reg_write_byte_dt(&Base::descriptor_value.native, address, value),
            hardware::Operation::Write, Base::path());
    }

    [[nodiscard]] static Result<void, Error> read_registers(std::uint8_t start,
                                                            std::span<std::byte> bytes) noexcept
    {
        return hardware::detail::native_result(
            i2c_burst_read_dt(&Base::descriptor_value.native, start,
                              reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size()),
            hardware::Operation::Read, Base::path());
    }

    [[nodiscard]] static Result<void, Error>
    write_registers(std::uint8_t start, std::span<const std::byte> bytes) noexcept
    {
        return hardware::detail::native_result(
            i2c_burst_write_dt(&Base::descriptor_value.native, start,
                               reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()),
            hardware::Operation::Write, Base::path());
    }

    [[nodiscard]] static constexpr std::uint16_t address() noexcept
    {
        return Base::descriptor_value.native.addr;
    }
};

#if defined(CONFIG_SOLAR_HARDWARE_RTIO) && defined(CONFIG_I2C_RTIO)
template <typename EndpointT> struct RtioEndpoint
{
    inline static constinit i2c_dt_spec native_spec = EndpointT::descriptor().native;
    inline static constinit ::rtio_iodev native_iodev{
        .api = &i2c_iodev_api,
        .data = &native_spec,
    };

    [[nodiscard]] static bool ready() noexcept
    {
        return i2c_is_ready_iodev(&native_iodev);
    }

    [[nodiscard]] static Result<::rtio_sqe*, Error> copy(rtio::Context& context,
                                                         std::span<const i2c_msg> messages) noexcept
    {
        if (messages.empty() || messages.size() > UINT8_MAX) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = hardware::Operation::Submit,
                              .native = -EINVAL,
                              .endpoint = EndpointT::path()});
        }
        auto* last = i2c_rtio_copy(context.native_handle(), &native_iodev, messages.data(),
                                   static_cast<std::uint8_t>(messages.size()));
        if (last == nullptr) {
            return fail(Error{.status = Status::NoMemory,
                              .reason = Reason::ResourceExhausted,
                              .operation = hardware::Operation::Submit,
                              .native = -ENOMEM,
                              .endpoint = EndpointT::path()});
        }
        return last;
    }

    [[nodiscard]] static constexpr ::rtio_iodev* native_handle() noexcept
    {
        return &native_iodev;
    }
};
#endif

using Completion = void (*)(Result<void, Error>) noexcept;

#if defined(CONFIG_SOLAR_HARDWARE_I2C_ASYNC)
template <typename EndpointT> class AsyncTransfer
{
  public:
    [[nodiscard]] Result<async::Token, Error> submit(std::span<i2c_msg> messages,
                                                     Completion completion) noexcept
    {
        if (completion == nullptr || messages.size() > UINT8_MAX) {
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
        const auto result =
            i2c_transfer_cb_dt(&EndpointT::descriptor().native, messages.data(),
                               static_cast<std::uint8_t>(messages.size()), &trampoline, this);
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
};
#else
template <typename EndpointT> class AsyncTransfer
{
    static_assert(sizeof(EndpointT) == 0,
                  "SOLAR_DIAGNOSTIC_HARDWARE_I2C_ASYNC_DISABLED: I2C Operation requires "
                  "CONFIG_SOLAR_HARDWARE_I2C_ASYNC");
};
#endif

template <auto Spec> struct Controller : hardware::Endpoint<Spec>
{
    static_assert(dt::DeviceDescriptorType<decltype(Spec)> &&
                      Spec.identity.endpoint_kind == EndpointKind::I2c,
                  "SOLAR_DIAGNOSTIC_HARDWARE_I2C_CONTROLLER_REQUIRED: I2C Controller requires "
                  "a controller device descriptor");

    using Base = hardware::Endpoint<Spec>;

    [[nodiscard]] static Result<void, Error> configure(std::uint32_t configuration) noexcept
    {
        return hardware::detail::native_result(i2c_configure(Base::native_device(), configuration),
                                               hardware::Operation::Configure, Base::path());
    }

    [[nodiscard]] static Result<std::uint32_t, Error> configuration() noexcept
    {
        std::uint32_t value{};
        const auto result = i2c_get_config(Base::native_device(), &value);
        if (result != 0) {
            return fail(
                hardware::detail::native_error(result, hardware::Operation::Read, Base::path()));
        }
        return value;
    }

    [[nodiscard]] static Result<void, Error> recover() noexcept
    {
        return hardware::detail::native_result(i2c_recover_bus(Base::native_device()),
                                               hardware::Operation::Recover, Base::path());
    }
};

} // namespace solar::hardware::i2c
