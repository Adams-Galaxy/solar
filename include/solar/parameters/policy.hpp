#pragma once

#include <cmath>
#include <concepts>
#include <type_traits>

#include "solar/core/time.hpp"
#include "solar/execution/types.hpp"
#include "solar/parameters/types.hpp"
#include "solar/system/sections.hpp"

namespace solar::parameters
{

struct Reject
{};

struct Clamp
{};

struct AcceptAny
{};

template <auto Minimum, auto Maximum, typename Behavior = Reject> struct Range
{
    static constexpr auto minimum = Minimum;
    static constexpr auto maximum = Maximum;
    using BehaviorType = Behavior;
};

template <auto... Values> struct OneOf
{};

template <typename Validator> struct Custom
{
    using ValidatorType = Validator;
};

struct ReadWrite
{};

struct ReadOnly
{};

template <typename Authority> struct Privileged
{
    using AuthorityType = Authority;
};

struct LocalOnly
{};

struct ExternallyReadable
{};

template <typename Permission = void> struct ExternallyWritable
{
    using PermissionType = Permission;
};

namespace storage
{
struct Mutex
{};

struct Atomic
{};

struct Immutable
{};
} // namespace storage

using MutexStorage = storage::Mutex;
using Atomic = storage::Atomic;
using Immutable = storage::Immutable;

namespace persistence
{
struct Volatile
{};

template <typename Store> struct Immediate
{
    using StoreType = Store;
};

template <typename Store, typename QuietPeriod> struct Deferred
{
    using StoreType = Store;
    using Delay = QuietPeriod;
};

template <typename Store> struct Manual
{
    using StoreType = Store;
};

template <typename Group> struct Transactional
{
    using GroupType = Group;
};
} // namespace persistence

using Volatile = persistence::Volatile;
template <typename Store> using Immediate = persistence::Immediate<Store>;
template <typename Store, typename QuietPeriod>
using Deferred = persistence::Deferred<Store, QuietPeriod>;
template <typename Store> using Manual = persistence::Manual<Store>;
template <typename Group> using Transactional = persistence::Transactional<Group>;

namespace delay
{
template <std::int64_t Value> struct Milliseconds
{
    static_assert(Value > 0,
                  "SOLAR_DIAGNOSTIC_PARAMETER_NONPOSITIVE_DELAY: delay must be positive");
    static constexpr DurationValue value{std::chrono::milliseconds{Value}};
};

template <std::int64_t Value> struct Seconds
{
    static_assert(Value > 0,
                  "SOLAR_DIAGNOSTIC_PARAMETER_NONPOSITIVE_DELAY: delay must be positive");
    static constexpr DurationValue value{std::chrono::seconds{Value}};
};
} // namespace delay

namespace load
{
struct UseDefaultAndReport
{};

struct FailBoot
{};
} // namespace load

namespace stop
{
struct FlushDeferred
{};

struct CancelPending
{};
} // namespace stop

template <typename... Parameters> struct Members
{
    using Types = TypeList<Parameters...>;
};

template <typename... Groups> struct PersistenceGroups
{
    using Types = TypeList<Groups...>;
};

template <typename Policy> struct DefaultValidation
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultAccess
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultExternal
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultStorage
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultPersistence
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultLoadFailure
{
    using PolicyType = Policy;
};

template <typename Executor> struct PersistenceExecutor
{
    using ExecutorType = Executor;
};

template <typename Policy> struct PersistenceStop
{
    using PolicyType = Policy;
};

namespace detail
{

struct DefaultValidationAxis
{};
struct DefaultAccessAxis
{};
struct DefaultExternalAxis
{};
struct DefaultStorageAxis
{};
struct DefaultPersistenceAxis
{};
struct DefaultLoadFailureAxis
{};
struct PersistenceExecutorAxis
{};
struct PersistenceStopAxis
{};
struct PersistenceGroupsAxis
{};

template <typename Parameter, typename = void> struct DeclaredValidation
{
    using type = NoPolicy;
};

template <typename Parameter>
struct DeclaredValidation<Parameter, std::void_t<typename Parameter::Validation>>
{
    using type = typename Parameter::Validation;
};

template <typename Parameter, typename = void> struct DeclaredAccess
{
    using type = NoPolicy;
};

template <typename Parameter>
struct DeclaredAccess<Parameter, std::void_t<typename Parameter::Access>>
{
    using type = typename Parameter::Access;
};

template <typename Parameter, typename = void> struct DeclaredExternal
{
    using type = NoPolicy;
};

template <typename Parameter>
struct DeclaredExternal<Parameter, std::void_t<typename Parameter::External>>
{
    using type = typename Parameter::External;
};

template <typename Parameter, typename = void> struct DeclaredStorage
{
    using type = NoPolicy;
};

template <typename Parameter>
struct DeclaredStorage<Parameter, std::void_t<typename Parameter::Storage>>
{
    using type = typename Parameter::Storage;
};

template <typename Parameter, typename = void> struct DeclaredPersistence
{
    using type = NoPolicy;
};

template <typename Parameter>
struct DeclaredPersistence<Parameter, std::void_t<typename Parameter::Persistence>>
{
    using type = typename Parameter::Persistence;
};

template <typename Parameter, typename = void> struct DeclaredLoadFailure
{
    using type = NoPolicy;
};

template <typename Parameter>
struct DeclaredLoadFailure<Parameter, std::void_t<typename Parameter::LoadFailure>>
{
    using type = typename Parameter::LoadFailure;
};

template <typename Policy> struct ValidationTraits
{
    static constexpr bool valid = false;
    static constexpr ValidationKind kind = ValidationKind::Custom;
};

template <> struct ValidationTraits<AcceptAny>
{
    static constexpr bool valid = true;
    static constexpr ValidationKind kind = ValidationKind::AcceptAny;

    template <typename Value>
    [[nodiscard]] static constexpr Result<Normalized<Value>, ValidationError>
    normalize(Value candidate) noexcept
    {
        return Normalized<Value>{.value = candidate};
    }
};

template <auto Minimum, auto Maximum, typename Behavior>
struct ValidationTraits<Range<Minimum, Maximum, Behavior>>
{
    static constexpr bool valid =
        std::is_same_v<Behavior, Reject> || std::is_same_v<Behavior, Clamp>;
    static constexpr ValidationKind kind =
        std::is_same_v<Behavior, Clamp> ? ValidationKind::RangeClamp : ValidationKind::RangeReject;

