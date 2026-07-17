#pragma once

#include <cmath>
#include <limits>

#include "solar/lifecycle/protocol.hpp"
#include "solar/metrics/runtime.hpp"
#include "solar/system/frontend.hpp"

namespace solar::metrics::detail
{

[[nodiscard]] constexpr Error frontend_error(frontend::Error error, Operation operation) noexcept
{
    switch (error) {
    case frontend::Error::NotReady:
        return {
            .status = solar::Status::NotReady, .reason = Reason::NotReady, .operation = operation};
    case frontend::Error::Disabled:
        return {.status = solar::Status::NotSupported,
                .reason = Reason::Disabled,
                .operation = operation};
    case frontend::Error::NotRegistered:
        return {.status = solar::Status::NotFound,
                .reason = Reason::NotRegistered,
                .operation = operation};
    }
    return {.status = solar::Status::Error,
            .reason = Reason::InternalInvariant,
            .operation = operation};
}

enum class AccessMode : std::uint8_t
{
    Normal,
    Try,
    Isr,
};

struct DurationSample
{
    long double seconds{};
};

template <AccessMode Mode, Operation Verb> struct AddFrontend
{
    using CatalogTag = Tag;
    template <typename MetricT> using Signature = Result<Update, Error>(typename MetricT::Value);

    template <typename System, typename MetricT>
    [[nodiscard]] static Result<Update, Error> invoke(typename MetricT::Value amount) noexcept
    {
#if defined(CONFIG_SOLAR_METRICS)
        if constexpr (!InstrumentTraits<typename MetricT::Instrument>::counter) {
            return fail<Error>(
                make_error<System, MetricT>(Verb, Status::NotSupported, Reason::ReducerFailure));
        } else {
            return add_metric<System, MetricT>(amount, Mode != AccessMode::Normal,
                                               Mode == AccessMode::Isr, Verb);
        }
#else
        return fail<Error>(frontend_error(frontend::Error::Disabled, Verb));
#endif
    }

    template <typename System, typename MetricT> static consteval void validate()
    {
#if defined(CONFIG_SOLAR_METRICS)
        using Instrument = InstrumentTraits<typename MetricT::Instrument>;
        static_assert(
            Instrument::counter,
            "SOLAR_DIAGNOSTIC_METRIC_ADD_INSTRUMENT: increment and add require a counter");
        if constexpr (Mode == AccessMode::Isr) {
            using Policies = typename System::MetricFacility::template Policies<MetricT>;
            static_assert(
                metric_isr_compatible<MetricT, Policies>,
                "SOLAR_DIAGNOSTIC_METRIC_ISR_POLICY: metric policy is not ISR compatible");
        }
#endif
    }

    template <typename MetricT>
    [[nodiscard]] static Result<Update, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Verb));
    }
};

template <AccessMode Mode> struct SetFrontend
{
    using CatalogTag = Tag;
    template <typename MetricT> using Signature = Result<Update, Error>(typename MetricT::Value);

    template <typename System, typename MetricT>
    [[nodiscard]] static Result<Update, Error> invoke(typename MetricT::Value value) noexcept
    {
#if defined(CONFIG_SOLAR_METRICS)
        if constexpr (!InstrumentTraits<typename MetricT::Instrument>::gauge) {
            return fail<Error>(make_error<System, MetricT>(Operation::Set, Status::NotSupported,
                                                           Reason::ReducerFailure));
        } else {
            return set_metric<System, MetricT>(value, Mode != AccessMode::Normal,
                                               Mode == AccessMode::Isr, Operation::Set);
        }
#else
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::Set));
#endif
    }

    template <typename System, typename MetricT> static consteval void validate()
    {
#if defined(CONFIG_SOLAR_METRICS)
        using Instrument = InstrumentTraits<typename MetricT::Instrument>;
        static_assert(Instrument::gauge,
                      "SOLAR_DIAGNOSTIC_METRIC_SET_INSTRUMENT: set requires a gauge");
        if constexpr (Mode == AccessMode::Isr) {
            using Policies = typename System::MetricFacility::template Policies<MetricT>;
            static_assert(
                metric_isr_compatible<MetricT, Policies>,
                "SOLAR_DIAGNOSTIC_METRIC_ISR_POLICY: metric policy is not ISR compatible");
        }
#endif
    }

    template <typename MetricT>
    [[nodiscard]] static Result<Update, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Set));
    }
};

