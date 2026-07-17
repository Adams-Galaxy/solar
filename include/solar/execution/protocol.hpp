#pragma once

#include "solar/execution/runtime.hpp"

namespace solar::execution::detail
{

[[nodiscard]] constexpr Error frontend_error(frontend::Error error, Operation operation) noexcept
{
    switch (error) {
    case frontend::Error::NotReady:
        return {.status = solar::Status::NotReady,
                .reason = ErrorReason::SubsystemNotReady,
                .operation = operation};
    case frontend::Error::Disabled:
        return {.status = solar::Status::NotSupported,
                .reason = ErrorReason::SubsystemNotReady,
                .operation = operation};
    case frontend::Error::NotRegistered:
        return {.status = solar::Status::NotFound,
                .reason = ErrorReason::NotRegistered,
                .operation = operation};
    }
    return {.status = solar::Status::Error,
            .reason = ErrorReason::InternalInvariant,
            .operation = operation};
}

struct SubmitFrontend
{
    using CatalogTag = Tag;
    template <typename> using Signature = Result<Submission, Error>();

    template <typename System, typename Registration>
    [[nodiscard]] static Result<Submission, Error> invoke() noexcept
    {
        if constexpr (registration_traits<Registration>::kind == RegistrationKind::OnDemand) {
            return submit_registration<System, Registration>(false);
        } else {
            return fail<Error>(make_error<System, Registration>(
                Operation::Submit, Status::NotSupported, ErrorReason::UnsupportedOperation));
        }
    }

    [[nodiscard]] static Result<Submission, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Submit));
    }
};

struct SubmitIsrFrontend
{
    using CatalogTag = Tag;
    template <typename> using Signature = Result<Submission, Error>();

    template <typename System, typename Registration>
    [[nodiscard]] static Result<Submission, Error> invoke() noexcept
    {
        if constexpr (registration_traits<Registration>::kind == RegistrationKind::OnDemand) {
            return submit_registration<System, Registration>(true);
        } else {
            return fail<Error>(make_error<System, Registration>(
                Operation::SubmitIsr, Status::NotSupported, ErrorReason::UnsupportedOperation));
        }
    }

    [[nodiscard]] static Result<Submission, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::SubmitIsr));
    }
};

template <bool Replace> struct ScheduleFrontend
{
    using CatalogTag = Tag;
    template <typename> using Signature = Result<Submission, Error>(std::chrono::nanoseconds);

    template <typename System, typename Registration>
    [[nodiscard]] static Result<Submission, Error> invoke(std::chrono::nanoseconds delay) noexcept
    {
        if constexpr (registration_traits<Registration>::kind == RegistrationKind::Delayable) {
            return schedule_registration<System, Registration>(delay, Replace);
        } else {
            return fail<Error>(make_error<System, Registration>(
                Replace ? Operation::Reschedule : Operation::Schedule, Status::NotSupported,
                ErrorReason::UnsupportedOperation));
        }
    }

    [[nodiscard]] static Result<Submission, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(
            frontend_error(error, Replace ? Operation::Reschedule : Operation::Schedule));
    }
};

template <bool Synchronous> struct CancelFrontend
{
    using CatalogTag = Tag;
    template <typename> using Signature = Result<Cancellation, Error>();

    template <typename System, typename Registration>
    [[nodiscard]] static Result<Cancellation, Error> invoke() noexcept
    {
        return cancel_registration<System, Registration>(Synchronous);
    }

    [[nodiscard]] static Result<Cancellation, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(
            frontend_error(error, Synchronous ? Operation::CancelSync : Operation::Cancel));
    }
};

struct FlushFrontend
{
    using CatalogTag = Tag;
    template <typename> using Signature = Result<Cancellation, Error>();

    template <typename System, typename Registration>
    [[nodiscard]] static Result<Cancellation, Error> invoke() noexcept
    {
        return flush_registration<System, Registration>();
    }

    [[nodiscard]] static Result<Cancellation, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Flush));
    }
};

struct RecordFrontend
{
    using CatalogTag = Tag;
    template <typename> using Signature = Result<RegistrationRecord, Error>();

    template <typename System, typename Registration>
    [[nodiscard]] static Result<RegistrationRecord, Error> invoke() noexcept
    {
        return registration_state<System, Registration>().copy();
    }

    [[nodiscard]] static Result<RegistrationRecord, Error>
    unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Submit));
    }
};

