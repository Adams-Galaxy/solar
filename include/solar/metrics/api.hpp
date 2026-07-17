#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>

#include "solar/metrics/protocol.hpp"

namespace solar::metrics
{

namespace detail
{

template <typename MetricT> consteval void require_counter()
{
    static_assert(InstrumentTraits<typename MetricT::Instrument>::counter,
                  "SOLAR_DIAGNOSTIC_METRIC_ADD_INSTRUMENT: increment and add require a counter");
}

template <typename MetricT> consteval void require_gauge()
{
    static_assert(InstrumentTraits<typename MetricT::Instrument>::gauge,
                  "SOLAR_DIAGNOSTIC_METRIC_SET_INSTRUMENT: set requires a gauge");
}

template <typename MetricT> consteval void require_distribution()
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    static_assert(Instrument::distribution && !Instrument::timer,
                  "SOLAR_DIAGNOSTIC_METRIC_OBSERVE_INSTRUMENT: observe requires a non-timer "
                  "distribution");
}

template <typename MetricT> consteval void require_timer()
{
    static_assert(InstrumentTraits<typename MetricT::Instrument>::timer,
                  "SOLAR_DIAGNOSTIC_METRIC_RECORD_INSTRUMENT: record requires a timer");
}

template <typename Application, AccessMode Mode, Operation Verb, Metric MetricT>
[[nodiscard]] Result<Update, Error> add_for(typename MetricT::Value amount) noexcept
{
    require_counter<MetricT>();
    return frontend::Operation<AddFrontend<Mode, Verb>, MetricT, Application>::call(amount);
}

template <typename Application, AccessMode Mode, Metric MetricT>
[[nodiscard]] Result<Update, Error> set_for(typename MetricT::Value value) noexcept
{
    require_gauge<MetricT>();
    return frontend::Operation<SetFrontend<Mode>, MetricT, Application>::call(value);
}

template <typename Application, AccessMode Mode, Metric MetricT>
[[nodiscard]] Result<Update, Error> observe_for(typename MetricT::Value value) noexcept
{
    require_distribution<MetricT>();
    return frontend::Operation<ObserveFrontend<Mode>, MetricT, Application>::call(value);
}

template <typename Application, AccessMode Mode, Metric MetricT>
[[nodiscard]] Result<Update, Error> record_value_for(typename MetricT::Value value) noexcept
{
    require_timer<MetricT>();
    return frontend::Operation<ObserveFrontend<Mode, true>, MetricT, Application>::call(value);
}

template <typename Application, AccessMode Mode, Metric MetricT, typename Rep, typename Period>
[[nodiscard]] Result<Update, Error>
record_duration_for(std::chrono::duration<Rep, Period> duration) noexcept
{
    require_timer<MetricT>();
    const auto seconds = std::chrono::duration<long double>{duration}.count();
    return frontend::Operation<DurationFrontend<Mode>, MetricT, Application>::call(
        DurationSample{.seconds = seconds});
}

template <Metric MetricT, typename Application = DefaultApplication>
[[nodiscard]] Error reject_adapter_conversion(Operation operation = Operation::Observe) noexcept
{
#if defined(CONFIG_SOLAR_METRICS)
    using System = bound_system_t<Application>;
    return reject_metric<System, MetricT>(operation, Status::Overflow, Reason::ConversionOverflow,
                                          false);
#else
    return frontend_error(frontend::Error::Disabled, operation);
#endif
}

template <typename MetricT, typename View> struct ViewTraits
{
    static constexpr bool valid = false;
};

template <typename MetricT> struct ViewTraits<MetricT, view::Value>
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    static constexpr bool valid = Instrument::counter || Instrument::gauge;
    using Value = typename MetricT::Value;
    static constexpr ViewKind kind = ViewKind::Value;
};

template <typename MetricT> struct ViewTraits<MetricT, view::Count>
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    static constexpr bool valid = Instrument::distribution;
    using Value = std::uint64_t;
    static constexpr ViewKind kind = ViewKind::Count;
};

