#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "solar/component.hpp"
#include "solar/health/contribution.hpp"
#include "solar/health/declaration.hpp"
#include "solar/health/facility.hpp"
#include "solar/supervisor/policy.hpp"
#include "solar/supervisor/types.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_SUPERVISOR)
#include <chrono>

#include "solar/execution/service.hpp"
#include "solar/kernel/fatal.hpp"
#include "solar/kernel/mutex.hpp"
#include "solar/kernel/semaphore.hpp"
#include "solar/kernel/stop.hpp"
#include "solar/kernel/time.hpp"
#include "solar/lifecycle/protocol.hpp"
#endif

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_SUPERVISOR)
namespace solar::health::detail
{
template <typename System> [[nodiscard]] Result<void, Error> refresh() noexcept;
template <typename System> void set_supervisor_waker(void (*waker)() noexcept) noexcept;
} // namespace solar::health::detail
#endif

namespace solar::supervisor
{

#if defined(CONFIG_SOLAR_SUPERVISOR)
inline constexpr bool enabled = true;
#else
inline constexpr bool enabled = false;
#endif

template <typename ArchitectureT> struct Service;

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_SUPERVISOR)

namespace detail
{

template <typename ServiceT, typename System>
[[nodiscard]] Result<void, Error> cycle(Tick now) noexcept;

template <typename Needle, typename List> struct TypeIndex;

template <typename Needle, typename... Tail>
struct TypeIndex<Needle, TypeList<Needle, Tail...>> : std::integral_constant<std::size_t, 0>
{};

template <typename Needle, typename Head, typename... Tail>
struct TypeIndex<Needle, TypeList<Head, Tail...>>
    : std::integral_constant<std::size_t, 1 + TypeIndex<Needle, TypeList<Tail...>>::value>
{};

template <typename Rule> struct RuleTraits;

#define SOLAR_SUPERVISOR_RULE_TRAITS(TYPE, TRIGGER)                                                \
    template <typename Component, typename... Actions>                                             \
    struct RuleTraits<TYPE<Component, Actions...>>                                                 \
    {                                                                                              \
        using Subject = Component;                                                                 \
        using Responses = TypeList<Actions...>;                                                    \
        static constexpr supervisor::Trigger trigger = supervisor::Trigger::TRIGGER;               \
    }

SOLAR_SUPERVISOR_RULE_TRAITS(OnFault, Fault);
SOLAR_SUPERVISOR_RULE_TRAITS(OnDegraded, Degraded);
SOLAR_SUPERVISOR_RULE_TRAITS(OnStall, Stall);
SOLAR_SUPERVISOR_RULE_TRAITS(OnRecoveryFailure, RecoveryFailure);

#undef SOLAR_SUPERVISOR_RULE_TRAITS

template <typename ActionT> struct ActionTraits;

#define SOLAR_SUPERVISOR_SIMPLE_ACTION(TYPE, VALUE)                                                \
    template <> struct ActionTraits<TYPE>                                                          \
    {                                                                                              \
        static constexpr supervisor::Action kind = supervisor::Action::VALUE;                      \
    }

SOLAR_SUPERVISOR_SIMPLE_ACTION(Observe, Observe);
SOLAR_SUPERVISOR_SIMPLE_ACTION(Warn, Warn);
SOLAR_SUPERVISOR_SIMPLE_ACTION(Latch, Latch);
SOLAR_SUPERVISOR_SIMPLE_ACTION(RequestSystemStop, RequestSystemStop);
SOLAR_SUPERVISOR_SIMPLE_ACTION(RequestReboot, RequestReboot);
SOLAR_SUPERVISOR_SIMPLE_ACTION(StopFeedingWatchdog, StopFeedingWatchdog);
SOLAR_SUPERVISOR_SIMPLE_ACTION(Panic, Panic);

#undef SOLAR_SUPERVISOR_SIMPLE_ACTION

template <typename Component> struct ActionTraits<TryRecover<Component>>
{
    static constexpr supervisor::Action kind = supervisor::Action::TryRecover;
};

template <typename SafeState> struct ActionTraits<EnterSafeState<SafeState>>
{
    static constexpr supervisor::Action kind = supervisor::Action::EnterSafeState;
};

template <typename Component> struct ActionTraits<RequestStop<Component>>
{
    static constexpr supervisor::Action kind = supervisor::Action::RequestStop;
};

template <typename T> struct IsTryRecover : std::false_type
{};

template <typename Component> struct IsTryRecover<TryRecover<Component>> : std::true_type
{};

template <typename T> struct IsRecoveryFailureRule : std::false_type
{};

template <typename Component, typename... Actions>
struct IsRecoveryFailureRule<OnRecoveryFailure<Component, Actions...>> : std::true_type
{};

template <typename ErrorT> [[nodiscard]] constexpr Status error_status(const ErrorT& error) noexcept
{
    if constexpr (std::is_same_v<std::remove_cvref_t<ErrorT>, Status>) {
        return error;
    } else if constexpr (requires { error.status; }) {
        return static_cast<Status>(error.status);
    } else {
        return Status::Error;
    }
}

struct RuleState
{
    Tick next_attempt{};
    std::uint32_t attempts{};
    bool active{};
    bool latched{};
};

template <typename Architecture> struct Storage
{
    using Rules = typename Architecture::ResponsePolicy::RuleTypes;
    static constexpr std::size_t rule_count = list_size_v<Rules>;

    kernel::Mutex mutex{};
    kernel::Mutex cycle_mutex{};
    kernel::BinarySemaphore wake{};
    StateRecord state{};
    WatchdogRecord watchdog{};
    std::array<RuleState, rule_count> rules{};
    std::array<ResponseRecord, CONFIG_SOLAR_SUPERVISOR_RESPONSE_HISTORY_DEPTH> history{};
    std::uint64_t next_sequence{1};
    std::uint64_t overwritten{};
    bool stop_requested{};
    bool reboot_requested{};
    bool panic_requested{};
};

template <typename ServiceT> [[nodiscard]] auto& storage() noexcept
{
    return ServiceT::state_storage;
}

template <typename ResultT> [[nodiscard]] Result<void> normalize_result(ResultT&& result) noexcept
{
    using R = std::remove_cvref_t<ResultT>;
    static_assert(VoidResult<R>, "SOLAR_DIAGNOSTIC_SUPERVISOR_RESULT: operation must return "
                                 "Result<void, ErrorType>");
    return result ? Result<void>{}
                  : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
}

template <typename Provider> [[nodiscard]] Result<void> start_provider() noexcept
{
    if constexpr (requires { Provider::start(); }) {
        return normalize_result(Provider::start());
    }
    return {};
}

template <typename Provider> [[nodiscard]] Result<void> stop_provider() noexcept
{
    if constexpr (requires { Provider::stop(); }) {
        return normalize_result(Provider::stop());
    }
    return {};
}

template <typename Provider> [[nodiscard]] Result<void> feed_provider() noexcept
{
    static_assert(
        requires { Provider::feed(); },
        "SOLAR_DIAGNOSTIC_SUPERVISOR_WATCHDOG_PROVIDER: watchdog provider must expose "
        "static Result<void, ErrorType> feed()");
    return normalize_result(Provider::feed());
}

template <typename Rule>
[[nodiscard]] bool triggered(const health::SubjectRecord& record, bool recovery_failed) noexcept
{
    if constexpr (RuleTraits<Rule>::trigger == Trigger::Fault) {
        return record.condition == health::Condition::Faulted;
    } else if constexpr (RuleTraits<Rule>::trigger == Trigger::Degraded) {
        return record.condition == health::Condition::Degraded;
    } else if constexpr (RuleTraits<Rule>::trigger == Trigger::Stall) {
        return record.liveness == health::Liveness::Stalled;
    } else {
        return recovery_failed;
    }
}

template <typename ServiceT>
void append_response(ResponseRecord record, SubjectRecord& subject_record) noexcept
{
    auto& state = storage<ServiceT>();
    auto guard = kernel::LockGuard<kernel::Mutex>::acquire(state.mutex);
    record.sequence = state.next_sequence++;
    if (record.sequence > state.history.size()) {
        ++state.overwritten;
    }
    state.history[(record.sequence - 1) % state.history.size()] = record;
    subject_record.subject = record.subject;
    subject_record.last_trigger = record.trigger;
    subject_record.last_action = record.action;
    subject_record.last_outcome = record.outcome;
    subject_record.last_response_at = record.attempted_at;
    subject_record.attempts = record.attempt;
    subject_record.active = true;
    if (record.action == Action::Latch && record.outcome == Outcome::Succeeded) {
        subject_record.latched = true;
    }
}

struct ActionResult
{
    Outcome outcome{Outcome::Succeeded};
    Status status{Status::Ok};
    bool recovery_failed{};
    bool panic{};
};

template <typename ServiceT, typename System, typename Subject, typename ActionT>
[[nodiscard]] ActionResult execute_action() noexcept
{
    auto& state = storage<ServiceT>();
    if constexpr (std::is_same_v<ActionT, Observe> || std::is_same_v<ActionT, Warn> ||
                  std::is_same_v<ActionT, Latch>) {
        return {};
    } else if constexpr (IsTryRecover<ActionT>::value) {
        using Target = typename ActionT::Target;
        static_assert(
            requires { Target::Health::recover(); },
            "SOLAR_DIAGNOSTIC_SUPERVISOR_RECOVERY_HOOK: TryRecover target must expose "
            "static Result<void> Health::recover()");
        auto result = normalize_result(Target::Health::recover());
        return result ? ActionResult{}
                      : ActionResult{.outcome = Outcome::Failed,
                                     .status = status_of(result.error()),
                                     .recovery_failed = true};
    } else if constexpr (requires { typename ActionT::Target; } &&
                         ActionTraits<ActionT>::kind == Action::EnterSafeState) {
        using Target = typename ActionT::Target;
        static_assert(
            requires { Target::enter(); },
            "SOLAR_DIAGNOSTIC_SUPERVISOR_SAFE_STATE_HOOK: safe-state action must expose "
            "static Result<void> enter()");
        auto result = normalize_result(Target::enter());
        return result
                   ? ActionResult{}
                   : ActionResult{.outcome = Outcome::Failed, .status = status_of(result.error())};
    } else if constexpr (requires { typename ActionT::Target; } &&
                         ActionTraits<ActionT>::kind == Action::RequestStop) {
        using Target = typename ActionT::Target;
        static_assert(System::Catalogs::template Of<component::Tag>::template contains<Target>,
                      "SOLAR_DIAGNOSTIC_SUPERVISOR_STOP_TARGET: RequestStop target is absent from "
                      "the effective component graph");
        const auto stopped = lifecycle::ExecutionProtocol<System, Target>::request_stop();
        const auto status = stopped ? Status::Ok : status_of(stopped.error());
        return {.outcome =
                    stopped || status == Status::Already ? Outcome::Requested : Outcome::Failed,
                .status = status};
    } else if constexpr (std::is_same_v<ActionT, RequestSystemStop>) {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(state.mutex);
        state.stop_requested = true;
        state.state.system_stop_requested = true;
        return {.outcome = Outcome::Requested};
    } else if constexpr (std::is_same_v<ActionT, RequestReboot>) {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(state.mutex);
        state.reboot_requested = true;
        state.state.reboot_requested = true;
        return {.outcome = Outcome::Requested};
    } else if constexpr (std::is_same_v<ActionT, StopFeedingWatchdog>) {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(state.mutex);
        state.watchdog.deliberately_withheld = true;
        state.watchdog.feed_permitted = false;
        return {};
    } else if constexpr (std::is_same_v<ActionT, Panic>) {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(state.mutex);
        state.panic_requested = true;
        state.state.panic_requested = true;
        return {.outcome = Outcome::Requested, .panic = true};
    }
}

template <typename ServiceT, typename System, typename Rule>
void evaluate_rule(
    Tick now, std::uint64_t health_generation, bool recovery_failed, std::uint32_t& response_budget,
    std::span<bool> recovery_failures,
    std::array<SubjectRecord,
               list_size_v<typename ServiceT::Architecture::ResponsePolicy::RuleTypes>>&
        subjects) noexcept
{
    using Traits = RuleTraits<Rule>;
    using Subject = typename Traits::Subject;
    using Rules = typename ServiceT::Architecture::ResponsePolicy::RuleTypes;
    constexpr auto rule_index = TypeIndex<Rule, Rules>::value;
    static_assert(System::Catalogs::template Of<component::Tag>::template contains<Subject>,
                  "SOLAR_DIAGNOSTIC_SUPERVISOR_RULE_SUBJECT: response rule subject is absent from "
                  "the effective component graph");
    constexpr auto subject_id =
        System::Catalogs::template Of<component::Tag>::template Entry<Subject>::local_id;
    auto health_record = health::detail::subject_record<System, Subject>();
    if (!health_record) {
        return;
    }
    const bool active = triggered<Rule>(*health_record, recovery_failed);
    auto& state = storage<ServiceT>();
    auto& rule_state = state.rules[rule_index];
    auto& subject_record = subjects[rule_index];
    if (!active) {
        rule_state.active = false;
        rule_state.attempts = 0;
        rule_state.next_attempt = 0;
        subject_record.active = false;
        return;
    }

    constexpr bool retries = []<typename... Actions>(TypeList<Actions...>) {
        return (IsTryRecover<Actions>::value || ...);
    }(typename Traits::Responses{});
    if (rule_state.active &&
        (!retries || rule_state.attempts >= CONFIG_SOLAR_SUPERVISOR_RECOVERY_ATTEMPTS ||
         now < rule_state.next_attempt)) {
        return;
    }
    rule_state.active = true;
    ++rule_state.attempts;
    rule_state.next_attempt = now + kernel::to_ticks_ceil(std::chrono::milliseconds{
                                        CONFIG_SOLAR_SUPERVISOR_RECOVERY_COOLDOWN_MS});

    for_each_type<typename Traits::Responses>([&]<typename ActionT> {
        if (response_budget >= CONFIG_SOLAR_SUPERVISOR_MAX_RESPONSES_PER_CYCLE) {
            auto guard = kernel::LockGuard<kernel::Mutex>::acquire(state.mutex);
            ++state.state.deferred_responses;
            return;
        }
        ++response_budget;
        const auto result = execute_action<ServiceT, System, Subject, ActionT>();
        append_response<ServiceT>({.subject = subject_id,
                                   .trigger = Traits::trigger,
                                   .action = ActionTraits<ActionT>::kind,
                                   .outcome = result.outcome,
                                   .status = result.status,
                                   .attempted_at = now,
                                   .attempt = rule_state.attempts,
                                   .health_generation = health_generation},
                                  subject_record);
        if (result.recovery_failed) {
            recovery_failures[subject_id.index()] = true;
        }
        if (result.panic) {
            kernel::panic(result.status == Status::Ok ? Status::Error : result.status);
        }
    });
}

template <typename ServiceT, typename System>
[[nodiscard]] Result<void, Error> cycle(Tick now) noexcept
{
    using Architecture = typename ServiceT::Architecture;
    using Rules = typename Architecture::ResponsePolicy::RuleTypes;
    auto& storage = detail::storage<ServiceT>();
    auto cycle_guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.cycle_mutex);
    if (!cycle_guard) {
        return fail<Error>({.status = status_of(cycle_guard.error()),
                            .reason = Reason::NotReady,
                            .operation = Operation::Cycle});
    }
    const auto started = now;
    {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
        storage.state.last_phase = Phase::RefreshHealth;
        storage.state.last_cycle_started = started;
        storage.state.cycle_complete = false;
        storage.watchdog.feed_permitted = false;
        storage.watchdog.deliberately_withheld = false;
    }

