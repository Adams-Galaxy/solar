#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/mutex.hpp"
#include "solar/kernel/spinlock.hpp"
#include "solar/kernel/time.hpp"
#include "solar/metrics/declaration.hpp"

namespace solar::metrics::detail
{

template <typename MetricT, typename Instrument = typename MetricT::Instrument>
struct InstrumentState;

template <typename MetricT> struct InstrumentState<MetricT, Counter>
{
    typename MetricT::Value value{};
};

template <typename MetricT> struct InstrumentState<MetricT, Gauge>
{
    typename MetricT::Value value{};
    bool initialized{};
};

template <typename MetricT, typename Reducer>
struct InstrumentState<MetricT, Distribution<Reducer>>
    : ReducerState<typename MetricT::Value, Reducer>
{};

template <typename MetricT, typename Reducer>
struct InstrumentState<MetricT, Timer<Reducer>> : InstrumentState<MetricT, Distribution<Reducer>>
{};

template <typename MetricT> struct SlotState
{
    InstrumentState<MetricT> instrument{};
    MetricRecord record{};
};

inline constexpr bool atomic_metadata_lock_free = std::atomic<std::uint32_t>::is_always_lock_free &&
                                                  std::atomic<std::uint64_t>::is_always_lock_free &&
                                                  std::atomic<std::int64_t>::is_always_lock_free &&
                                                  std::atomic_bool::is_always_lock_free;

template <typename MetricT>
inline constexpr bool atomic_metric_valid =
    (InstrumentTraits<typename MetricT::Instrument>::counter ||
     InstrumentTraits<typename MetricT::Instrument>::gauge) &&
    std::atomic<typename MetricT::Value>::is_always_lock_free && atomic_metadata_lock_free;

template <typename MetricT, typename Requested> struct EffectiveConcurrency
{
    using type = Requested;
};

template <typename MetricT> struct EffectiveConcurrency<MetricT, concurrency::Automatic>
{
  private:
    static constexpr bool atomic_available =
        atomic_metric_valid<MetricT> && IS_ENABLED(CONFIG_SOLAR_METRICS_ATOMIC_BACKEND);
    static constexpr bool spin_available = IS_ENABLED(CONFIG_SOLAR_METRICS_SPINLOCK_BACKEND);
    static constexpr bool mutex_available = IS_ENABLED(CONFIG_SOLAR_METRICS_MUTEX_BACKEND);
    static constexpr bool small_state = sizeof(SlotState<MetricT>) <= 256;

  public:
    static_assert(atomic_available || spin_available || mutex_available,
                  "SOLAR_DIAGNOSTIC_METRIC_NO_CONCURRENCY_BACKEND: automatic concurrency has no "
                  "usable compiled backend");
    using type = std::conditional_t<
        atomic_available, concurrency::Atomic,
        std::conditional_t<spin_available && (small_state || !mutex_available),
                           concurrency::SpinLocked, concurrency::MutexProtected>>;
};

template <typename MetricT, typename Policies>
using effective_concurrency_t =
    typename EffectiveConcurrency<MetricT, typename Policies::Concurrency>::type;

template <typename MetricT, typename Policies>
inline constexpr bool metric_isr_compatible = [] {
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    using Concurrency = effective_concurrency_t<MetricT, Policies>;
    if constexpr (std::is_same_v<Concurrency, concurrency::MutexProtected>) {
        return false;
    } else if constexpr (Instrument::distribution) {
        return ReducerTraits<typename Instrument::Reducer>::bounded_isr;
    } else {
        return true;
    }
}();

template <typename MetricT>
[[nodiscard]] Error slot_error(LocalId id, Operation operation, Status status,
                               Reason reason) noexcept
{
    return {.status = status, .reason = reason, .operation = operation, .metric = id};
}

template <typename MetricT, typename Policies>
[[nodiscard]] constexpr bool valid_numeric(typename MetricT::Value value) noexcept
{
    if constexpr (std::is_floating_point_v<typename MetricT::Value> &&
                  std::is_same_v<typename Policies::Numeric, numeric::RejectNonFinite>) {
        return std::isfinite(value);
    }
    return true;
}

template <typename Policies>
void record_failure(MetricRecord& record, Status status, Reason reason) noexcept
{
    ++record.rejected;
    record.last_status = status;
    record.last_failure = reason;
    if constexpr (std::is_same_v<typename Policies::Timestamps, timestamps::Enabled>) {
        record.failure_at = kernel::now_ticks();
    }
}

template <typename Value, typename Overflow>
[[nodiscard]] Result<std::pair<Value, UpdateDisposition>, Reason> checked_add(Value current,
                                                                              Value amount) noexcept
{
    const auto maximum = std::numeric_limits<Value>::max();
    if (amount <= maximum - current) {
        return std::pair{static_cast<Value>(current + amount), UpdateDisposition::Updated};
    }
    if constexpr (std::is_same_v<Overflow, overflow::Reject>) {
        return fail(Reason::Overflow);
    } else if constexpr (std::is_same_v<Overflow, overflow::Wrap>) {
        return std::pair{static_cast<Value>(current + amount), UpdateDisposition::Wrapped};
    } else {
        return std::pair{maximum, UpdateDisposition::Saturated};
    }
}

template <typename MetricT> void initialize_instrument(InstrumentState<MetricT>& state) noexcept
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    state = {};
    if constexpr (Instrument::gauge && requires { MetricT::initial_value; }) {
        state.value = static_cast<typename MetricT::Value>(MetricT::initial_value);
        state.initialized = true;
    }
}