template <AccessMode Mode, bool TimerOperation = false> struct ObserveFrontend
{
    using CatalogTag = Tag;
    template <typename MetricT> using Signature = Result<Update, Error>(typename MetricT::Value);

    static constexpr Operation operation =
        TimerOperation ? Operation::RecordDuration : Operation::Observe;

    template <typename System, typename MetricT>
    [[nodiscard]] static Result<Update, Error> invoke(typename MetricT::Value value) noexcept
    {
#if defined(CONFIG_SOLAR_METRICS)
        using Instrument = InstrumentTraits<typename MetricT::Instrument>;
        if constexpr (TimerOperation ? !Instrument::timer
                                     : (!Instrument::distribution || Instrument::timer)) {
            return fail<Error>(make_error<System, MetricT>(operation, Status::NotSupported,
                                                           Reason::ReducerFailure));
        } else {
            return observe_metric<System, MetricT>(value, Mode != AccessMode::Normal,
                                                   Mode == AccessMode::Isr, operation);
        }
#else
        return fail<Error>(frontend_error(frontend::Error::Disabled, operation));
#endif
    }

    template <typename System, typename MetricT> static consteval void validate()
    {
#if defined(CONFIG_SOLAR_METRICS)
        using Instrument = InstrumentTraits<typename MetricT::Instrument>;
        static_assert(TimerOperation ? Instrument::timer
                                     : (Instrument::distribution && !Instrument::timer),
                      "SOLAR_DIAGNOSTIC_METRIC_OBSERVE_INSTRUMENT: operation requires its matching "
                      "distribution or timer instrument");
        if constexpr (Mode == AccessMode::Isr) {
            using Policies = typename System::MetricFacility::template Policies<MetricT>;
            static_assert(
                metric_isr_compatible<MetricT, Policies>,
                "SOLAR_DIAGNOSTIC_METRIC_ISR_POLICY: metric policy is not ISR compatible");
        }
#endif
    }

    template <typename MetricT>
    [[nodiscard]] static Result<Update, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, operation));
    }
};

template <AccessMode Mode> struct DurationFrontend
{
    using CatalogTag = Tag;
    template <typename MetricT> using Signature = Result<Update, Error>(DurationSample);

    template <typename System, typename MetricT>
    [[nodiscard]] static Result<Update, Error> invoke(DurationSample sample) noexcept
    {
#if defined(CONFIG_SOLAR_METRICS)
        if constexpr (!InstrumentTraits<typename MetricT::Instrument>::timer) {
            return fail<Error>(make_error<System, MetricT>(
                Operation::RecordDuration, Status::NotSupported, Reason::ReducerFailure));
        } else {
            using Value = typename MetricT::Value;
            using Ratio = typename MetricT::Unit::Ratio;
            const auto converted = sample.seconds * static_cast<long double>(Ratio::den) /
                                   static_cast<long double>(Ratio::num);
            if (!std::isfinite(converted) ||
                converted < static_cast<long double>(std::numeric_limits<Value>::lowest()) ||
                converted > static_cast<long double>(std::numeric_limits<Value>::max())) {
                const auto error = reject_metric<System, MetricT>(
                    Operation::RecordDuration, Status::Overflow, Reason::ConversionOverflow,
                    Mode == AccessMode::Try);
                return fail<Error>(error);
            }
            return observe_metric<System, MetricT>(static_cast<Value>(converted),
                                                   Mode != AccessMode::Normal, false,
                                                   Operation::RecordDuration);
        }
#else
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::RecordDuration));
#endif
    }

    template <typename System, typename MetricT> static consteval void validate()
    {
#if defined(CONFIG_SOLAR_METRICS)
        static_assert(InstrumentTraits<typename MetricT::Instrument>::timer,
                      "SOLAR_DIAGNOSTIC_METRIC_RECORD_INSTRUMENT: duration recording requires a "
                      "timer");
#endif
    }

    template <typename MetricT>
    [[nodiscard]] static Result<Update, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::RecordDuration));
    }
};

