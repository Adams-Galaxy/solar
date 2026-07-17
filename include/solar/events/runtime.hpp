#pragma once

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

#include "solar/events/facility.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_EVENTS)
#include "solar/execution/runtime.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/fatal.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/this_thread.hpp"
#include "solar/kernel/time.hpp"
#endif

namespace solar::events::detail
{

template <typename System, typename EventT> [[nodiscard]] EventRecord event_record() noexcept;

template <typename System, typename EventT>
[[nodiscard]] Result<ConditionRecord, Error> condition_record(SourceId source) noexcept;

template <typename System, typename Processor>
[[nodiscard]] ProcessorRecord processor_record() noexcept;

template <typename System> [[nodiscard]] FacilityRecord facility_record() noexcept;

template <typename System>
[[nodiscard]] HistoryPage read_history(Cursor cursor, std::span<Record> output,
                                       std::optional<LocalId> filter = std::nullopt) noexcept;

template <typename System>
[[nodiscard]] Result<Record, Error> latest_history(std::optional<LocalId> filter) noexcept;

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_EVENTS)

inline constexpr std::uint16_t history_entry_size(const StoredRecord& record) noexcept
{
    return static_cast<std::uint16_t>(sizeof(RecordHeader) + record.header.payload_size);
}

inline std::uint16_t read_history_size(const std::byte* bytes) noexcept
{
    std::uint16_t value{};
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

inline StoredRecord read_history_record(const std::byte* bytes, std::uint16_t size) noexcept
{
    StoredRecord record{};
    std::memcpy(&record.header, bytes + sizeof(std::uint16_t), sizeof(RecordHeader));
    const auto payload_size = static_cast<std::size_t>(size) - sizeof(RecordHeader);
    if (payload_size != 0) {
        std::memcpy(record.payload.data(), bytes + sizeof(std::uint16_t) + sizeof(RecordHeader),
                    payload_size);
    }
    return record;
}

inline CompactHistory::AppendResult CompactHistory::append(const StoredRecord& record) noexcept
{
    const auto entry_size = history_entry_size(record);
    const auto total_size = prefix_size + static_cast<std::size_t>(entry_size);
    if (total_size > bytes_.size()) {
        return {};
    }

    AppendResult result{.stored = true};
    while (used_ + total_size > bytes_.size()) {
        if (used_ < prefix_size) {
            used_ = 0;
            break;
        }
        const auto oldest_size = read_history_size(bytes_.data());
        const auto oldest_total = prefix_size + static_cast<std::size_t>(oldest_size);
        if (oldest_total > used_) {
            used_ = 0;
            break;
        }
        std::memmove(bytes_.data(), bytes_.data() + oldest_total, used_ - oldest_total);
        used_ -= oldest_total;
        ++result.evicted;
        ++evicted_;
    }

    std::memcpy(bytes_.data() + used_, &entry_size, prefix_size);
    std::memcpy(bytes_.data() + used_ + prefix_size, &record.header, sizeof(RecordHeader));
    if (record.header.payload_size != 0) {
        std::memcpy(bytes_.data() + used_ + prefix_size + sizeof(RecordHeader),
                    record.payload.data(), record.header.payload_size);
    }
    used_ += total_size;
    return result;
}

inline HistoryPage CompactHistory::read(Cursor cursor, std::span<Record> output,
                                        std::optional<LocalId> filter) const noexcept
{
    HistoryPage page{.next = cursor};
    std::size_t offset{};
    Sequence oldest{};
    Sequence latest{};
    while (offset + prefix_size <= used_) {
        const auto entry_size = read_history_size(bytes_.data() + offset);
        const auto total_size = prefix_size + static_cast<std::size_t>(entry_size);
        if (entry_size < sizeof(RecordHeader) || offset + total_size > used_) {
            break;
        }
        const auto record = read_history_record(bytes_.data() + offset, entry_size);
        if (oldest == 0) {
            oldest = record.header.sequence;
        }
        latest = record.header.sequence;
        if (record.header.sequence >= cursor.next_sequence &&
            (!filter || record.header.event == *filter)) {
            ++page.available;
            if (page.written < output.size()) {
                output[page.written].header = record.header;
                std::copy_n(record.payload.begin(), record.header.payload_size,
                            output[page.written].payload.begin());
                ++page.written;
                page.next.next_sequence = record.header.sequence + 1;
            }
        }
        offset += total_size;
    }
    if (oldest != 0 && cursor.next_sequence < oldest) {
        page.stale = true;
        page.evicted_before = oldest - cursor.next_sequence;
        if (page.written == 0) {
            page.next.next_sequence = oldest;
        }
    }
    if (page.written == 0 && latest != 0 && cursor.next_sequence > latest) {
        page.next = cursor;
    }
    return page;
}

inline Result<Record, Error> CompactHistory::latest(std::optional<LocalId> filter) const noexcept
{
    std::optional<Record> latest_record{};
    std::size_t offset{};
    while (offset + prefix_size <= used_) {
        const auto entry_size = read_history_size(bytes_.data() + offset);
        const auto total_size = prefix_size + static_cast<std::size_t>(entry_size);
        if (entry_size < sizeof(RecordHeader) || offset + total_size > used_) {
            break;
        }
        const auto stored = read_history_record(bytes_.data() + offset, entry_size);
        if (!filter || stored.header.event == *filter) {
            Record record{.header = stored.header};
            std::copy_n(stored.payload.begin(), stored.header.payload_size, record.payload.begin());
            latest_record = record;
        }
        offset += total_size;
    }
    if (!latest_record) {
        return fail<Error>({.status = solar::Status::NotFound,
                            .reason = Reason::HistoryEmpty,
                            .operation = Operation::Query});
    }
    return *latest_record;
}

inline std::optional<StoredRecord>
CompactHistory::next(Sequence sequence, std::optional<LocalId> filter) const noexcept
{
    std::size_t offset{};
    while (offset + prefix_size <= used_) {
        const auto entry_size = read_history_size(bytes_.data() + offset);
        const auto total_size = prefix_size + static_cast<std::size_t>(entry_size);
        if (entry_size < sizeof(RecordHeader) || offset + total_size > used_) {
            break;
        }
        const auto stored = read_history_record(bytes_.data() + offset, entry_size);
        if (stored.header.sequence >= sequence && (!filter || stored.header.event == *filter)) {
            return stored;
        }
        offset += total_size;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr SourceId source_from_owner(OwnerView owner) noexcept
{
    switch (owner.kind) {
    case OwnerKind::Application:
        return {.kind = SourceKind::Application};
    case OwnerKind::Component:
        return {.kind = SourceKind::Component, .component = owner.component};
    case OwnerKind::Builtin:
        return {.kind = SourceKind::Builtin, .component = owner.component};
    }
    return {};
}

template <typename System, typename EventT>
[[nodiscard]] Error make_error(Operation operation, Status status, Reason reason,
                               SourceId source = {}) noexcept
{
    Error error{
        .status = status,
        .reason = reason,
        .operation = operation,
        .source = source,
    };
    if constexpr (System::EventCatalog::template contains<EventT>) {
        error.event = System::EventCatalog::template Entry<EventT>::local_id;
    }
    return error;
}

template <typename EventT>
[[nodiscard]] StoredRecord make_stored_record(const typename EventT::Payload& payload,
                                              RecordHeader header) noexcept
    requires(!payload_free_v<EventT>)
{
    StoredRecord record{.header = header};
    std::memcpy(record.payload.data(), &payload, sizeof(payload));
    return record;
}

template <typename EventT>
[[nodiscard]] StoredRecord make_stored_record(RecordHeader header) noexcept
    requires payload_free_v<EventT>
{
    return StoredRecord{.header = header};
}

template <typename Facility> [[nodiscard]] std::size_t critical_pending() noexcept
{
    std::size_t pending{};
    for_each_type<typename Facility::EventTypes>([&]<typename EventT> {
        pending += Facility::template event_state<EventT>.critical_ingress.size();
    });
    return pending;
}

template <typename Facility> [[nodiscard]] bool ingress_empty() noexcept
{
    return Facility::thread_ingress.size() == 0 && Facility::isr_ingress.size() == 0 &&
           critical_pending<Facility>() == 0;
}

template <typename Facility> void update_facility_pending() noexcept
{
    Facility::facility_record.thread_pending =
        static_cast<std::uint32_t>(Facility::thread_ingress.size());
    Facility::facility_record.isr_pending =
        static_cast<std::uint32_t>(Facility::isr_ingress.size());
    Facility::facility_record.critical_pending =
        static_cast<std::uint32_t>(critical_pending<Facility>());
    Facility::facility_record.processor_pending =
        Facility::processor_pending.load(std::memory_order_acquire);
}

template <typename Facility> [[nodiscard]] Result<void> request_processing(bool from_isr) noexcept
{
    if (Facility::processor_pending.exchange(true, std::memory_order_acq_rel)) {
        return {};
    }
    if (Facility::schedule_processor == nullptr) {
        Facility::processor_pending.store(false, std::memory_order_release);
        return fail<solar::Error>({.status = solar::Status::NotReady});
    }
    auto submitted = Facility::schedule_processor(from_isr);
    if (!submitted) {
        Facility::processor_pending.store(false, std::memory_order_release);
        auto guard = Facility::lock.acquire();
        Facility::facility_record.last_status = status_of(submitted.error());
        Facility::facility_record.processor_pending = false;
        return fail<solar::Error>(submitted.error());
    }
    return {};
}

template <typename Capture>
[[nodiscard]] bool should_materialize(std::uint64_t attempts, std::int64_t now,
                                      std::int64_t& window_start) noexcept
{
    if constexpr (Capture::kind == CaptureKind::EveryOccurrence) {
        return true;
    } else if constexpr (Capture::kind == CaptureKind::SampleEvery) {
        return (attempts - 1U) % Capture::period == 0;
    } else if constexpr (Capture::kind == CaptureKind::RateLimited) {
        const auto interval_ticks = kernel::to_ticks_ceil(Capture::interval.duration());
        if (attempts == 1 || now - window_start >= interval_ticks) {
            window_start = now;
            return true;
        }
        return false;
    } else {
        return false;
    }
}

template <typename System, typename EventT, bool Isr, Operation CaptureOperation,
          typename PayloadArgument>
[[nodiscard]] Result<Receipt, Error> capture_event(PayloadArgument&& payload, SourceId source,
                                                   ObserveOptions options) noexcept
{
    using Facility = typename System::EventFacility;
    using Policies = typename Facility::template Policies<EventT>;
    using Capture = CaptureTraits<typename Policies::Capture>;
    using Retention = RetentionTraits<typename Policies::Retention>;
    constexpr Operation operation = CaptureOperation;

    if constexpr (Isr) {
        static_assert(CONFIG_SOLAR_EVENTS_ISR_INGRESS_DEPTH > 0 || Retention::critical,
                      "SOLAR_DIAGNOSTIC_EVENT_ISR_DISABLED: ISR event ingress is disabled");
        if (!kernel::in_isr()) {
            return fail<Error>(make_error<System, EventT>(operation, Status::Invalid,
                                                          Reason::InvalidContext, source));
        }
    } else if (kernel::in_isr()) {
        return fail<Error>(
            make_error<System, EventT>(operation, Status::Invalid, Reason::InvalidContext, source));
    }

    if (!Facility::ready.load(std::memory_order_acquire)) {
        return fail<Error>(
            make_error<System, EventT>(operation, Status::NotReady, Reason::NotReady, source));
    }
    if (!Facility::accepting.load(std::memory_order_acquire)) {
        return fail<Error>(
            make_error<System, EventT>(operation, Status::NotReady, Reason::CaptureClosed, source));
    }

    Receipt receipt{};
    bool admitted{};
    {
        auto guard = [&]() -> std::optional<kernel::SpinLock::Guard> {
            if constexpr (CaptureOperation == Operation::Observe) {
                return Facility::lock.acquire();
            } else {
                return Facility::lock.try_acquire();
            }
        }();
        if (!guard) {
            return fail<Error>(make_error<System, EventT>(operation, Status::WouldBlock,
                                                          Reason::WouldBlock, source));
        }
        auto& state = Facility::template event_state<EventT>;
        auto& event_record = state.record;
        ++event_record.attempts;
        const auto now = kernel::now_ticks();

        if constexpr (Capture::kind == CaptureKind::SampleEvery) {
            if (!should_materialize<Capture>(event_record.attempts, now,
                                             state.policy_window_start)) {
                ++event_record.sampled;
                receipt = {.disposition = CaptureDisposition::SampledOut,
                           .timestamp = now,
                           .occurrence_count = 1,
                           .materialized = false};
                return receipt;
            }
        } else if constexpr (Capture::kind == CaptureKind::RateLimited) {
            if (!should_materialize<Capture>(event_record.attempts, now,
                                             state.policy_window_start)) {
                ++event_record.rate_limited;
                receipt = {.disposition = CaptureDisposition::RateLimited,
                           .timestamp = now,
                           .occurrence_count = 1,
                           .materialized = false};
                return receipt;
            }
        }

        RecordHeader header{
            .event = System::EventCatalog::template Entry<EventT>::local_id,
            .source = source,
            .timestamp = now,
            .context = Isr ? ContextKind::Isr : ContextKind::Thread,
            .timestamp_quality = TimestampQuality::Monotonic,
            .severity = descriptor_traits<Tag, EventT>::descriptor.severity,
            .correlation = options.correlation,
            .log_intent = options.log_intent,
            .occurrence_count = 1,
            .lost_before = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                state.policy_counter, std::numeric_limits<std::uint32_t>::max())),
            .payload_size = static_cast<std::uint16_t>(payload_size_v<EventT>),
            .schema_version = descriptor_traits<Tag, EventT>::descriptor.version,
            .flags = Retention::critical ? RecordFlag::Critical : RecordFlag::None,
        };
        if (header.lost_before != 0) {
            header.flags = header.flags | RecordFlag::LostBefore;
        }
        if constexpr (!std::is_void_v<typename ResolvedEvent<EventT>::type>) {
            header.flags = header.flags | RecordFlag::Recovery;
        }
        auto stored = [&] {
            if constexpr (payload_free_v<EventT>) {
                return make_stored_record<EventT>(header);
            } else {
                return make_stored_record<EventT>(payload, header);
            }
        }();

        CaptureDisposition disposition = CaptureDisposition::Captured;
        if constexpr (Capture::aggregate) {
            const auto window_ticks = kernel::to_ticks_ceil(Capture::window.duration());
            auto accumulate = [&](auto& aggregate) -> bool {
                if (aggregate.count == 0) {
                    aggregate.window_start = now;
                    aggregate.count = 1;
                    aggregate.representative = stored;
                } else if (now - aggregate.window_start < window_ticks) {
                    ++aggregate.count;
                    aggregate.representative = stored;
                } else {
                    stored.header.occurrence_count = aggregate.count + 1;
                    aggregate = {};
                    return true;
                }
                ++event_record.aggregated;
                receipt = {.disposition = CaptureDisposition::Aggregated,
                           .timestamp = now,
                           .occurrence_count = aggregate.count,
                           .materialized = false};
                return false;
            };

            if constexpr (Capture::keyed) {
                const auto key = Capture::KeyType::get(payload);
                auto* selected = static_cast<decltype(&state.aggregation.entries[0])>(nullptr);
                for (auto& entry : state.aggregation.entries) {
                    if (entry.occupied && entry.key == key) {
                        selected = &entry;
                        break;
                    }
                    if (selected == nullptr && !entry.occupied) {
                        selected = &entry;
                    }
                }
                if (selected == nullptr) {
                    ++event_record.aggregation_rejected;
                    ++event_record.known_lost;
                    ++state.policy_counter;
                    event_record.last_status = Status::NoSpace;
                    event_record.last_failure = Reason::AggregationKeysFull;
                    return fail<Error>(make_error<System, EventT>(
                        operation, Status::NoSpace, Reason::AggregationKeysFull, source));
                }
                if (!selected->occupied) {
                    selected->key = key;
                    selected->occupied = true;
                }
                if (!accumulate(*selected)) {
                    return receipt;
                }
            } else if (!accumulate(state.aggregation)) {
                return receipt;
            }
            disposition = CaptureDisposition::Captured;
        }

        stored.header.sequence = Facility::next_sequence;
        if (stored.header.occurrence_count > 1) {
            stored.header.flags = stored.header.flags | RecordFlag::Aggregated;
        }

        if constexpr (Retention::critical) {
            const auto reserved = Retention::reserved_slots;
            const auto occupied = state.critical_ingress.size() + state.critical_history.size() +
                                  state.critical_inflight;
            admitted = occupied < reserved && state.critical_ingress.push(stored);
        } else if constexpr (Isr) {
            admitted = Facility::isr_ingress.push(stored);
        } else {
            admitted = Facility::thread_ingress.push(stored);
        }
        if (!admitted) {
            ++event_record.ingress_rejected;
            ++event_record.known_lost;
            ++state.policy_counter;
            event_record.overflow_latched = true;
            event_record.last_status = Status::NoBuffer;
            event_record.last_failure =
                Retention::critical ? Reason::RequiredCaptureExhausted : Reason::CaptureFull;
            ++Facility::facility_record.ingress_rejected;
            Facility::facility_record.overflow_latched = true;
            update_facility_pending<Facility>();
            if constexpr (Retention::panic_on_exhaustion) {
                kernel::panic(Status::NoBuffer);
            }
            return fail<Error>(make_error<System, EventT>(
                operation, Status::NoBuffer,
                Retention::critical ? Reason::RequiredCaptureExhausted : Reason::CaptureFull,
                source));
        }

        ++Facility::next_sequence;
        ++Facility::facility_record.accepted;
        ++event_record.captured;
        event_record.last_sequence = stored.header.sequence;
        event_record.last_timestamp = now;
        event_record.last_status = Status::Ok;
        event_record.last_failure = Reason::None;
        state.policy_counter = 0;
        update_facility_pending<Facility>();
        receipt = {.disposition = disposition,
                   .sequence = stored.header.sequence,
                   .timestamp = now,
                   .occurrence_count = stored.header.occurrence_count,
                   .materialized = true};
    }

    (void)request_processing<Facility>(Isr);
    return receipt;
}

template <typename Facility> [[nodiscard]] bool pop_next(StoredRecord& output) noexcept
{
    enum class RingKind : std::uint8_t
    {
        None,
        Thread,
        Isr,
        Critical,
    };
    RingKind selected{RingKind::None};
    Sequence sequence = std::numeric_limits<Sequence>::max();
    LocalId critical_event{};

    const auto consider = [&](const StoredRecord* candidate, RingKind kind, LocalId event = {}) {
        if (candidate != nullptr && candidate->header.sequence < sequence) {
            sequence = candidate->header.sequence;
            selected = kind;
            critical_event = event;
        }
    };
    consider(Facility::thread_ingress.front(), RingKind::Thread);
    consider(Facility::isr_ingress.front(), RingKind::Isr);
    for_each_type<typename Facility::EventTypes>([&]<typename EventT> {
        consider(Facility::template event_state<EventT>.critical_ingress.front(),
                 RingKind::Critical, Facility::template event_state<EventT>.record.event);
    });

    bool popped{};
    switch (selected) {
    case RingKind::Thread:
        popped = Facility::thread_ingress.pop(output);
        break;
    case RingKind::Isr:
        popped = Facility::isr_ingress.pop(output);
        break;
    case RingKind::Critical:
        for_each_type<typename Facility::EventTypes>([&]<typename EventT> {
            if (!popped && Facility::template event_state<EventT>.record.event == critical_event) {
                popped = Facility::template event_state<EventT>.critical_ingress.pop(output);
                if (popped) {
                    ++Facility::template event_state<EventT>.critical_inflight;
                }
            }
        });
        break;
    case RingKind::None:
        break;
    }
    update_facility_pending<Facility>();
    return popped;
}

template <typename Facility, typename EventT>
[[nodiscard]] Result<void> retain_record(const StoredRecord& stored) noexcept
{
    using Policies = typename Facility::template Policies<EventT>;
    using Retention = RetentionTraits<typename Policies::Retention>;
    auto& state = Facility::template event_state<EventT>;

    if constexpr (Retention::kind == RetentionKind::Transient) {
        return {};
    } else if constexpr (Retention::kind == RetentionKind::Buffered) {
        auto guard = Facility::lock.acquire();
        const auto appended = Facility::history.append(stored);
        if (!appended.stored) {
            ++state.record.known_lost;
            state.record.overflow_latched = true;
            return fail<solar::Error>({.status = solar::Status::NoBuffer});
        }
        ++state.record.retained;
        state.record.history_evicted += appended.evicted;
        Facility::facility_record.history_evicted += appended.evicted;
        Facility::facility_record.history_used = Facility::history.used();
        return {};
    } else if constexpr (Retention::kind == RetentionKind::Critical) {
        auto guard = Facility::lock.acquire();
        const auto retained = state.critical_history.push(stored);
        if (state.critical_inflight != 0) {
            --state.critical_inflight;
        }
        if (!retained) {
            ++state.record.known_lost;
            state.record.overflow_latched = true;
            return fail<solar::Error>({.status = solar::Status::NoBuffer});
        }
        ++state.record.retained;
        return {};
    } else {
        auto persisted = Retention::StoreType::write(stored.view());
        if (!persisted) {
            return fail<solar::Error>({.status = status_of(persisted.error())});
        }
        auto guard = Facility::lock.acquire();
        ++state.record.retained;
        return {};
    }
}

template <typename System, typename EventT>
[[nodiscard]] Result<void> process_record_for(const StoredRecord& stored) noexcept
{
    using Facility = typename System::EventFacility;
    auto retained = retain_record<Facility, EventT>(stored);
    Status first_error = retained ? Status::Ok : status_of(retained.error());

    for_each_type<typename Facility::ProcessorTypes>([&]<typename Processor> {
        using Traits = processor_traits<Processor>;
        if constexpr (std::is_same_v<typename Traits::EventType, EventT>) {
            auto& record = Facility::template processor_record_state<Processor>;
            {
                auto guard = Facility::lock.acquire();
                ++record.offered;
            }
            auto processed = invoke_processor<typename Traits::ObserverType>(stored.view());
            auto guard = Facility::lock.acquire();
            if (processed) {
                ++record.accepted;
                record.last_status = Status::Ok;
            } else {
                ++record.failed;
                record.last_status = status_of(processed.error());
                ++Facility::facility_record.processor_failures;
                ++Facility::template event_state<EventT>.record.processor_failures;
                if (first_error == Status::Ok) {
                    first_error = status_of(processed.error());
                }
            }
        }
    });

    {
        auto guard = Facility::lock.acquire();
        auto& record = Facility::template event_state<EventT>.record;
        auto& conditions = Facility::template event_state<EventT>.conditions.entries;
        using ConditionEntry = typename std::remove_reference_t<decltype(conditions)>::value_type;
        auto* condition = static_cast<ConditionEntry*>(nullptr);
        for (auto& candidate : conditions) {
            if (candidate.occupied && candidate.record.source == stored.header.source) {
                condition = &candidate;
                break;
            }
            if (condition == nullptr && !candidate.occupied) {
                condition = &candidate;
            }
        }
        if (condition != nullptr) {
            condition->occupied = true;
            condition->record.event = stored.header.event;
            condition->record.source = stored.header.source;
            ++condition->record.consecutive;
            condition->record.last_sequence = stored.header.sequence;
            condition->record.last_timestamp = stored.header.timestamp;
            condition->record.active = true;
            record.consecutive = condition->record.consecutive;
            record.condition_active = true;
        }
        using Resolved = typename ResolvedEvent<EventT>::type;
        if constexpr (!std::is_void_v<Resolved>) {
            static_assert(contains_v<Resolved, typename Facility::EventTypes>,
                          "SOLAR_DIAGNOSTIC_EVENT_RECOVERY_UNREGISTERED: recovery references an "
                          "unregistered event");
            auto& resolved_state = Facility::template event_state<Resolved>;
            for (auto& candidate : resolved_state.conditions.entries) {
                if (candidate.occupied && candidate.record.source == stored.header.source) {
                    candidate.record.active = false;
                    candidate.record.consecutive = 0;
                }
            }
            const auto any_active =
                std::ranges::any_of(resolved_state.conditions.entries, [](const auto& candidate) {
                    return candidate.occupied && candidate.record.active;
                });
            resolved_state.record.condition_active = any_active;
            if (!any_active) {
                resolved_state.record.consecutive = 0;
            }
        }
        ++Facility::facility_record.processed;
        Facility::facility_record.last_status =
            first_error == Status::Ok ? Status::Ok : first_error;
    }
    return first_error == Status::Ok ? Result<void>{}
                                     : Result<void>{fail<solar::Error>({.status = first_error})};
}

template <typename System>
[[nodiscard]] Result<void> process_one(const StoredRecord& stored) noexcept
{
    using Facility = typename System::EventFacility;
    Result<void> result = fail<solar::Error>({.status = solar::Status::NotFound});
    for_each_type<typename Facility::EventTypes>([&]<typename EventT> {
        if (stored.header.event == System::EventCatalog::template Entry<EventT>::local_id) {
            result = process_record_for<System, EventT>(stored);
        }
    });
    return result;
}

template <typename Facility, typename EventT>
[[nodiscard]] bool take_pending_aggregate(StoredRecord& output) noexcept
{
    using Capture = CaptureTraits<typename Facility::template Policies<EventT>::Capture>;
    if constexpr (!Capture::aggregate) {
        return false;
    } else {
        auto& state = Facility::template event_state<EventT>;
        auto materialize = [&](auto& aggregate) {
            if (aggregate.count == 0) {
                return false;
            }
            output = aggregate.representative;
            output.header.sequence = Facility::next_sequence++;
            output.header.occurrence_count = aggregate.count;
            output.header.flags = output.header.flags | RecordFlag::Aggregated;
            aggregate = {};
            ++Facility::facility_record.accepted;
            ++state.record.captured;
            state.record.last_sequence = output.header.sequence;
            state.record.last_timestamp = output.header.timestamp;
            state.record.last_status = Status::Ok;
            state.record.last_failure = Reason::None;
            state.policy_counter = 0;
            return true;
        };

        if constexpr (Capture::keyed) {
            for (auto& entry : state.aggregation.entries) {
                if (entry.occupied && materialize(entry)) {
                    return true;
                }
            }
            return false;
        } else {
            return materialize(state.aggregation);
        }
    }
}

template <typename Facility>
[[nodiscard]] Result<void> flush_aggregates(const kernel::Deadline* deadline = nullptr) noexcept
{
    Result<void> first_error{};
    for_each_type<typename Facility::EventTypes>([&]<typename EventT> {
        while (true) {
            if (deadline != nullptr && deadline->expired()) {
                if (first_error) {
                    first_error = fail<solar::Error>({.status = solar::Status::Timeout});
                }
                break;
            }
            StoredRecord stored{};
            {
                auto guard = Facility::lock.acquire();
                if (!take_pending_aggregate<Facility, EventT>(stored)) {
                    break;
                }
            }
            auto processed =
                Facility::process_record == nullptr
                    ? Result<void>{fail<solar::Error>({.status = solar::Status::NotReady})}
                    : Facility::process_record(stored);
            if (!processed && first_error) {
                first_error = fail<solar::Error>(processed.error());
            }
        }
    });
    return first_error;
}

template <typename Facility> void cancel_aggregates() noexcept
{
    for_each_type<typename Facility::EventTypes>([]<typename EventT> {
        using Capture = CaptureTraits<typename Facility::template Policies<EventT>::Capture>;
        if constexpr (Capture::aggregate) {
            auto& state = Facility::template event_state<EventT>;
            std::uint64_t discarded{};
            if constexpr (Capture::keyed) {
                for (auto& entry : state.aggregation.entries) {
                    discarded += entry.count;
                    entry = {};
                }
            } else {
                discarded = state.aggregation.count;
                state.aggregation = {};
            }
            state.record.known_lost += discarded;
        }
    });
}

template <typename Facility>
[[nodiscard]] Result<void> drain_processor(const kernel::Deadline* deadline = nullptr) noexcept
{
    Facility::processor_pending.store(false, std::memory_order_release);
    Result<void> first_error{};
    while (true) {
        detail::StoredRecord record{};
        {
            auto guard = Facility::lock.acquire();
            if (detail::ingress_empty<Facility>()) {
                break;
            }
            if (deadline != nullptr && deadline->expired()) {
                return fail<solar::Error>({.status = solar::Status::Timeout});
            }
            if (!detail::pop_next<Facility>(record)) {
                break;
            }
        }
        auto processed = Facility::process_record == nullptr
                             ? Result<void>{fail<solar::Error>({.status = solar::Status::NotReady})}
                             : Facility::process_record(record);
        if (!processed && first_error) {
            first_error = fail<solar::Error>(processed.error());
        }
    }
    return first_error;
}

template <typename System, typename EventT> [[nodiscard]] EventRecord event_record() noexcept
{
    using Facility = typename System::EventFacility;
    auto guard = Facility::lock.acquire();
    return Facility::template event_state<EventT>.record;
}

template <typename System, typename EventT>
[[nodiscard]] Result<ConditionRecord, Error> condition_record(SourceId source) noexcept
{
    using Facility = typename System::EventFacility;
    auto guard = Facility::lock.acquire();
    for (const auto& candidate : Facility::template event_state<EventT>.conditions.entries) {
        if (candidate.occupied && candidate.record.source == source) {
            return candidate.record;
        }
    }
    return fail<Error>(make_error<System, EventT>(Operation::Query, Status::NotFound,
                                                  Reason::NotRegistered, source));
}

template <typename System, typename Processor>
[[nodiscard]] ProcessorRecord processor_record() noexcept
{
    using Facility = typename System::EventFacility;
    auto guard = Facility::lock.acquire();
    return Facility::template processor_record_state<Processor>;
}

template <typename System> [[nodiscard]] FacilityRecord facility_record() noexcept
{
    using Facility = typename System::EventFacility;
    auto guard = Facility::lock.acquire();
    auto record = Facility::facility_record;
    record.next_sequence = Facility::next_sequence;
    record.ready = Facility::ready.load(std::memory_order_acquire);
    record.accepting = Facility::accepting.load(std::memory_order_acquire);
    record.processor_pending = Facility::processor_pending.load(std::memory_order_acquire);
    return record;
}

template <typename System>
[[nodiscard]] HistoryPage read_history(Cursor cursor, std::span<Record> output,
                                       std::optional<LocalId> filter) noexcept
{
    using Facility = typename System::EventFacility;
    auto guard = Facility::lock.acquire();
    HistoryPage page{.next = cursor};
    Sequence search = cursor.next_sequence;
    Sequence oldest = std::numeric_limits<Sequence>::max();

    if (auto ordinary = Facility::history.next(1, filter)) {
        oldest = ordinary->header.sequence;
    }
    for_each_type<typename Facility::EventTypes>([&]<typename EventT> {
        Facility::template event_state<EventT>.critical_history.for_each(
            [&](const StoredRecord& stored) {
                if ((!filter || stored.header.event == *filter) &&
                    stored.header.sequence < oldest) {
                    oldest = stored.header.sequence;
                }
            });
    });

    while (true) {
        auto selected = Facility::history.next(search, filter);
        for_each_type<typename Facility::EventTypes>([&]<typename EventT> {
            Facility::template event_state<EventT>.critical_history.for_each(
                [&](const StoredRecord& stored) {
                    if (stored.header.sequence >= search &&
                        (!filter || stored.header.event == *filter) &&
                        (!selected || stored.header.sequence < selected->header.sequence)) {
                        selected = stored;
                    }
                });
        });
        if (!selected) {
            break;
        }
        ++page.available;
        if (page.written < output.size()) {
            output[page.written].header = selected->header;
            std::copy_n(selected->payload.begin(), selected->header.payload_size,
                        output[page.written].payload.begin());
            ++page.written;
            page.next.next_sequence = selected->header.sequence + 1;
        }
        search = selected->header.sequence + 1;
    }

    if (oldest != std::numeric_limits<Sequence>::max() && cursor.next_sequence < oldest &&
        Facility::history.evicted() != 0) {
        page.stale = true;
        page.evicted_before = oldest - cursor.next_sequence;
        if (page.written == 0) {
            page.next.next_sequence = oldest;
        }
    }
    return page;
}

template <typename System>
[[nodiscard]] Result<Record, Error> latest_history(std::optional<LocalId> filter) noexcept
{
    using Facility = typename System::EventFacility;
    auto guard = Facility::lock.acquire();
    std::optional<Record> selected{};
    if (auto ordinary = Facility::history.latest(filter)) {
        selected = *ordinary;
    }
    for_each_type<typename Facility::EventTypes>([&]<typename EventT> {
        Facility::template event_state<EventT>.critical_history.for_each(
            [&](const StoredRecord& stored) {
                if ((!filter || stored.header.event == *filter) &&
                    (!selected || stored.header.sequence > selected->header.sequence)) {
                    Record record{.header = stored.header};
                    std::copy_n(stored.payload.begin(), stored.header.payload_size,
                                record.payload.begin());
                    selected = record;
                }
            });
    });
    if (!selected) {
        return fail<Error>({.status = solar::Status::NotFound,
                            .reason = Reason::HistoryEmpty,
                            .operation = Operation::Query});
    }
    return *selected;
}

} // namespace solar::events::detail