    auto refreshed = health::detail::refresh<System>();
    if (!refreshed) {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
        ++storage.state.refresh_failures;
        storage.state.state = ServiceState::Faulted;
        return fail<Error>({.status = refreshed.error().status,
                            .reason = Reason::HealthRefreshFailed,
                            .operation = Operation::Cycle});
    }
    auto health_state = health::detail::system_record<System>();
    const auto generation = health_state ? health_state->assessment_generation : 0;
    {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
        storage.state.last_phase = Phase::EvaluatePolicy;
        storage.state.health_generation = generation;
    }

    std::uint32_t responses{};
    std::array<bool, list_size_v<typename System::Components>> recovery_failures{};
    std::array<SubjectRecord, list_size_v<Rules>> subject_updates{};
    {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
        subject_updates = ServiceT::subject_records;
    }
    for_each_type<Rules>([&]<typename Rule> {
        if constexpr (!IsRecoveryFailureRule<Rule>::value) {
            evaluate_rule<ServiceT, System, Rule>(now, generation, false, responses,
                                                  recovery_failures, subject_updates);
        }
    });
    for_each_type<Rules>([&]<typename Rule> {
        if constexpr (IsRecoveryFailureRule<Rule>::value) {
            using Subject = typename RuleTraits<Rule>::Subject;
            constexpr auto subject_id =
                System::Catalogs::template Of<component::Tag>::template Entry<Subject>::local_id;
            evaluate_rule<ServiceT, System, Rule>(now, generation,
                                                  recovery_failures[subject_id.index()], responses,
                                                  recovery_failures, subject_updates);
        }
    });

