#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "solar/catalog/catalog.hpp"
#include "solar/component.hpp"
#include "solar/core/status.hpp"
#include "solar/core/time.hpp"

namespace solar::execution
{

struct Tag
{};

struct IdentityDomain
{};

using Id = StableId<IdentityDomain>;
using LocalId = solar::LocalId<Tag>;

enum class RegistrationKind : std::uint8_t
{
    OnDemand,
    Delayable,
    Periodic,
    PollTriggered,
    BusRoute,
    ParameterPersistence,
    EventProcessor,
    LogProcessor,
    MetricsExporter,
    RemoteHandler,
    Infrastructure,
};

struct Descriptor
{
    std::string_view name;
    RegistrationKind kind{RegistrationKind::OnDemand};
    std::string_view description{};
    std::optional<Id> stable_id{};
    std::uint16_t version{1};
};

using DescriptorView = catalog::BasicDescriptorView<Tag, Descriptor>;

enum class TargetKind : std::uint8_t
{
    SystemWorkQueue,
    OwnedWorkQueue,
};

enum class TargetSource : std::uint8_t
{
    Explicit,
    KconfigDefault,
};

enum class Availability : std::uint8_t
{
    Uninitialized,
    Inactive,
    Active,
    Suspended,
    Stopping,
    Quiescent,
    Failed,
};

enum class Operation : std::uint8_t
{
    Submit,
    SubmitIsr,
    Schedule,
    Reschedule,
    Cancel,
    CancelSync,
    Flush,
    Activate,
    Stop,
};

enum class SubmissionDisposition : std::uint8_t
{
    Queued,
    AlreadyPending,
    RequeuedAfterCurrent,
    Counted,
};

enum class ErrorReason : std::uint8_t
{
    SubsystemNotReady,
    RegistrationInactive,
    RegistrationSuspended,
    NotRegistered,
    ExecutorUnavailable,
    QueueStopped,
    QueueDraining,
    WorkCancelling,
    AdmissionFull,
    InvalidContext,
    Timeout,
    UnsupportedOperation,
    NativeFailure,
    InternalInvariant,
};

struct Error
{
    Status status{Status::Error};
    ErrorReason reason{ErrorReason::InternalInvariant};
    LocalId registration{};
    component::LocalId target{};
    TargetKind target_kind{TargetKind::SystemWorkQueue};
    Operation operation{Operation::Submit};
    Availability availability{Availability::Uninitialized};
    int native_error{};
};

struct Submission
{
    LocalId registration{};
    component::LocalId target{};
    TargetKind target_kind{TargetKind::SystemWorkQueue};
    SubmissionDisposition disposition{SubmissionDisposition::Queued};
    std::uint32_t accepted{};
    std::uint64_t sequence{};
    bool from_isr{};
};

struct Cancellation
{
    LocalId registration{};
    bool pending_cancelled{};
    bool quiescent{};
};

enum class ContainmentState : std::uint8_t
{
    NotPrepared,
    Prepared,
    Clean,
    Forced,
    Uncontained,
};

enum class ThreadState : std::uint8_t
{
    Unknown,
    Empty,
    Prepared,
    Scheduled,
    Running,
    Suspended,
    Exited,
    Aborted,
};

struct RegistrationRecord
{
    DescriptorView descriptor{};
    LocalId local_id{};
    component::LocalId owner{};
    component::LocalId target{};
    RegistrationKind kind{RegistrationKind::OnDemand};
    TargetKind target_kind{TargetKind::SystemWorkQueue};
    TargetSource target_source{TargetSource::Explicit};
    Availability availability{Availability::Uninitialized};
    Status last_status{Status::NotReady};
    std::uint64_t release_attempts{};
    std::uint64_t submissions{};
    std::uint64_t isr_submissions{};
    std::uint64_t queued{};
    std::uint64_t already_pending{};
    std::uint64_t requeued{};
    std::uint64_t counted{};
    std::uint64_t admission_rejected{};
    std::uint64_t started{};
    std::uint64_t completed{};
    std::uint64_t failed{};
    std::uint64_t cancelled{};
    std::uint64_t missed_releases{};
    std::uint64_t overruns{};
    std::uint64_t deadline_misses{};
    std::uint32_t pending_count{};
    std::uint32_t pending_high_water{};
    std::int64_t last_release{};
    std::int64_t last_start{};
    std::int64_t last_completion{};
    std::int64_t last_duration{};
    std::int64_t maximum_duration{};
    bool initialized{};
    bool active{};
    bool accepting{};
    bool armed{};
    bool queued_now{};
    bool in_flight{};
    bool stop_requested{};
    bool quiescent{true};
};

struct ServiceRecord
{
    component::LocalId component{};
    std::size_t stack_bytes{};
    int priority{};
    Milliseconds stop_timeout{};
    Status run_status{Status::NotReady};
    ThreadState thread_state{ThreadState::Empty};
    void* thread{};
    ContainmentState containment{ContainmentState::NotPrepared};
    bool abort_on_timeout{};
    bool thread_created{};
    bool thread_started{};
    bool running{};
    bool stop_requested{};
    bool exited{};
    bool exited_after_stop{};
    bool join_attempted{};
    bool joined{};
    bool timed_out{};
    bool abort_attempted{};
    bool abort_succeeded{};
    bool abort_failed{};
};

struct ExecutorRecord
{
    component::LocalId component{};
    std::size_t stack_bytes{};
    int priority{};
    Milliseconds work_timeout{};
    Milliseconds stop_timeout{};
    Status last_status{Status::NotReady};
    void* thread{};
    ContainmentState containment{ContainmentState::NotPrepared};
    std::size_t registration_count{};
    std::size_t active_registrations{};
    std::uint64_t submissions{};
    std::uint64_t started_items{};
    std::uint64_t completed_items{};
    std::uint64_t failed_items{};
    std::uint64_t pending_count{};
    std::uint64_t pending_high_water{};
    bool yields_between_items{true};
    bool abort_on_timeout{};
    bool initialized{};
    bool started{};
    bool accepting{};
    bool draining{};
    bool plugged{};
    bool stopped{};
    bool timed_out{};
    bool abort_attempted{};
    bool abort_succeeded{};
    bool abort_failed{};
    bool unexpected_exit{};
};

struct SystemTargetRecord
{
    TargetKind kind{TargetKind::SystemWorkQueue};
    std::size_t registration_count{};
    std::uint64_t submissions{};
    std::uint64_t completions{};
    bool externally_owned{true};
    bool globally_controllable{};
};

struct RecordPage
{
    std::size_t offset{};
    std::size_t count{};
    std::size_t total{};

    [[nodiscard]] constexpr bool has_more() const noexcept
    {
        return offset + count < total;
    }
};

} // namespace solar::execution

template <> struct solar::catalog_traits<solar::execution::Tag>
{
    using Descriptor = solar::execution::Descriptor;
    using DescriptorView = solar::execution::DescriptorView;
    using IdentityDomain = solar::execution::IdentityDomain;

    template <typename Declaration> static constexpr bool requires_stable_id = false;

    static consteval bool validate(const Descriptor& descriptor)
    {
        return descriptor.version != 0;
    }

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor = solar::catalog::descriptor_for_view(
                solar::descriptor_traits<solar::execution::Tag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};
