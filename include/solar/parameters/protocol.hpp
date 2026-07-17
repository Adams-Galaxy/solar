#pragma once

#include "solar/lifecycle/protocol.hpp"
#include "solar/parameters/runtime.hpp"
#include "solar/system/frontend.hpp"

namespace solar::parameters::detail
{

[[nodiscard]] constexpr Error frontend_error(frontend::Error error, Operation operation) noexcept
{
    switch (error) {
    case frontend::Error::NotReady:
        return {
            .status = solar::Status::NotReady, .reason = Reason::NotReady, .operation = operation};
    case frontend::Error::Disabled:
        return {.status = solar::Status::NotSupported,
                .reason = Reason::Disabled,
                .operation = operation};
    case frontend::Error::NotRegistered:
        return {.status = solar::Status::NotFound,
                .reason = Reason::NotRegistered,
                .operation = operation};
    }
    return {.status = solar::Status::Error,
            .reason = Reason::InternalInvariant,
            .operation = operation};
}

template <bool Try> struct GetFrontend
{
    using CatalogTag = Tag;
    template <typename ParameterT> using Signature = Result<typename ParameterT::Value, Error>();

    template <typename System, typename ParameterT>
    [[nodiscard]] static Result<typename ParameterT::Value, Error> invoke() noexcept
    {
        return read_parameter<System, ParameterT>(Try);
    }

    template <typename ParameterT>
    [[nodiscard]] static Result<typename ParameterT::Value, Error>
    unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Try ? Operation::TryGet : Operation::Get));
    }
};

template <bool Try> struct SetFrontend
{
    using CatalogTag = Tag;
    template <typename ParameterT>
    using Signature = Result<Update<ParameterT>, Error>(typename ParameterT::Value);

    template <typename System, typename ParameterT>
    [[nodiscard]] static Result<Update<ParameterT>, Error>
    invoke(typename ParameterT::Value value) noexcept
    {
        using Policies = typename System::ParameterFacility::template Policies<ParameterT>;
        using Access = AccessTraits<typename Policies::Access>;
        if constexpr (!Access::writable) {
            return fail<Error>(make_error<System, ParameterT>(
                Try ? Operation::TrySet : Operation::Set, Status::NotSupported, Reason::ReadOnly));
        } else if constexpr (Access::privileged) {
            return fail<Error>(make_error<System, ParameterT>(
                Try ? Operation::TrySet : Operation::Set, Status::PermissionDenied,
                Reason::PrivilegeRequired));
        } else {
            return set_parameter<System, ParameterT>(std::move(value), Try, UpdateOrigin::LocalSet,
                                                     false);
        }
    }

    template <typename System, typename ParameterT> static consteval void validate()
    {
        using Policies = typename System::ParameterFacility::template Policies<ParameterT>;
        static_assert(AccessTraits<typename Policies::Access>::writable,
                      "SOLAR_DIAGNOSTIC_PARAMETER_READ_ONLY: ordinary mutation is unavailable "
                      "for a read-only parameter");
        static_assert(!AccessTraits<typename Policies::Access>::privileged,
                      "SOLAR_DIAGNOSTIC_PARAMETER_PRIVILEGE_REQUIRED: parameter mutation "
                      "requires its declared authority token");
    }

    template <typename ParameterT>
    [[nodiscard]] static Result<Update<ParameterT>, Error>
    unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Try ? Operation::TrySet : Operation::Set));
    }
};

struct ResetFrontend
{
    using CatalogTag = Tag;
    template <typename ParameterT> using Signature = Result<Update<ParameterT>, Error>();

    template <typename System, typename ParameterT>
    [[nodiscard]] static Result<Update<ParameterT>, Error> invoke() noexcept
    {
        using Policies = typename System::ParameterFacility::template Policies<ParameterT>;
        using Access = AccessTraits<typename Policies::Access>;
        if constexpr (!Access::writable) {
            return fail<Error>(make_error<System, ParameterT>(
                Operation::Reset, Status::NotSupported, Reason::ReadOnly));
        } else if constexpr (Access::privileged) {
            return fail<Error>(make_error<System, ParameterT>(
                Operation::Reset, Status::PermissionDenied, Reason::PrivilegeRequired));
        } else {
            return set_parameter<System, ParameterT>(
                typename ParameterT::Value{ParameterT::default_value}, false, UpdateOrigin::Reset,
                true);
        }
    }

    template <typename System, typename ParameterT> static consteval void validate()
    {
        using Policies = typename System::ParameterFacility::template Policies<ParameterT>;
        static_assert(AccessTraits<typename Policies::Access>::writable &&
                          !AccessTraits<typename Policies::Access>::privileged,
                      "SOLAR_DIAGNOSTIC_PARAMETER_RESET_ACCESS: reset requires ordinary writable "
                      "parameter access");
    }