template <typename MetricT>
[[nodiscard]] constexpr bool instrument_initialized(const InstrumentState<MetricT>& state) noexcept
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    if constexpr (Instrument::counter) {
        return true;
    } else if constexpr (Instrument::gauge) {
        return state.initialized;
    } else {
        return false;
    }
}

template <typename MetricT>
void initialize_state(SlotState<MetricT>& state, LocalId id, ConcurrencyKind concurrency) noexcept
{
    initialize_instrument<MetricT>(state.instrument);
    state.record = {
        .metric = id,
        .last_status = Status::Ok,
        .concurrency = concurrency,
        .initialized = instrument_initialized<MetricT>(state.instrument),
    };
}

template <typename MetricT>
void fill_metadata(Reading<MetricT>& reading, const MetricRecord& record) noexcept
{
    static_cast<ReadingMetadata&>(reading) = {
        .epoch = record.epoch,
        .revision = record.revision,
        .updates = record.updates,
        .updated_at = record.updated_at,
        .initialized = record.initialized,
        .saturated = record.saturated,
        .wrapped = record.wrapped,
        .degraded = record.degraded,
    };
}

template <typename MetricT>
[[nodiscard]] Reading<MetricT> make_reading(const SlotState<MetricT>& state) noexcept
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    Reading<MetricT> reading{};
    fill_metadata<MetricT>(reading, state.record);
    if constexpr (Instrument::counter || Instrument::gauge) {
        reading.value = state.instrument.value;
    } else {
        using Reducer = typename Instrument::Reducer;
        const auto& reducer = state.instrument;
        if constexpr (std::is_same_v<Reducer, Last> || std::is_same_v<Reducer, Minimum> ||
                      std::is_same_v<Reducer, Maximum>) {
            reading.value = reducer.value;
            reading.count = reducer.count;
        } else if constexpr (std::is_same_v<Reducer, Summary>) {
            reading.count = reducer.count;
            reading.sum = reducer.sum;
            reading.minimum = reducer.minimum;
            reading.maximum = reducer.maximum;
            reading.mean = reducer.count == 0 ? 0.0
                                              : static_cast<double>(reducer.sum) /
                                                    static_cast<double>(reducer.count);
        } else if constexpr (requires { Reducer::size; }) {
            reading.count = reducer.count;
            reading.latest = reducer.latest;
            reading.mean = reducer.count == 0 ? 0.0
                                              : static_cast<double>(reducer.sum) /
                                                    static_cast<double>(reducer.count);
        } else if constexpr (requires {
                                 Reducer::numerator;
                                 Reducer::denominator;
                             }) {
            reading.count = reducer.count;
            reading.latest = reducer.latest;
            reading.mean = static_cast<double>(reducer.mean);
        } else if constexpr (requires { Reducer::boundaries; }) {
            reading.count = reducer.count;
            reading.sum = reducer.sum;
            reading.buckets = reducer.buckets;
        }
    }
    return reading;
}