template <typename MetricT> struct ViewTraits<MetricT, view::Sum>
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    using Reducer = typename Instrument::Reducer;
    static constexpr bool valid = Instrument::distribution && (std::is_same_v<Reducer, Summary> ||
                                                               requires { Reducer::boundaries; });
    using Value = accumulator_t<typename MetricT::Value>;
    static constexpr ViewKind kind = ViewKind::Sum;
};

template <typename MetricT> struct ViewTraits<MetricT, view::Minimum>
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    using Reducer = typename Instrument::Reducer;
    static constexpr bool valid = Instrument::distribution && (std::is_same_v<Reducer, Minimum> ||
                                                               std::is_same_v<Reducer, Summary>);
    using Value = typename MetricT::Value;
    static constexpr ViewKind kind = ViewKind::Minimum;
};

template <typename MetricT> struct ViewTraits<MetricT, view::Maximum>
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    using Reducer = typename Instrument::Reducer;
    static constexpr bool valid = Instrument::distribution && (std::is_same_v<Reducer, Maximum> ||
                                                               std::is_same_v<Reducer, Summary>);
    using Value = typename MetricT::Value;
    static constexpr ViewKind kind = ViewKind::Maximum;
};

template <typename MetricT> struct ViewTraits<MetricT, view::Mean>
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    using Reducer = typename Instrument::Reducer;
    static constexpr bool valid = Instrument::distribution &&
                                  (std::is_same_v<Reducer, Summary> ||
                                   requires { Reducer::size; } ||
                                   requires { Reducer::numerator; Reducer::denominator; });
    using Value = double;
    static constexpr ViewKind kind = ViewKind::Mean;
};

template <typename MetricT> struct ViewTraits<MetricT, view::Last>
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    using Reducer = typename Instrument::Reducer;
    static constexpr bool valid = Instrument::distribution &&
                                  (std::is_same_v<Reducer, Last> || requires { Reducer::size; } ||
                                   requires { Reducer::numerator; Reducer::denominator; });
    using Value = typename MetricT::Value;
    static constexpr ViewKind kind = ViewKind::Last;
};

template <typename MetricT, std::size_t Index> struct ViewTraits<MetricT, view::Bucket<Index>>
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    using Reducer = typename Instrument::Reducer;
    static constexpr bool histogram = Instrument::distribution && requires { Reducer::boundaries; };
    static constexpr bool valid = [] {
        if constexpr (histogram) {
            return Index < Reducer::bucket_count;
        } else {
            return false;
        }
    }();
    using Value = std::uint64_t;
    static constexpr ViewKind kind = ViewKind::Bucket;
    static constexpr std::size_t index = Index;
};

template <typename MetricT, typename View>
using view_value_t = typename ViewTraits<MetricT, View>::Value;

template <typename MetricT, typename View>
[[nodiscard]] view_value_t<MetricT, View> extract_view(const Reading<MetricT>& reading) noexcept
{
    if constexpr (std::is_same_v<View, view::Value>) {
        return reading.value;
    } else if constexpr (std::is_same_v<View, view::Count>) {
        return reading.count;
    } else if constexpr (std::is_same_v<View, view::Sum>) {
        return reading.sum;
    } else if constexpr (std::is_same_v<View, view::Minimum>) {
        if constexpr (std::is_same_v<
                          typename InstrumentTraits<typename MetricT::Instrument>::Reducer,
                          Minimum>) {
            return reading.value;
        } else {
            return reading.minimum;
        }
    } else if constexpr (std::is_same_v<View, view::Maximum>) {
        if constexpr (std::is_same_v<
                          typename InstrumentTraits<typename MetricT::Instrument>::Reducer,
                          Maximum>) {
            return reading.value;
        } else {
            return reading.maximum;
        }
    } else if constexpr (std::is_same_v<View, view::Mean>) {
        return reading.mean;
    } else if constexpr (std::is_same_v<View, view::Last>) {
        if constexpr (std::is_same_v<
                          typename InstrumentTraits<typename MetricT::Instrument>::Reducer, Last>) {
            return reading.value;
        } else {
            return reading.latest;
        }
    } else {
        return reading.buckets[ViewTraits<MetricT, View>::index];
    }
}