    template <typename Value>
    [[nodiscard]] static constexpr Result<Normalized<Value>, ValidationError>
    normalize(Value candidate) noexcept
    {
        static_assert(std::convertible_to<decltype(Minimum), Value> &&
                          std::convertible_to<decltype(Maximum), Value>,
                      "SOLAR_DIAGNOSTIC_PARAMETER_RANGE_TYPE: range bounds must convert to the "
                      "parameter value type");
        constexpr Value minimum = static_cast<Value>(Minimum);
        constexpr Value maximum = static_cast<Value>(Maximum);
        static_assert(minimum <= maximum,
                      "SOLAR_DIAGNOSTIC_PARAMETER_RANGE_ORDER: range minimum exceeds maximum");
        if constexpr (std::floating_point<Value>) {
            if (!std::isfinite(candidate)) {
                return fail<ValidationError>({});
            }
        }
        if (candidate < minimum) {
            if constexpr (std::is_same_v<Behavior, Clamp>) {
                return Normalized<Value>{.value = minimum, .adjusted = true};
            }
            return fail<ValidationError>({});
        }
        if (candidate > maximum) {
            if constexpr (std::is_same_v<Behavior, Clamp>) {
                return Normalized<Value>{.value = maximum, .adjusted = true};
            }
            return fail<ValidationError>({});
        }
        return Normalized<Value>{.value = candidate};
    }
};

template <auto... Values> struct ValidationTraits<OneOf<Values...>>
{
    static constexpr bool valid = sizeof...(Values) != 0;
    static constexpr ValidationKind kind = ValidationKind::OneOf;

    template <typename Value>
    [[nodiscard]] static constexpr Result<Normalized<Value>, ValidationError>
    normalize(Value candidate) noexcept
    {
        if (((candidate == static_cast<Value>(Values)) || ...)) {
            return Normalized<Value>{.value = candidate};
        }
        return fail<ValidationError>({});
    }
};

template <typename Validator> struct ValidationTraits<Custom<Validator>>
{
    static constexpr bool valid = true;
    static constexpr ValidationKind kind = ValidationKind::Custom;

    template <typename Value>
    [[nodiscard]] static constexpr Result<Normalized<Value>, ValidationError>
    normalize(Value candidate) noexcept
    {
        static_assert(
            requires {
                { Validator::normalize(candidate) } -> std::same_as<Result<Value, ValidationError>>;
            }, "SOLAR_DIAGNOSTIC_PARAMETER_CUSTOM_VALIDATOR: custom validator must return "
               "Result<Value, ValidationError>");
        auto result = Validator::normalize(candidate);
        if (!result) {
            return fail<ValidationError>(result.error());
        }
        return Normalized<Value>{.value = *result, .adjusted = !(*result == candidate)};
    }
};

template <typename Policy, typename Value>
[[nodiscard]] constexpr Result<Normalized<Value>, ValidationError>
normalize(Value candidate) noexcept
{
    static_assert(ValidationTraits<Policy>::valid,
                  "SOLAR_DIAGNOSTIC_PARAMETER_VALIDATION_POLICY: invalid validation policy");
    return ValidationTraits<Policy>::template normalize<Value>(candidate);
}

template <typename Policy> struct AccessTraits
{
    static constexpr bool valid = false;
};

template <> struct AccessTraits<ReadWrite>
{
    static constexpr bool valid = true;
    static constexpr bool writable = true;
    static constexpr bool privileged = false;
    static constexpr AccessKind kind = AccessKind::ReadWrite;
    using Authority = void;
};

template <> struct AccessTraits<ReadOnly>
{
    static constexpr bool valid = true;
    static constexpr bool writable = false;
    static constexpr bool privileged = false;
    static constexpr AccessKind kind = AccessKind::ReadOnly;
    using Authority = void;
};

template <typename AuthorityT> struct AccessTraits<Privileged<AuthorityT>>
{
    static constexpr bool valid = true;
    static constexpr bool writable = true;
    static constexpr bool privileged = true;
    static constexpr AccessKind kind = AccessKind::Privileged;
    using Authority = AuthorityT;
};

template <typename Policy> struct ExternalTraits
{
    static constexpr bool valid = false;
};

template <> struct ExternalTraits<LocalOnly>
{
    static constexpr bool valid = true;
    static constexpr ExternalKind kind = ExternalKind::LocalOnly;
};

template <> struct ExternalTraits<ExternallyReadable>
{
    static constexpr bool valid = true;
    static constexpr ExternalKind kind = ExternalKind::Readable;
};

template <typename Permission> struct ExternalTraits<ExternallyWritable<Permission>>
{
    static constexpr bool valid = true;
    static constexpr ExternalKind kind = ExternalKind::Writable;
};

template <typename Policy> struct StorageTraits
{
    static constexpr bool valid = false;
};

template <> struct StorageTraits<storage::Mutex>
{
    static constexpr bool valid = true;
    static constexpr StorageKind kind = StorageKind::Mutex;
};

template <> struct StorageTraits<storage::Atomic>
{
    static constexpr bool valid = true;
    static constexpr StorageKind kind = StorageKind::Atomic;
};

template <> struct StorageTraits<storage::Immutable>
{
    static constexpr bool valid = true;
    static constexpr StorageKind kind = StorageKind::Immutable;
};

template <typename Policy> struct PersistenceTraits
{
    static constexpr bool valid = false;
    static constexpr bool persistent = false;
    static constexpr bool deferred = false;
    static constexpr PersistenceKind kind = PersistenceKind::Volatile;
    using Store = void;
    using Group = void;
};

template <> struct PersistenceTraits<persistence::Volatile>
{
    static constexpr bool valid = true;
    static constexpr bool persistent = false;
    static constexpr bool deferred = false;
    static constexpr PersistenceKind kind = PersistenceKind::Volatile;
    using Store = void;
    using Group = void;
};

template <typename StoreT> struct PersistenceTraits<persistence::Immediate<StoreT>>
{
    static constexpr bool valid = true;
    static constexpr bool persistent = true;
    static constexpr bool deferred = false;
    static constexpr PersistenceKind kind = PersistenceKind::Immediate;
    using Store = StoreT;
    using Group = void;
};

template <typename StoreT, typename QuietPeriod>
struct PersistenceTraits<persistence::Deferred<StoreT, QuietPeriod>>
{
    static_assert(
        requires { QuietPeriod::value; },
        "SOLAR_DIAGNOSTIC_PARAMETER_DELAY_POLICY: deferred persistence requires a "
        "typed positive delay");
    static constexpr bool valid = true;
    static constexpr bool persistent = true;
    static constexpr bool deferred = true;
    static constexpr PersistenceKind kind = PersistenceKind::Deferred;
    static constexpr DurationValue delay = QuietPeriod::value;
    using Store = StoreT;
    using Group = void;
};

template <typename StoreT> struct PersistenceTraits<persistence::Manual<StoreT>>
{
    static constexpr bool valid = true;
    static constexpr bool persistent = true;
    static constexpr bool deferred = false;
    static constexpr PersistenceKind kind = PersistenceKind::Manual;
    using Store = StoreT;
    using Group = void;
};

template <typename GroupT> struct PersistenceTraits<persistence::Transactional<GroupT>>
{
    static constexpr bool valid = true;
    static constexpr bool persistent = true;
    static constexpr bool deferred = false;
    static constexpr PersistenceKind kind = PersistenceKind::Transactional;
    using Store = void;
    using Group = GroupT;
};

} // namespace detail

template <typename... Policies> using Configuration = SubsystemConfiguration<Tag, Policies...>;

} // namespace solar::parameters

