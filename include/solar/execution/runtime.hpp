#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <type_traits>

#include "solar/execution/service_runtime.hpp"
#include "solar/kernel/this_thread.hpp"
#include "solar/kernel/triggered_work.hpp"
#include "solar/kernel/work.hpp"
#include "solar/kernel/work_queue.hpp"
#include "solar/system/frontend.hpp"

namespace solar::execution::detail
{

struct ExecutorStateKey
{};

struct RegistrationStateKey
{};

struct RuntimeStateKey
{};

template <typename System, typename Executor> struct ExecutorState
{
    using Policy = typename Executor::Policy;
    static_assert(Policy::priority < CONFIG_NUM_PREEMPT_PRIORITIES,
                  "SOLAR_DIAGNOSTIC_INVALID_EXECUTOR_PRIORITY: workqueue priority exceeds Zephyr's "
                  "configured preemptive range");

    kernel::WorkQueue<Policy::stack_bytes> queue{};
    kernel::SpinLock record_lock{};
    ExecutorRecord record{};

    template <typename Mutator> void mutate(Mutator&& mutator) noexcept
    {
        auto guard = record_lock.acquire();
        mutator(record);
    }

    [[nodiscard]] ExecutorRecord copy() noexcept
    {
        auto guard = record_lock.acquire();
        record.thread = queue.thread_id();
        return record;
    }
};

template <typename System, typename Executor> [[nodiscard]] auto& executor_state() noexcept
{
    using State = ExecutorState<System, Executor>;
    return System::template StateSlot<Executor, ExecutorStateKey, State>::value;
}

template <typename System> struct RuntimeState
{
    kernel::SpinLock lock{};
    lifecycle::Failure last_failure{};
    std::atomic_size_t uncontained_system_registrations{};
};

template <typename System> [[nodiscard]] auto& runtime_state() noexcept
{
    using State = RuntimeState<System>;
    return System::template StateSlot<System, RuntimeStateKey, State>::value;
}

template <typename T> struct IsApplicationOwner : std::false_type
{};

template <> struct IsApplicationOwner<ApplicationOwner> : std::true_type
{};

template <typename System, typename Owner, bool Application = IsApplicationOwner<Owner>::value>
struct OwnerDependencies
{
    static_assert(contains_v<Owner, typename System::Components>,
                  "SOLAR_DIAGNOSTIC_EXECUTION_OWNER_ABSENT: contributed registration owner is "
                  "absent from the effective component graph");
    using type = concat_t<TypeList<Owner>, typename System::Graph::template DependenciesOf<Owner>>;
};

template <typename System, typename Owner> struct OwnerDependencies<System, Owner, true>
{
    using type = TypeList<>;
};

template <typename System, typename Registration> struct ResolvedTarget
{
    using Authored = typename registration_traits<Registration>::Target;

#if defined(CONFIG_SOLAR_EXECUTION_DEFAULT_SYSTEM_WORKQUEUE)
    using Candidate =
        std::conditional_t<std::is_same_v<Authored, DefaultTarget>, SystemWorkQueue, Authored>;
    static constexpr TargetSource source = std::is_same_v<Authored, DefaultTarget>
                                               ? TargetSource::KconfigDefault
                                               : TargetSource::Explicit;
#else
    static_assert(!std::is_same_v<Authored, DefaultTarget>,
                  "SOLAR_DIAGNOSTIC_EXECUTION_TARGET_REQUIRED: registration omits a target while "
                  "CONFIG_SOLAR_EXECUTION_DEFAULT_SYSTEM_WORKQUEUE is disabled");
    using Candidate =
        std::conditional_t<std::is_same_v<Authored, DefaultTarget>, SystemWorkQueue, Authored>;
    static constexpr TargetSource source = TargetSource::Explicit;
#endif

    static constexpr bool valid = target_traits<Candidate>::valid;
    static constexpr bool registered =
        std::is_same_v<Candidate, SystemWorkQueue> ||
        contains_v<Candidate, typename System::Effective::UserExecutors>;
    static constexpr bool executor =
        std::is_same_v<Candidate, SystemWorkQueue> || WorkQueueExecutor<Candidate>;

    static_assert(valid, "SOLAR_DIAGNOSTIC_INVALID_EXECUTION_TARGET: registration target is not a "
                         "Solar execution target");
    static_assert(registered, "SOLAR_DIAGNOSTIC_UNREGISTERED_EXECUTION_TARGET: application "
                              "workqueue target is absent from Executors<...>");
    static_assert(executor, "SOLAR_DIAGNOSTIC_INVALID_EXECUTOR_COMPONENT: registered execution "
                            "target is not an execution::WorkQueue");

    // Keep rejected targets from leaking into later dependency and catalog
    // instantiations; the assertions above remain the authored diagnostics.
    using type = std::conditional_t<valid && registered && executor, Candidate, SystemWorkQueue>;
};

template <typename System, typename Registration>
using resolved_target_t = typename ResolvedTarget<System, Registration>::type;

template <typename System, typename Registration> struct RegistrationMetadata
{
    using Traits = registration_traits<Registration>;
    using Entry = typename System::ExecutionCatalog::template Entry<Registration>;
    using Owner = typename Entry::Owner;
    using Target = resolved_target_t<System, Registration>;
    using OwnerDependencyList = typename OwnerDependencies<System, Owner>::type;
    using TargetDependencies =
        std::conditional_t<std::is_same_v<Target, SystemWorkQueue>, TypeList<>, TypeList<Target>>;
    using Dependencies =
        unique_t<concat_t<OwnerDependencyList, typename Traits::Dependencies, TargetDependencies>>;

    template <typename Dependency>
    struct IsPresent : std::bool_constant<contains_v<Dependency, typename System::Components>>
    {};

    static_assert(
        []<typename... DependenciesT>(TypeList<DependenciesT...>) {
            return (contains_v<DependenciesT, typename System::Components> && ...);
        }(Dependencies{}),
        "SOLAR_DIAGNOSTIC_EXECUTION_DEPENDENCY_ABSENT: registration dependency is absent from the "
        "effective component graph");
};

template <typename Behavior>
concept BehaviorVoid = requires {
    { Behavior::execute() } -> std::same_as<void>;
};

template <typename Behavior>
concept BehaviorResult =
    requires { Behavior::execute(); } && VoidResult<decltype(Behavior::execute())>;

template <typename Behavior>
concept TokenBehaviorVoid = requires(StopToken token) {
    { Behavior::execute(token) } -> std::same_as<void>;
};

template <typename Behavior>
concept TokenBehaviorResult = requires(StopToken token) { Behavior::execute(token); } &&
                              VoidResult<decltype(Behavior::execute(std::declval<StopToken>()))>;

template <typename Behavior>
concept ValidBehavior = BehaviorVoid<Behavior> || BehaviorResult<Behavior> ||
                        TokenBehaviorVoid<Behavior> || TokenBehaviorResult<Behavior>;

template <typename Behavior> [[nodiscard]] Result<void> invoke_behavior(StopToken token) noexcept
{
    static_assert(ValidBehavior<Behavior>,
                  "SOLAR_DIAGNOSTIC_INVALID_EXECUTION_BEHAVIOR: behavior must implement static "
                  "void or Result<void, ErrorType> execute(), optionally with StopToken");
    if constexpr (TokenBehaviorVoid<Behavior>) {
        Behavior::execute(token);
        return {};
    } else if constexpr (TokenBehaviorResult<Behavior>) {
        auto result = Behavior::execute(token);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else if constexpr (BehaviorVoid<Behavior>) {
        Behavior::execute();
        return {};
    } else if constexpr (BehaviorResult<Behavior>) {
        auto result = Behavior::execute();
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else {
        return fail<solar::Error>({.status = solar::Status::Invalid});
    }
}

template <typename Traits> struct WorkTypeFor
{
    using type =
        std::conditional_t<Traits::kind == RegistrationKind::OnDemand, kernel::Work,
                           std::conditional_t<Traits::kind == RegistrationKind::PollTriggered,
                                              kernel::TriggeredWork, kernel::DelayableWork>>;
};

template <bool Enabled> struct RegistrationStopState
{
    [[nodiscard]] StopToken stop_token() noexcept
    {
        return {};
    }

    void request_stop() noexcept {}
};

template <> struct RegistrationStopState<true>
{
    [[nodiscard]] StopToken stop_token() noexcept
    {
        return source.token();
    }

    void request_stop() noexcept
    {
        (void)source.request_stop();
    }

    kernel::StopSource source{};
};

template <bool Enabled> struct RegistrationCountedState
{
    [[nodiscard]] std::uint32_t pending_count() const noexcept
    {
        return 0;
    }

    void reset_pending() noexcept {}
};

template <> struct RegistrationCountedState<true>
{
    [[nodiscard]] std::uint32_t pending_count() const noexcept
    {
        return pending.load(std::memory_order_acquire);
    }

    void reset_pending() noexcept
    {
        pending.store(0, std::memory_order_release);
    }

    std::atomic_uint32_t pending{};
};

template <bool Enabled> struct RegistrationPeriodicState
{};

template <> struct RegistrationPeriodicState<true>
{
    kernel::Tick next_ideal_release{};
};

template <typename Registration> struct RegistrationCapabilities
{
    using Traits = registration_traits<Registration>;
    using Behavior = typename Traits::BehaviorType;

    static constexpr bool stop_token = TokenBehaviorVoid<Behavior> || TokenBehaviorResult<Behavior>;
    static constexpr bool counted = [] {
        if constexpr (Traits::kind == RegistrationKind::OnDemand) {
            return detail::IsCounted<typename Traits::Admission>::value;
        } else {
            return false;
        }
    }();
    static constexpr bool periodic = Traits::kind == RegistrationKind::Periodic;
};

template <typename System, typename Registration>
struct RegistrationState
    : private RegistrationStopState<RegistrationCapabilities<Registration>::stop_token>,
      private RegistrationCountedState<RegistrationCapabilities<Registration>::counted>,
      private RegistrationPeriodicState<RegistrationCapabilities<Registration>::periodic>
{
    using Traits = registration_traits<Registration>;
    using Metadata = RegistrationMetadata<System, Registration>;
    using Target = typename Metadata::Target;
    using WorkType = typename WorkTypeFor<Traits>::type;
    using Capabilities = RegistrationCapabilities<Registration>;
    using StopState = RegistrationStopState<Capabilities::stop_token>;
    using CountedState = RegistrationCountedState<Capabilities::counted>;
    using PeriodicState = RegistrationPeriodicState<Capabilities::periodic>;

    RegistrationState() noexcept : work(&RegistrationState::handle) {}

    WorkType work;
    kernel::SpinLock record_lock{};
    RegistrationRecord record{};
    std::atomic_bool accepting{};
    std::atomic_bool in_flight{};
    std::atomic_bool draining{};
    std::atomic_bool suspended{};
    std::atomic_bool uncontained{};

    [[nodiscard]] StopToken stop_token() noexcept
    {
        return StopState::stop_token();
    }

    void request_stop() noexcept
    {
        StopState::request_stop();
    }

    [[nodiscard]] std::uint32_t pending_count() const noexcept
    {
        return CountedState::pending_count();
    }

    void reset_pending() noexcept
    {
        CountedState::reset_pending();
    }

    [[nodiscard]] std::atomic_uint32_t& pending_counter() noexcept
        requires(Capabilities::counted)
    {
        return CountedState::pending;
    }

    [[nodiscard]] kernel::Tick& next_release() noexcept
        requires(Capabilities::periodic)
    {
        return PeriodicState::next_ideal_release;
    }

    template <typename Mutator> void mutate(Mutator&& mutator) noexcept
    {
        auto guard = record_lock.acquire();
        mutator(record);
    }

    [[nodiscard]] RegistrationRecord copy() noexcept
    {
        auto guard = record_lock.acquire();
        record.accepting = accepting.load(std::memory_order_acquire);
        record.in_flight = in_flight.load(std::memory_order_acquire);
        record.pending_count = pending_count();
        record.queued_now = work.pending();
        record.quiescent = !record.in_flight && !record.queued_now && record.pending_count == 0;
        return record;
    }

    static void handle(WorkType&) noexcept;
};

template <typename System, typename Registration> [[nodiscard]] auto& registration_state() noexcept
{
    using State = RegistrationState<System, Registration>;
    return System::template StateSlot<Registration, RegistrationStateKey, State>::value;
}

template <typename System, typename Registration> [[nodiscard]] auto& target_handle() noexcept
{
    using Target = resolved_target_t<System, Registration>;
    if constexpr (std::is_same_v<Target, SystemWorkQueue>) {
        return kernel::system_work_queue;
    } else {
        return executor_state<System, Target>().queue;
    }
}

[[nodiscard]] constexpr SubmissionDisposition
submission_disposition(kernel::WorkSubmission submission) noexcept
{
    switch (submission) {
    case kernel::WorkSubmission::AlreadyQueued:
        return SubmissionDisposition::AlreadyPending;
    case kernel::WorkSubmission::Queued:
        return SubmissionDisposition::Queued;
    case kernel::WorkSubmission::RequeuedAfterCurrent:
        return SubmissionDisposition::RequeuedAfterCurrent;
    }
    return SubmissionDisposition::Queued;
}

template <typename System, typename Registration>
[[nodiscard]] Error make_error(Operation operation, Status status, ErrorReason reason,
                               int native_error = 0) noexcept
{
    using Metadata = RegistrationMetadata<System, Registration>;
    using Target = typename Metadata::Target;
    constexpr auto entry = typename Metadata::Entry{};
    constexpr auto target_id = [] {
        if constexpr (std::is_same_v<Target, SystemWorkQueue>) {
            return component::LocalId{};
        } else {
            using Components = typename System::Catalogs::template Of<component::Tag>;
            return Components::template Entry<Target>::local_id;
        }
    }();
    return {
        .status = status,
        .reason = reason,
        .registration = entry.local_id,
        .target = target_id,
        .target_kind = target_traits<Target>::kind,
        .operation = operation,
        .availability = registration_state<System, Registration>().copy().availability,
        .native_error = native_error,
    };
}

template <typename System, typename Registration>
[[nodiscard]] Error make_work_error(Operation operation, const kernel::WorkError& error) noexcept
{
    ErrorReason reason = ErrorReason::NativeFailure;
    switch (error.reason) {
    case kernel::WorkErrorReason::Busy:
        reason = ErrorReason::WorkCancelling;
        break;
    case kernel::WorkErrorReason::QueueNotStarted:
        reason = ErrorReason::QueueStopped;
        break;
    case kernel::WorkErrorReason::InvalidContext:
    case kernel::WorkErrorReason::Deadlock:
        reason = ErrorReason::InvalidContext;
        break;
    default:
        break;
    }
    return make_error<System, Registration>(operation, error.status, reason, error.native_error);
}

template <typename System, typename Registration>
[[nodiscard]] Result<void> validate_registration() noexcept
{
    static_assert(execution::Registration<Registration>,
                  "SOLAR_DIAGNOSTIC_INVALID_EXECUTION_REGISTRATION: execution catalog entry is not "
                  "a supported registration type");
    using Traits = registration_traits<Registration>;
    static_assert(ValidBehavior<typename Traits::BehaviorType>,
                  "SOLAR_DIAGNOSTIC_INVALID_EXECUTION_BEHAVIOR: behavior must implement static "
                  "void or Result<void, ErrorType> execute(), optionally with StopToken");
    using Metadata = RegistrationMetadata<System, Registration>;
    using Target = typename Metadata::Target;
    if constexpr (!std::is_same_v<Target, SystemWorkQueue>) {
        if (!executor_state<System, Target>().queue.started()) {
            return fail<solar::Error>({.status = solar::Status::NotReady});
        }
    }
    if constexpr (Traits::kind == RegistrationKind::PollTriggered) {
        static_assert(
            kernel::triggered_work_available,
            "SOLAR_DIAGNOSTIC_POLL_TRIGGERED_DISABLED: PollTriggered requires CONFIG_POLL");
        using PollSet = typename Traits::PollSetType;
        static_assert(
            requires { PollSet::events(); }, "SOLAR_DIAGNOSTIC_INVALID_POLL_SET: PollTriggered "
                                             "poll-set type must expose static events()");
        if (PollSet::events().size() == 0) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
    }
    return {};
}

template <typename System, typename Registration> void record_runtime_failure(Status status)
{
    using Metadata = RegistrationMetadata<System, Registration>;
    using Owner = typename Metadata::Owner;
    lifecycle::Failure failure{
        .component = Metadata::Entry::owner_id,
        .category = lifecycle::ComponentCategory::Facility,
        .operation = lifecycle::Operation::ActivateExecution,
        .status = status,
        .primary = false,
        .catalog =
            lifecycle::CatalogSubject{
                .kind = 1,
                .local_id = Metadata::Entry::local_id.value,
            },
    };
    if constexpr (!IsApplicationOwner<Owner>::value) {
        using Category = typename System::Effective::template CategoryOf<Owner>;
        if constexpr (std::is_same_v<Category, category::Device>) {
            failure.category = lifecycle::ComponentCategory::Device;
        } else if constexpr (std::is_same_v<Category, category::Service>) {
            failure.category = lifecycle::ComponentCategory::Service;
        } else if constexpr (std::is_same_v<Category, category::Executor>) {
            failure.category = lifecycle::ComponentCategory::Executor;
        }
    }
    auto& runtime = runtime_state<System>();
    auto guard = runtime.lock.acquire();
    runtime.last_failure = failure;
}

template <typename System, typename Registration>
[[nodiscard]] Result<void> initialize_registration()
{
    if (auto validation = validate_registration<System, Registration>(); !validation) {
        record_runtime_failure<System, Registration>(status_of(validation.error()));
        return validation;
    }

    using Metadata = RegistrationMetadata<System, Registration>;
    using Target = typename Metadata::Target;
    constexpr auto entry = typename Metadata::Entry{};
    constexpr auto descriptors = System::ExecutionCatalog::descriptors();
    constexpr auto target_id = [] {
        if constexpr (std::is_same_v<Target, SystemWorkQueue>) {
            return component::LocalId{};
        } else {
            using Components = typename System::Catalogs::template Of<component::Tag>;
            return Components::template Entry<Target>::local_id;
        }
    }();

    auto& state = registration_state<System, Registration>();
    state.mutate([&](RegistrationRecord& record) {
        record = {
            .descriptor = descriptors[entry.local_id.index()],
            .local_id = entry.local_id,
            .owner = entry.owner_id,
            .target = target_id,
            .kind = registration_traits<Registration>::kind,
            .target_kind = target_traits<Target>::kind,
            .target_source = ResolvedTarget<System, Registration>::source,
            .availability = Availability::Inactive,
            .last_status = Status::Ok,
            .initialized = true,
            .quiescent = true,
        };
    });
    return {};
}

template <typename System, typename Registration>
[[nodiscard]] Result<kernel::WorkSubmission, kernel::WorkError>
submit_native(bool from_isr = false) noexcept
{
    auto& state = registration_state<System, Registration>();
    return from_isr ? state.work.try_submit_isr(target_handle<System, Registration>())
                    : state.work.submit(target_handle<System, Registration>());
}

template <typename System, typename Registration>
void update_submission_record(kernel::WorkSubmission native, bool from_isr,
                              bool counted = false) noexcept
{
    auto& state = registration_state<System, Registration>();
    state.mutate([&](RegistrationRecord& record) {
        ++record.submissions;
        if (from_isr) {
            ++record.isr_submissions;
        }
        ++record.release_attempts;
        ++record.queued;
        record.last_release = kernel::now_ticks();
        record.last_status = Status::Ok;
        record.queued_now = true;
        record.quiescent = false;
        if (counted) {
            ++record.counted;
        } else if (native == kernel::WorkSubmission::AlreadyQueued) {
            ++record.already_pending;
        } else if (native == kernel::WorkSubmission::RequeuedAfterCurrent) {
            ++record.requeued;
        }
    });
    (void)from_isr;
}

template <typename System, typename Registration>
[[nodiscard]] Result<Submission, Error> submit_registration(bool from_isr) noexcept
{
    using Traits = registration_traits<Registration>;
    static_assert(
        Traits::kind == RegistrationKind::OnDemand,
        "SOLAR_DIAGNOSTIC_EXECUTION_SUBMIT_KIND: submit requires an OnDemand registration");
    auto& state = registration_state<System, Registration>();
    if (!from_isr && kernel::in_isr()) {
        return fail<Error>(make_error<System, Registration>(Operation::Submit, Status::Invalid,
                                                            ErrorReason::InvalidContext));
    }
    if (!state.accepting.load(std::memory_order_acquire)) {
        const auto reason = state.suspended.load(std::memory_order_acquire)
                                ? ErrorReason::RegistrationSuspended
                                : ErrorReason::RegistrationInactive;
        return fail<Error>(make_error<System, Registration>(
            from_isr ? Operation::SubmitIsr : Operation::Submit, Status::NotReady, reason));
    }

    constexpr bool counted = detail::IsCounted<typename Traits::Admission>::value;
    std::uint32_t accepted = 1;
    if constexpr (counted) {
        auto& pending_counter = state.pending_counter();
        auto pending = pending_counter.load(std::memory_order_acquire);
        while (true) {
            if (pending >= Traits::Admission::capacity) {
                state.mutate([](RegistrationRecord& record) { ++record.admission_rejected; });
                return fail<Error>(make_error<System, Registration>(
                    from_isr ? Operation::SubmitIsr : Operation::Submit, Status::Full,
                    ErrorReason::AdmissionFull));
            }
            if (pending_counter.compare_exchange_weak(pending, pending + 1,
                                                      std::memory_order_acq_rel)) {
                accepted = pending + 1;
                break;
            }
        }
    }

    auto submission = submit_native<System, Registration>(from_isr);
    if (!submission) {
        if constexpr (counted) {
            state.pending_counter().fetch_sub(1, std::memory_order_acq_rel);
        }
        return fail<Error>(make_work_error<System, Registration>(
            from_isr ? Operation::SubmitIsr : Operation::Submit, submission.error()));
    }
    update_submission_record<System, Registration>(*submission, from_isr, counted);
    if constexpr (counted) {
        state.mutate([&](RegistrationRecord& record) {
            record.pending_count = accepted;
            if (accepted > record.pending_high_water) {
                record.pending_high_water = accepted;
            }
        });
    }

    using Metadata = RegistrationMetadata<System, Registration>;
    using Target = typename Metadata::Target;
    constexpr auto target_id = [] {
        if constexpr (std::is_same_v<Target, SystemWorkQueue>) {
            return component::LocalId{};
        } else {
            using Components = typename System::Catalogs::template Of<component::Tag>;
            return Components::template Entry<Target>::local_id;
        }
    }();
    return Submission{
        .registration = Metadata::Entry::local_id,
        .target = target_id,
        .target_kind = target_traits<Target>::kind,
        .disposition =
            counted ? SubmissionDisposition::Counted : submission_disposition(*submission),
        .accepted = accepted,
        .sequence = registration_state<System, Registration>().copy().release_attempts,
        .from_isr = from_isr,
    };
}

template <typename System, typename Registration>
[[nodiscard]] Result<Submission, Error> schedule_registration(std::chrono::nanoseconds delay,
                                                              bool replace) noexcept
{
    using Traits = registration_traits<Registration>;
    static_assert(Traits::kind == RegistrationKind::Delayable,
                  "SOLAR_DIAGNOSTIC_EXECUTION_SCHEDULE_KIND: schedule and reschedule require a "
                  "Delayable registration");
    auto& state = registration_state<System, Registration>();
    if (kernel::in_isr()) {
        return fail<Error>(
            make_error<System, Registration>(replace ? Operation::Reschedule : Operation::Schedule,
                                             Status::Invalid, ErrorReason::InvalidContext));
    }
    if (!state.accepting.load(std::memory_order_acquire)) {
        return fail<Error>(
            make_error<System, Registration>(replace ? Operation::Reschedule : Operation::Schedule,
                                             Status::NotReady, ErrorReason::RegistrationInactive));
    }
    auto result = replace ? state.work.reschedule(target_handle<System, Registration>(), delay)
                          : state.work.schedule(target_handle<System, Registration>(), delay);
    if (!result) {
        return fail<Error>(make_work_error<System, Registration>(
            replace ? Operation::Reschedule : Operation::Schedule, result.error()));
    }
    update_submission_record<System, Registration>(*result, false);
    state.mutate([](RegistrationRecord& record) { record.armed = true; });
    using Target = resolved_target_t<System, Registration>;
    return Submission{
        .registration = RegistrationMetadata<System, Registration>::Entry::local_id,
        .target_kind = target_traits<Target>::kind,
        .disposition = submission_disposition(*result),
        .accepted = 1,
        .sequence = state.copy().release_attempts,
    };
}

template <typename System, typename Registration>
[[nodiscard]] Result<Cancellation, Error> cancel_registration(bool synchronous) noexcept
{
    auto& state = registration_state<System, Registration>();
    if (kernel::in_isr() && synchronous) {
        return fail<Error>(make_error<System, Registration>(Operation::CancelSync, Status::Invalid,
                                                            ErrorReason::InvalidContext));
    }
    const bool was_pending = state.work.pending();
    if (synchronous) {
        auto result = state.work.cancel_sync();
        if (!result) {
            return fail<Error>(
                make_work_error<System, Registration>(Operation::CancelSync, result.error()));
        }
    } else {
        if constexpr (registration_traits<Registration>::kind == RegistrationKind::PollTriggered) {
            const auto result = state.work.cancel_trigger();
            const auto status = result ? Status::Ok : status_of(result.error());
            if (!result && status != Status::Busy) {
                return fail<Error>(make_error<System, Registration>(Operation::Cancel, status,
                                                                    ErrorReason::NativeFailure));
            }
        } else {
            (void)state.work.cancel();
        }
    }
    if constexpr (registration_traits<Registration>::kind == RegistrationKind::OnDemand) {
        if constexpr (detail::IsCounted<
                          typename registration_traits<Registration>::Admission>::value) {
            state.reset_pending();
        }
    }
    const bool quiescent =
        !state.work.pending() && !state.in_flight.load(std::memory_order_acquire);
    state.mutate([&](RegistrationRecord& record) {
        if (was_pending) {
            ++record.cancelled;
        }
        record.queued_now = state.work.pending();
        record.quiescent = quiescent;
    });
    return Cancellation{.registration = RegistrationMetadata<System, Registration>::Entry::local_id,
                        .pending_cancelled = was_pending,
                        .quiescent = quiescent};
}

template <typename System, typename Registration>
[[nodiscard]] Result<Cancellation, Error> flush_registration() noexcept
{
    if (kernel::in_isr()) {
        return fail<Error>(make_error<System, Registration>(Operation::Flush, Status::Invalid,
                                                            ErrorReason::InvalidContext));
    }
    auto& state = registration_state<System, Registration>();
    auto result = state.work.flush();
    if (!result) {
        return fail<Error>(make_work_error<System, Registration>(Operation::Flush, result.error()));
    }
    return Cancellation{.registration = RegistrationMetadata<System, Registration>::Entry::local_id,
                        .pending_cancelled = false,
                        .quiescent = !state.work.pending()};
}

template <typename System, typename Registration>
void finish_invocation(Result<void> result, kernel::Tick started) noexcept
{
    using Traits = registration_traits<Registration>;
    auto& state = registration_state<System, Registration>();
    const auto completed = kernel::now_ticks();
    const auto duration = completed - started;
    state.in_flight.store(false, std::memory_order_release);
    state.mutate([&](RegistrationRecord& record) {
        ++record.completed;
        if (!result) {
            ++record.failed;
        }
        record.last_status = result ? Status::Ok : status_of(result.error());
        record.last_completion = completed;
        record.last_duration = duration;
        if (duration > record.maximum_duration) {
            record.maximum_duration = duration;
        }
        record.in_flight = false;
    });

    if (!result && std::is_same_v<typename Traits::FailurePolicy, failure::Suspend>) {
        state.suspended.store(true, std::memory_order_release);
        state.accepting.store(false, std::memory_order_release);
        state.mutate([](RegistrationRecord& record) {
            record.availability = Availability::Suspended;
            record.accepting = false;
        });
    }
}

template <typename System, typename Registration> void continue_registration() noexcept
{
    using Traits = registration_traits<Registration>;
    auto& state = registration_state<System, Registration>();
    if constexpr (Traits::kind == RegistrationKind::OnDemand) {
        if constexpr (detail::IsCounted<typename Traits::Admission>::value) {
            if (state.pending_count() != 0 && (state.accepting.load(std::memory_order_acquire) ||
                                               state.draining.load(std::memory_order_acquire))) {
                (void)submit_native<System, Registration>();
            }
        }
    } else if constexpr (Traits::kind == RegistrationKind::Periodic) {
        if (!state.accepting.load(std::memory_order_acquire)) {
            return;
        }
        const auto now = kernel::now_ticks();
        kernel::Tick delay_ticks{};
        if constexpr (std::is_same_v<typename Traits::Cadence, periodic::FixedDelay>) {
            delay_ticks = kernel::to_ticks_ceil(Traits::period.duration());
            state.next_release() = now + delay_ticks;
        } else {
            const auto period_ticks = kernel::to_ticks_ceil(Traits::period.duration());
            state.next_release() += period_ticks;
            std::uint64_t missed{};
            while (state.next_release() <= now) {
                state.next_release() += period_ticks;
                ++missed;
            }
            delay_ticks = state.next_release() - now;
            if (missed != 0) {
                state.mutate([&](RegistrationRecord& record) {
                    record.missed_releases += missed;
                    ++record.overruns;
                });
            }
        }
        auto scheduled = state.work.schedule(target_handle<System, Registration>(),
                                             kernel::Timeout::after_ticks(delay_ticks));
        if (!scheduled) {
            state.mutate([&](RegistrationRecord& record) {
                record.last_status = scheduled.error().status;
                record.availability = Availability::Failed;
            });
            state.accepting.store(false, std::memory_order_release);
            record_runtime_failure<System, Registration>(scheduled.error().status);
        } else {
            update_submission_record<System, Registration>(*scheduled, false);
            state.mutate([](RegistrationRecord& record) { record.armed = true; });
        }
    } else if constexpr (Traits::kind == RegistrationKind::PollTriggered) {
        if constexpr (std::is_same_v<typename Traits::Rearm, poll::AutoRearm>) {
            if (state.accepting.load(std::memory_order_acquire)) {
                auto& events = Traits::PollSetType::events();
                auto armed = state.work.submit(events, target_handle<System, Registration>());
                if (!armed) {
                    state.mutate([&](RegistrationRecord& record) {
                        record.last_status = armed.error().status;
                        record.availability = Availability::Failed;
                    });
                    state.accepting.store(false, std::memory_order_release);
                }
            }
        }
    }
}

template <typename System, typename Registration>
void RegistrationState<System, Registration>::handle(WorkType&) noexcept
{
    using Traits = registration_traits<Registration>;
    auto& state = registration_state<System, Registration>();
    if constexpr (Traits::kind == RegistrationKind::OnDemand) {
        if constexpr (detail::IsCounted<typename Traits::Admission>::value) {
            auto& pending_counter = state.pending_counter();
            auto pending = pending_counter.load(std::memory_order_acquire);
            while (pending != 0 && !pending_counter.compare_exchange_weak(
                                       pending, pending - 1, std::memory_order_acq_rel)) {
            }
            if (pending == 0) {
                return;
            }
        }
    }

    const auto started = kernel::now_ticks();
    state.in_flight.store(true, std::memory_order_release);
    state.mutate([&](RegistrationRecord& record) {
        ++record.started;
        record.last_start = started;
        record.in_flight = true;
        record.armed = false;
        record.quiescent = false;
    });
    auto result = invoke_behavior<typename Traits::BehaviorType>(state.stop_token());

    if constexpr (Traits::kind == RegistrationKind::Periodic) {
        if constexpr (!std::is_void_v<typename Traits::DeadlinePolicy>) {
            const auto elapsed = kernel::from_ticks(kernel::now_ticks() - state.next_release());
            if (elapsed > std::chrono::duration_cast<kernel::TickDuration>(
                              Traits::DeadlinePolicy::value.duration())) {
                state.mutate([](RegistrationRecord& record) { ++record.deadline_misses; });
            }
        }
    }

    finish_invocation<System, Registration>(result, started);
    continue_registration<System, Registration>();
}

template <typename System, typename Registration> [[nodiscard]] Result<void> activate_registration()
{
    using Traits = registration_traits<Registration>;
    auto& state = registration_state<System, Registration>();
    state.accepting.store(true, std::memory_order_release);
    state.mutate([](RegistrationRecord& record) {
        record.availability = Availability::Active;
        record.active = true;
        record.accepting = true;
        record.quiescent = true;
    });

    if constexpr (Traits::kind == RegistrationKind::Periodic) {
        const auto period_ticks = kernel::to_ticks_ceil(Traits::period.duration());
        const auto now = kernel::now_ticks();
        constexpr bool immediate =
            std::is_same_v<typename Traits::InitialRelease, StartImmediately>;
        state.next_release() = immediate ? now : now + period_ticks;
        const auto delay =
            immediate ? kernel::Timeout::no_wait() : kernel::Timeout::after_ticks(period_ticks);
        auto result = state.work.schedule(target_handle<System, Registration>(), delay);
        if (!result) {
            state.accepting.store(false, std::memory_order_release);
            state.mutate([&](RegistrationRecord& record) {
                record.availability = Availability::Failed;
                record.last_status = result.error().status;
            });
            record_runtime_failure<System, Registration>(result.error().status);
            return fail<solar::Error>(
                {.status = result.error().status, .native = result.error().native_error});
        }
        update_submission_record<System, Registration>(*result, false);
        state.mutate([](RegistrationRecord& record) { record.armed = true; });
    } else if constexpr (Traits::kind == RegistrationKind::PollTriggered) {
        auto& events = Traits::PollSetType::events();
        auto result = state.work.submit(events, target_handle<System, Registration>());
        if (!result) {
            state.accepting.store(false, std::memory_order_release);
            state.mutate([&](RegistrationRecord& record) {
                record.availability = Availability::Failed;
                record.last_status = result.error().status;
            });
            record_runtime_failure<System, Registration>(result.error().status);
            return fail<solar::Error>(
                {.status = result.error().status, .native = result.error().native_error});
        }
        state.mutate([](RegistrationRecord& record) {
            record.armed = true;
            record.quiescent = false;
        });
    }
    return {};
}

template <typename System, typename Registration> void request_registration_stop() noexcept
{
    auto& state = registration_state<System, Registration>();
    state.accepting.store(false, std::memory_order_release);
    state.draining.store(
        std::is_same_v<typename registration_traits<Registration>::StopPolicy, stop::Drain>,
        std::memory_order_release);
    state.request_stop();
    state.mutate([](RegistrationRecord& record) {
        record.accepting = false;
        record.stop_requested = true;
        record.availability = Availability::Stopping;
    });
}

template <typename System, typename Registration> void begin_registration_containment() noexcept
{
    using Traits = registration_traits<Registration>;
    auto& state = registration_state<System, Registration>();
    if constexpr (std::is_same_v<typename Traits::StopPolicy, stop::CancelPending>) {
        (void)cancel_registration<System, Registration>(false);
    } else if constexpr (Traits::kind == RegistrationKind::Delayable) {
        if (state.work.pending() && !state.in_flight.load(std::memory_order_acquire)) {
            (void)state.work.reschedule(target_handle<System, Registration>(),
                                        kernel::Timeout::no_wait());
        }
    } else if constexpr (Traits::kind == RegistrationKind::Periodic) {
        (void)state.work.cancel();
    } else if constexpr (Traits::kind == RegistrationKind::PollTriggered) {
        (void)state.work.cancel_trigger();
    }
}

template <typename System, typename Registration>
[[nodiscard]] bool registration_quiescent() noexcept
{
    auto& state = registration_state<System, Registration>();
    return !state.in_flight.load(std::memory_order_acquire) && !state.work.pending() &&
           state.pending_count() == 0;
}

template <typename System, typename Registration>
[[nodiscard]] bool wait_registration_quiescent(const kernel::Deadline& deadline) noexcept
{
    while (!registration_quiescent<System, Registration>()) {
        if (deadline.expired()) {
            return false;
        }
        (void)kernel::this_thread::sleep_for(Milliseconds{1});
    }
    auto& state = registration_state<System, Registration>();
    state.draining.store(false, std::memory_order_release);
    state.mutate([](RegistrationRecord& record) {
        record.availability = Availability::Quiescent;
        record.active = false;
        record.armed = false;
        record.queued_now = false;
        record.in_flight = false;
        record.quiescent = true;
    });
    return true;
}

template <typename System, typename Function> void for_each_registration(Function&& function)
{
    for_each_type<typename System::EffectiveExecutionRegistrations>(
        [&]<typename Entry> { function.template operator()<typename Entry::Declaration>(); });
}

template <typename System, typename Target, typename Function>
void for_each_target_registration(Function&& function)
{
    for_each_registration<System>([&]<typename Registration> {
        if constexpr (std::is_same_v<resolved_target_t<System, Registration>, Target>) {
            function.template operator()<Registration>();
        }
    });
}

template <typename System> [[nodiscard]] Result<void> prepare_registrations() noexcept
{
    Result<void> result{};
    for_each_registration<System>([&]<typename Registration> {
        if (result) {
            result = initialize_registration<System, Registration>();
        }
    });
    return result;
}

template <typename System> [[nodiscard]] Result<void> validate_registrations() noexcept
{
    Result<void> result{};
    for_each_registration<System>([&]<typename Registration> {
        if (result) {
            result = validate_registration<System, Registration>();
        }
    });
    return result;
}

template <typename System> void activate_registrations() noexcept
{
    for_each_registration<System>([]<typename Registration> {
        if (auto result = activate_registration<System, Registration>(); !result) {
            record_runtime_failure<System, Registration>(status_of(result.error()));
        }
    });
}

template <typename System> [[nodiscard]] Result<void> request_registrations_stop() noexcept
{
    for_each_registration<System>(
        []<typename Registration> { request_registration_stop<System, Registration>(); });
    return {};
}

template <typename System> [[nodiscard]] lifecycle::Containment contain_registrations() noexcept
{
    for_each_registration<System>(
        []<typename Registration> { begin_registration_containment<System, Registration>(); });

    const auto deadline = kernel::Deadline::after(
        kernel::Timeout::after(Milliseconds{CONFIG_SOLAR_EXECUTION_QUIESCENCE_TIMEOUT_MS}));
    std::size_t uncontained{};
    for_each_registration<System>([&]<typename Registration> {
        const bool contained = wait_registration_quiescent<System, Registration>(deadline);
        if (!contained) {
            using Target = resolved_target_t<System, Registration>;
            if constexpr (std::is_same_v<Target, SystemWorkQueue>) {
                ++uncontained;
                auto& state = registration_state<System, Registration>();
                state.uncontained.store(true, std::memory_order_release);
                state.mutate([](RegistrationRecord& record) {
                    record.availability = Availability::Failed;
                    record.last_status = Status::Timeout;
                    record.quiescent = false;
                });
                record_runtime_failure<System, Registration>(Status::Timeout);
            }
        }
    });
    runtime_state<System>().uncontained_system_registrations.store(uncontained,
                                                                   std::memory_order_release);
    return uncontained == 0 ? lifecycle::Containment{}
                            : lifecycle::Containment{.status = solar::Status::Timeout,
                                                     .contained = false,
                                                     .timed_out = true};
}

template <typename System, typename Executor> [[nodiscard]] Result<void> prepare_executor() noexcept
{
    using Policy = typename Executor::Policy;
    auto& state = executor_state<System, Executor>();
    using Components = typename System::Catalogs::template Of<component::Tag>;
    constexpr auto component_entry = Components::template entry<Executor>();
    std::size_t registration_count{};
    for_each_target_registration<System, Executor>([&]<typename> { ++registration_count; });
    state.mutate([&](ExecutorRecord& record) {
        record = {
            .component = component_entry.local_id,
            .stack_bytes = Policy::stack_bytes,
            .priority = static_cast<int>(Policy::priority),
            .work_timeout = Policy::work_timeout,
            .stop_timeout = Policy::stop_timeout,
            .last_status = Status::NotReady,
            .containment = ContainmentState::NotPrepared,
            .registration_count = registration_count,
            .yields_between_items = Policy::yields_between_items,
            .abort_on_timeout = Policy::abort_on_timeout,
            .initialized = true,
        };
    });
    const auto priority = kernel::Priority::template preemptive<Policy::priority>();
    const auto started = state.queue.start(kernel::WorkQueueConfiguration{
        .priority = priority,
        .name = nullptr,
        .no_yield = !Policy::yields_between_items,
        .essential = false,
        .work_timeout = Policy::work_timeout,
    });
    state.mutate([&](ExecutorRecord& record) {
        const auto status = started ? Status::Ok : status_of(started.error());
        record.last_status = status;
        record.started = started.has_value();
        record.accepting = false;
        record.thread = state.queue.thread_id();
        record.containment = started ? ContainmentState::Prepared : ContainmentState::NotPrepared;
    });
    return started;
}

template <typename System, typename Executor>
[[nodiscard]] Result<void> validate_executor() noexcept
{
    return executor_state<System, Executor>().queue.started()
               ? Result<void>{}
               : Result<void>{fail<solar::Error>({.status = solar::Status::NotReady})};
}

template <typename System, typename Executor> void activate_executor() noexcept
{
    executor_state<System, Executor>().mutate([](ExecutorRecord& record) {
        record.accepting = true;
        record.last_status = Status::Ok;
    });
}

template <typename System, typename Executor>
[[nodiscard]] Result<void> request_executor_stop() noexcept
{
    executor_state<System, Executor>().mutate([](ExecutorRecord& record) {
        record.accepting = false;
        record.draining = true;
    });
    return {};
}

template <typename System, typename Executor>
void mark_executor_registrations_contained(bool forced) noexcept
{
    for_each_target_registration<System, Executor>([&]<typename Registration> {
        auto& state = registration_state<System, Registration>();
        state.uncontained.store(false, std::memory_order_release);
        state.in_flight.store(false, std::memory_order_release);
        state.reset_pending();
        state.mutate([&](RegistrationRecord& record) {
            record.availability = Availability::Quiescent;
            record.active = false;
            record.armed = false;
            record.queued_now = false;
            record.in_flight = false;
            record.quiescent = true;
            if (forced) {
                record.last_status = Status::Cancelled;
            }
        });
    });
}

template <typename System, typename Executor>
[[nodiscard]] lifecycle::Containment contain_executor() noexcept
{
    using Policy = typename Executor::Policy;
    auto& state = executor_state<System, Executor>();
    const auto deadline = kernel::Deadline::after(kernel::Timeout::after(Policy::stop_timeout));
    bool registrations_quiescent = true;
    for_each_target_registration<System, Executor>([&]<typename Registration> {
        if (!wait_registration_quiescent<System, Registration>(deadline)) {
            registrations_quiescent = false;
        }
    });

    if (registrations_quiescent) {
        auto drained = state.queue.drain(true);
        if (drained) {
            state.mutate([](ExecutorRecord& record) {
                record.draining = false;
                record.plugged = true;
            });
            const auto stopped = state.queue.stop(deadline);
            if (stopped) {
                state.mutate([](ExecutorRecord& record) {
                    record.stopped = true;
                    record.plugged = false;
                    record.last_status = Status::Ok;
                    record.containment = ContainmentState::Clean;
                });
                mark_executor_registrations_contained<System, Executor>(false);
                return {};
            }
            registrations_quiescent = false;
        } else {
            registrations_quiescent = false;
        }
    }

    state.mutate([](ExecutorRecord& record) { record.timed_out = true; });
    if constexpr (Policy::abort_on_timeout) {
        state.mutate([](ExecutorRecord& record) { record.abort_attempted = true; });
        const auto abort_result = state.queue.abort();
        const auto abort_status = abort_result ? Status::Ok : status_of(abort_result.error());
        const bool aborted = abort_result || abort_status == Status::Already;
        state.mutate([&](ExecutorRecord& record) {
            record.abort_succeeded = aborted;
            record.abort_failed = !aborted;
            record.stopped = aborted;
            record.last_status = aborted ? Status::Timeout : abort_status;
            record.containment = aborted ? ContainmentState::Forced : ContainmentState::Uncontained;
        });
        if (aborted) {
            mark_executor_registrations_contained<System, Executor>(true);
        }
        return {
            .status = aborted ? Status::Timeout : abort_status,
            .contained = aborted,
            .forced = aborted,
            .timed_out = true,
            .abort_attempted = true,
            .abort_failed = !aborted,
        };
    }

    state.mutate([](ExecutorRecord& record) {
        record.last_status = Status::Timeout;
        record.containment = ContainmentState::Uncontained;
    });
    return {.status = solar::Status::Timeout, .contained = false, .timed_out = true};
}

} // namespace solar::execution::detail
