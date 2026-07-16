#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "solar/catalog/catalog.hpp"
#include "solar/component.hpp"
#include "solar/core/status.hpp"

namespace solar::parameters
{

struct Tag
{};

struct ChangeTag
{};

struct IdentityDomain
{};

struct ChangeIdentityDomain
{};

struct GroupIdentityDomain
{};

using Id = StableId<IdentityDomain>;
using GroupId = StableId<GroupIdentityDomain>;
using LocalId = solar::LocalId<Tag>;
using ChangeLocalId = solar::LocalId<ChangeTag>;

struct Descriptor
{
    std::string_view name;
    std::string_view description{};
    std::string_view units{};
    std::optional<Id> stable_id{};
    std::uint16_t version{1};
};

struct ChangeDescriptor
{
    std::string_view name;
    std::uint16_t version{1};
};

using DescriptorCatalogView = catalog::BasicDescriptorView<Tag, Descriptor>;
using ChangeCatalogView = catalog::BasicDescriptorView<ChangeTag, ChangeDescriptor>;

enum class StorageKind : std::uint8_t
{
    Mutex,
    Atomic,
    Immutable,
};

enum class ValidationKind : std::uint8_t
{
    AcceptAny,
    RangeReject,
    RangeClamp,
    OneOf,
    Custom,
};

enum class AccessKind : std::uint8_t
{
    ReadWrite,
    ReadOnly,
    Privileged,
};

enum class ExternalKind : std::uint8_t
{
    LocalOnly,
    Readable,
    Writable,
};

enum class PersistenceKind : std::uint8_t
{
    Volatile,
    Immediate,
    Deferred,
    Manual,
    Transactional,
};

enum class PersistenceState : std::uint8_t
{
    Volatile,
    Clean,
    Dirty,
    Scheduled,
    Writing,
    Failed,
    ResetPending,
    MigrationPending,
};

enum class LoadSource : std::uint8_t
{
    Default,
    Store,
    Migration,
};

enum class LoadOutcome : std::uint8_t
{
    NotAttempted,
    VolatileDefault,
    MissingDefault,
    Loaded,
    Migrated,
    DefaultAfterFailure,
    Failed,
};

enum class UpdateOrigin : std::uint8_t
{
    LocalSet,
    LocalTransaction,
    Reset,
    PrivilegedProvisioning,
    External,
    BootLoad,
    Migration,
};

enum class Operation : std::uint8_t
{
    Get,
    TryGet,
    GetIsr,
    Set,
    TrySet,
    Reset,
    Snapshot,
    Transaction,
    Save,
    SaveAll,
    Flush,
    Query,
    Initialize,
    ActivateChanges,
    Stop,
};

enum class Reason : std::uint8_t
{
    None,
    NotReady,
    Disabled,
    NotRegistered,
    ReadOnly,
    PrivilegeRequired,
    ValidationRejected,
    WouldBlock,
    InvalidContext,
    PersistenceUnavailable,
    PersistenceFailed,
    CodecFailed,
    VersionMismatch,
    CorruptRecord,
    TransactionConflict,
    ChangeHandlerFailed,
    InternalInvariant,
};

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::None};
    Operation operation{Operation::Query};
    LocalId parameter{};
    ChangeLocalId change{};
    std::optional<std::uint64_t> stable_id{};
    std::optional<std::uint64_t> group_id{};
    std::uint16_t expected_version{};
    std::uint16_t observed_version{};
    int native_error{};
};

[[nodiscard]] constexpr Status status_of(const Error& error) noexcept
{
    return error.status;
}

struct ValidationError
{
    Status status{Status::Invalid};
};

template <typename ValueT> struct Normalized
{
    ValueT value;
    bool adjusted{};
};

template <typename Parameter> struct Update
{
    using Value = typename Parameter::Value;

    Value effective_value;
    std::uint64_t revision{};
    PersistenceState persistence{PersistenceState::Volatile};
    std::size_t change_failures{};
    bool changed{};
    bool adjusted{};
    bool changes_deferred{};
};

