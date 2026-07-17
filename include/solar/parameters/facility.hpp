#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "solar/parameters/codec.hpp"
#include "solar/parameters/contribution.hpp"
#include "solar/parameters/declaration.hpp"
#include "solar/parameters/persistence.hpp"
#include "solar/parameters/policy.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_PARAMETERS)
#include "solar/execution/registration.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/mutex.hpp"
#include "solar/kernel/spinlock.hpp"
#endif

namespace solar::parameters
{

#if defined(CONFIG_SOLAR) && !defined(CONFIG_SOLAR_PARAMETERS)
inline constexpr bool enabled = false;
#else
inline constexpr bool enabled = true;
#endif

template <typename Architecture> struct Facility;

namespace detail
{

#if defined(CONFIG_SOLAR_PARAMETERS_PERSISTENCE)
inline constexpr auto persistence_timeout =
    std::chrono::milliseconds{CONFIG_SOLAR_PARAMETERS_PERSISTENCE_TIMEOUT_MS};
#else
inline constexpr auto persistence_timeout = std::chrono::milliseconds{0};
#endif

template <typename List> struct DeclarationsOf;

template <typename... Entries> struct DeclarationsOf<TypeList<Entries...>>
{
    using type = TypeList<typename Entries::Declaration...>;
};

template <typename List> using declarations_of_t = typename DeclarationsOf<List>::type;

} // namespace detail

#if !defined(__ZEPHYR__) || !defined(CONFIG_SOLAR_PARAMETERS)

template <typename ParameterDeclarations, typename ChangeDeclarations, typename Components,
          typename Configuration>
struct Architecture
{
    using Parameters = ParameterDeclarations;
    using Changes = ChangeDeclarations;
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;
    using BootstrapDependencies = TypeList<>;
    using Groups = TypeList<>;

    static constexpr bool demanded = list_size_v<Parameters> != 0 || list_size_v<Changes> != 0 ||
                                     list_size_v<ConfigurationPolicies> != 0;
    static constexpr bool has_deferred = false;
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    using ParameterTypes = typename Architecture::Parameters;
    using ChangeTypes = typename Architecture::Changes;
    using Groups = typename Architecture::Groups;

    static constexpr component::Descriptor descriptor{
        .name = "solar.parameters",
        .description = "Typed runtime parameters",
    };

    template <typename> static void activate_runtime() noexcept {}
};

namespace detail
{

template <typename ParameterT>
inline constexpr bool default_codec_available_v =
    requires { typename ParameterT::Codec; } || std::is_integral_v<typename ParameterT::Value> ||
    std::is_enum_v<typename ParameterT::Value> ||
    std::is_floating_point_v<typename ParameterT::Value>;

} // namespace detail

#else

namespace detail
{

template <typename Axis, typename Policies> struct PolicyForAxis;

template <typename Axis> struct PolicyForAxis<Axis, TypeList<>>
{
    using type = NoPolicy;
};

template <typename Axis, typename Head, typename... Tail>
struct PolicyForAxis<Axis, TypeList<Head, Tail...>>
{
  private:
    using Remaining = typename PolicyForAxis<Axis, TypeList<Tail...>>::type;
    using HeadAxis = typename subsystem_policy_traits<Tag, Head>::Axis;