template <typename Value> [[nodiscard]] ScalarValue erase_scalar(Value value) noexcept
{
    if constexpr (std::is_same_v<Value, bool>) {
        return value;
    } else if constexpr (std::is_floating_point_v<Value>) {
        return static_cast<double>(value);
    } else if constexpr (std::is_signed_v<Value>) {
        return static_cast<std::int64_t>(value);
    } else {
        return static_cast<std::uint64_t>(value);
    }
}

template <typename System, typename MetricT, typename Value>
[[nodiscard]] MetricViewRecord make_view_record(const Reading<MetricT>& reading, ViewKind kind,
                                                std::uint16_t index, UnitDescriptor unit,
                                                Value value) noexcept
{
    return {
        .metric = System::MetricCatalog::template Entry<MetricT>::local_id,
        .view = kind,
        .view_index = index,
        .owner = System::MetricCatalog::template Entry<MetricT>::owner_view().component,
        .instrument = InstrumentTraits<typename MetricT::Instrument>::kind,
        .unit = unit,
        .value = erase_scalar(value),
        .epoch = reading.epoch,
        .revision = reading.revision,
        .updated_at = reading.updated_at,
        .initialized = reading.initialized,
        .saturated = reading.saturated,
        .wrapped = reading.wrapped,
    };
}

#if defined(CONFIG_SOLAR_METRICS)
template <typename System, typename MetricT, typename Emit>
void emit_metric_views(const Reading<MetricT>& reading, Emit&& emit)
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    const auto metric_unit = MetricT::Unit::descriptor;
    const auto count_unit = units::Count::descriptor;
    if constexpr (Instrument::counter || Instrument::gauge) {
        emit(ViewKind::Value, 0, 0, metric_unit, reading.value);
    } else {
        using Reducer = typename Instrument::Reducer;
        if constexpr (std::is_same_v<Reducer, Last>) {
            emit(ViewKind::Last, 0, 0, metric_unit, reading.value);
            emit(ViewKind::Count, 1, 0, count_unit, reading.count);
        } else if constexpr (std::is_same_v<Reducer, Minimum>) {
            emit(ViewKind::Minimum, 0, 0, metric_unit, reading.value);
            emit(ViewKind::Count, 1, 0, count_unit, reading.count);
        } else if constexpr (std::is_same_v<Reducer, Maximum>) {
            emit(ViewKind::Maximum, 0, 0, metric_unit, reading.value);
            emit(ViewKind::Count, 1, 0, count_unit, reading.count);
        } else if constexpr (std::is_same_v<Reducer, Summary>) {
            emit(ViewKind::Count, 0, 0, count_unit, reading.count);
            emit(ViewKind::Sum, 1, 0, metric_unit, reading.sum);
            emit(ViewKind::Minimum, 2, 0, metric_unit, reading.minimum);
            emit(ViewKind::Maximum, 3, 0, metric_unit, reading.maximum);
            emit(ViewKind::Mean, 4, 0, metric_unit, reading.mean);
        } else if constexpr (requires { Reducer::size; }) {
            emit(ViewKind::Count, 0, 0, count_unit, reading.count);
            emit(ViewKind::Last, 1, 0, metric_unit, reading.latest);
            emit(ViewKind::Mean, 2, 0, metric_unit, reading.mean);
        } else if constexpr (requires {
                                 Reducer::numerator;
                                 Reducer::denominator;
                             }) {
            emit(ViewKind::Last, 0, 0, metric_unit, reading.latest);
            emit(ViewKind::Mean, 1, 0, metric_unit, reading.mean);
        } else if constexpr (requires { Reducer::boundaries; }) {
            emit(ViewKind::Count, 0, 0, count_unit, reading.count);
            emit(ViewKind::Sum, 1, 0, metric_unit, reading.sum);
            for (std::size_t index = 0; index < reading.buckets.size(); ++index) {
                emit(ViewKind::Bucket, static_cast<std::uint16_t>(index + 2),
                     static_cast<std::uint16_t>(index), count_unit, reading.buckets[index]);
            }
        }
    }
}