template <bool Try> struct GetFrontend
{
    using CatalogTag = Tag;
    template <typename MetricT> using Signature = Result<Reading<MetricT>, Error>();

    template <typename System, typename MetricT>
    [[nodiscard]] static Result<Reading<MetricT>, Error> invoke() noexcept
    {
#if defined(CONFIG_SOLAR_METRICS)
        return read_metric<System, MetricT>(Try);
#else
        return fail<Error>(
            frontend_error(frontend::Error::Disabled, Try ? Operation::TryGet : Operation::Get));
#endif
    }

    template <typename MetricT>
    [[nodiscard]] static Result<Reading<MetricT>, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Try ? Operation::TryGet : Operation::Get));
    }
};

template <bool Try> struct ResetFrontend
{
    using CatalogTag = Tag;
    template <typename MetricT> using Signature = Result<Update, Error>();

    template <typename System, typename MetricT>
    [[nodiscard]] static Result<Update, Error> invoke() noexcept
    {
#if defined(CONFIG_SOLAR_METRICS)
        using Policies = typename System::MetricFacility::template Policies<MetricT>;
        if constexpr (!std::is_same_v<typename Policies::Reset, RuntimeResettable>) {
            return fail<Error>(make_error<System, MetricT>(Operation::Reset, Status::NotSupported,
                                                           Reason::ResetForbidden));
        } else {
            return reset_metric<System, MetricT>(Try);
        }
#else
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::Reset));
#endif
    }

    template <typename System, typename MetricT> static consteval void validate()
    {
#if defined(CONFIG_SOLAR_METRICS)
        using Policies = typename System::MetricFacility::template Policies<MetricT>;
        static_assert(std::is_same_v<typename Policies::Reset, RuntimeResettable>,
                      "SOLAR_DIAGNOSTIC_METRIC_RESET_FORBIDDEN: metric is boot-reset-only");
#endif
    }

    template <typename MetricT>
    [[nodiscard]] static Result<Update, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Reset));
    }
};

struct MetricRecordFrontend
{
    using CatalogTag = Tag;
    template <typename MetricT> using Signature = Result<MetricRecord, Error>();

    template <typename System, typename MetricT>
    [[nodiscard]] static Result<MetricRecord, Error> invoke() noexcept
    {
#if defined(CONFIG_SOLAR_METRICS)
        return metric_record<System, MetricT>();
#else
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::Query));
#endif
    }

    template <typename MetricT>
    [[nodiscard]] static Result<MetricRecord, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Query));
    }
};