  public:
    using type = std::conditional_t<std::is_same_v<Axis, HeadAxis>, Head, Remaining>;
};

template <typename Axis, typename Policies>
using policy_for_axis_t = typename PolicyForAxis<Axis, Policies>::type;

template <typename Wrapper, typename Fallback, bool Missing = std::is_same_v<Wrapper, NoPolicy>>
struct UnwrapPolicy
{
    using type = typename Wrapper::PolicyType;
};

template <typename Wrapper, typename Fallback> struct UnwrapPolicy<Wrapper, Fallback, true>
{
    using type = Fallback;
};

template <typename Axis, typename Configuration, typename Fallback>
using configured_policy_t =
    typename UnwrapPolicy<policy_for_axis_t<Axis, Configuration>, Fallback>::type;

#if defined(CONFIG_SOLAR_PARAMETERS_DEFAULT_LOAD_FAIL_BOOT)
using KconfigLoadFailure = load::FailBoot;
#else
using KconfigLoadFailure = load::UseDefaultAndReport;
#endif

#if defined(CONFIG_SOLAR_PARAMETERS_DEFAULT_STOP_CANCEL)
using KconfigPersistenceStop = stop::CancelPending;
#else
using KconfigPersistenceStop = stop::FlushDeferred;
#endif

template <typename ParameterT, typename Configuration> struct ParameterPolicies
{
    using Validation =
        resolve_policy_t<typename DeclaredValidation<ParameterT>::type,
                         configured_policy_t<DefaultValidationAxis, Configuration, AcceptAny>,
                         AcceptAny>;
    using Access =
        resolve_policy_t<typename DeclaredAccess<ParameterT>::type,
                         configured_policy_t<DefaultAccessAxis, Configuration, ReadWrite>,
                         ReadWrite>;
    using External =
        resolve_policy_t<typename DeclaredExternal<ParameterT>::type,
                         configured_policy_t<DefaultExternalAxis, Configuration, LocalOnly>,
                         LocalOnly>;
    using Storage =
        resolve_policy_t<typename DeclaredStorage<ParameterT>::type,
                         configured_policy_t<DefaultStorageAxis, Configuration, storage::Mutex>,
                         storage::Mutex>;
    using Persistence = resolve_policy_t<
        typename DeclaredPersistence<ParameterT>::type,
        configured_policy_t<DefaultPersistenceAxis, Configuration, persistence::Volatile>,
        persistence::Volatile>;
    using LoadFailure = resolve_policy_t<
        typename DeclaredLoadFailure<ParameterT>::type,
        configured_policy_t<DefaultLoadFailureAxis, Configuration, KconfigLoadFailure>,
        KconfigLoadFailure>;
};

template <typename Value, typename Storage> struct AtomicStorageValid : std::true_type
{};

template <typename Value>
struct AtomicStorageValid<Value, storage::Atomic>
    : std::bool_constant<std::is_trivially_copyable_v<Value> &&
                         std::atomic<Value>::is_always_lock_free>
{};

template <typename ParameterT>
inline constexpr bool default_codec_available_v =
    requires { typename ParameterT::Codec; } || std::is_integral_v<typename ParameterT::Value> ||
    std::is_enum_v<typename ParameterT::Value> ||
    std::is_floating_point_v<typename ParameterT::Value>;

template <typename ParameterT, typename Policies> [[nodiscard]] consteval bool valid_default()
{
    constexpr auto normalized = normalize<typename Policies::Validation>(
        typename ParameterT::Value{ParameterT::default_value});
    return normalized.has_value() && !normalized->adjusted &&
           normalized->value == typename ParameterT::Value{ParameterT::default_value};
}

template <typename ParameterT, typename Configuration> struct ValidateParameter
{
    static_assert(Parameter<ParameterT>,
                  "SOLAR_DIAGNOSTIC_INVALID_PARAMETER: declaration requires Value, Descriptor, "
                  "constant default_value, copyable owned value semantics, and equality");
    using Value = typename ParameterT::Value;
    using Policies = ParameterPolicies<ParameterT, Configuration>;
    using Persistence = PersistenceTraits<typename Policies::Persistence>;

    static_assert(sizeof(Value) <= CONFIG_SOLAR_PARAMETERS_MAX_VALUE_BYTES,
                  "SOLAR_DIAGNOSTIC_PARAMETER_VALUE_SIZE: parameter value exceeds the configured "
                  "hard byte ceiling");
    static_assert(!std::is_pointer_v<Value>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_BORROWED_VALUE: pointer values cannot own canonical "
                  "parameter state");
    static_assert(ValidationTraits<typename Policies::Validation>::valid,
                  "SOLAR_DIAGNOSTIC_PARAMETER_VALIDATION_POLICY: invalid validation policy");
    static_assert(AccessTraits<typename Policies::Access>::valid,
                  "SOLAR_DIAGNOSTIC_PARAMETER_ACCESS_POLICY: invalid local access policy");
    static_assert(ExternalTraits<typename Policies::External>::valid,
                  "SOLAR_DIAGNOSTIC_PARAMETER_EXTERNAL_POLICY: invalid external policy");
    static_assert(StorageTraits<typename Policies::Storage>::valid,
                  "SOLAR_DIAGNOSTIC_PARAMETER_STORAGE_POLICY: invalid storage policy");
    static_assert(Persistence::valid,
                  "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_POLICY: invalid persistence policy");
    static_assert(valid_default<ParameterT, Policies>(),
                  "SOLAR_DIAGNOSTIC_PARAMETER_INVALID_DEFAULT: validation must accept the "
                  "declared constant default without adjustment");
    static_assert(AtomicStorageValid<Value, typename Policies::Storage>::value,
                  "SOLAR_DIAGNOSTIC_PARAMETER_ATOMIC_NOT_LOCK_FREE: explicit atomic parameter "
                  "storage must be always lock-free on this target");
    static_assert(!std::is_same_v<typename Policies::Storage, storage::Immutable> ||
                      !AccessTraits<typename Policies::Access>::writable,
                  "SOLAR_DIAGNOSTIC_PARAMETER_IMMUTABLE_WRITABLE: immutable storage requires a "
                  "non-writable access policy");
    static_assert(!Persistence::persistent ||
                      descriptor_traits<Tag, ParameterT>::descriptor.stable_id.has_value(),
                  "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_STABLE_ID: persistent parameters "
                  "require an explicit stable ID");
    static_assert(!Persistence::persistent || default_codec_available_v<ParameterT>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_CODEC_REQUIRED: persistent parameters require an "
                  "explicit codec or supported scalar value");
    static_assert(Persistence::kind == PersistenceKind::Volatile ||
                      IS_ENABLED(CONFIG_SOLAR_PARAMETERS_PERSISTENCE),
                  "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_DISABLED: parameter persistence is "
                  "disabled by Kconfig");
    static constexpr bool valid = true;
};

template <typename Return>
concept ChangeReturn = std::same_as<Return, void> || std::same_as<Return, Status> ||
                       std::same_as<Return, Result<void>>;

template <typename Handler, typename ParameterT>
concept ValidChangeHandler = requires(const Change<ParameterT>& change) {
    requires ChangeReturn<decltype(Handler::changed(change))>;
};

template <typename ChangeT, typename Parameters, typename Components> struct ValidateChange
{
    static_assert(change_traits<ChangeT>::valid,
                  "SOLAR_DIAGNOSTIC_INVALID_PARAMETER_CHANGE: change registration has an invalid "
                  "shape");
    using Traits = change_traits<ChangeT>;
    using ParameterType = typename Traits::ParameterType;
    static_assert(contains_v<ParameterType, Parameters>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_CHANGE_UNREGISTERED: change hook references an "
                  "unregistered parameter");
    static_assert(contains_v<typename Traits::ObserverType, Components>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_CHANGE_OBSERVER: change observer is absent from the "
                  "component graph");
    static_assert(ValidChangeHandler<typename Traits::HandlerType, ParameterType>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_CHANGE_HANDLER: handler must expose static void, "
                  "Status, or Result<void> changed(const Change<P>&)");
    static constexpr bool valid = true;
};

template <typename Left, typename Right> [[nodiscard]] consteval bool same_change_key()
{
    using L = change_traits<Left>;
    using R = change_traits<Right>;
    return std::is_same_v<typename L::ObserverType, typename R::ObserverType> &&
           std::is_same_v<typename L::ParameterType, typename R::ParameterType> &&
           std::is_same_v<typename L::RouteTagType, typename R::RouteTagType>;
}

template <typename... Changes> struct ValidateChangePairs;

template <> struct ValidateChangePairs<>
{
    static constexpr bool valid = true;
};

template <typename Head, typename... Tail> struct ValidateChangePairs<Head, Tail...>
{
    static_assert((!same_change_key<Head, Tail>() && ...),
                  "SOLAR_DIAGNOSTIC_DUPLICATE_PARAMETER_CHANGE: observer, parameter, and route "
                  "tag identify more than one change hook");
    static constexpr bool valid = ValidateChangePairs<Tail...>::valid;
};

template <typename Policy> struct StoreForPersistence
{
    using type = typename PersistenceTraits<Policy>::Store;
};

template <typename Group> struct StoreForPersistence<persistence::Transactional<Group>>
{
    using type = typename Group::Store;
};

template <typename Parameters, typename Configuration> struct PersistenceStores;

template <typename Configuration> struct PersistenceStores<TypeList<>, Configuration>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail, typename Configuration>
struct PersistenceStores<TypeList<Head, Tail...>, Configuration>
{
  private:
    using Policy = typename ParameterPolicies<Head, Configuration>::Persistence;
    using Store = typename StoreForPersistence<Policy>::type;
    using Current = std::conditional_t<std::is_void_v<Store>, TypeList<>, TypeList<Store>>;