template <typename MetricT>
inline constexpr std::size_t metric_view_count = [] {
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    if constexpr (Instrument::counter || Instrument::gauge) {
        return std::size_t{1};
    } else {
        return ReducerTraits<typename Instrument::Reducer>::views;
    }
}();

template <typename Application>
[[nodiscard]] Result<RecordPage, Error> read_records(Cursor cursor,
                                                     std::span<MetricViewRecord> output) noexcept
{
    using System = bound_system_t<Application>;
    RecordPage page{.next = cursor};
    std::optional<Error> failure;
    std::size_t metric_index{};
    for_each_type<typename System::MetricArchitecture::Metrics>([&]<typename MetricT> {
        if (failure) {
            ++metric_index;
            return;
        }
        auto reading = read_metric<System, MetricT>(false);
        if (!reading) {
            failure = reading.error();
            ++metric_index;
            return;
        }
        const bool stale_metric =
            metric_index == cursor.metric && cursor.epoch_valid && reading->epoch != cursor.epoch;
        page.stale = page.stale || stale_metric;
        const auto first_view = stale_metric ? std::uint16_t{} : cursor.view;
        emit_metric_views<System, MetricT>(*reading, [&](ViewKind kind, std::uint16_t cursor_view,
                                                         std::uint16_t record_index,
                                                         UnitDescriptor unit, auto value) {
            if (metric_index < cursor.metric ||
                (metric_index == cursor.metric && cursor_view < first_view)) {
                return;
            }
            ++page.available;
            if (page.written == output.size()) {
                return;
            }
            output[page.written++] =
                make_view_record<System, MetricT>(*reading, kind, record_index, unit, value);
            if (cursor_view + 1 == metric_view_count<MetricT>) {
                page.next = Cursor{.metric = static_cast<std::uint16_t>(metric_index + 1)};
            } else {
                page.next = Cursor{.metric = static_cast<std::uint16_t>(metric_index),
                                   .view = static_cast<std::uint16_t>(cursor_view + 1),
                                   .epoch = reading->epoch,
                                   .epoch_valid = true};
            }
        });
        ++metric_index;
    });
    if (failure) {
        return fail<Error>(*failure);
    }
    if (page.written == page.available) {
        page.next = Cursor{.metric = static_cast<std::uint16_t>(System::MetricCatalog::size)};
    }
    return page;
}
#endif

#if defined(CONFIG_SOLAR_METRICS)
template <typename System, typename Entry> [[nodiscard]] consteval DescriptorView make_view()
{
    using MetricT = typename Entry::Declaration;
    using Policies = typename System::MetricFacility::template Policies<MetricT>;
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    using Concurrency = effective_concurrency_t<MetricT, Policies>;
    constexpr auto view_count = [] {
        if constexpr (Instrument::counter || Instrument::gauge) {
            return std::size_t{1};
        } else {
            return ReducerTraits<typename Instrument::Reducer>::views;
        }
    }();
    return {
        .local_id = Entry::local_id,
        .descriptor = catalog::descriptor_for_view(descriptor_traits<Tag, MetricT>::descriptor),
        .owner = Entry::owner_view(),
        .origin = Entry::origin_kind,
        .unit = MetricT::Unit::descriptor,
        .instrument = Instrument::kind,
        .concurrency = ConcurrencyTraits<Concurrency>::kind,
        .overflow = OverflowTraits<typename Policies::Overflow>::kind,
        .value_size = sizeof(typename MetricT::Value),
        .value_alignment = alignof(typename MetricT::Value),
        .state_size = sizeof(MetricSlot<MetricT, Policies>),
        .view_count = view_count,
        .runtime_resettable = std::is_same_v<typename Policies::Reset, RuntimeResettable>,
        .timestamped = std::is_same_v<typename Policies::Timestamps, timestamps::Enabled>,
        .isr_compatible = metric_isr_compatible<MetricT, Policies>,
    };
}

