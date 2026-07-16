#pragma once

#include <type_traits>

#include "solar/core/fixed_string.hpp"
#include "solar/execution/contribution.hpp"
#include "solar/execution/policy.hpp"

namespace solar::execution
{

template <typename Target> struct target_traits
{
    static constexpr bool valid = false;
    static constexpr TargetKind kind = TargetKind::OwnedWorkQueue;
};

template <> struct target_traits<DefaultTarget>
{
    static constexpr bool valid = true;
    static constexpr TargetKind kind = TargetKind::SystemWorkQueue;
};

template <> struct target_traits<SystemWorkQueue>
{
    static constexpr bool valid = true;
    static constexpr TargetKind kind = TargetKind::SystemWorkQueue;
};

template <FixedString Name, typename Behavior, typename... Options> struct OnDemand
{
    using BehaviorType = Behavior;
    using OptionsList = TypeList<Options...>;

    static constexpr Descriptor descriptor{
        .name = Name.view(),
        .kind = RegistrationKind::OnDemand,
    };
};

template <FixedString Name, typename Behavior, typename... Options> struct Delayable
{
    using BehaviorType = Behavior;
    using OptionsList = TypeList<Options...>;

    static constexpr Descriptor descriptor{
        .name = Name.view(),
        .kind = RegistrationKind::Delayable,
    };
};

template <FixedString Name, typename Behavior, DurationValue Period, typename... Options>
struct Periodic
{
    static_assert(
        Period.positive(),
        "SOLAR_DIAGNOSTIC_EXECUTION_NONPOSITIVE_PERIOD: periodic duration must be positive");

    using BehaviorType = Behavior;
    using OptionsList = TypeList<Options...>;
    static constexpr DurationValue period = Period;

    static constexpr Descriptor descriptor{
        .name = Name.view(),
        .kind = RegistrationKind::Periodic,
    };
};

template <FixedString Name, typename Behavior, typename PollSet, typename... Options>
struct PollTriggered
{
    using BehaviorType = Behavior;
    using PollSetType = PollSet;
    using OptionsList = TypeList<Options...>;

    static constexpr Descriptor descriptor{
        .name = Name.view(),
        .kind = RegistrationKind::PollTriggered,
    };
};

template <typename Registration> struct registration_traits
{
    static constexpr bool valid = false;
};

namespace detail
{

template <typename T> struct IsCounted : std::false_type
{};

template <std::size_t Capacity> struct IsCounted<Counted<Capacity>> : std::true_type
{};

template <typename T> struct IsDependencies : std::false_type
{};

template <typename... Components> struct IsDependencies<DependsOn<Components...>> : std::true_type
{};

template <typename T> struct IsDeadline : std::false_type
{};

template <DurationValue Value> struct IsDeadline<Deadline<Value>> : std::true_type
{};

template <typename T>
inline constexpr bool is_target_option_v =
    target_traits<T>::valid && !std::is_same_v<T, DefaultTarget>;

template <typename T>
inline constexpr bool is_admission_option_v =
    std::is_same_v<T, NativeCoalescing> || IsCounted<T>::value;

template <typename T>
inline constexpr bool is_stop_option_v =
    std::is_same_v<T, stop::Drain> || std::is_same_v<T, stop::CancelPending>;

template <typename T>
inline constexpr bool is_failure_option_v =
    std::is_same_v<T, failure::RecordAndContinue> || std::is_same_v<T, failure::Suspend>;

template <typename T>
inline constexpr bool is_cadence_option_v =
    std::is_same_v<T, periodic::FixedRate> || std::is_same_v<T, periodic::FixedDelay>;

template <typename T>
inline constexpr bool is_start_option_v =
    std::is_same_v<T, StartAfterPeriod> || std::is_same_v<T, StartImmediately>;

template <typename T>
inline constexpr bool is_poll_option_v =
    std::is_same_v<T, poll::OneShot> || std::is_same_v<T, poll::AutoRearm>;

template <typename T>
inline constexpr bool is_common_registration_option_v =
    is_target_option_v<T> || IsDependencies<T>::value || is_stop_option_v<T> ||
    is_failure_option_v<T>;

template <typename Default, template <typename> typename Match, typename... Options>
struct SelectOption
{
    static constexpr std::size_t count = 0;
    using type = Default;
};

template <typename Default, template <typename> typename Match, typename Head, typename... Tail>
struct SelectOption<Default, Match, Head, Tail...>
{
  private:
    using RemainingSelection = SelectOption<Default, Match, Tail...>;
    using Remaining = typename RemainingSelection::type;