  public:
    using type = unique_t<
        concat_t<Current, typename PersistenceStores<TypeList<Tail...>, Configuration>::type>>;
};

template <typename Parameters, typename Configuration> struct HasDeferred;

template <typename... ParametersT, typename Configuration>
struct HasDeferred<TypeList<ParametersT...>, Configuration>
    : std::bool_constant<(PersistenceTraits<typename ParameterPolicies<
                              ParametersT, Configuration>::Persistence>::deferred ||
                          ...)>
{};

template <typename Wrapper, bool Missing = std::is_same_v<Wrapper, NoPolicy>> struct ResolveExecutor
{
    using type = typename Wrapper::ExecutorType;
};

template <typename Wrapper> struct ResolveExecutor<Wrapper, true>
{
    using type =
        std::conditional_t<IS_ENABLED(CONFIG_SOLAR_PARAMETERS_DEFERRED_SYSTEM_WORKQUEUE_DEFAULT),
                           execution::SystemWorkQueue, execution::DefaultTarget>;
};

template <typename Configuration> struct ConfiguredExecutor
{
    using type =
        typename ResolveExecutor<policy_for_axis_t<PersistenceExecutorAxis, Configuration>>::type;
};

template <typename Configuration> struct ConfiguredStop
{
    using type = configured_policy_t<PersistenceStopAxis, Configuration, KconfigPersistenceStop>;
};

template <typename Wrapper, bool Missing = std::is_same_v<Wrapper, NoPolicy>> struct GroupList
{
    using type = typename Wrapper::Types;
};

template <typename Wrapper> struct GroupList<Wrapper, true>
{
    using type = TypeList<>;
};

template <typename Configuration> struct ConfiguredGroups
{
    using type = typename GroupList<policy_for_axis_t<PersistenceGroupsAxis, Configuration>>::type;
};

template <typename Group, typename = void> struct DeclaredGroupCommit
{
    using type = persistence::Manual<typename Group::Store>;
};

template <typename Group> struct DeclaredGroupCommit<Group, std::void_t<typename Group::Commit>>
{
    using type = typename Group::Commit;
};

template <typename Group> struct GroupTraits
{
    static constexpr bool shaped = requires {
        typename Group::Members;
        typename Group::Members::Types;
        typename Group::Store;
        { Group::stable_id } -> std::convertible_to<GroupId>;
        { Group::version } -> std::convertible_to<std::uint16_t>;
    };
    using Members = typename Group::Members::Types;
    using Store = typename Group::Store;
    using Commit = typename DeclaredGroupCommit<Group>::type;
    using CommitTraits = PersistenceTraits<Commit>;
};

template <typename Policy>
struct EffectiveDeferred : std::bool_constant<PersistenceTraits<Policy>::deferred>
{};

template <typename Group>
struct EffectiveDeferred<persistence::Transactional<Group>>
    : std::bool_constant<GroupTraits<Group>::CommitTraits::deferred>
{};

template <typename Group, typename Parameters, typename Configuration> struct ValidateGroup
{
    using Traits = GroupTraits<Group>;
    static_assert(Traits::shaped,
                  "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_SHAPE: persistence group requires Members, "
                  "Store, stable_id, and version");
    static_assert(list_size_v<typename Traits::Members> != 0,
                  "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_EMPTY: persistence group has no members");
    static_assert(unique_types_v<typename Traits::Members>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_DUPLICATE_MEMBER: persistence group repeats "
                  "a member");
    static_assert(
        []<typename... MembersT>(TypeList<MembersT...>) {
            return (contains_v<MembersT, Parameters> && ...);
        }(typename Traits::Members{}),
        "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_UNREGISTERED_MEMBER: persistence group "
        "contains an unregistered parameter");
    static_assert(
        []<typename... MembersT>(TypeList<MembersT...>) {
            return (std::is_same_v<typename PersistenceTraits<typename ParameterPolicies<
                                       MembersT, Configuration>::Persistence>::Group,
                                   Group> &&
                    ...);
        }(typename Traits::Members{}),
        "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_POLICY_MISMATCH: every group member must "
        "select Transactional<this-group>");
    static_assert(Traits::CommitTraits::valid && Traits::CommitTraits::persistent &&
                      Traits::CommitTraits::kind != PersistenceKind::Transactional,
                  "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_COMMIT: group Commit must be Immediate, "
                  "Deferred, or Manual");
    static_assert(std::is_same_v<typename Traits::CommitTraits::Store, typename Traits::Store>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_STORE_MISMATCH: group Commit store must match "
                  "the declared Store");
    static constexpr bool valid = true;
};

template <typename Groups> struct FlattenGroupMembers;

template <> struct FlattenGroupMembers<TypeList<>>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail> struct FlattenGroupMembers<TypeList<Head, Tail...>>
{
    using type = concat_t<typename GroupTraits<Head>::Members,
                          typename FlattenGroupMembers<TypeList<Tail...>>::type>;
};

template <typename Parameters, typename Configuration, typename Groups>
struct ValidateTransactionalParameters;

template <typename Parameters, typename Configuration, typename... GroupsT>
struct ValidateTransactionalParameters<Parameters, Configuration, TypeList<GroupsT...>>
{
    using Members = typename FlattenGroupMembers<TypeList<GroupsT...>>::type;
    static_assert(unique_types_v<Members>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_MULTIPLE_GROUPS: a parameter belongs to more than "
                  "one transactional persistence group");
    static_assert(
        []<typename... ParametersT>(TypeList<ParametersT...>) {
            return (
                (PersistenceTraits<
                     typename ParameterPolicies<ParametersT, Configuration>::Persistence>::kind !=
                     PersistenceKind::Transactional ||
                 contains_v<ParametersT, Members>) &&
                ...);
        }(Parameters{}),
        "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_NOT_REGISTERED: transactional parameter "
        "references a group absent from PersistenceGroups");
    static constexpr bool valid = (ValidateGroup<GroupsT, Parameters, Configuration>::valid && ...);
};

template <typename Stores, typename Components> struct ComponentStores;

template <typename Components> struct ComponentStores<TypeList<>, Components>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail, typename Components>
struct ComponentStores<TypeList<Head, Tail...>, Components>
{
    using Current = std::conditional_t<contains_v<Head, Components>, TypeList<Head>, TypeList<>>;
    using type = concat_t<Current, typename ComponentStores<TypeList<Tail...>, Components>::type>;
};

template <typename Executor, bool Deferred, typename Components> struct ExecutorDependency
{
    using type = TypeList<>;
};

template <typename Executor, typename Components>
struct ExecutorDependency<Executor, true, Components>
{
    static_assert(!std::is_same_v<Executor, execution::DefaultTarget>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_EXECUTOR_REQUIRED: deferred "
                  "persistence requires a configured executor target");
    static_assert(execution::target_traits<Executor>::valid,
                  "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_EXECUTOR: deferred persistence target "
                  "is not a valid executor");
    static_assert(std::is_same_v<Executor, execution::SystemWorkQueue> ||
                      contains_v<Executor, Components>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_EXECUTOR_UNREGISTERED: named deferred "
                  "persistence executor is absent from the component graph");
    using type = std::conditional_t<std::is_same_v<Executor, execution::SystemWorkQueue>,
                                    TypeList<>, TypeList<Executor>>;
};

template <typename List> struct AsDependencies;

template <typename... Components> struct AsDependencies<TypeList<Components...>>
{
    using type = Dependencies<Components...>;
};

template <typename FacilityT> struct DeferredBehavior
{
    [[nodiscard]] static Result<void> execute() noexcept
    {
        return FacilityT::run_deferred();
    }
};

template <typename FacilityT, bool Enabled> struct DeferredTasks
{
    using type = execution::Tasks<>;
    using Registration = void;
};

template <typename FacilityT> struct DeferredTasks<FacilityT, true>
{
    using Registration = execution::Delayable<"parameters-persistence", DeferredBehavior<FacilityT>,
                                              typename FacilityT::PersistenceExecutor,
                                              execution::stop::CancelPending>;
    using type = execution::Tasks<Registration>;
};

template <typename Value, typename Storage> class ValueCell;

template <typename Value> class ValueCell<Value, storage::Mutex>
{
  public:
    [[nodiscard]] Result<Value> read(bool no_wait) noexcept
    {
        auto guard = kernel::lock_guard(mutex_, no_wait ? kernel::Timeout::no_wait()
                                                        : kernel::Timeout::forever());
        if (!guard) {
            return fail<solar::Error>(guard.error());
        }
        return *value_;
    }

    [[nodiscard]] Result<void> write(const Value& value) noexcept
    {
        auto guard = kernel::lock_guard(mutex_);
        if (!guard) {
            return fail<solar::Error>(guard.error());
        }
        value_ = value;
        return {};
    }

  private:
    kernel::Mutex mutex_{};
    std::optional<Value> value_{};
};

template <typename Value>
class ValueCell<Value, storage::Immutable> : public ValueCell<Value, storage::Mutex>
{};

template <typename Value> class ValueCell<Value, storage::Atomic>
{
  public:
    [[nodiscard]] Result<Value> read(bool) noexcept
    {
        return value_.load(std::memory_order_acquire);
    }

    [[nodiscard]] Result<void> write(const Value& value) noexcept
    {
        value_.store(value, std::memory_order_release);
        return {};
    }

    [[nodiscard]] Value read_isr() noexcept
    {
        return value_.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<Value> value_;
};

template <typename ParameterT, typename Policies> class ParameterSlot
{
  public:
    using Value = typename ParameterT::Value;

    void initialize(LocalId id) noexcept
    {
        (void)value_.write(Value{ParameterT::default_value});
        auto guard = record_lock_.acquire();
        record_ = {
            .parameter = id,
            .value = Value{ParameterT::default_value},
            .last_error = {.status = solar::Status::Ok, .parameter = id},
            .current_version = descriptor_traits<Tag, ParameterT>::descriptor.version,
            .persistence = PersistenceTraits<typename Policies::Persistence>::persistent
                               ? PersistenceState::Clean
                               : PersistenceState::Volatile,
            .load_source = LoadSource::Default,
            .load_outcome = PersistenceTraits<typename Policies::Persistence>::persistent
                                ? LoadOutcome::NotAttempted
                                : LoadOutcome::VolatileDefault,
            .last_origin = UpdateOrigin::BootLoad,
            .writable = AccessTraits<typename Policies::Access>::writable,
        };
    }

    [[nodiscard]] Result<Value> read(bool no_wait) noexcept
    {
        auto result = value_.read(no_wait);
        auto guard = record_lock_.acquire();
        if (result) {
            ++record_.reads;
        }
        return result;
    }

    [[nodiscard]] Result<Value> peek(bool no_wait = false) noexcept
    {
        return value_.read(no_wait);
    }

    [[nodiscard]] Value read_isr() noexcept
        requires std::is_same_v<typename Policies::Storage, storage::Atomic>
    {
        return value_.read_isr();
    }

    [[nodiscard]] Result<void> write(const Value& value) noexcept
    {
        return value_.write(value);
    }

    template <typename Mutator> void mutate_record(Mutator&& mutator) noexcept
    {
        auto guard = record_lock_.acquire();
        mutator(record_);
    }

    [[nodiscard]] ParameterRecord<ParameterT> copy_record(const Value& value) noexcept
    {
        auto guard = record_lock_.acquire();
        auto copy = record_;
        copy.value = value;
        return copy;
    }

    void mark_dirty(std::uint64_t revision, bool reset,
                    std::optional<kernel::Deadline> due = std::nullopt) noexcept
    {
        auto guard = record_lock_.acquire();
        record_.dirty = true;
        record_.pending = true;
        record_.pending_revision = revision;
        record_.persistence = reset ? PersistenceState::ResetPending
                                    : (due ? PersistenceState::Scheduled : PersistenceState::Dirty);
        due_ = due;
        erase_pending_ = reset;
    }

    struct PersistenceWork
    {
        Value value;
        std::uint64_t revision{};
        std::optional<kernel::Deadline> due{};
        bool dirty{};
        bool erase{};
    };

    [[nodiscard]] PersistenceWork persistence_work(const Value& value) noexcept
    {
        auto guard = record_lock_.acquire();
        return {
            .value = value,
            .revision = record_.pending_revision,
            .due = due_,
            .dirty = record_.dirty,
            .erase = erase_pending_,
        };
    }

    void finish_persistence(std::uint64_t revision, Status status) noexcept
    {
        auto guard = record_lock_.acquire();
        if (status == Status::Ok) {
            ++record_.saves;
            record_.persisted_revision = revision;
            if (record_.pending_revision == revision) {
                record_.dirty = false;
                record_.pending = false;
                record_.persistence = PersistenceState::Clean;
                due_.reset();
                erase_pending_ = false;
            }
        } else {
            ++record_.save_failures;
            record_.persistence = PersistenceState::Failed;
            due_.reset();
        }
    }

    void mark_schedule_failure(Status status) noexcept
    {
        auto guard = record_lock_.acquire();
        record_.persistence = PersistenceState::Failed;
        record_.last_error = {
            .status = status,
            .reason = Reason::PersistenceFailed,
            .operation = Operation::Save,
            .parameter = record_.parameter,
        };
        due_.reset();
    }

    void mark_persisted(std::uint64_t revision) noexcept
    {
        auto guard = record_lock_.acquire();
        ++record_.saves;
        record_.persisted_revision = revision;
        record_.pending_revision = revision;
        record_.persistence = PersistenceState::Clean;
        record_.dirty = false;
        record_.pending = false;
        due_.reset();
        erase_pending_ = false;
    }

    void mark_persistence_failure(Status status, Operation operation) noexcept
    {
        auto guard = record_lock_.acquire();
        ++record_.save_failures;
        record_.persistence = PersistenceState::Failed;
        record_.last_error = {
            .status = status,
            .reason = Reason::PersistenceFailed,
            .operation = operation,
            .parameter = record_.parameter,
        };
    }

    [[nodiscard]] std::optional<kernel::Deadline> next_due() noexcept
    {
        auto guard = record_lock_.acquire();
        return record_.dirty ? due_ : std::nullopt;
    }

    void mark_loaded(const Value& value, LoadSource source, LoadOutcome outcome,
                     std::uint16_t stored_version = 0) noexcept
    {
        (void)value_.write(value);
        auto guard = record_lock_.acquire();
        record_.value = value;
        record_.stored_version = stored_version;
        record_.load_source = source;
        record_.load_outcome = outcome;
        record_.persistence = PersistenceState::Clean;
        record_.dirty = false;
        record_.pending = false;
        due_.reset();
        erase_pending_ = false;
    }

  private:
    ValueCell<Value, typename Policies::Storage> value_{};
    kernel::SpinLock record_lock_{};
    ParameterRecord<ParameterT> record_{.value = Value{ParameterT::default_value}};
    std::optional<kernel::Deadline> due_{};
    bool erase_pending_{};
};

template <typename Group> class GroupState
{
  public:
    struct Work
    {
        std::uint64_t revision{};
        std::optional<kernel::Deadline> due{};
        bool dirty{};
    };

    void initialize() noexcept
    {
        auto guard = lock_.acquire();
        revision_ = 0;
        dirty_ = false;
        due_.reset();
    }

    void mark_dirty(std::optional<kernel::Deadline> due = std::nullopt) noexcept
    {
        auto guard = lock_.acquire();
        ++revision_;
        dirty_ = true;
        due_ = due;
    }

    [[nodiscard]] Work work() noexcept
    {
        auto guard = lock_.acquire();
        return {.revision = revision_, .due = due_, .dirty = dirty_};
    }

    [[nodiscard]] bool finish(std::uint64_t revision, bool success) noexcept
    {
        auto guard = lock_.acquire();
        if (success && revision_ == revision) {
            dirty_ = false;
            due_.reset();
            return true;
        } else if (!success) {
            due_.reset();
        }
        return false;
    }

  private:
    kernel::SpinLock lock_{};
    std::uint64_t revision_{};
    std::optional<kernel::Deadline> due_{};
    bool dirty_{};
};

template <typename ChangeT> class ChangeState
{
  public:
    using Traits = change_traits<ChangeT>;
    using ParameterType = typename Traits::ParameterType;

    void initialize(ChangeLocalId change, LocalId parameter, component::LocalId observer) noexcept
    {
        auto guard = lock_.acquire();
        pending_.reset();
        record_ = {
            .change = change,
            .parameter = parameter,
            .observer = observer,
        };
    }

    void defer(const Change<ParameterType>& change) noexcept
    {
        auto guard = lock_.acquire();
        if (pending_) {
            pending_->new_value = change.new_value;
            pending_->revision = change.revision;
            pending_->origin = change.origin;
            pending_->adjusted = pending_->adjusted || change.adjusted;
            pending_->transaction = pending_->transaction || change.transaction;
            pending_->reset = pending_->reset || change.reset;
            ++record_.coalesced;
        } else {
            pending_ = change;
            ++record_.deferred;
        }
        record_.pending = true;
    }

    [[nodiscard]] std::optional<Change<ParameterType>> take_pending() noexcept
    {
        auto guard = lock_.acquire();
        auto pending = pending_;
        pending_.reset();
        record_.pending = false;
        return pending;
    }

    void begin_invoke() noexcept
    {
        auto guard = lock_.acquire();
        record_.invoking = true;
    }

    void finish_invoke(std::uint64_t revision, Status status) noexcept
    {
        auto guard = lock_.acquire();
        ++record_.invocations;
        if (status == Status::Ok) {
            ++record_.succeeded;
        } else {
            ++record_.failed;
        }
        record_.last_revision = revision;
        record_.last_status = status;
        record_.invoking = false;
    }

    [[nodiscard]] ChangeRecord copy() noexcept
    {
        auto guard = lock_.acquire();
        return record_;
    }

  private:
    kernel::SpinLock lock_{};
    std::optional<Change<ParameterType>> pending_{};
    ChangeRecord record_{};
};

} // namespace detail

template <typename ParameterDeclarations, typename ChangeDeclarations, typename Components,
          typename Configuration>
struct Architecture
{
    using Parameters = ParameterDeclarations;
    using Changes = ChangeDeclarations;
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;

