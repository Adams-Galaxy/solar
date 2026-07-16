#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

#include "solar/component.hpp"
#include "solar/health/catalog.hpp"
#include "solar/system/sections.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_HEALTH)
#include <atomic>
#include <chrono>
#include <utility>

#include "solar/core/type_list.hpp"
#include "solar/kernel/message_queue.hpp"
#include "solar/kernel/mutex.hpp"
#include "solar/kernel/time.hpp"
#endif

namespace solar::health
{

#if defined(CONFIG_SOLAR_HEALTH)
inline constexpr bool enabled = true;
#else
inline constexpr bool enabled = false;
#endif

struct Facility
{
    using Dependencies = solar::Dependencies<>;

    static constexpr component::Descriptor descriptor{
        .name = "solar.health",
        .description = "Passive system health assessment",
    };
};

namespace detail
{

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_HEALTH)

struct StateOwner;
struct StateKey;

struct EvidenceState
{
    Assessment assessment{};
    EvidenceQuality quality{EvidenceQuality::Direct};
    SourceAvailability availability{SourceAvailability::Unknown};
    Tick observed_at{};
    Tick stale_after{};
    SourceKind source_kind{SourceKind::SelfReport};
    std::uint32_t source_id{};
    bool required{};
    bool present{};
};

struct SubjectState
{
    EvidenceState reported{};
    EvidenceState assessment{};
    EvidenceState lifecycle{};
    EvidenceState execution{};
    EvidenceState events{};
    EvidenceState metrics{};
    EvidenceState logging{};
    EvidenceState remote{};
    EvidenceState hardware{};
};

struct ProgressState
{
    std::atomic<std::uint64_t> generation{};
    std::atomic<Tick> observed_at{};
};

struct IsrReport
{
    SubjectId subject{};
    CompactObservation observation{};
    Tick observed_at{};
};

template <typename System, typename Component> consteval std::size_t subject_index()
{
    return System::Catalogs::template Of<component::Tag>::template Entry<Component>::local_id
        .index();
}

template <typename System, typename Monitor> consteval std::size_t monitor_index()
{
    return System::HealthMonitorCatalog::template Entry<Monitor>::local_id.index();
}

[[nodiscard]] constexpr Error make_error(Status status, Reason reason, Operation operation,
                                         SubjectId subject = {}, MonitorId monitor = {},
                                         int native = 0, std::uint32_t detail = 0) noexcept
{
    return {.status = status,
            .reason = reason,
            .operation = operation,
            .subject = subject,
            .monitor = monitor,
            .native = native,
            .detail = detail};
}

template <typename ErrorT>
[[nodiscard]] constexpr Error normalize_error(const ErrorT& error, Operation operation,
                                              SubjectId subject = {},
                                              MonitorId monitor = {}) noexcept
{
    if constexpr (std::is_same_v<std::remove_cvref_t<ErrorT>, Error>) {
        auto copy = error;
        copy.operation = operation;
        copy.subject = subject;
        copy.monitor = monitor;
        return copy;
    } else if constexpr (std::is_same_v<std::remove_cvref_t<ErrorT>, Status>) {
        return make_error(error, Reason::SourceFailed, operation, subject, monitor,
                          to_native_errno(error));
    } else if constexpr (requires { error.status; }) {
        const auto status = static_cast<Status>(error.status);
        int native{};
        if constexpr (requires { error.native; }) {
            native = static_cast<int>(error.native);
        } else if constexpr (requires { error.native_error; }) {
            native = static_cast<int>(error.native_error);
        }
        return make_error(status, Reason::SourceFailed, operation, subject, monitor, native);
    } else {
        return make_error(Status::Error, Reason::SourceFailed, operation, subject, monitor);
    }
}

[[nodiscard]] constexpr Tick default_stale_ticks() noexcept
{
#if defined(CONFIG_SOLAR_HEALTH)
    return kernel::to_ticks_ceil(std::chrono::milliseconds{CONFIG_SOLAR_HEALTH_DEFAULT_STALE_MS});
#else
    return 0;
#endif
}

[[nodiscard]] constexpr Tick stale_ticks(std::int64_t nanoseconds) noexcept
{
    return nanoseconds > 0 ? kernel::to_ticks_ceil(std::chrono::nanoseconds{nanoseconds})
                           : default_stale_ticks();
}

[[nodiscard]] constexpr bool expired(Tick now, Tick observed, Tick stale_after) noexcept
{
    return observed > 0 && stale_after > 0 && now - observed > stale_after;
}

template <typename System> struct Storage
{
    static constexpr std::size_t subject_count = list_size_v<typename System::Components>;
    static constexpr std::size_t monitor_count = System::HealthMonitorCatalog::size;

#if defined(CONFIG_SOLAR_HEALTH)
    static_assert(subject_count <= CONFIG_SOLAR_HEALTH_MAX_SUBJECTS,
                  "SOLAR_DIAGNOSTIC_HEALTH_SUBJECT_CEILING: effective component count exceeds "
                  "CONFIG_SOLAR_HEALTH_MAX_SUBJECTS");
    static_assert(monitor_count <= CONFIG_SOLAR_HEALTH_MAX_MONITORS,
                  "SOLAR_DIAGNOSTIC_HEALTH_MONITOR_CEILING: effective monitor count exceeds "
                  "CONFIG_SOLAR_HEALTH_MAX_MONITORS");
#endif

    Storage() noexcept
    {
        using Components = typename System::Components;
        using ComponentCatalog = typename System::Catalogs::template Of<component::Tag>;
        for_each_type<Components>([this]<typename Component> {
            constexpr auto index = subject_index<System, Component>();
            subjects[index].descriptor = ComponentCatalog::descriptors()[index];
            subjects[index].subject = ComponentCatalog::template Entry<Component>::local_id;
        });
        for_each_type<typename System::HealthMonitorCatalog::EntryTypes>([this]<typename Entry> {
            constexpr auto index = Entry::local_id.index();
            monitors[index].descriptor = System::HealthMonitorCatalog::descriptors()[index];
            monitors[index].monitor = Entry::local_id;
            monitors[index].subject = Entry::owner_id;
            monitors[index].observation.required = monitors[index].descriptor.descriptor.required;
        });
    }

    kernel::Mutex mutex{};
    std::array<SubjectRecord, subject_count> subjects{};
    std::array<SubjectState, subject_count> subject_state{};
    std::array<MonitorRecord, monitor_count> monitors{};
    std::array<ProgressState, subject_count> progress{};
#if defined(CONFIG_SOLAR_HEALTH)
    std::array<TransitionRecord, CONFIG_SOLAR_HEALTH_HISTORY_DEPTH> history{};
    kernel::MessageQueue<IsrReport, CONFIG_SOLAR_HEALTH_ISR_INGRESS_DEPTH> ingress{};
#else
    std::array<TransitionRecord, 1> history{};
    kernel::MessageQueue<IsrReport, 1> ingress{};
#endif
    SystemRecord system{};
    std::uint64_t next_history_sequence{1};
    std::uint64_t history_overwritten{};
    Tick first_refresh{};
    std::atomic<std::uint64_t> isr_admitted{};
    std::atomic<std::uint64_t> isr_dropped{};
    std::atomic<void (*)() noexcept> supervisor_waker{};
};

template <typename System>
using StateSlot = typename System::template StateSlot<StateOwner, StateKey, Storage<System>>;

template <typename System> [[nodiscard]] Storage<System>& storage() noexcept
{
    return StateSlot<System>::value;
}

template <typename System> void set_supervisor_waker(void (*waker)() noexcept) noexcept
{
    storage<System>().supervisor_waker.store(waker, std::memory_order_release);
}

template <typename System> void wake_supervisor() noexcept
{
    if (auto waker = storage<System>().supervisor_waker.load(std::memory_order_acquire);
        waker != nullptr) {
        waker();
    }
}

[[nodiscard]] constexpr Condition worse(Condition left, Condition right) noexcept
{
    return static_cast<unsigned>(right) > static_cast<unsigned>(left) ? right : left;
}

[[nodiscard]] constexpr Liveness worse(Liveness left, Liveness right) noexcept
{
    return static_cast<unsigned>(right) > static_cast<unsigned>(left) ? right : left;
}

[[nodiscard]] constexpr Readiness worse(Readiness left, Readiness right) noexcept
{
    if (left == Readiness::NotReady || right == Readiness::NotReady) {
        return Readiness::NotReady;
    }
    if (left == Readiness::Unknown || right == Readiness::Unknown) {
        return Readiness::Unknown;
    }
    return Readiness::Ready;
}

[[nodiscard]] constexpr Safety worse(Safety left, Safety right) noexcept
{
    return static_cast<unsigned>(right) > static_cast<unsigned>(left) ? right : left;
}

template <typename System>
void append_history(Storage<System>& state, SubjectRecord& record, Condition previous,
                    std::optional<EvidenceRef> evidence) noexcept
{
    if (previous == record.condition) {
        return;
    }
    auto& target = state.history[(state.next_history_sequence - 1) % state.history.size()];
    if (state.next_history_sequence > state.history.size()) {
        ++state.history_overwritten;
    }
    target = {.sequence = state.next_history_sequence++,
              .subject = record.subject,
              .previous = previous,
              .current = record.condition,
              .occurred_at = record.assessed_at,
              .evidence = evidence};
}

template <typename System> void recompute_system_locked(Storage<System>& state, Tick now) noexcept
{
    SystemRecord next{};
    next.readiness = Readiness::Ready;
    next.safety = Safety::Acceptable;
    next.freshness = Freshness::Current;
    Condition primary_condition{Condition::Unknown};
    for (const auto& subject : state.subjects) {
        switch (subject.condition) {
        case Condition::Unknown:
            ++next.unknown_subjects;
            break;
        case Condition::Nominal:
            ++next.nominal_subjects;
            break;
        case Condition::Degraded:
            ++next.degraded_subjects;
            break;
        case Condition::Faulted:
            ++next.faulted_subjects;
            break;
        }
        if (subject.freshness == Freshness::Stale) {
            ++next.stale_subjects;
            next.freshness = Freshness::Stale;
        } else if (subject.freshness == Freshness::Unknown && next.freshness != Freshness::Stale) {
            next.freshness = Freshness::Unknown;
        }
        next.readiness = worse(next.readiness, subject.readiness);
        next.safety = worse(next.safety, subject.safety);
        if (subject.primary_evidence &&
            static_cast<unsigned>(subject.condition) > static_cast<unsigned>(primary_condition)) {
            primary_condition = subject.condition;
            next.primary_evidence = subject.primary_evidence;
        }
    }
    if (next.faulted_subjects > 0) {
        next.condition = Condition::Faulted;
    } else if (next.degraded_subjects > 0) {
        next.condition = Condition::Degraded;
    } else if (next.unknown_subjects > 0) {
        next.condition = Condition::Unknown;
    } else {
        next.condition = Condition::Nominal;
    }
    next.assessment_generation = state.system.assessment_generation + 1;
    next.assessed_at = Timestamp{now};
    next.isr_reports_admitted = state.isr_admitted.load(std::memory_order_acquire);
    next.isr_reports_dropped = state.isr_dropped.load(std::memory_order_acquire);
    state.system = next;
}

template <typename System>
void recompute_subject_locked(Storage<System>& state, std::size_t index, Tick now) noexcept
{
    auto& record = state.subjects[index];
    auto& sources = state.subject_state[index];
    const auto previous = record.condition;
    const auto previous_assessment = Assessment{.condition = record.condition,
                                                .liveness = record.liveness,
                                                .readiness = record.readiness,
                                                .safety = record.safety,
                                                .freshness = record.freshness,
                                                .last_error = record.last_error,
                                                .recovering = record.recovering};

    Assessment aggregate{};
    aggregate.freshness = Freshness::Unknown;
    bool has_evidence{};
    bool required_unknown{};
    std::optional<EvidenceRef> primary{};

    const auto merge = [&](const EvidenceState& evidence, MonitorId monitor = {}) {
        if (!evidence.present) {
            if (evidence.required) {
                required_unknown = true;
            }
            return;
        }
        has_evidence = true;
        if (evidence.availability != SourceAvailability::Available && evidence.required) {
            required_unknown = true;
            return;
        }
        auto assessment = evidence.assessment;
        if (expired(now, evidence.observed_at, evidence.stale_after)) {
            assessment.freshness = Freshness::Stale;
            if (evidence.required && assessment.condition != Condition::Faulted) {
                assessment.condition = Condition::Degraded;
            }
        }
        if (!evidence.required && assessment.condition != Condition::Nominal) {
            return;
        }
        const auto old = aggregate.condition;
        aggregate.condition = worse(aggregate.condition, assessment.condition);
        if (assessment.liveness != Liveness::Unknown) {
            aggregate.liveness = worse(aggregate.liveness, assessment.liveness);
        }
        if (assessment.readiness != Readiness::Unknown) {
            aggregate.readiness = aggregate.readiness == Readiness::Unknown
                                      ? assessment.readiness
                                      : worse(aggregate.readiness, assessment.readiness);
        }
        if (assessment.safety != Safety::Unknown) {
            aggregate.safety = worse(aggregate.safety, assessment.safety);
        }
        aggregate.freshness =
            static_cast<unsigned>(assessment.freshness) > static_cast<unsigned>(aggregate.freshness)
                ? assessment.freshness
                : aggregate.freshness;
        aggregate.recovering = aggregate.recovering || assessment.recovering;
        if (assessment.last_error && (!aggregate.last_error || aggregate.condition != old)) {
            aggregate.last_error = assessment.last_error;
        }
        if (!primary || aggregate.condition != old) {
            primary = EvidenceRef{.quality = evidence.quality,
                                  .subject = record.subject,
                                  .monitor = monitor,
                                  .source_kind = evidence.source_kind,
                                  .source_id = evidence.source_id};
        }
    };

    merge(sources.reported);
    merge(sources.assessment);
    merge(sources.lifecycle);
    merge(sources.execution);
    merge(sources.events);
    merge(sources.metrics);
    merge(sources.logging);
    merge(sources.remote);
    merge(sources.hardware);
    for (auto& monitor : state.monitors) {
        if (monitor.subject != record.subject) {
            continue;
        }
        EvidenceState evidence{.assessment = monitor.observation.assessment,
                               .quality = monitor.observation.quality,
                               .availability = monitor.observation.availability,
                               .observed_at = monitor.observation.observed_at,
                               .stale_after = monitor.observation.stale_after,
                               .required = monitor.observation.required,
                               .present = monitor.has_observation};
        const auto is_stale =
            evidence.present && expired(now, evidence.observed_at, evidence.stale_after);
        if (is_stale && !monitor.stale) {
            ++monitor.stale_transitions;
        }
        monitor.stale = is_stale;
        merge(evidence, monitor.monitor);
    }

    if (!has_evidence || (required_unknown && aggregate.condition == Condition::Nominal)) {
        aggregate.condition = Condition::Unknown;
    }
    record.condition = aggregate.condition;
    record.liveness = aggregate.liveness;
    record.readiness = aggregate.readiness;
    record.safety = aggregate.safety;
    record.freshness = aggregate.freshness;
    record.last_error = aggregate.last_error;
    record.primary_evidence = primary;
    record.recovering = aggregate.recovering;
    record.assessed_at = Timestamp{now};
    ++record.assessment_generation;

    const auto current_assessment = Assessment{.condition = record.condition,
                                               .liveness = record.liveness,
                                               .readiness = record.readiness,
                                               .safety = record.safety,
                                               .freshness = record.freshness,
                                               .last_error = record.last_error,
                                               .recovering = record.recovering};
    if (current_assessment != previous_assessment) {
        if (previous != record.condition) {
            ++record.transition_count;
            record.last_transition = record.assessed_at;
        }
        append_history(state, record, previous, primary);
    }
}

template <typename System, typename Function>
[[nodiscard]] Result<void, Error> with_storage(Function&& function,
                                               Operation operation = Operation::Query) noexcept
{
    auto& state = storage<System>();
    auto lock = kernel::LockGuard<kernel::Mutex>::acquire(state.mutex);
    if (!lock) {
        return fail(make_error(lock.error(), Reason::Busy, operation));
    }
    std::forward<Function>(function)(state);
    return {};
}

template <typename System, typename Component>
[[nodiscard]] Result<Receipt, Error> report(Assessment assessment, EvidenceQuality quality) noexcept
{
    constexpr auto index = subject_index<System, Component>();
    constexpr auto subject =
        System::Catalogs::template Of<component::Tag>::template Entry<Component>::local_id;
    const auto now = kernel::now_ticks();
    Receipt receipt{.subject = subject};
    auto result = with_storage<System>(
        [&](auto& state) {
            auto& source = state.subject_state[index].reported;
            const bool repeated = source.present && source.assessment == assessment;
            source = {.assessment = assessment,
                      .quality = quality,
                      .availability = SourceAvailability::Available,
                      .observed_at = now,
                      .stale_after = default_stale_ticks(),
                      .source_kind = SourceKind::SelfReport,
                      .required = true,
                      .present = true};
            auto& record = state.subjects[index];
            const auto transitions = record.transition_count;
            ++record.report_count;
            if (repeated) {
                ++record.repeated_count;
            }
            recompute_subject_locked(state, index, now);
            recompute_system_locked(state, now);
            receipt.generation = record.assessment_generation;
            receipt.transitioned = transitions != record.transition_count;
            receipt.repeated = repeated;
        },
        Operation::Report);
    if (!result) {
        return fail(result.error());
    }
    wake_supervisor<System>();
    return receipt;
}

template <typename System, typename Component>
[[nodiscard]] Result<ProgressReceipt, Error> progress() noexcept
{
    constexpr auto index = subject_index<System, Component>();
    constexpr auto subject =
        System::Catalogs::template Of<component::Tag>::template Entry<Component>::local_id;
    auto& cell = storage<System>().progress[index];
    const auto now = kernel::now_ticks();
    cell.observed_at.store(now, std::memory_order_release);
    const auto generation = cell.generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    return ProgressReceipt{.subject = subject, .generation = generation, .observed_at = now};
}

template <typename System, typename Component>
[[nodiscard]] Result<void, Error> commit_adapter(Assessment assessment, SourceKind source_kind,
                                                 std::uint32_t source_id = 0) noexcept
{
    constexpr auto index = subject_index<System, Component>();
    const auto now = kernel::now_ticks();
    return with_storage<System>(
        [&](auto& state) {
            auto* destination = &state.subject_state[index].events;
            switch (source_kind) {
            case SourceKind::Metrics:
                destination = &state.subject_state[index].metrics;
                break;
            case SourceKind::Logging:
                destination = &state.subject_state[index].logging;
                break;
            case SourceKind::Remote:
                destination = &state.subject_state[index].remote;
                break;
            case SourceKind::Hardware:
                destination = &state.subject_state[index].hardware;
                break;
            default:
                break;
            }
            *destination = {
                .assessment = assessment,
                .quality = EvidenceQuality::Direct,
                .availability = SourceAvailability::Available,
                .observed_at = now,
                .stale_after = default_stale_ticks(),
                .source_kind = source_kind,
                .source_id = source_id,
                .required = true,
                .present = true,
            };
            recompute_subject_locked(state, index, now);
            recompute_system_locked(state, now);
        },
        Operation::Report);
}

template <typename System, typename Component>
[[nodiscard]] Result<void, Error> report_isr(CompactObservation observation) noexcept
{
    static_assert(std::is_trivially_copyable_v<CompactObservation>,
                  "SOLAR_DIAGNOSTIC_HEALTH_ISR_OBSERVATION: compact ISR observations must be "
                  "trivially copyable");
    constexpr auto subject =
        System::Catalogs::template Of<component::Tag>::template Entry<Component>::local_id;
    auto& state = storage<System>();
    IsrReport report{
        .subject = subject, .observation = observation, .observed_at = kernel::now_ticks()};
    if (state.ingress.try_send_isr(report) != Status::Ok) {
        state.isr_dropped.fetch_add(1, std::memory_order_relaxed);
        return fail(
            make_error(Status::NoSpace, Reason::IngressFull, Operation::ReportIsr, subject));
    }
    state.isr_admitted.fetch_add(1, std::memory_order_relaxed);
    wake_supervisor<System>();
    return {};
}

template <typename System, typename Monitor>
[[nodiscard]] Result<void, Error> commit_monitor(Observation observation) noexcept
{
    constexpr auto index = monitor_index<System, Monitor>();
    constexpr auto monitor = System::HealthMonitorCatalog::template Entry<Monitor>::local_id;
    constexpr auto subject = System::HealthMonitorCatalog::template Entry<Monitor>::owner_id;
    if (observation.observed_at == 0) {
        observation.observed_at = kernel::now_ticks();
    }
    if (observation.stale_after == 0) {
        observation.stale_after = stale_ticks(
            System::HealthMonitorCatalog::descriptors()[index].descriptor.stale_after_ns);
    }
    observation.required = System::HealthMonitorCatalog::descriptors()[index].descriptor.required;
    auto result = with_storage<System>(
        [&](auto& state) {
            auto& record = state.monitors[index];
            const auto prior = record.observation.assessment.condition;
            record.observation = observation;
            ++record.observations;
            record.has_observation = true;
            if (observation.availability != SourceAvailability::Available) {
                ++record.failures;
            }
            if (observation.explicit_recovery && prior != Condition::Nominal &&
                observation.assessment.condition == Condition::Nominal) {
                ++record.recoveries;
            }
            recompute_subject_locked(state, subject.index(), observation.observed_at);
            recompute_system_locked(state, observation.observed_at);
        },
        Operation::Check);
    return result;
}

template <typename System> [[nodiscard]] Result<std::size_t, Error> drain_isr() noexcept
{
    auto& state = storage<System>();
    std::size_t drained{};
    while (auto admitted = state.ingress.try_receive()) {
        const auto& compact = admitted->observation;
        Assessment assessment{.condition = compact.condition,
                              .liveness = compact.liveness,
                              .readiness = compact.readiness,
                              .safety = compact.safety,
                              .freshness = Freshness::Current,
                              .detail = compact.detail,
                              .recovering = compact.recovering};
        if (compact.status != Status::Ok) {
            assessment.last_error =
                make_error(compact.status, Reason::SourceFailed, Operation::ReportIsr,
                           admitted->subject, {}, to_native_errno(compact.status), compact.detail);
        }
        bool matched{};
        for_each_type<typename System::Components>([&]<typename Component> {
            constexpr auto subject =
                System::Catalogs::template Of<component::Tag>::template Entry<Component>::local_id;
            if (!matched && subject == admitted->subject) {
                (void)report<System, Component>(assessment, EvidenceQuality::Reported);
                matched = true;
            }
        });
        ++drained;
    }
    return drained;
}

template <typename System, typename Component>
[[nodiscard]] Result<SubjectRecord, Error> subject_record() noexcept
{
    SubjectRecord copy{};
    auto result = with_storage<System>(
        [&](const auto& state) { copy = state.subjects[subject_index<System, Component>()]; });
    if (!result) {
        return fail(result.error());
    }
    return copy;
}

template <typename System> [[nodiscard]] auto subject_records() noexcept
{
    std::array<SubjectRecord, Storage<System>::subject_count> copy{};
    (void)with_storage<System>([&](const auto& state) { copy = state.subjects; });
    return copy;
}

template <typename System, typename Component> [[nodiscard]] auto monitor_records() noexcept
{
    constexpr auto count = [] {
        std::size_t value{};
        for_each_type<typename System::HealthMonitorCatalog::EntryTypes>([&]<typename Entry> {
            if constexpr (std::is_same_v<typename Entry::Owner, Component>) {
                ++value;
            }
        });
        return value;
    }();
    std::array<MonitorRecord, count> copy{};
    (void)with_storage<System>([&](const auto& state) {
        std::size_t output{};
        for (const auto& monitor : state.monitors) {
            constexpr auto subject =
                System::Catalogs::template Of<component::Tag>::template Entry<Component>::local_id;
            if (monitor.subject == subject) {
                copy[output++] = monitor;
            }
        }
    });
    return copy;
}

template <typename System> [[nodiscard]] Result<SystemRecord, Error> system_record() noexcept
{
    SystemRecord copy{};
    auto result = with_storage<System>([&](const auto& state) { copy = state.system; });
    if (!result) {
        return fail(result.error());
    }
    return copy;
}

template <typename System>
[[nodiscard]] Result<HistoryPage, Error>
read_history(HistoryCursor cursor, std::span<TransitionRecord> destination) noexcept
{
    HistoryPage page{};
    auto result = with_storage<System>(
        [&](const auto& state) {
            const auto newest = state.next_history_sequence;
            const auto oldest = newest > state.history.size() ? newest - state.history.size() : 1;
            auto sequence = cursor.sequence == 0 ? oldest : cursor.sequence;
            if (sequence < oldest) {
                sequence = oldest;
            }
            while (sequence < newest && page.written < destination.size()) {
                const auto& entry = state.history[(sequence - 1) % state.history.size()];
                if (entry.sequence == sequence) {
                    destination[page.written++] = entry;
                }
                ++sequence;
            }
            page.next.sequence = sequence;
            page.available = static_cast<std::size_t>(newest - oldest);
            page.overwritten = state.history_overwritten;
        },
        Operation::HistoryRead);
    if (!result) {
        return fail(result.error());
    }
    return page;
}

#else

[[nodiscard]] constexpr Error make_error(Status status, Reason reason, Operation operation,
                                         SubjectId subject = {}, MonitorId monitor = {},
                                         int native = 0, std::uint32_t detail = 0) noexcept
{
    return {.status = status,
            .reason = reason,
            .operation = operation,
            .subject = subject,
            .monitor = monitor,
            .native = native,
            .detail = detail};
}

template <typename ErrorT>
[[nodiscard]] constexpr Error normalize_error(const ErrorT&, Operation operation,
                                              SubjectId subject = {},
                                              MonitorId monitor = {}) noexcept
{
    return make_error(Status::NotSupported, Reason::Disabled, operation, subject, monitor);
}

template <typename System, typename Component>
[[nodiscard]] Result<Receipt, Error> report(Assessment, EvidenceQuality) noexcept
{
    return fail(make_error(Status::NotSupported, Reason::Disabled, Operation::Report));
}

template <typename System, typename Component>
[[nodiscard]] Result<ProgressReceipt, Error> progress() noexcept
{
    return fail(make_error(Status::NotSupported, Reason::Disabled, Operation::Progress));
}

template <typename System, typename Component>
[[nodiscard]] Result<void, Error> report_isr(CompactObservation) noexcept
{
    return fail(make_error(Status::NotSupported, Reason::Disabled, Operation::ReportIsr));
}

template <typename System, typename Component>
[[nodiscard]] Result<void, Error> commit_adapter(Assessment, SourceKind, std::uint32_t = 0) noexcept
{
    return fail(make_error(Status::NotSupported, Reason::Disabled, Operation::Report));
}

template <typename System, typename Monitor>
[[nodiscard]] Result<void, Error> commit_monitor(Observation) noexcept
{
    return fail(make_error(Status::NotSupported, Reason::Disabled, Operation::Check));
}

template <typename System, typename Component>
[[nodiscard]] Result<SubjectRecord, Error> subject_record() noexcept
{
    return fail(make_error(Status::NotSupported, Reason::Disabled, Operation::Query));
}

template <typename System> [[nodiscard]] auto subject_records() noexcept
{
    return std::array<SubjectRecord, 0>{};
}

template <typename System, typename Component> [[nodiscard]] auto monitor_records() noexcept
{
    return std::array<MonitorRecord, 0>{};
}

template <typename System> [[nodiscard]] Result<SystemRecord, Error> system_record() noexcept
{
    return fail(make_error(Status::NotSupported, Reason::Disabled, Operation::Query));
}

template <typename System>
[[nodiscard]] Result<HistoryPage, Error> read_history(HistoryCursor,
                                                      std::span<TransitionRecord>) noexcept
{
    return fail(make_error(Status::NotSupported, Reason::Disabled, Operation::HistoryRead));
}

#endif

} // namespace detail

} // namespace solar::health

template <> struct solar::builtin_traits<solar::health::Facility>
{
    static constexpr bool enabled = solar::health::enabled;
    static constexpr bool always_present = solar::health::enabled;
    using Requirements = solar::TypeList<>;

    template <typename> static constexpr bool demanded = solar::health::enabled;
};