namespace solar::events
{

template <typename Architecture>
template <typename System>
void Facility<Architecture>::activate_runtime() noexcept
{
    schedule_processor = [](bool from_isr) noexcept -> Result<void> {
        auto submitted =
            execution::detail::submit_registration<System, ProcessorRegistration>(from_isr);
        return submitted ? Result<void>{}
                         : Result<void>{fail<solar::Error>({.status = submitted.error().status})};
    };
    process_record = [](const detail::StoredRecord& record) noexcept -> Result<void> {
        return detail::process_one<System>(record);
    };
}

template <typename Architecture> Result<void> Facility<Architecture>::init() noexcept
{
    {
        auto guard = lock.acquire();
        thread_ingress.clear();
        isr_ingress.clear();
        history.clear();
        next_sequence = 1;
        facility_record = {
            .last_status = Status::Ok,
            .next_sequence = 1,
            .history_capacity = CONFIG_SOLAR_EVENTS_HISTORY_BYTES,
            .ready = true,
            .accepting = true,
        };
        for_each_type<EventTypes>([]<typename EventT> {
            auto& state = event_state<EventT>;
            state = {};
            state.record = {
                .event = LocalId{static_cast<LocalId::Representation>(
                    detail::type_index_v<EventT, EventTypes>)},
                .last_status = Status::Ok,
            };
        });
        for_each_type<ProcessorTypes>([]<typename Processor> {
            using EventT = typename processor_traits<Processor>::EventType;
            processor_record_state<Processor> = {
                .processor = ProcessorLocalId{static_cast<ProcessorLocalId::Representation>(
                    detail::type_index_v<Processor, ProcessorTypes>)},
                .event = LocalId{static_cast<LocalId::Representation>(
                    detail::type_index_v<EventT, EventTypes>)},
                .last_status = Status::Ok,
            };
        });
    }
    ready.store(true, std::memory_order_release);
    accepting.store(true, std::memory_order_release);
    processor_pending.store(false, std::memory_order_release);

    Result<void> initialized{};
    for_each_type<typename Architecture::Stores>([&]<typename Store> {
        if (initialized) {
            initialized = Store::initialize();
        }
    });
    return initialized;
}

template <typename Architecture> Result<void> Facility<Architecture>::start() noexcept
{
    accepting.store(true, std::memory_order_release);
    bool pending{};
    {
        auto guard = lock.acquire();
        facility_record.accepting = true;
        facility_record.last_status = Status::Ok;
        pending = !detail::ingress_empty<Facility>();
    }
    if (pending) {
        processor_pending.store(false, std::memory_order_release);
        return detail::request_processing<Facility>(false);
    }
    return {};
}

template <typename Architecture> Result<void> Facility<Architecture>::run_processor() noexcept
{
    return detail::drain_processor<Facility>();
}

template <typename Architecture> Result<void> Facility<Architecture>::stop() noexcept
{
    accepting.store(false, std::memory_order_release);
    {
        auto guard = lock.acquire();
        facility_record.accepting = false;
    }
    if constexpr (std::is_same_v<ProcessorStopPolicy, stop::CancelPending>) {
        auto guard = lock.acquire();
        thread_ingress.clear();
        isr_ingress.clear();
        for_each_type<EventTypes>([]<typename EventT> {
            Facility<Architecture>::template event_state<EventT>.critical_ingress.clear();
        });
        detail::cancel_aggregates<Facility>();
        detail::update_facility_pending<Facility>();
        return {};
    } else {
        const auto deadline =
            kernel::Deadline::after(std::chrono::milliseconds{CONFIG_SOLAR_EVENTS_STOP_TIMEOUT_MS});
        auto drained = detail::drain_processor<Facility>(&deadline);
        auto aggregates = detail::flush_aggregates<Facility>(&deadline);
        return !drained ? drained : aggregates;
    }
}

template <typename Architecture> Result<void> Facility<Architecture>::deinit() noexcept
{
    ready.store(false, std::memory_order_release);
    schedule_processor = nullptr;
    process_record = nullptr;
    auto guard = lock.acquire();
    facility_record.ready = false;
    return {};
}

#endif

} // namespace solar::events