    static_assert([]<typename... ParameterTypes>(TypeList<ParameterTypes...>) {
        return (detail::ValidateParameter<ParameterTypes, Configuration>::valid && ...);
    }(Parameters{}));
    static_assert([]<typename... ChangeTypes>(TypeList<ChangeTypes...>) {
        return detail::ValidateChangePairs<ChangeTypes...>::valid &&
               (detail::ValidateChange<ChangeTypes, Parameters, Components>::valid && ...);
    }(Changes{}));

    using Groups = typename detail::ConfiguredGroups<Configuration>::type;
    static_assert(
        detail::ValidateTransactionalParameters<Parameters, Configuration, Groups>::valid);
    using Stores = typename detail::PersistenceStores<Parameters, Configuration>::type;
    static constexpr bool has_deferred =
        detail::HasDeferred<Parameters, Configuration>::value ||
        []<typename... GroupsT>(TypeList<GroupsT...>) {
            return (detail::GroupTraits<GroupsT>::CommitTraits::deferred || ...);
        }(Groups{});
    using PersistenceExecutor = typename detail::ConfiguredExecutor<Configuration>::type;
    using StoreDependencies = typename detail::ComponentStores<Stores, Components>::type;
    using ExecutorDependencies =
        typename detail::ExecutorDependency<PersistenceExecutor, has_deferred, Components>::type;
    using BootstrapDependencies = unique_t<concat_t<StoreDependencies, ExecutorDependencies>>;
    using Dependencies = BootstrapDependencies;