template <typename System, typename Entries> struct DescriptorTable;

template <typename System, typename... Entries> struct DescriptorTable<System, TypeList<Entries...>>
{
    inline static constexpr std::array<DescriptorView, sizeof...(Entries)> values{
        make_view<System, Entries>()...};
};
#endif

template <typename Application> struct DescriptorAccess
{
#if defined(CONFIG_SOLAR_METRICS)
    using System = bound_system_t<Application>;
    using Catalog = typename System::MetricCatalog;
    using Table = DescriptorTable<System, typename Catalog::EntryTypes>;

    [[nodiscard]] static constexpr std::span<const DescriptorView> all() noexcept
    {
        return Table::values;
    }

    template <Metric MetricT>
    [[nodiscard]] static Result<std::reference_wrapper<const DescriptorView>, catalog::LookupError>
    one() noexcept
    {
        if constexpr (Catalog::template contains<MetricT>) {
            return std::cref(Table::values[Catalog::template Entry<MetricT>::local_id.index()]);
        } else {
            static_assert(!frontend::strict,
                          "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_METRIC_QUERY: queried metric is "
                          "absent from the bound catalog");
            return fail<catalog::LookupError>(catalog::LookupError::Unavailable);
        }
    }
#else
    [[nodiscard]] static constexpr std::span<const DescriptorView> all() noexcept
    {
        return {};
    }

    template <Metric MetricT>
    [[nodiscard]] static Result<std::reference_wrapper<const DescriptorView>, catalog::LookupError>
    one() noexcept
    {
        return fail<catalog::LookupError>(catalog::LookupError::Unavailable);
    }
#endif
};

} // namespace detail

template <Metric MetricT> [[nodiscard]] Result<Update, Error> inc() noexcept
{
    return detail::add_for<DefaultApplication, detail::AccessMode::Normal, Operation::Increment,
                           MetricT>(typename MetricT::Value{1});
}

template <Metric MetricT> [[nodiscard]] Result<Update, Error> try_inc() noexcept
{
    return detail::add_for<DefaultApplication, detail::AccessMode::Try, Operation::Increment,
                           MetricT>(typename MetricT::Value{1});
}