template <typename System, typename Application = DefaultApplication>
void bind_execution_frontends() noexcept
{
    frontend::bind_catalog<System, SubmitFrontend, Application>();
    frontend::bind_catalog<System, SubmitIsrFrontend, Application>();
    frontend::bind_catalog<System, ScheduleFrontend<false>, Application>();
    frontend::bind_catalog<System, ScheduleFrontend<true>, Application>();
    frontend::bind_catalog<System, CancelFrontend<false>, Application>();
    frontend::bind_catalog<System, CancelFrontend<true>, Application>();
    frontend::bind_catalog<System, FlushFrontend, Application>();
    frontend::bind_catalog<System, RecordFrontend, Application>();
}

template <typename System>
[[nodiscard]] lifecycle::Failure protocol_failure(lifecycle::Operation operation,
                                                  Status status) noexcept
{
    auto& runtime = runtime_state<System>();
    auto guard = runtime.lock.acquire();
    auto failure = runtime.last_failure;
    failure.operation = operation;
    failure.status = status;
    return failure;
}

} // namespace solar::execution::detail

namespace solar::lifecycle
{

template <typename System>
    requires requires { typename System::ExecutionCatalog; }
struct SystemExecutionProtocol<System>
{
    static constexpr bool participates = System::ExecutionCatalog::size != 0;

    [[nodiscard]] static Result<void> prepare() noexcept
    {
        return execution::detail::prepare_registrations<System>();
    }

    [[nodiscard]] static Result<void> validate_activation() noexcept
    {
        return execution::detail::validate_registrations<System>();
    }

    static void activate() noexcept
    {
        execution::detail::activate_registrations<System>();
    }

    [[nodiscard]] static Result<void> request_stop() noexcept
    {
        return execution::detail::request_registrations_stop<System>();
    }

    [[nodiscard]] static Containment contain() noexcept
    {
        return execution::detail::contain_registrations<System>();
    }

    [[nodiscard]] static Failure failure(Operation operation, Status status) noexcept
    {
        return execution::detail::protocol_failure<System>(operation, status);
    }

    [[nodiscard]] static std::size_t uncontained_count() noexcept
    {
        return execution::detail::runtime_state<System>().uncontained_system_registrations.load(
            std::memory_order_acquire);
    }

    template <typename Visitor>
    static void visit_uncontained_dependencies(Visitor&& visitor) noexcept
    {
        execution::detail::for_each_registration<System>([&]<typename Registration> {
            using Target = execution::detail::resolved_target_t<System, Registration>;
            if constexpr (std::is_same_v<Target, execution::SystemWorkQueue>) {
                if (execution::detail::registration_state<System, Registration>().uncontained.load(
                        std::memory_order_acquire)) {
                    using Dependencies = typename execution::detail::RegistrationMetadata<
                        System, Registration>::Dependencies;
                    for_each_type<Dependencies>(visitor);
                }
            }
        });
    }
};

template <> struct ApplicationBindingProtocol<execution::Tag>
{
    template <typename System, typename Application> static void bind() noexcept
    {
        execution::detail::bind_execution_frontends<System, Application>();
    }
};

template <typename System, typename Component>
    requires(std::is_same_v<typename System::Effective::template CategoryOf<Component>,
                            category::Executor>)
struct ExecutionProtocol<System, Component>
{
    static_assert(execution::WorkQueueExecutor<Component>,
                  "SOLAR_DIAGNOSTIC_INVALID_EXECUTOR_COMPONENT: Executors<...> entries must "
                  "satisfy the execution executor contract");
    static constexpr bool participates = true;

    [[nodiscard]] static Result<void> prepare() noexcept
    {
        return execution::detail::prepare_executor<System, Component>();
    }

    [[nodiscard]] static Result<void> validate_activation() noexcept
    {
        return execution::detail::validate_executor<System, Component>();
    }

    static void activate() noexcept
    {
        execution::detail::activate_executor<System, Component>();
    }

    [[nodiscard]] static Result<void> request_stop() noexcept
    {
        return execution::detail::request_executor_stop<System, Component>();
    }

    [[nodiscard]] static Containment contain() noexcept
    {
        return execution::detail::contain_executor<System, Component>();
    }

    template <typename Visitor>
    static void visit_uncontained_dependencies(Visitor&& visitor) noexcept
    {
        if (execution::detail::executor_state<System, Component>().copy().containment !=
            execution::ContainmentState::Uncontained) {
            return;
        }
        execution::detail::for_each_target_registration<System, Component>(
            [&]<typename Registration> {
                using Dependencies =
                    typename execution::detail::RegistrationMetadata<System,
                                                                     Registration>::Dependencies;
                for_each_type<Dependencies>(visitor);
            });
    }
};

} // namespace solar::lifecycle