template <typename Parameter> struct Change
{
    using Value = typename Parameter::Value;

    Value old_value;
    Value new_value;
    std::uint64_t revision{};
    UpdateOrigin origin{UpdateOrigin::LocalSet};
    bool adjusted{};
    bool transaction{};
    bool reset{};
};

struct DescriptorView
{
    LocalId local_id{};
    Descriptor descriptor{};
    OwnerView owner{};
    OriginKind origin{OriginKind::Direct};
    std::size_t value_size{};
    std::size_t value_alignment{};
    StorageKind storage{StorageKind::Mutex};
    ValidationKind validation{ValidationKind::AcceptAny};
    AccessKind access{AccessKind::ReadWrite};
    ExternalKind external{ExternalKind::LocalOnly};
    PersistenceKind persistence{PersistenceKind::Volatile};
    std::size_t encoded_size{};
};

template <typename Parameter> struct ParameterRecord
{
    using Value = typename Parameter::Value;

    LocalId parameter{};
    Value value;
    Error last_error{};
    std::uint64_t revision{};
    std::uint64_t pending_revision{};
    std::uint64_t persisted_revision{};
    std::uint64_t reads{};
    std::uint64_t updates{};
    std::uint64_t unchanged{};
    std::uint64_t rejected{};
    std::uint64_t saves{};
    std::uint64_t save_failures{};
    std::uint64_t validation_adjustments{};
    std::uint64_t change_failures{};
    std::uint16_t current_version{};
    std::uint16_t stored_version{};
    PersistenceState persistence{PersistenceState::Volatile};
    LoadSource load_source{LoadSource::Default};
    LoadOutcome load_outcome{LoadOutcome::NotAttempted};
    UpdateOrigin last_origin{UpdateOrigin::BootLoad};
    bool ready{};
    bool writable{};
    bool dirty{};
    bool pending{};
};

struct ChangeRecord
{
    ChangeLocalId change{};
    LocalId parameter{};
    component::LocalId observer{};
    Status last_status{Status::NotReady};
    std::uint64_t invocations{};
    std::uint64_t succeeded{};
    std::uint64_t failed{};
    std::uint64_t deferred{};
    std::uint64_t coalesced{};
    std::uint64_t last_revision{};
    bool pending{};
    bool invoking{};
};

template <typename... Parameters> class Snapshot
{
  public:
    using Values = std::tuple<typename Parameters::Value...>;

    constexpr explicit Snapshot(Values values) : values_(std::move(values)) {}

    template <typename Parameter> [[nodiscard]] constexpr const auto& get() const noexcept
    {
        return std::get<index_of<Parameter>()>(values_);
    }

    [[nodiscard]] constexpr const Values& values() const noexcept
    {
        return values_;
    }

  private:
    template <typename Needle, std::size_t Index, typename Head, typename... Tail>
    [[nodiscard]] static consteval std::size_t find_index()
    {
        if constexpr (std::is_same_v<Needle, Head>) {
            return Index;
        } else {
            static_assert(sizeof...(Tail) != 0,
                          "SOLAR_DIAGNOSTIC_PARAMETER_SNAPSHOT_LOOKUP: parameter is absent from "
                          "this snapshot");
            return find_index<Needle, Index + 1, Tail...>();
        }
    }

    template <typename Parameter> [[nodiscard]] static consteval std::size_t index_of()
    {
        return find_index<Parameter, 0, Parameters...>();
    }

    Values values_;
};

template <typename Parameter> struct Assignment
{
    using ParameterType = Parameter;
    typename Parameter::Value value;
};

template <typename Parameter>
[[nodiscard]] constexpr Assignment<Parameter> assign(typename Parameter::Value value)
{
    return {.value = std::move(value)};
}

struct TransactionUpdate
{
    std::size_t changed{};
    std::size_t adjusted{};
    std::size_t change_failures{};
    bool changes_deferred{};
};

struct PersistenceReport
{
    std::size_t visited{};
    std::size_t saved{};
    std::size_t clean{};
    std::size_t failed{};
};

} // namespace solar::parameters