template <typename MetricT, typename Policies>
[[nodiscard]] Result<Update, Error>
add_counter(SlotState<MetricT>& state, typename MetricT::Value amount, Operation operation) noexcept
{
    if (amount == 0) {
        return Update{.disposition = UpdateDisposition::Unchanged,
                      .revision = state.record.revision,
                      .epoch = state.record.epoch};
    }
    const auto added = checked_add<typename MetricT::Value, typename Policies::Overflow>(
        state.instrument.value, amount);
    if (!added) {
        record_failure<Policies>(state.record, Status::Overflow, added.error());
        ++state.record.overflows;
        return fail(
            slot_error<MetricT>(state.record.metric, operation, Status::Overflow, added.error()));
    }
    state.instrument.value = added->first;
    ++state.record.revision;
    ++state.record.updates;
    state.record.last_status = Status::Ok;
    state.record.initialized = true;
    if (added->second == UpdateDisposition::Saturated) {
        state.record.saturated = true;
        ++state.record.saturations;
    }
    if (added->second == UpdateDisposition::Wrapped) {
        state.record.wrapped = true;
        ++state.record.overflows;
    }
    if constexpr (std::is_same_v<typename Policies::Timestamps, timestamps::Enabled>) {
        state.record.updated_at = kernel::now_ticks();
    }
    return Update{.disposition = added->second,
                  .revision = state.record.revision,
                  .epoch = state.record.epoch,
                  .saturated = state.record.saturated,
                  .wrapped = state.record.wrapped};
}

template <typename MetricT, typename Policies>
[[nodiscard]] Result<Update, Error>
set_gauge(SlotState<MetricT>& state, typename MetricT::Value value, Operation operation) noexcept
{
    if (!valid_numeric<MetricT, Policies>(value)) {
        record_failure<Policies>(state.record, Status::Invalid, Reason::InvalidNumeric);
        ++state.record.invalid_numeric;
        return fail(slot_error<MetricT>(state.record.metric, operation, Status::Invalid,
                                        Reason::InvalidNumeric));
    }
    state.instrument.value = value;
    state.instrument.initialized = true;
    ++state.record.revision;
    ++state.record.updates;
    state.record.last_status = Status::Ok;
    state.record.initialized = true;
    if constexpr (std::is_same_v<typename Policies::Timestamps, timestamps::Enabled>) {
        state.record.updated_at = kernel::now_ticks();
    }
    return Update{.revision = state.record.revision, .epoch = state.record.epoch};
}

template <typename Sum> struct SumUpdate
{
    Sum value{};
    UpdateDisposition disposition{UpdateDisposition::Updated};
};

template <typename Value, typename Sum, typename Overflow>
[[nodiscard]] Result<SumUpdate<Sum>, Reason> add_sum(Sum current, Value value) noexcept
{
    if constexpr (std::is_floating_point_v<Sum>) {
        const auto next = current + static_cast<Sum>(value);
        if (std::isfinite(next)) {
            return SumUpdate<Sum>{.value = next};
        }
        if constexpr (std::is_same_v<Overflow, overflow::Saturate>) {
            return SumUpdate<Sum>{
                .value = std::signbit(next) ? std::numeric_limits<Sum>::lowest()
                                            : std::numeric_limits<Sum>::max(),
                .disposition = UpdateDisposition::Saturated,
            };
        }
        return fail(Reason::Overflow);
    } else if constexpr (std::is_unsigned_v<Sum>) {
        return checked_add<Sum, Overflow>(current, static_cast<Sum>(value))
            .transform([](auto result) {
                return SumUpdate<Sum>{.value = result.first, .disposition = result.second};
            });
    } else {
        const auto converted = static_cast<Sum>(value);
        const bool overflowed =
            (converted > 0 && current > std::numeric_limits<Sum>::max() - converted) ||
            (converted < 0 && current < std::numeric_limits<Sum>::min() - converted);
        if (!overflowed) {
            return SumUpdate<Sum>{.value = static_cast<Sum>(current + converted)};
        }
        if constexpr (std::is_same_v<Overflow, overflow::Reject>) {
            return fail(Reason::Overflow);
        } else if constexpr (std::is_same_v<Overflow, overflow::Saturate>) {
            return SumUpdate<Sum>{
                .value = converted < 0 ? std::numeric_limits<Sum>::lowest()
                                       : std::numeric_limits<Sum>::max(),
                .disposition = UpdateDisposition::Saturated,
            };
        } else {
            using Unsigned = std::make_unsigned_t<Sum>;
            const auto wrapped = static_cast<Unsigned>(current) + static_cast<Unsigned>(converted);
            return SumUpdate<Sum>{.value = std::bit_cast<Sum>(wrapped),
                                  .disposition = UpdateDisposition::Wrapped};
        }
    }
}