template <Metric MetricT> [[nodiscard]] Result<Update, Error> try_inc_isr() noexcept
{
    return detail::add_for<DefaultApplication, detail::AccessMode::Isr, Operation::Increment,
                           MetricT>(typename MetricT::Value{1});
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> add(typename MetricT::Value amount) noexcept
{
    return detail::add_for<DefaultApplication, detail::AccessMode::Normal, Operation::Add, MetricT>(
        amount);
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> try_add(typename MetricT::Value amount) noexcept
{
    return detail::add_for<DefaultApplication, detail::AccessMode::Try, Operation::Add, MetricT>(
        amount);
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> try_add_isr(typename MetricT::Value amount) noexcept
{
    return detail::add_for<DefaultApplication, detail::AccessMode::Isr, Operation::Add, MetricT>(
        amount);
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> set(typename MetricT::Value value) noexcept
{
    return detail::set_for<DefaultApplication, detail::AccessMode::Normal, MetricT>(value);
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> try_set(typename MetricT::Value value) noexcept
{
    return detail::set_for<DefaultApplication, detail::AccessMode::Try, MetricT>(value);
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> try_set_isr(typename MetricT::Value value) noexcept
{
    return detail::set_for<DefaultApplication, detail::AccessMode::Isr, MetricT>(value);
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> observe(typename MetricT::Value value) noexcept
{
    return detail::observe_for<DefaultApplication, detail::AccessMode::Normal, MetricT>(value);
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> try_observe(typename MetricT::Value value) noexcept
{
    return detail::observe_for<DefaultApplication, detail::AccessMode::Try, MetricT>(value);
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> try_observe_isr(typename MetricT::Value value) noexcept
{
    return detail::observe_for<DefaultApplication, detail::AccessMode::Isr, MetricT>(value);
}

template <Metric MetricT>
[[nodiscard]] Result<Update, Error> record(typename MetricT::Value value) noexcept
{
    return detail::record_value_for<DefaultApplication, detail::AccessMode::Normal, MetricT>(value);
}

template <Metric MetricT, typename Rep, typename Period>
[[nodiscard]] Result<Update, Error> record(std::chrono::duration<Rep, Period> duration) noexcept
{
    return detail::record_duration_for<DefaultApplication, detail::AccessMode::Normal, MetricT>(
        duration);
}

template <Metric MetricT, typename Rep, typename Period>
[[nodiscard]] Result<Update, Error> try_record(std::chrono::duration<Rep, Period> duration) noexcept
{
    return detail::record_duration_for<DefaultApplication, detail::AccessMode::Try, MetricT>(
        duration);
}

template <Metric MetricT> [[nodiscard]] Result<Reading<MetricT>, Error> get() noexcept
{
    return frontend::Operation<detail::GetFrontend<false>, MetricT>::call();
}

template <Metric MetricT> [[nodiscard]] Result<Reading<MetricT>, Error> try_get() noexcept
{
    return frontend::Operation<detail::GetFrontend<true>, MetricT>::call();
}

template <Metric MetricT, typename View>
[[nodiscard]] Result<detail::view_value_t<MetricT, View>, Error> get_view() noexcept
{
    static_assert(detail::ViewTraits<MetricT, View>::valid,
                  "SOLAR_DIAGNOSTIC_METRIC_UNSUPPORTED_VIEW: metric does not expose this view");
    return get<MetricT>().transform(
        [](const auto& reading) { return detail::extract_view<MetricT, View>(reading); });
}

template <Metric MetricT> [[nodiscard]] Result<Update, Error> reset() noexcept
{
    static_assert(std::is_same_v<typename detail::DeclaredReset<MetricT>::type, RuntimeResettable>,
                  "SOLAR_DIAGNOSTIC_METRIC_RESET_FORBIDDEN: metric is boot-reset-only");
    return frontend::Operation<detail::ResetFrontend<false>, MetricT>::call();
}

template <Metric MetricT> [[nodiscard]] Result<Update, Error> try_reset() noexcept
{
    static_assert(std::is_same_v<typename detail::DeclaredReset<MetricT>::type, RuntimeResettable>,
                  "SOLAR_DIAGNOSTIC_METRIC_RESET_FORBIDDEN: metric is boot-reset-only");
    return frontend::Operation<detail::ResetFrontend<true>, MetricT>::call();
}

namespace records
{
template <Metric MetricT> [[nodiscard]] Result<MetricRecord, Error> metric() noexcept
{
    return frontend::Operation<detail::MetricRecordFrontend, MetricT>::call();
}

template <Metric MetricT, typename View, typename Application = DefaultApplication>
[[nodiscard]] Result<MetricViewRecord, Error> view() noexcept
{
    static_assert(detail::ViewTraits<MetricT, View>::valid,
                  "SOLAR_DIAGNOSTIC_METRIC_UNSUPPORTED_VIEW: metric does not expose this view");
#if defined(CONFIG_SOLAR_METRICS)
    using System = bound_system_t<Application>;
    auto reading = frontend::Operation<detail::GetFrontend<false>, MetricT, Application>::call();
    if (!reading) {
        return fail<Error>(reading.error());
    }
    constexpr auto kind = detail::ViewTraits<MetricT, View>::kind;
    constexpr auto index = [] {
        if constexpr (requires { detail::ViewTraits<MetricT, View>::index; }) {
            return static_cast<std::uint16_t>(detail::ViewTraits<MetricT, View>::index);
        } else {
            return std::uint16_t{};
        }
    }();
    constexpr bool count_unit = std::is_same_v<View, solar::metrics::view::Count> ||
                                requires { detail::ViewTraits<MetricT, View>::index; };
    return detail::make_view_record<System, MetricT>(
        *reading, kind, index, count_unit ? units::Count::descriptor : MetricT::Unit::descriptor,
        detail::extract_view<MetricT, View>(*reading));
#else
    return fail<Error>(detail::frontend_error(frontend::Error::Disabled, Operation::GetView));
#endif
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<RecordPage, Error> read(Cursor cursor,
                                             std::span<MetricViewRecord> output) noexcept
{
#if defined(CONFIG_SOLAR_METRICS)
    return detail::read_records<Application>(cursor, output);
#else
    return fail<Error>(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
#endif
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<FacilityRecord, Error> facility() noexcept
{
#if defined(CONFIG_SOLAR_METRICS)
    using System = bound_system_t<Application>;
    return System::MetricFacility::record();
#else
    return fail<Error>(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
#endif
}
} // namespace records

namespace catalog
{
template <typename Application = DefaultApplication>
[[nodiscard]] constexpr std::span<const DescriptorView> descriptors() noexcept
{
    return detail::DescriptorAccess<Application>::all();
}

template <Metric MetricT, typename Application = DefaultApplication>
[[nodiscard]] Result<std::reference_wrapper<const DescriptorView>, solar::catalog::LookupError>
descriptor() noexcept
{
    return detail::DescriptorAccess<Application>::template one<MetricT>();
}
} // namespace catalog

template <Metric MetricT, typename Application = DefaultApplication,
          typename Clock = kernel::SteadyClock>
class ScopedTimer
{
  public:
    ScopedTimer() noexcept : started_(Clock::now()) {}

    ~ScopedTimer()
    {
        if (active_) {
            (void)finish();
        }
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    ScopedTimer(ScopedTimer&& other) noexcept
        : started_(other.started_), active_(std::exchange(other.active_, false))
    {}

    ScopedTimer& operator=(ScopedTimer&&) = delete;

    [[nodiscard]] Result<Update, Error> finish() noexcept
    {
        if (!active_) {
            return fail<Error>({.status = solar::Status::Already,
                                .reason = Reason::InternalInvariant,
                                .operation = Operation::RecordDuration});
        }
        active_ = false;
        return detail::record_duration_for<Application, detail::AccessMode::Normal, MetricT>(
            Clock::now() - started_);
    }

    void cancel() noexcept
    {
        active_ = false;
    }

    [[nodiscard]] bool active() const noexcept
    {
        return active_;
    }

  private:
    typename Clock::time_point started_{};
    bool active_{true};
};

template <Metric MetricT, typename Clock = kernel::SteadyClock>
[[nodiscard]] ScopedTimer<MetricT, DefaultApplication, Clock> scoped() noexcept
{
    detail::require_timer<MetricT>();
    return {};
}

template <typename Application> struct Of
{
    template <Metric MetricT> [[nodiscard]] static Result<Update, Error> inc() noexcept
    {
        return detail::add_for<Application, detail::AccessMode::Normal, Operation::Increment,
                               MetricT>(typename MetricT::Value{1});
    }

    template <Metric MetricT>
    [[nodiscard]] static Result<Update, Error> add(typename MetricT::Value amount) noexcept
    {
        return detail::add_for<Application, detail::AccessMode::Normal, Operation::Add, MetricT>(
            amount);
    }

    template <Metric MetricT>
    [[nodiscard]] static Result<Update, Error> set(typename MetricT::Value value) noexcept
    {
        return detail::set_for<Application, detail::AccessMode::Normal, MetricT>(value);
    }

    template <Metric MetricT>
    [[nodiscard]] static Result<Update, Error> observe(typename MetricT::Value value) noexcept
    {
        return detail::observe_for<Application, detail::AccessMode::Normal, MetricT>(value);
    }

    template <Metric MetricT> [[nodiscard]] static Result<Reading<MetricT>, Error> get() noexcept
    {
        return frontend::Operation<detail::GetFrontend<false>, MetricT, Application>::call();
    }

    template <Metric MetricT, typename Clock = kernel::SteadyClock>
    [[nodiscard]] static ScopedTimer<MetricT, Application, Clock> scoped() noexcept
    {
        detail::require_timer<MetricT>();
        return {};
    }
};

} // namespace solar::metrics