    {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
        storage.state.last_phase = Phase::EvaluateWatchdog;
        storage.watchdog.feed_permitted = !storage.watchdog.deliberately_withheld && health_state &&
                                          health_state->condition != health::Condition::Faulted &&
                                          health_state->safety != health::Safety::Unsafe &&
                                          health_state->freshness != health::Freshness::Stale;
    }
    if constexpr (Architecture::WatchdogPolicy::configured) {
        using Provider = typename Architecture::WatchdogPolicy::ProviderType;
        bool feed{};
        {
            auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
            feed = storage.watchdog.feed_permitted;
        }
        if (feed) {
            auto result = feed_provider<Provider>();
            auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
            storage.watchdog.last_status = result ? Status::Ok : status_of(result.error());
            if (result) {
                ++storage.watchdog.feeds;
                storage.watchdog.last_feed_at = now;
            } else {
                ++storage.watchdog.failures;
            }
        } else {
            auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
            ++storage.watchdog.withheld;
        }
    }

    const auto completed = (std::max)(kernel::now_ticks(), started);
    const auto duration = completed - started;
    const auto deadline = kernel::to_ticks_ceil(std::chrono::milliseconds{
        CONFIG_SOLAR_SUPERVISOR_PERIOD_MS + CONFIG_SOLAR_SUPERVISOR_CYCLE_GRACE_MS});
    {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
        storage.state.last_phase = Phase::Complete;
        storage.state.last_cycle_completed = completed;
        storage.state.last_cycle_duration = duration;
        storage.state.maximum_cycle_duration =
            (std::max)(storage.state.maximum_cycle_duration, duration);
        storage.state.cycle_complete = true;
        storage.state.state = ServiceState::Running;
        ++storage.state.cycles;
        ++storage.state.cycle_generation;
        if (duration > deadline) {
            ++storage.state.overruns;
        }
        ServiceT::subject_records = subject_updates;
    }
    (void)health::detail::progress<System, ServiceT>();
    return {};
}

} // namespace detail