    static_assert(
        []<typename... StoresT>(TypeList<StoresT...>) {
            return (persistence::Adapter<StoresT> && ...);
        }(Stores{}),
        "SOLAR_DIAGNOSTIC_PARAMETER_STORE_ADAPTER: persistence stores must implement "
        "initialize/load/save/erase with bounded spans");

    static constexpr bool demanded = list_size_v<Parameters> != 0 || list_size_v<Changes> != 0 ||
                                     list_size_v<ConfigurationPolicies> != 0;
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    using ParameterTypes = typename Architecture::Parameters;
    using ChangeTypes = typename Architecture::Changes;
    using Configuration = typename Architecture::ConfigurationPolicies;
    using Groups = typename Architecture::Groups;
    using PersistenceExecutor = typename Architecture::PersistenceExecutor;
    using PersistenceStopPolicy = typename detail::ConfiguredStop<Configuration>::type;
    using Dependencies = typename detail::AsDependencies<typename Architecture::Dependencies>::type;

    static constexpr component::Descriptor descriptor{
        .name = "solar.parameters",
        .description = "Typed runtime parameters",
    };

    template <typename ParameterT>
    using Policies = detail::ParameterPolicies<ParameterT, Configuration>;

    using DeferredTaskBuilder = detail::DeferredTasks<Facility, Architecture::has_deferred>;
    using DeferredRegistration = typename DeferredTaskBuilder::Registration;
    using Tasks = typename DeferredTaskBuilder::type;