template <typename MetricT, typename Policies>
[[nodiscard]] Result<Update, Error> observe_distribution(SlotState<MetricT>& state,
                                                         typename MetricT::Value value,
                                                         Operation operation) noexcept
{
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    using Reducer = typename Instrument::Reducer;
    if (!valid_numeric<MetricT, Policies>(value)) {
        record_failure<Policies>(state.record, Status::Invalid, Reason::InvalidNumeric);
        ++state.record.invalid_numeric;
        return fail(slot_error<MetricT>(state.record.metric, operation, Status::Invalid,
                                        Reason::InvalidNumeric));
    }

    auto prepared = state.instrument;
    auto disposition = UpdateDisposition::Updated;
    if constexpr (std::is_same_v<Reducer, Last>) {
        prepared.value = value;
        ++prepared.count;
        prepared.initialized = true;
    } else if constexpr (std::is_same_v<Reducer, Minimum>) {
        if (!prepared.initialized || value < prepared.value) {
            prepared.value = value;
        }
        ++prepared.count;
        prepared.initialized = true;
    } else if constexpr (std::is_same_v<Reducer, Maximum>) {
        if (!prepared.initialized || value > prepared.value) {
            prepared.value = value;
        }
        ++prepared.count;
        prepared.initialized = true;
    } else if constexpr (std::is_same_v<Reducer, Summary>) {
        auto sum =
            add_sum<typename MetricT::Value, decltype(prepared.sum), typename Policies::Overflow>(
                prepared.sum, value);
        if (!sum) {
            record_failure<Policies>(state.record, Status::Overflow, Reason::Overflow);
            ++state.record.overflows;
            return fail(slot_error<MetricT>(state.record.metric, operation, Status::Overflow,
                                            Reason::Overflow));
        }
        prepared.sum = sum->value;
        disposition = sum->disposition;
        if (!prepared.initialized) {
            prepared.minimum = value;
            prepared.maximum = value;
        } else {
            prepared.minimum = std::min(prepared.minimum, value);
            prepared.maximum = std::max(prepared.maximum, value);
        }
        ++prepared.count;
        prepared.initialized = true;
    } else if constexpr (requires { Reducer::size; }) {
        auto next_sum = prepared.sum;
        if (prepared.count == Reducer::size) {
            next_sum -= static_cast<decltype(next_sum)>(prepared.values[prepared.next]);
        }
        auto sum =
            add_sum<typename MetricT::Value, decltype(prepared.sum), typename Policies::Overflow>(
                next_sum, value);
        if (!sum) {
            record_failure<Policies>(state.record, Status::Overflow, Reason::Overflow);
            ++state.record.overflows;
            return fail(slot_error<MetricT>(state.record.metric, operation, Status::Overflow,
                                            Reason::Overflow));
        }
        if (prepared.count != Reducer::size) {
            ++prepared.count;
        }
        prepared.values[prepared.next] = value;
        prepared.next = (prepared.next + 1) % Reducer::size;
        prepared.sum = sum->value;
        disposition = sum->disposition;
        prepared.latest = value;
    } else if constexpr (requires {
                             Reducer::numerator;
                             Reducer::denominator;
                         }) {
        prepared.latest = value;
        if (!prepared.initialized) {
            prepared.mean = static_cast<long double>(value);
            prepared.initialized = true;
        } else {
            constexpr auto alpha = static_cast<long double>(Reducer::numerator) /
                                   static_cast<long double>(Reducer::denominator);
            prepared.mean =
                alpha * static_cast<long double>(value) + (1.0L - alpha) * prepared.mean;
        }
        ++prepared.count;
    } else if constexpr (requires { Reducer::boundaries; }) {
        const auto sum =
            add_sum<typename MetricT::Value, decltype(prepared.sum), typename Policies::Overflow>(
                prepared.sum, value);
        if (!sum) {
            record_failure<Policies>(state.record, Status::Overflow, Reason::Overflow);
            ++state.record.overflows;
            return fail(slot_error<MetricT>(state.record.metric, operation, Status::Overflow,
                                            Reason::Overflow));
        }
        prepared.sum = sum->value;
        disposition = sum->disposition;
        std::size_t bucket{};
        while (bucket < Reducer::boundaries.size() && value > Reducer::boundaries[bucket]) {
            ++bucket;
        }
        ++prepared.buckets[bucket];
        ++prepared.count;
    }

    state.instrument = prepared;
    ++state.record.revision;
    ++state.record.updates;
    state.record.last_status = Status::Ok;
    state.record.initialized = true;
    if (disposition == UpdateDisposition::Saturated) {
        state.record.saturated = true;
        state.record.degraded = true;
        ++state.record.saturations;
    } else if (disposition == UpdateDisposition::Wrapped) {
        state.record.wrapped = true;
        state.record.degraded = true;
        ++state.record.overflows;
    }
    if constexpr (std::is_same_v<typename Policies::Timestamps, timestamps::Enabled>) {
        state.record.updated_at = kernel::now_ticks();
    }
    return Update{.disposition = disposition,
                  .revision = state.record.revision,
                  .epoch = state.record.epoch,
                  .saturated = state.record.saturated,
                  .wrapped = state.record.wrapped};
}

