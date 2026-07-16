#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "solar/catalog/catalog.hpp"
#include "solar/component.hpp"
#include "solar/core/status.hpp"

namespace solar::health
{

struct CheckTag;
struct CheckIdentityDomain;

using CheckId = StableId<CheckIdentityDomain>;
using MonitorId = LocalId<CheckTag>;
using SubjectId = component::LocalId;

using Tick = std::int64_t;

struct Timestamp
{
    Tick ticks{};

    constexpr bool operator==(const Timestamp&) const = default;
    constexpr auto operator<=>(const Timestamp&) const = default;
};

enum class Condition : std::uint8_t
{
    Unknown,
    Nominal,
    Degraded,
    Faulted,
};

enum class Liveness : std::uint8_t
{
    Unknown,
    Live,
    Late,
    Stalled,
    Exited,
};

enum class Readiness : std::uint8_t
{
    Unknown,
    Ready,
    NotReady,
};

enum class Safety : std::uint8_t
{
    Unknown,
    Acceptable,
    AtRisk,
    Unsafe,
};

enum class Freshness : std::uint8_t
{
    Unknown,
    Current,
    Stale,
};

enum class EvidenceQuality : std::uint8_t
{
    Direct,
    Reported,
    Inferred,
};

enum class SourceKind : std::uint8_t
{
    SelfReport,
    ComponentAssessment,
    Lifecycle,
    Execution,
    Monitor,
    Events,
    Metrics,
    Logging,
    Remote,
    Hardware,
};

enum class SourceAvailability : std::uint8_t
{
    Unknown,
    Available,
    Unavailable,
    Unsupported,
};

enum class MonitorKind : std::uint8_t
{
    Check,
    Progress,
    StackMargin,
    Execution,
    Signal,
};

enum class Operation : std::uint8_t
{
    Report,
    ReportIsr,
    Progress,
    Assess,
    Check,
    Refresh,
    Query,
    HistoryRead,
};

enum class Reason : std::uint8_t
{
    NotReady,
    Disabled,
    NotRegistered,
    InvalidAssessment,
    Unsupported,
    Unavailable,
    Busy,
    Stale,
    IngressFull,
    SourceFailed,
    InternalInvariant,
};

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::InternalInvariant};
    Operation operation{Operation::Query};
    SubjectId subject{};
    MonitorId monitor{};
    int native{};
    std::uint32_t detail{};

    constexpr bool operator==(const Error&) const = default;
};

struct EvidenceRef
{
    EvidenceQuality quality{EvidenceQuality::Reported};
    SubjectId subject{};
    MonitorId monitor{};
    SourceKind source_kind{SourceKind::SelfReport};
    std::uint32_t source_id{};
};

struct Assessment
{
    Condition condition{Condition::Unknown};
    Liveness liveness{Liveness::Unknown};
    Readiness readiness{Readiness::Unknown};
    Safety safety{Safety::Unknown};
    Freshness freshness{Freshness::Current};
    std::optional<Error> last_error{};
    std::uint32_t detail{};
    bool recovering{};

    constexpr bool operator==(const Assessment&) const = default;
};

struct Observation
{
    Assessment assessment{};
    EvidenceQuality quality{EvidenceQuality::Direct};
    SourceAvailability availability{SourceAvailability::Available};
    Tick observed_at{};
    Tick stale_after{};
    bool required{};
    bool explicit_recovery{};

    constexpr bool operator==(const Observation&) const = default;
};

struct CompactObservation
{
    Condition condition{Condition::Unknown};
    Liveness liveness{Liveness::Unknown};
    Readiness readiness{Readiness::Unknown};
    Safety safety{Safety::Unknown};
    Status status{Status::Ok};
    std::uint32_t detail{};
    bool recovering{};

    constexpr bool operator==(const CompactObservation&) const = default;
};

struct CheckDescriptor
{
    std::string_view name;
    std::string_view description{};
    std::optional<CheckId> stable_id{};
    MonitorKind kind{MonitorKind::Check};
    std::int64_t period_ns{};
    std::int64_t stale_after_ns{};
    std::size_t stack_margin_bytes{};
    bool required{};
};

struct CheckDescriptorView : catalog::BasicDescriptorView<CheckTag, CheckDescriptor>
{};

struct SubjectRecord
{
    component::DescriptorView descriptor{};
    SubjectId subject{};
    Condition condition{Condition::Unknown};
    Liveness liveness{Liveness::Unknown};
    Readiness readiness{Readiness::Unknown};
    Safety safety{Safety::Unknown};
    Freshness freshness{Freshness::Unknown};
    Timestamp assessed_at{};
    Timestamp last_transition{};
    std::uint64_t assessment_generation{};
    std::uint64_t transition_count{};
    std::uint64_t report_count{};
    std::uint64_t repeated_count{};
    std::optional<Error> last_error{};
    std::optional<EvidenceRef> primary_evidence{};
    bool recovering{};
};

struct MonitorRecord
{
    CheckDescriptorView descriptor{};
    MonitorId monitor{};
    SubjectId subject{};
    Observation observation{};
    std::uint64_t observations{};
    std::uint64_t failures{};
    std::uint64_t recoveries{};
    std::uint64_t stale_transitions{};
    bool has_observation{};
    bool stale{};
};

struct SystemRecord
{
    Condition condition{Condition::Unknown};
    Readiness readiness{Readiness::Unknown};
    Safety safety{Safety::Unknown};
    Freshness freshness{Freshness::Unknown};
    std::uint32_t nominal_subjects{};
    std::uint32_t degraded_subjects{};
    std::uint32_t faulted_subjects{};
    std::uint32_t unknown_subjects{};
    std::uint32_t stale_subjects{};
    std::optional<EvidenceRef> primary_evidence{};
    std::uint64_t assessment_generation{};
    Timestamp assessed_at{};
    std::uint64_t isr_reports_admitted{};
    std::uint64_t isr_reports_dropped{};
};

struct Receipt
{
    SubjectId subject{};
    std::uint64_t generation{};
    bool transitioned{};
    bool repeated{};
};

struct ProgressReceipt
{
    SubjectId subject{};
    std::uint64_t generation{};
    Tick observed_at{};
};

struct TransitionRecord
{
    std::uint64_t sequence{};
    SubjectId subject{};
    Condition previous{Condition::Unknown};
    Condition current{Condition::Unknown};
    Timestamp occurred_at{};
    std::optional<EvidenceRef> evidence{};
};

struct HistoryCursor
{
    std::uint64_t sequence{};
};

struct HistoryPage
{
    HistoryCursor next{};
    std::size_t written{};
    std::size_t available{};
    std::uint64_t overwritten{};
};

} // namespace solar::health