    template <typename ParameterT>
    inline static detail::ParameterSlot<ParameterT, Policies<ParameterT>> slot{};

    template <typename ChangeT> inline static detail::ChangeState<ChangeT> change_state{};
    template <typename Group> inline static detail::GroupState<Group> group_state{};

    inline static kernel::Mutex write_gate{};
    inline static kernel::Mutex persistence_gate{};
    inline static std::atomic_bool ready{};
    inline static std::atomic_bool mutation_open{};
    inline static std::atomic_bool activating_changes{};
    inline static std::atomic_bool persistence_active{};
    using SchedulePersistence = Result<void> (*)(std::chrono::nanoseconds) noexcept;
    inline static SchedulePersistence schedule_persistence{};

    [[nodiscard]] static Result<void> init() noexcept;
    [[nodiscard]] static Result<void> start() noexcept;
    [[nodiscard]] static Result<void> stop() noexcept;
    [[nodiscard]] static Result<void> deinit() noexcept;
    [[nodiscard]] static Result<void> run_deferred() noexcept;

    template <typename System> [[nodiscard]] static Result<void> activate_changes() noexcept;
    template <typename System> static void activate_runtime() noexcept;
};

#endif

} // namespace solar::parameters

template <typename Architecture>
struct solar::builtin_traits<solar::parameters::Facility<Architecture>>
{
    static constexpr bool enabled = solar::parameters::enabled;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<>;

    template <typename> static constexpr bool demanded = Architecture::demanded;
};

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_PARAMETERS)
template <typename Component, typename Architecture, typename AllComponents>
struct solar::generated_component_dependency<Component, solar::parameters::Facility<Architecture>,
                                             AllComponents>
    : std::bool_constant<
          !std::is_same_v<Component, solar::parameters::Facility<Architecture>> &&
          solar::contains_v<Component, typename Architecture::ComponentTypes> &&
          !solar::contains_v<Component, typename Architecture::BootstrapDependencies>>
{};
#endif