template <typename MetricT, typename Policies>
[[nodiscard]] Result<Update, Error> reset_state(SlotState<MetricT>& state) noexcept
{
    initialize_instrument<MetricT>(state.instrument);
    ++state.record.epoch;
    state.record.revision = 0;
    state.record.updates = 0;
    state.record.updated_at = 0;
    state.record.saturated = false;
    state.record.wrapped = false;
    state.record.degraded = false;
    state.record.initialized = instrument_initialized<MetricT>(state.instrument);
    ++state.record.resets;
    state.record.last_status = Status::Ok;
    state.record.last_failure = Reason::None;
    state.record.failure_at = 0;
    return Update{.revision = 0, .epoch = state.record.epoch};
}

template <typename MetricT, typename Policies, typename Concurrency> class LockedSlot
{
  public:
    void initialize(LocalId id) noexcept
    {
        id_ = id;
        (void)access(false, Operation::Initialize, [&](auto& state) -> Result<void, Error> {
            constexpr auto kind = std::is_same_v<Concurrency, concurrency::SpinLocked>
                                      ? ConcurrencyKind::SpinLocked
                                      : ConcurrencyKind::MutexProtected;
            initialize_state<MetricT>(state, id, kind);
            return {};
        });
    }

    [[nodiscard]] Result<Update, Error> add(typename MetricT::Value amount, bool no_wait, bool isr,
                                            Operation operation) noexcept
    {
        if (isr && !metric_isr_compatible<MetricT, Policies>) {
            return fail(
                slot_error<MetricT>(id_, operation, Status::NotSupported, Reason::InvalidContext));
        }
        return access(no_wait || isr, operation, [&](auto& state) {
            return add_counter<MetricT, Policies>(state, amount, operation);
        });
    }

    [[nodiscard]] Result<Update, Error> set(typename MetricT::Value value, bool no_wait, bool isr,
                                            Operation operation) noexcept
    {
        if (isr && !metric_isr_compatible<MetricT, Policies>) {
            return fail(
                slot_error<MetricT>(id_, operation, Status::NotSupported, Reason::InvalidContext));
        }
        return access(no_wait || isr, operation, [&](auto& state) {
            return set_gauge<MetricT, Policies>(state, value, operation);
        });
    }

    [[nodiscard]] Result<Update, Error> observe(typename MetricT::Value value, bool no_wait,
                                                bool isr, Operation operation) noexcept
    {
        if (isr && !metric_isr_compatible<MetricT, Policies>) {
            return fail(
                slot_error<MetricT>(id_, operation, Status::NotSupported, Reason::InvalidContext));
        }
        return access(no_wait || isr, operation, [&](auto& state) {
            return observe_distribution<MetricT, Policies>(state, value, operation);
        });
    }

    [[nodiscard]] Result<Reading<MetricT>, Error> get(bool no_wait) noexcept
    {
        return access(no_wait, no_wait ? Operation::TryGet : Operation::Get,
                      [](auto& state) -> Result<Reading<MetricT>, Error> {
                          return make_reading<MetricT>(state);
                      });
    }

    [[nodiscard]] Result<Update, Error> reset(bool no_wait) noexcept
    {
        return access(no_wait, Operation::Reset,
                      [](auto& state) { return reset_state<MetricT, Policies>(state); });
    }

    [[nodiscard]] Error reject(Operation operation, Status status, Reason reason,
                               bool no_wait) noexcept
    {
        auto result = access(no_wait, operation, [&](auto& state) -> Result<void, Error> {
            record_failure<Policies>(state.record, status, reason);
            if (reason == Reason::Overflow || reason == Reason::ConversionOverflow) {
                ++state.record.overflows;
            } else if (reason == Reason::InvalidNumeric) {
                ++state.record.invalid_numeric;
            }
            return {};
        });
        return result ? slot_error<MetricT>(id_, operation, status, reason) : result.error();
    }

#if defined(CONFIG_ZTEST)
    template <typename Function> bool with_lock_for_test(Function&& function) noexcept
    {
        if constexpr (std::is_same_v<Concurrency, concurrency::SpinLocked>) {
            auto guard = lock_.acquire();
            std::forward<Function>(function)();
            return true;
        } else {
            auto guard = kernel::lock_guard(lock_);
            if (!guard) {
                return false;
            }
            std::forward<Function>(function)();
            return true;
        }
    }
#endif

    [[nodiscard]] MetricRecord record() noexcept
    {
        auto result =
            access(false, Operation::Query,
                   [](auto& state) -> Result<MetricRecord, Error> { return state.record; });
        return result ? *result
                      : MetricRecord{.metric = id_,
                                     .last_status = result.error().status,
                                     .last_failure = result.error().reason};
    }

  private:
    template <typename Function>
    [[nodiscard]] auto access(bool no_wait, Operation operation, Function&& function)
    {
        using Return = decltype(function(state_));
        if constexpr (std::is_same_v<Concurrency, concurrency::SpinLocked>) {
            auto guard = no_wait ? lock_.try_acquire()
                                 : std::optional<kernel::SpinLock::Guard>{lock_.acquire()};
            if (!guard) {
                record_contention_fallback();
                return Return{fail(
                    slot_error<MetricT>(id_, operation, Status::WouldBlock, Reason::WouldBlock))};
            }
            merge_contention();
            return function(state_);
        } else {
            auto guard = kernel::lock_guard(lock_, no_wait ? kernel::Timeout::no_wait()
                                                           : kernel::Timeout::forever());
            if (!guard) {
                record_contention_fallback();
                return Return{
                    fail(slot_error<MetricT>(id_, operation, guard.error(), Reason::WouldBlock))};
            }
            merge_contention();
            return function(state_);
        }
    }

    void merge_contention() noexcept
    {
        auto fallback_guard = fallback_lock_.acquire();
        const auto contention = std::exchange(contention_fallback_, 0);
        const auto contention_at = contention_at_fallback_;
        state_.record.contention += contention;
        if (contention != 0) {
            state_.record.last_status = Status::WouldBlock;
            state_.record.last_failure = Reason::WouldBlock;
            if constexpr (std::is_same_v<typename Policies::Timestamps, timestamps::Enabled>) {
                state_.record.failure_at = contention_at;
            }
        }
    }

    void record_contention_fallback() noexcept
    {
        auto guard = fallback_lock_.acquire();
        ++contention_fallback_;
        contention_at_fallback_ = kernel::now_ticks();
    }

    using Lock = std::conditional_t<std::is_same_v<Concurrency, concurrency::SpinLocked>,
                                    kernel::SpinLock, kernel::Mutex>;
    LocalId id_{};
    Lock lock_{};
    SlotState<MetricT> state_{};
    kernel::SpinLock fallback_lock_{};
    std::uint64_t contention_fallback_{};
    std::int64_t contention_at_fallback_{};
};

