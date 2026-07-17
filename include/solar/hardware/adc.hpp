#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#if defined(CONFIG_SOLAR_HARDWARE_RTIO) && defined(CONFIG_ADC_STREAM)
#include "solar/hardware/rtio.hpp"
#endif

#include "solar/hardware/async.hpp"
#include "solar/hardware/endpoint.hpp"

namespace solar::hardware::adc
{

inline constexpr bool available =
#if defined(CONFIG_SOLAR_HARDWARE_ADC)
    true;
#else
    false;
#endif

template <auto Spec> struct Channel : hardware::Endpoint<Spec>
{
    static_assert(available, "SOLAR_DIAGNOSTIC_HARDWARE_ADC_DISABLED: ADC wrappers require "
                             "CONFIG_SOLAR_HARDWARE_ADC");
    static_assert(dt::AdcDescriptorType<decltype(Spec)>,
                  "SOLAR_DIAGNOSTIC_HARDWARE_ADC_DESCRIPTOR_REQUIRED: ADC Channel requires an "
                  "ADC devicetree descriptor");

    using Base = hardware::Endpoint<Spec>;

    [[nodiscard]] static Result<void, Error> setup() noexcept
    {
        if (auto ready = Base::require_ready(); !ready) {
            return ready;
        }
        return hardware::detail::native_result(adc_channel_setup_dt(&Base::descriptor_value.native),
                                               Operation::Configure, Base::path());
    }

    [[nodiscard]] static Result<void, Error> read(adc_sequence& sequence) noexcept
    {
        return hardware::detail::native_result(
            adc_read_dt(&Base::descriptor_value.native, &sequence), Operation::Read, Base::path());
    }

    [[nodiscard]] static Result<std::int16_t, Error> sample() noexcept
    {
        std::int16_t value{};
        adc_sequence sequence{};
        const auto initialized = adc_sequence_init_dt(&Base::descriptor_value.native, &sequence);
        if (initialized != 0) {
            return fail<Error>(
                hardware::detail::native_error(initialized, Operation::Configure, Base::path()));
        }
        sequence.buffer = &value;
        sequence.buffer_size = sizeof(value);
        const auto result = adc_read_dt(&Base::descriptor_value.native, &sequence);
        if (result != 0) {
            return fail<Error>(
                hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return value;
    }

    [[nodiscard]] static Result<std::int32_t, Error> to_millivolts(std::int32_t raw) noexcept
    {
        const auto result = adc_raw_to_millivolts_dt(&Base::descriptor_value.native, &raw);
        if (result != 0) {
            return fail<Error>(
                hardware::detail::native_error(result, Operation::Convert, Base::path()));
        }
        return raw;
    }

    [[nodiscard]] static constexpr std::uint8_t channel() noexcept
    {
        return Base::descriptor_value.native.channel_id;
    }

    [[nodiscard]] static constexpr std::uint8_t resolution() noexcept
    {
        return Base::descriptor_value.native.resolution;
    }

    [[nodiscard]] static constexpr std::uint8_t oversampling() noexcept
    {
        return Base::descriptor_value.native.oversampling;
    }

    [[nodiscard]] static constexpr std::uint16_t reference_millivolts() noexcept
    {
        return Base::descriptor_value.native.vref_mv;
    }

    [[nodiscard]] static constexpr const adc_channel_cfg& configuration() noexcept
    {
        return Base::descriptor_value.native.channel_cfg;
    }
};

template <typename T>
concept ChannelType = requires {
    T::descriptor();
    T::native_device();
    T::channel();
} && dt::AdcDescriptorType<std::remove_cvref_t<decltype(T::descriptor())>>;

template <ChannelType First, ChannelType... Rest> struct Sequence
{
    static_assert(((Rest::native_device() == First::native_device()) && ...),
                  "SOLAR_DIAGNOSTIC_HARDWARE_ADC_SEQUENCE_CONTROLLER_MISMATCH: all ADC "
                  "Sequence channels must use the same controller");
    static_assert(((Rest::resolution() == First::resolution()) && ...),
                  "SOLAR_DIAGNOSTIC_HARDWARE_ADC_SEQUENCE_RESOLUTION_MISMATCH: all ADC "
                  "Sequence channels must use one resolution");
    static_assert(((Rest::oversampling() == First::oversampling()) && ...),
                  "SOLAR_DIAGNOSTIC_HARDWARE_ADC_SEQUENCE_OVERSAMPLING_MISMATCH: all ADC "
                  "Sequence channels must use one oversampling setting");
    static_assert(First::channel() < 32U && ((Rest::channel() < 32U) && ...),
                  "SOLAR_DIAGNOSTIC_HARDWARE_ADC_SEQUENCE_CHANNEL_RANGE: ADC Sequence channel "
                  "identifiers must fit Zephyr's 32-bit channel mask");

    inline static constexpr std::size_t channel_count = 1U + sizeof...(Rest);
    inline static constexpr std::uint32_t channels =
        BIT(First::channel()) | (0U | ... | BIT(Rest::channel()));
    inline static constexpr std::size_t sample_bytes = First::resolution() > 16U ? 4U : 2U;

    [[nodiscard]] static Result<void, Error> setup() noexcept
    {
        Result<void, Error> result{};
        const auto setup_one = [&result]<typename ChannelT>() {
            if (result) {
                result = ChannelT::setup();
            }
        };
        setup_one.template operator()<First>();
        (setup_one.template operator()<Rest>(), ...);
        return result;
    }

    [[nodiscard]] static Result<adc_sequence, Error>
    native(std::span<std::byte> buffer, const adc_sequence_options* options = nullptr) noexcept
    {
        const auto samplings = 1U + (options == nullptr ? 0U : options->extra_samplings);
        const auto one_sampling = channel_count * sample_bytes;
        if (samplings > std::numeric_limits<std::size_t>::max() / one_sampling ||
            buffer.size() < one_sampling * samplings) {
            return fail<Error>({.status = solar::Status::NoBuffer,
                                .reason = Reason::ResourceExhausted,
                                .operation = hardware::Operation::Configure,
                                .native = -ENOMEM,
                                .endpoint = First::path()});
        }
        return adc_sequence{
            .options = options,
            .channels = channels,
            .buffer = buffer.data(),
            .buffer_size = buffer.size(),
#if defined(CONFIG_ADC_SEQUENCE_PRIORITY)
            .priority = 0,
#endif
            .resolution = First::resolution(),
            .oversampling = First::oversampling(),
            .calibrate = false,
        };
    }

    [[nodiscard]] static Result<void, Error>
    read(std::span<std::byte> buffer, const adc_sequence_options* options = nullptr) noexcept
    {
        auto sequence = native(buffer, options);
        if (!sequence) {
            return fail<Error>(sequence.error());
        }
        return hardware::detail::native_result(adc_read(First::native_device(), &*sequence),
                                               hardware::Operation::Read, First::path());
    }

    [[nodiscard]] static constexpr const device* native_device() noexcept
    {
        return First::native_device();
    }
};

#if defined(CONFIG_SOLAR_HARDWARE_RTIO) && defined(CONFIG_ADC_STREAM)
template <typename Configuration>
concept StreamConfiguration = requires {
    Configuration::channels;
    Configuration::triggers;
    Configuration::channels.size();
    Configuration::triggers.size();
};

template <StreamConfiguration Configuration> struct Stream
{
    static_assert(Configuration::channels.size() > 0,
                  "SOLAR_DIAGNOSTIC_HARDWARE_ADC_STREAM_CHANNELS_REQUIRED: ADC Stream requires "
                  "at least one adc_dt_spec");
    static_assert(Configuration::triggers.size() > 0,
                  "SOLAR_DIAGNOSTIC_HARDWARE_ADC_STREAM_TRIGGERS_REQUIRED: ADC Stream requires "
                  "at least one trigger");
    static_assert(
        [] {
            const auto* controller = Configuration::channels[0].dev;
            for (const auto& channel : Configuration::channels) {
                if (channel.dev != controller) {
                    return false;
                }
            }
            return true;
        }(),
        "SOLAR_DIAGNOSTIC_HARDWARE_ADC_STREAM_CONTROLLER_MISMATCH: all ADC Stream channels "
        "must use the same controller");

    inline static constexpr auto native_channels = Configuration::channels;
    inline static constexpr auto native_triggers = Configuration::triggers;
    inline static constinit adc_read_config native_configuration{
        .adc = native_channels[0].dev,
        .is_streaming = true,
        .adc_spec = native_channels.data(),
        .triggers = native_triggers.data(),
        .sequence = nullptr,
        .fifo_watermark_lvl = 0,
        .fifo_mode = 0,
        .adc_spec_cnt = native_channels.size(),
        .trigger_cnt = native_triggers.size(),
    };
    inline static constinit ::rtio_iodev native_iodev{
        .api = &__adc_iodev_api,
        .data = &native_configuration,
    };

    [[nodiscard]] static bool ready() noexcept
    {
        for (const auto& channel : native_channels) {
            if (!adc_is_ready_dt(&channel)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static Result<::rtio_sqe*, Error> start(rtio::Context& context,
                                                          void* user_data = nullptr) noexcept
    {
        ::rtio_sqe* handle{};
        const auto result = adc_stream(&native_iodev, context.native_handle(), user_data, &handle);
        if (result != 0) {
            return fail<Error>(hardware::detail::native_error(result, Operation::Submit));
        }
        return handle;
    }

    [[nodiscard]] static Result<const adc_decoder_api*, Error> decoder() noexcept
    {
        const adc_decoder_api* value{};
        const auto result = adc_get_decoder(native_configuration.adc, &value);
        if (result != 0 || value == nullptr) {
            return fail<Error>(
                hardware::detail::native_error(result == 0 ? -ENOTSUP : result, Operation::Read));
        }
        return value;
    }

    [[nodiscard]] static constexpr ::rtio_iodev* native_handle() noexcept
    {
        return &native_iodev;
    }
};
#else
template <typename Configuration> struct Stream
{
    static_assert(sizeof(Configuration) == 0,
                  "SOLAR_DIAGNOSTIC_HARDWARE_ADC_STREAM_DISABLED: ADC Stream requires "
                  "CONFIG_ADC_STREAM and CONFIG_SOLAR_HARDWARE_RTIO");
};
#endif

#if defined(CONFIG_SOLAR_HARDWARE_ADC_ASYNC)
template <typename ChannelT> class Operation
{
  public:
    Operation() noexcept
    {
        k_poll_signal_init(&signal_);
    }

    [[nodiscard]] Result<async::Token, Error> submit(adc_sequence& sequence) noexcept
    {
        auto admitted = gate_.begin();
        if (!admitted) {
            return fail<Error>(admitted.error());
        }
        token_ = *admitted;
        k_poll_signal_reset(&signal_);
        const auto result = adc_read_async_dt(&ChannelT::descriptor().native, &sequence, &signal_);
        if (result != 0) {
            (void)gate_.complete(token_);
            return fail<Error>(hardware::detail::native_error(result, hardware::Operation::Submit,
                                                              ChannelT::path()));
        }
        return token_;
    }

    [[nodiscard]] Result<void, Error> wait(k_timeout_t timeout = K_FOREVER) noexcept
    {
        k_poll_event event{};
        k_poll_event_init(&event, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &signal_);
        const auto waited = k_poll(&event, 1, timeout);
        if (waited != 0) {
            return fail<Error>(hardware::detail::native_error(waited, hardware::Operation::Complete,
                                                              ChannelT::path()));
        }
        unsigned signaled{};
        int result{};
        k_poll_signal_check(&signal_, &signaled, &result);
        if (signaled == 0U) {
            return fail<Error>({.status = solar::Status::WouldBlock,
                                .reason = Reason::Busy,
                                .operation = hardware::Operation::Complete,
                                .native = -EAGAIN,
                                .endpoint = ChannelT::path()});
        }
        (void)gate_.complete(token_);
        if (result != 0) {
            return fail<Error>(hardware::detail::native_error(result, hardware::Operation::Complete,
                                                              ChannelT::path()));
        }
        return {};
    }

    [[nodiscard]] bool active() const noexcept
    {
        return gate_.active(token_);
    }
    [[nodiscard]] k_poll_signal* native_signal() noexcept
    {
        return &signal_;
    }

  private:
    async::Gate gate_{};
    async::Token token_{};
    k_poll_signal signal_{};
};
#else
template <typename ChannelT> class Operation
{
    static_assert(sizeof(ChannelT) == 0,
                  "SOLAR_DIAGNOSTIC_HARDWARE_ADC_ASYNC_DISABLED: ADC Operation requires "
                  "CONFIG_SOLAR_HARDWARE_ADC_ASYNC");
};
#endif

} // namespace solar::hardware::adc