  public:
    static constexpr std::size_t count =
        std::size_t{Match<Head>::value} + RemainingSelection::count;
    static_assert(count <= 1, "SOLAR_DIAGNOSTIC_DUPLICATE_EXECUTION_POLICY_AXIS: registration "
                              "selects more than one policy for an exclusive axis");
    using type = std::conditional_t<Match<Head>::value, Head, Remaining>;
};

template <typename T> struct IsTarget : std::bool_constant<is_target_option_v<T>>
{};

template <typename T> struct IsAdmission : std::bool_constant<is_admission_option_v<T>>
{};

template <typename T> struct IsStop : std::bool_constant<is_stop_option_v<T>>
{};

template <typename T> struct IsFailure : std::bool_constant<is_failure_option_v<T>>
{};

template <typename T> struct IsCadence : std::bool_constant<is_cadence_option_v<T>>
{};

template <typename T> struct IsStart : std::bool_constant<is_start_option_v<T>>
{};

template <typename T> struct IsPoll : std::bool_constant<is_poll_option_v<T>>
{};

template <typename... Options> struct ParsedCommonOptions
{
    using Target = typename SelectOption<DefaultTarget, IsTarget, Options...>::type;
    using DependenciesPolicy = typename SelectOption<DependsOn<>, IsDependencies, Options...>::type;
    using Dependencies = typename DependenciesPolicy::ComponentsList;
    using StopPolicy = typename SelectOption<stop::Drain, IsStop, Options...>::type;
    using FailurePolicy =
        typename SelectOption<failure::RecordAndContinue, IsFailure, Options...>::type;
};

template <typename Registration, RegistrationKind Kind, typename Behavior, typename... Options>
struct BasicRegistrationTraits : ParsedCommonOptions<Options...>
{
    static constexpr bool valid = true;
    static constexpr RegistrationKind kind = Kind;
    using RegistrationType = Registration;
    using BehaviorType = Behavior;
    using OptionsList = TypeList<Options...>;
};

} // namespace detail

template <FixedString Name, typename Behavior, typename... Options>
struct registration_traits<OnDemand<Name, Behavior, Options...>>
    : detail::BasicRegistrationTraits<OnDemand<Name, Behavior, Options...>,
                                      RegistrationKind::OnDemand, Behavior, Options...>
{
    static_assert(
        ((detail::is_common_registration_option_v<Options> ||
          detail::is_admission_option_v<Options>) &&
         ...),
        "SOLAR_DIAGNOSTIC_UNKNOWN_ON_DEMAND_POLICY: OnDemand contains an unsupported policy");
    using Admission =
        typename detail::SelectOption<NativeCoalescing, detail::IsAdmission, Options...>::type;
};

template <FixedString Name, typename Behavior, typename... Options>
struct registration_traits<Delayable<Name, Behavior, Options...>>
    : detail::BasicRegistrationTraits<Delayable<Name, Behavior, Options...>,
                                      RegistrationKind::Delayable, Behavior, Options...>
{
    static_assert(
        (detail::is_common_registration_option_v<Options> && ...),
        "SOLAR_DIAGNOSTIC_UNKNOWN_DELAYABLE_POLICY: Delayable contains an unsupported policy");
};

template <FixedString Name, typename Behavior, DurationValue Period, typename... Options>
struct registration_traits<Periodic<Name, Behavior, Period, Options...>>
    : detail::BasicRegistrationTraits<Periodic<Name, Behavior, Period, Options...>,
                                      RegistrationKind::Periodic, Behavior, Options...>
{
    static_assert(
        ((detail::is_common_registration_option_v<Options> ||
          detail::is_cadence_option_v<Options> || detail::is_start_option_v<Options> ||
          detail::IsDeadline<Options>::value || std::is_same_v<Options, overrun::Skip>) &&
         ...),
        "SOLAR_DIAGNOSTIC_UNKNOWN_PERIODIC_POLICY: Periodic contains an unsupported policy");
    static constexpr DurationValue period = Period;
    using Cadence =
        typename detail::SelectOption<periodic::FixedRate, detail::IsCadence, Options...>::type;
    using InitialRelease =
        typename detail::SelectOption<StartAfterPeriod, detail::IsStart, Options...>::type;
    using DeadlinePolicy =
        typename detail::SelectOption<void, detail::IsDeadline, Options...>::type;
};

template <FixedString Name, typename Behavior, typename PollSet, typename... Options>
struct registration_traits<PollTriggered<Name, Behavior, PollSet, Options...>>
    : detail::BasicRegistrationTraits<PollTriggered<Name, Behavior, PollSet, Options...>,
                                      RegistrationKind::PollTriggered, Behavior, Options...>
{
    static_assert(
        ((detail::is_common_registration_option_v<Options> || detail::is_poll_option_v<Options>) &&
         ...),
        "SOLAR_DIAGNOSTIC_UNKNOWN_POLL_POLICY: PollTriggered contains an unsupported policy");
    using PollSetType = PollSet;
    using Rearm = typename detail::SelectOption<poll::OneShot, detail::IsPoll, Options...>::type;
};

template <typename T>
concept Registration = registration_traits<T>::valid;

template <typename T>
concept OnDemandRegistration =
    Registration<T> && registration_traits<T>::kind == RegistrationKind::OnDemand;

template <typename T>
concept SchedulableRegistration =
    Registration<T> && registration_traits<T>::kind == RegistrationKind::Delayable;

} // namespace solar::execution