template <typename ArchitectureT> struct Service
{
    using Architecture = ArchitectureT;
    using Dependencies = solar::Dependencies<health::Facility>;
    using Execution =
        execution::Service<execution::StackSize<CONFIG_SOLAR_SUPERVISOR_SERVICE_STACK_SIZE>,
                           execution::Priority<CONFIG_SOLAR_SUPERVISOR_SERVICE_PRIORITY>>;

    struct Health
    {
        using Checks = health::Checks<
            health::Progress<DurationValue{std::chrono::milliseconds{
                CONFIG_SOLAR_SUPERVISOR_PERIOD_MS + CONFIG_SOLAR_SUPERVISOR_CYCLE_GRACE_MS}}>,
            health::StackMargin<1024>>;
    };

    static constexpr component::Descriptor descriptor{
        .name = "solar.supervisor",
        .description = "Bounded active system supervision",
    };

    inline static detail::Storage<Architecture> state_storage{};
    inline static std::array<SubjectRecord,
                             list_size_v<typename Architecture::ResponsePolicy::RuleTypes>>
        subject_records{};
    using CycleCallback = Result<void, Error> (*)(Tick) noexcept;
    inline static CycleCallback cycle_callback{};

    template <typename System> static void bind() noexcept
    {
        cycle_callback = &detail::cycle<Service, System>;
        health::detail::set_supervisor_waker<System>(&Service::notify_health);
    }

    static void notify_health() noexcept
    {
        state_storage.wake.give_isr();
    }

    [[nodiscard]] static Result<void> init() noexcept
    {
        auto& storage = state_storage;
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
        storage.state = {};
        storage.watchdog = {.configured = Architecture::WatchdogPolicy::configured};
        storage.rules = {};
        storage.history = {};
        storage.next_sequence = 1;
        storage.overwritten = 0;
        storage.stop_requested = false;
        storage.reboot_requested = false;
        storage.panic_requested = false;
        subject_records = {};
        storage.wake.reset();
        return {};
    }

    [[nodiscard]] static Result<void> start() noexcept
    {
        if constexpr (Architecture::WatchdogPolicy::configured) {
            using Provider = typename Architecture::WatchdogPolicy::ProviderType;
            auto result = detail::start_provider<Provider>();
            if (!result) {
                return result;
            }
        }
        auto& storage = state_storage;
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
        storage.state.state = ServiceState::Starting;
        storage.state.started_at = kernel::now_ticks();
        storage.watchdog.enabled = Architecture::WatchdogPolicy::configured;
        return {};
    }

    [[nodiscard]] static Result<void> stop() noexcept
    {
        auto& storage = state_storage;
        {
            auto guard = kernel::LockGuard<kernel::Mutex>::acquire(storage.mutex);
            storage.state.state = ServiceState::Stopping;
            storage.watchdog.feed_permitted = false;
        }
        storage.wake.give();
        if constexpr (Architecture::WatchdogPolicy::configured) {
            using Provider = typename Architecture::WatchdogPolicy::ProviderType;
            return detail::stop_provider<Provider>();
        }
        return {};
    }

    static void notify_stop() noexcept
    {
        state_storage.wake.give();
    }

    [[nodiscard]] static Result<void> deinit() noexcept
    {
        auto guard = kernel::LockGuard<kernel::Mutex>::acquire(state_storage.mutex);
        state_storage.state.state = ServiceState::Stopped;
        state_storage.watchdog.enabled = false;
        return {};
    }

    [[nodiscard]] static Result<void> run(kernel::StopToken stop_token) noexcept
    {
        const auto period =
            kernel::to_ticks_ceil(std::chrono::milliseconds{CONFIG_SOLAR_SUPERVISOR_PERIOD_MS});
        while (!stop_token.stop_requested()) {
            if (cycle_callback == nullptr) {
                return fail<solar::Error>({.status = solar::Status::NotReady});
            }
            auto result = cycle_callback(kernel::now_ticks());
            if (!result) {
                return fail<solar::Error>({.status = result.error().status});
            }
            const auto wake = state_storage.wake.take(kernel::Timeout::after_ticks(period));
            const auto status = wake ? Status::Ok : status_of(wake.error());
            if (!wake && status != Status::Timeout) {
                return fail<solar::Error>(wake.error());
            }
        }
        return {};
    }
};

#else

template <typename ArchitectureT> struct Service
{
    using Architecture = ArchitectureT;
};

#endif

} // namespace solar::supervisor