    template <typename ParameterT>
    [[nodiscard]] static Result<Update<ParameterT>, Error>
    unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Reset));
    }
};

struct RecordFrontend
{
    using CatalogTag = Tag;
    template <typename ParameterT> using Signature = Result<ParameterRecord<ParameterT>, Error>();

    template <typename System, typename ParameterT>
    [[nodiscard]] static Result<ParameterRecord<ParameterT>, Error> invoke() noexcept
    {
        return parameter_record<System, ParameterT>();
    }

    template <typename ParameterT>
    [[nodiscard]] static Result<ParameterRecord<ParameterT>, Error>
    unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Query));
    }
};

template <bool Try> struct SaveFrontend
{
    using CatalogTag = Tag;
    template <typename ParameterT> using Signature = Result<void, Error>();

    template <typename System, typename ParameterT>
    [[nodiscard]] static Result<void, Error> invoke() noexcept
    {
        return save_parameter<System, ParameterT>(Try);
    }

    template <typename System, typename ParameterT> static consteval void validate()
    {
        using Policies = typename System::ParameterFacility::template Policies<ParameterT>;
        using Persistence = PersistenceTraits<typename Policies::Persistence>;
        static_assert(Persistence::persistent,
                      "SOLAR_DIAGNOSTIC_PARAMETER_SAVE_VOLATILE: volatile parameters cannot be "
                      "saved");
    }

    template <typename ParameterT>
    [[nodiscard]] static Result<void, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Save));
    }
};

template <typename System, typename Application> void bind_parameter_frontends() noexcept
{
    if constexpr (enabled) {
        frontend::bind_catalog<System, GetFrontend<false>, Application>();
        frontend::bind_catalog<System, GetFrontend<true>, Application>();
        frontend::bind_catalog<System, SetFrontend<false>, Application>();
        frontend::bind_catalog<System, SetFrontend<true>, Application>();
        frontend::bind_catalog<System, ResetFrontend, Application>();
        frontend::bind_catalog<System, RecordFrontend, Application>();
        frontend::bind_catalog<System, SaveFrontend<false>, Application>();
        frontend::bind_catalog<System, SaveFrontend<true>, Application>();
    } else {
        frontend::bind_disabled<GetFrontend<false>, Application>();
        frontend::bind_disabled<GetFrontend<true>, Application>();
        frontend::bind_disabled<SetFrontend<false>, Application>();
        frontend::bind_disabled<SetFrontend<true>, Application>();
        frontend::bind_disabled<ResetFrontend, Application>();
        frontend::bind_disabled<RecordFrontend, Application>();
        frontend::bind_disabled<SaveFrontend<false>, Application>();
        frontend::bind_disabled<SaveFrontend<true>, Application>();
    }
}

} // namespace solar::parameters::detail

template <> struct solar::lifecycle::ApplicationBindingProtocol<solar::parameters::Tag>
{
    template <typename System, typename Application> static void bind() noexcept
    {
        solar::parameters::detail::bind_parameter_frontends<System, Application>();
    }
};

template <> struct solar::lifecycle::CatalogActivationProtocol<solar::parameters::ChangeTag>
{
    template <typename System>
    static constexpr bool participates = System::ParameterChangeCatalog::size != 0;

    template <typename System> [[nodiscard]] static solar::Result<void> commit() noexcept
    {
        return System::ParameterFacility::template activate_changes<System>();
    }

    template <typename System>
    [[nodiscard]] static solar::lifecycle::Failure failure(solar::lifecycle::Operation operation,
                                                           solar::Status status) noexcept
    {
        using Facility = typename System::ParameterFacility;
        return {
            .component = System::Catalogs::template Of<solar::component::Tag>::template Entry<
                Facility>::local_id,
            .category = solar::lifecycle::ComponentCategory::Facility,
            .operation = operation,
            .status = status,
            .primary = true,
        };
    }

    template <typename> static void activate() noexcept {}
};

template <> struct solar::lifecycle::CatalogActivationProtocol<solar::parameters::Tag>
{
    template <typename System>
    static constexpr bool participates =
        solar::parameters::enabled && System::ParameterArchitecture::has_deferred;

    template <typename> [[nodiscard]] static solar::Result<void> commit() noexcept
    {
        return {};
    }

    template <typename System>
    [[nodiscard]] static solar::lifecycle::Failure failure(solar::lifecycle::Operation operation,
                                                           solar::Status status) noexcept
    {
        using Facility = typename System::ParameterFacility;
        return {
            .component = System::Catalogs::template Of<solar::component::Tag>::template Entry<
                Facility>::local_id,
            .category = solar::lifecycle::ComponentCategory::Facility,
            .operation = operation,
            .status = status,
            .primary = true,
        };
    }

    template <typename System> static void activate() noexcept
    {
        if constexpr (solar::parameters::enabled) {
            System::ParameterFacility::template activate_runtime<System>();
        }
    }
};