template <typename System, typename Application> void bind_metric_frontends() noexcept
{
    if constexpr (enabled) {
        frontend::bind_catalog<System, AddFrontend<AccessMode::Normal, Operation::Increment>,
                               Application>();
        frontend::bind_catalog<System, AddFrontend<AccessMode::Try, Operation::Increment>,
                               Application>();
        frontend::bind_catalog<System, AddFrontend<AccessMode::Isr, Operation::Increment>,
                               Application>();
        frontend::bind_catalog<System, AddFrontend<AccessMode::Normal, Operation::Add>,
                               Application>();
        frontend::bind_catalog<System, AddFrontend<AccessMode::Try, Operation::Add>, Application>();
        frontend::bind_catalog<System, AddFrontend<AccessMode::Isr, Operation::Add>, Application>();
        frontend::bind_catalog<System, SetFrontend<AccessMode::Normal>, Application>();
        frontend::bind_catalog<System, SetFrontend<AccessMode::Try>, Application>();
        frontend::bind_catalog<System, SetFrontend<AccessMode::Isr>, Application>();
        frontend::bind_catalog<System, ObserveFrontend<AccessMode::Normal>, Application>();
        frontend::bind_catalog<System, ObserveFrontend<AccessMode::Try>, Application>();
        frontend::bind_catalog<System, ObserveFrontend<AccessMode::Isr>, Application>();
        frontend::bind_catalog<System, ObserveFrontend<AccessMode::Normal, true>, Application>();
        frontend::bind_catalog<System, ObserveFrontend<AccessMode::Try, true>, Application>();
        frontend::bind_catalog<System, DurationFrontend<AccessMode::Normal>, Application>();
        frontend::bind_catalog<System, DurationFrontend<AccessMode::Try>, Application>();
        frontend::bind_catalog<System, GetFrontend<false>, Application>();
        frontend::bind_catalog<System, GetFrontend<true>, Application>();
        frontend::bind_catalog<System, ResetFrontend<false>, Application>();
        frontend::bind_catalog<System, ResetFrontend<true>, Application>();
        frontend::bind_catalog<System, MetricRecordFrontend, Application>();
    } else {
        frontend::bind_disabled<AddFrontend<AccessMode::Normal, Operation::Increment>,
                                Application>();
        frontend::bind_disabled<AddFrontend<AccessMode::Try, Operation::Increment>, Application>();
        frontend::bind_disabled<AddFrontend<AccessMode::Isr, Operation::Increment>, Application>();
        frontend::bind_disabled<AddFrontend<AccessMode::Normal, Operation::Add>, Application>();
        frontend::bind_disabled<AddFrontend<AccessMode::Try, Operation::Add>, Application>();
        frontend::bind_disabled<AddFrontend<AccessMode::Isr, Operation::Add>, Application>();
        frontend::bind_disabled<SetFrontend<AccessMode::Normal>, Application>();
        frontend::bind_disabled<SetFrontend<AccessMode::Try>, Application>();
        frontend::bind_disabled<SetFrontend<AccessMode::Isr>, Application>();
        frontend::bind_disabled<ObserveFrontend<AccessMode::Normal>, Application>();
        frontend::bind_disabled<ObserveFrontend<AccessMode::Try>, Application>();
        frontend::bind_disabled<ObserveFrontend<AccessMode::Isr>, Application>();
        frontend::bind_disabled<ObserveFrontend<AccessMode::Normal, true>, Application>();
        frontend::bind_disabled<ObserveFrontend<AccessMode::Try, true>, Application>();
        frontend::bind_disabled<DurationFrontend<AccessMode::Normal>, Application>();
        frontend::bind_disabled<DurationFrontend<AccessMode::Try>, Application>();
        frontend::bind_disabled<GetFrontend<false>, Application>();
        frontend::bind_disabled<GetFrontend<true>, Application>();
        frontend::bind_disabled<ResetFrontend<false>, Application>();
        frontend::bind_disabled<ResetFrontend<true>, Application>();
        frontend::bind_disabled<MetricRecordFrontend, Application>();
    }
}

} // namespace solar::metrics::detail

template <> struct solar::lifecycle::ApplicationBindingProtocol<solar::metrics::Tag>
{
    template <typename System, typename Application> static void bind() noexcept
    {
        solar::metrics::detail::bind_metric_frontends<System, Application>();
    }
};

template <> struct solar::lifecycle::CatalogActivationProtocol<solar::metrics::Tag>
{
    template <typename System>
    static constexpr bool participates =
        solar::metrics::enabled && System::MetricCatalog::size != 0;

    template <typename> [[nodiscard]] static solar::Result<void> commit() noexcept
    {
        return {};
    }

    template <typename System>
    [[nodiscard]] static solar::lifecycle::Failure failure(solar::lifecycle::Operation operation,
                                                           solar::Status status) noexcept
    {
        using Facility = typename System::MetricFacility;
        return {
            .component = System::Catalogs::template Of<solar::component::Tag>::template Entry<
                Facility>::local_id,
            .category = solar::lifecycle::ComponentCategory::Facility,
            .operation = operation,
            .status = status,
            .primary = true,
        };
    }

    template <typename System> static void activate() noexcept
    {
        if constexpr (solar::metrics::enabled) {
            System::MetricFacility::template activate_runtime<System>();
        }
    }
};