template <typename MetricT, typename Policies> class AtomicSlot
{
  public:
    using Value = typename MetricT::Value;

    void initialize(LocalId id) noexcept
    {
        id_ = id;
        value_.store(initial_value(), std::memory_order_relaxed);
        initialized_.store(initialized_value(), std::memory_order_relaxed);
        epoch_.store(0, std::memory_order_relaxed);
        revision_.store(0, std::memory_order_relaxed);
        updates_.store(0, std::memory_order_relaxed);
        rejected_.store(0, std::memory_order_relaxed);
        contention_.store(0, std::memory_order_relaxed);
        overflows_.store(0, std::memory_order_relaxed);
        saturations_.store(0, std::memory_order_relaxed);
        resets_.store(0, std::memory_order_relaxed);
        invalid_numeric_.store(0, std::memory_order_relaxed);
        updated_at_.store(0, std::memory_order_relaxed);
        failure_at_.store(0, std::memory_order_relaxed);
        last_status_.store(Status::Ok, std::memory_order_relaxed);
        last_reason_.store(Reason::None, std::memory_order_relaxed);
        saturated_.store(false, std::memory_order_relaxed);
        wrapped_.store(false, std::memory_order_relaxed);
        degraded_.store(false, std::memory_order_relaxed);
        writers_.store(0, std::memory_order_relaxed);
        generation_.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] Result<Update, Error> add(Value amount, bool, bool, Operation operation) noexcept
    {
        if (amount == 0) {
            return current_update(UpdateDisposition::Unchanged);
        }
        begin_write();
        auto current = value_.load(std::memory_order_relaxed);
        while (true) {
            auto added = checked_add<Value, typename Policies::Overflow>(current, amount);
            if (!added) {
                record_atomic_failure(Status::Overflow, Reason::Overflow);
                overflows_.fetch_add(1, std::memory_order_relaxed);
                end_write();
                return fail(
                    slot_error<MetricT>(id_, operation, Status::Overflow, Reason::Overflow));
            }
            if (value_.compare_exchange_weak(current, added->first, std::memory_order_acq_rel,
                                             std::memory_order_relaxed)) {
                return finish(added->second);
            }
        }
    }

    [[nodiscard]] Result<Update, Error> set(Value value, bool, bool, Operation operation) noexcept
    {
        if (!valid_numeric<MetricT, Policies>(value)) {
            record_atomic_failure(Status::Invalid, Reason::InvalidNumeric);
            invalid_numeric_.fetch_add(1, std::memory_order_relaxed);
            return fail(
                slot_error<MetricT>(id_, operation, Status::Invalid, Reason::InvalidNumeric));
        }
        begin_write();
        value_.store(value, std::memory_order_release);
        initialized_.store(true, std::memory_order_release);
        return finish(UpdateDisposition::Updated);
    }

    [[nodiscard]] Result<Update, Error> observe(Value, bool, bool, Operation operation) noexcept
    {
        return fail(
            slot_error<MetricT>(id_, operation, Status::NotSupported, Reason::ReducerFailure));
    }

    [[nodiscard]] Result<Reading<MetricT>, Error> get(bool no_wait) noexcept
    {
        while (true) {
            if (writers_.load(std::memory_order_acquire) != 0) {
                if (no_wait) {
                    record_atomic_failure(Status::WouldBlock, Reason::WouldBlock);
                    contention_.fetch_add(1, std::memory_order_relaxed);
                    return fail(slot_error<MetricT>(id_, Operation::TryGet, Status::WouldBlock,
                                                    Reason::WouldBlock));
                }
                k_sleep(K_TICKS(1));
                continue;
            }
            const auto generation = generation_.load(std::memory_order_acquire);
            Reading<MetricT> reading{};
            reading.value = value_.load(std::memory_order_acquire);
            reading.epoch = epoch_.load(std::memory_order_acquire);
            reading.revision = revision_.load(std::memory_order_acquire);
            reading.updates = updates_.load(std::memory_order_acquire);
            reading.updated_at = updated_at_.load(std::memory_order_acquire);
            reading.initialized = initialized_.load(std::memory_order_acquire);
            reading.saturated = saturated_.load(std::memory_order_acquire);
            reading.wrapped = wrapped_.load(std::memory_order_acquire);
            reading.degraded = degraded_.load(std::memory_order_acquire);
            if (writers_.load(std::memory_order_acquire) == 0 &&
                generation_.load(std::memory_order_acquire) == generation) {
                return reading;
            }
            if (no_wait) {
                record_atomic_failure(Status::WouldBlock, Reason::WouldBlock);
                contention_.fetch_add(1, std::memory_order_relaxed);
                return fail(slot_error<MetricT>(id_, Operation::TryGet, Status::WouldBlock,
                                                Reason::WouldBlock));
            }
        }
    }

    [[nodiscard]] Result<Update, Error> reset(bool) noexcept
    {
        begin_write();
        value_.store(initial_value(), std::memory_order_release);
        initialized_.store(initialized_value(), std::memory_order_release);
        const auto epoch = epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
        revision_.store(0, std::memory_order_release);
        updates_.store(0, std::memory_order_release);
        updated_at_.store(0, std::memory_order_release);
        saturated_.store(false, std::memory_order_release);
        wrapped_.store(false, std::memory_order_release);
        degraded_.store(false, std::memory_order_release);
        failure_at_.store(0, std::memory_order_release);
        resets_.fetch_add(1, std::memory_order_relaxed);
        last_status_.store(Status::Ok, std::memory_order_release);
        last_reason_.store(Reason::None, std::memory_order_release);
        auto update = Update{.epoch = epoch};
        end_write();
        return update;
    }

    [[nodiscard]] Error reject(Operation operation, Status status, Reason reason, bool) noexcept
    {
        record_atomic_failure(status, reason);
        if (reason == Reason::Overflow || reason == Reason::ConversionOverflow) {
            overflows_.fetch_add(1, std::memory_order_relaxed);
        } else if (reason == Reason::InvalidNumeric) {
            invalid_numeric_.fetch_add(1, std::memory_order_relaxed);
        }
        return slot_error<MetricT>(id_, operation, status, reason);
    }

    [[nodiscard]] MetricRecord record() noexcept
    {
        const auto reading = get(false);
        return {
            .metric = id_,
            .last_status = last_status_.load(std::memory_order_acquire),
            .last_failure = last_reason_.load(std::memory_order_acquire),
            .concurrency = ConcurrencyKind::Atomic,
            .epoch = reading->epoch,
            .revision = reading->revision,
            .updates = reading->updates,
            .rejected = rejected_.load(std::memory_order_acquire),
            .contention = contention_.load(std::memory_order_acquire),
            .overflows = overflows_.load(std::memory_order_acquire),
            .saturations = saturations_.load(std::memory_order_acquire),
            .invalid_numeric = invalid_numeric_.load(std::memory_order_acquire),
            .resets = resets_.load(std::memory_order_acquire),
            .updated_at = reading->updated_at,
            .failure_at = failure_at_.load(std::memory_order_acquire),
            .initialized = reading->initialized,
            .saturated = reading->saturated,
            .wrapped = reading->wrapped,
            .degraded = reading->degraded,
        };
    }

  private:
    [[nodiscard]] static constexpr Value initial_value() noexcept
    {
        if constexpr (InstrumentTraits<typename MetricT::Instrument>::gauge &&
                      requires { MetricT::initial_value; }) {
            return static_cast<Value>(MetricT::initial_value);
        }
        return {};
    }

    [[nodiscard]] static constexpr bool initialized_value() noexcept
    {
        return InstrumentTraits<typename MetricT::Instrument>::counter ||
               requires { MetricT::initial_value; };
    }

    [[nodiscard]] Result<Update, Error> finish(UpdateDisposition disposition) noexcept
    {
        const auto revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
        updates_.fetch_add(1, std::memory_order_relaxed);
        if constexpr (std::is_same_v<typename Policies::Timestamps, timestamps::Enabled>) {
            updated_at_.store(kernel::now_ticks(), std::memory_order_release);
        }
        if (disposition == UpdateDisposition::Saturated) {
            saturated_.store(true, std::memory_order_release);
            degraded_.store(true, std::memory_order_release);
            saturations_.fetch_add(1, std::memory_order_relaxed);
        } else if (disposition == UpdateDisposition::Wrapped) {
            wrapped_.store(true, std::memory_order_release);
            degraded_.store(true, std::memory_order_release);
            overflows_.fetch_add(1, std::memory_order_relaxed);
        }
        last_status_.store(Status::Ok, std::memory_order_release);
        auto update = Update{.disposition = disposition,
                             .revision = revision,
                             .epoch = epoch_.load(std::memory_order_acquire),
                             .saturated = saturated_.load(std::memory_order_acquire),
                             .wrapped = wrapped_.load(std::memory_order_acquire)};
        end_write();
        return update;
    }

    void record_atomic_failure(Status status, Reason reason) noexcept
    {
        rejected_.fetch_add(1, std::memory_order_relaxed);
        last_status_.store(status, std::memory_order_release);
        last_reason_.store(reason, std::memory_order_release);
        if constexpr (std::is_same_v<typename Policies::Timestamps, timestamps::Enabled>) {
            failure_at_.store(kernel::now_ticks(), std::memory_order_release);
        }
    }

    void begin_write() noexcept
    {
        writers_.fetch_add(1, std::memory_order_acq_rel);
    }

    void end_write() noexcept
    {
        generation_.fetch_add(1, std::memory_order_release);
        writers_.fetch_sub(1, std::memory_order_release);
    }

    [[nodiscard]] Update current_update(UpdateDisposition disposition) const noexcept
    {
        return {.disposition = disposition,
                .revision = revision_.load(std::memory_order_acquire),
                .epoch = epoch_.load(std::memory_order_acquire),
                .saturated = saturated_.load(std::memory_order_acquire),
                .wrapped = wrapped_.load(std::memory_order_acquire)};
    }

    LocalId id_{};
    std::atomic<Value> value_{};
    std::atomic_bool initialized_{};
    std::atomic<std::uint32_t> epoch_{};
    std::atomic<std::uint64_t> revision_{};
    std::atomic<std::uint64_t> updates_{};
    std::atomic<std::uint64_t> rejected_{};
    std::atomic<std::uint64_t> contention_{};
    std::atomic<std::uint64_t> overflows_{};
    std::atomic<std::uint64_t> saturations_{};
    std::atomic<std::uint64_t> resets_{};
    std::atomic<std::uint64_t> invalid_numeric_{};
    std::atomic<std::int64_t> updated_at_{};
    std::atomic<std::int64_t> failure_at_{};
    std::atomic<Status> last_status_{Status::NotReady};
    std::atomic<Reason> last_reason_{Reason::None};
    std::atomic_bool saturated_{};
    std::atomic_bool wrapped_{};
    std::atomic_bool degraded_{};
    std::atomic<std::uint32_t> writers_{};
    std::atomic<std::uint64_t> generation_{};
};

template <typename MetricT, typename Policies,
          typename Concurrency = effective_concurrency_t<MetricT, Policies>>
using MetricSlot =
    std::conditional_t<std::is_same_v<Concurrency, concurrency::Atomic>,
                       AtomicSlot<MetricT, Policies>, LockedSlot<MetricT, Policies, Concurrency>>;

} // namespace solar::metrics::detail