#define SOLAR_PARAMETER_POLICY_TRAITS(POLICY, AXIS)                                                \
    template <typename Value>                                                                      \
    struct solar::subsystem_policy_traits<solar::parameters::Tag, POLICY<Value>>                   \
    {                                                                                              \
        static constexpr bool recognized = true;                                                   \
        using Axis = solar::parameters::detail::AXIS;                                              \
    }

SOLAR_PARAMETER_POLICY_TRAITS(solar::parameters::DefaultValidation, DefaultValidationAxis);
SOLAR_PARAMETER_POLICY_TRAITS(solar::parameters::DefaultAccess, DefaultAccessAxis);
SOLAR_PARAMETER_POLICY_TRAITS(solar::parameters::DefaultExternal, DefaultExternalAxis);
SOLAR_PARAMETER_POLICY_TRAITS(solar::parameters::DefaultStorage, DefaultStorageAxis);
SOLAR_PARAMETER_POLICY_TRAITS(solar::parameters::DefaultPersistence, DefaultPersistenceAxis);
SOLAR_PARAMETER_POLICY_TRAITS(solar::parameters::DefaultLoadFailure, DefaultLoadFailureAxis);
SOLAR_PARAMETER_POLICY_TRAITS(solar::parameters::PersistenceExecutor, PersistenceExecutorAxis);
SOLAR_PARAMETER_POLICY_TRAITS(solar::parameters::PersistenceStop, PersistenceStopAxis);

#undef SOLAR_PARAMETER_POLICY_TRAITS

template <typename... Groups>
struct solar::subsystem_policy_traits<solar::parameters::Tag,
                                      solar::parameters::PersistenceGroups<Groups...>>
{
    static constexpr bool recognized = true;
    using Axis = solar::parameters::detail::PersistenceGroupsAxis;
};
