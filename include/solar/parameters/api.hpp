#pragma once

#include <array>
#include <functional>
#include <span>

#include "solar/parameters/protocol.hpp"

#if defined(__ZEPHYR__)
#include "solar/kernel/interrupt.hpp"
#endif

namespace solar::parameters
{

namespace detail
{

template <typename Application, Parameter ParameterT>
[[nodiscard]] Result<typename ParameterT::Value, Error> get_isr_for() noexcept
{
    using System = bound_system_t<Application>;
    using Facility = typename System::ParameterFacility;
    using Policies = typename Facility::template Policies<ParameterT>;
    static_assert(System::ParameterCatalog::template contains<ParameterT>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_NOT_REGISTERED: ISR-read parameter is absent from "
                  "the bound catalog");
    static_assert(std::is_same_v<typename Policies::Storage, storage::Atomic>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_ISR_REQUIRES_ATOMIC: ISR reads require explicit "
                  "always-lock-free atomic storage");
    if (!kernel::in_isr()) {
        return fail<Error>(make_error<System, ParameterT>(Operation::GetIsr, Status::Invalid,
                                                          Reason::InvalidContext));
    }
    if (!Facility::ready.load(std::memory_order_acquire)) {
        return fail<Error>(
            make_error<System, ParameterT>(Operation::GetIsr, Status::NotReady, Reason::NotReady));
    }
    return Facility::template slot<ParameterT>.read_isr();
}

template <typename Application, Parameter ParameterT, typename Authority>
[[nodiscard]] Result<Update<ParameterT>, Error> privileged_set(typename ParameterT::Value value,
                                                               const Authority&) noexcept
{
    using System = bound_system_t<Application>;
    using Policies = typename System::ParameterFacility::template Policies<ParameterT>;
    using Access = AccessTraits<typename Policies::Access>;
    static_assert(Access::privileged && std::is_same_v<Authority, typename Access::Authority>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_INVALID_AUTHORITY: authority does not match the "
                  "parameter's privileged access policy");
    return set_parameter<System, ParameterT>(std::move(value), false,
                                             UpdateOrigin::PrivilegedProvisioning, false);
}

template <typename Application, bool Try, Parameter... ParametersT>
[[nodiscard]] Result<Snapshot<ParametersT...>, Error> snapshot_for() noexcept
{
    using System = bound_system_t<Application>;
    if constexpr (!enabled) {
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::Snapshot));
    } else {
        static_assert((System::ParameterCatalog::template contains<ParametersT> && ...),
                      "SOLAR_DIAGNOSTIC_PARAMETER_SNAPSHOT_NOT_REGISTERED: snapshot contains an "
                      "unregistered parameter");
        static_assert(unique_types_v<TypeList<ParametersT...>>,
                      "SOLAR_DIAGNOSTIC_PARAMETER_SNAPSHOT_DUPLICATE: snapshot repeats a "
                      "parameter");
        return snapshot_parameters<System, ParametersT...>(Try);
    }
}

template <typename Application, bool Try, Parameter... ParametersT>
[[nodiscard]] Result<TransactionUpdate, Error>
set_all_for(std::tuple<Assignment<ParametersT>...> assignments) noexcept
{
    using System = bound_system_t<Application>;
    if constexpr (!enabled) {
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::Transaction));
    } else {
        return set_all_parameters<System>(std::move(assignments), Try);
    }
}

template <typename Application, bool Try>
[[nodiscard]] Result<PersistenceReport, Error> save_all_for() noexcept
{
    using System = bound_system_t<Application>;
    if constexpr (!enabled) {
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::SaveAll));
    } else {
        return save_all_parameters<System>(Try);
    }
}

template <typename Application, bool Try>
[[nodiscard]] Result<PersistenceReport, Error> flush_for() noexcept
{
    using System = bound_system_t<Application>;
    if constexpr (!enabled) {
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::Flush));
    } else {
        return flush_parameters<System>(Try);
    }
}

template <typename System, typename Entry> [[nodiscard]] consteval DescriptorView make_view()
{
    using ParameterT = typename Entry::Declaration;
    using Policies = typename System::ParameterFacility::template Policies<ParameterT>;
    constexpr auto encoded_size = [] {
        if constexpr (default_codec_available_v<ParameterT>) {
            return codec_for_t<ParameterT>::encoded_size;
        } else {
            return std::size_t{0};
        }
    }();
    return {
        .local_id = Entry::local_id,
        .descriptor = catalog::descriptor_for_view(descriptor_traits<Tag, ParameterT>::descriptor),
        .owner = Entry::owner_view(),
        .origin = Entry::origin_kind,
        .value_size = sizeof(typename ParameterT::Value),
        .value_alignment = alignof(typename ParameterT::Value),
        .storage = StorageTraits<typename Policies::Storage>::kind,
        .validation = ValidationTraits<typename Policies::Validation>::kind,
        .access = AccessTraits<typename Policies::Access>::kind,
        .external = ExternalTraits<typename Policies::External>::kind,
        .persistence = PersistenceTraits<typename Policies::Persistence>::kind,
        .encoded_size = encoded_size,
    };
}

template <typename System, typename Entries> struct DescriptorTable;

template <typename System, typename... Entries> struct DescriptorTable<System, TypeList<Entries...>>
{
    inline static constexpr std::array<DescriptorView, sizeof...(Entries)> values{
        make_view<System, Entries>()...};
};

template <typename Application> struct DescriptorAccess
{
    using System = bound_system_t<Application>;
    using Catalog = typename System::ParameterCatalog;
    using Table = DescriptorTable<System, typename Catalog::EntryTypes>;

    [[nodiscard]] static constexpr std::span<const DescriptorView> all() noexcept
    {
        return Table::values;
    }

    template <Parameter ParameterT>
    [[nodiscard]] static Result<std::reference_wrapper<const DescriptorView>, catalog::LookupError>
    one() noexcept
    {
        if constexpr (Catalog::template contains<ParameterT>) {
            return std::cref(Table::values[Catalog::template Entry<ParameterT>::local_id.index()]);
        } else {
            static_assert(!frontend::strict,
                          "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_PARAMETER_QUERY: queried "
                          "parameter is absent from the bound catalog");
            return fail<catalog::LookupError>(catalog::LookupError::Unavailable);
        }
    }
};

} // namespace detail

template <Parameter ParameterT>
[[nodiscard]] Result<typename ParameterT::Value, Error> get() noexcept
{
    return frontend::Operation<detail::GetFrontend<false>, ParameterT>::call();
}

template <Parameter ParameterT>
[[nodiscard]] Result<typename ParameterT::Value, Error> try_get() noexcept
{
    return frontend::Operation<detail::GetFrontend<true>, ParameterT>::call();
}

template <Parameter ParameterT>
[[nodiscard]] Result<typename ParameterT::Value, Error> get_isr() noexcept
{
    return detail::get_isr_for<DefaultApplication, ParameterT>();
}

template <Parameter ParameterT>
[[nodiscard]] Result<Update<ParameterT>, Error> set(typename ParameterT::Value value) noexcept
{
    using DeclaredAccess = typename detail::DeclaredAccess<ParameterT>::type;
    static_assert(
        [] {
            if constexpr (std::is_same_v<DeclaredAccess, NoPolicy>) {
                return true;
            } else {
                return detail::AccessTraits<DeclaredAccess>::writable &&
                       !detail::AccessTraits<DeclaredAccess>::privileged;
            }
        }(),
        "SOLAR_DIAGNOSTIC_PARAMETER_SET_ACCESS: ordinary set requires a writable, "
        "non-privileged declaration");
    return frontend::Operation<detail::SetFrontend<false>, ParameterT>::call(std::move(value));
}

template <Parameter ParameterT>
[[nodiscard]] Result<Update<ParameterT>, Error> try_set(typename ParameterT::Value value) noexcept
{
    using DeclaredAccess = typename detail::DeclaredAccess<ParameterT>::type;
    static_assert(
        [] {
            if constexpr (std::is_same_v<DeclaredAccess, NoPolicy>) {
                return true;
            } else {
                return detail::AccessTraits<DeclaredAccess>::writable &&
                       !detail::AccessTraits<DeclaredAccess>::privileged;
            }
        }(),
        "SOLAR_DIAGNOSTIC_PARAMETER_SET_ACCESS: ordinary try_set requires a writable, "
        "non-privileged declaration");
    return frontend::Operation<detail::SetFrontend<true>, ParameterT>::call(std::move(value));
}

template <Parameter ParameterT, typename Authority>
[[nodiscard]] Result<Update<ParameterT>, Error> set(typename ParameterT::Value value,
                                                    const Authority& authority) noexcept
{
    return detail::privileged_set<DefaultApplication, ParameterT>(std::move(value), authority);
}

template <Parameter ParameterT> [[nodiscard]] Result<Update<ParameterT>, Error> reset() noexcept
{
    using DeclaredAccess = typename detail::DeclaredAccess<ParameterT>::type;
    static_assert(
        [] {
            if constexpr (std::is_same_v<DeclaredAccess, NoPolicy>) {
                return true;
            } else {
                return detail::AccessTraits<DeclaredAccess>::writable &&
                       !detail::AccessTraits<DeclaredAccess>::privileged;
            }
        }(),
        "SOLAR_DIAGNOSTIC_PARAMETER_RESET_ACCESS: ordinary reset requires a writable, "
        "non-privileged declaration");
    return frontend::Operation<detail::ResetFrontend, ParameterT>::call();
}

template <Parameter ParameterT>
[[nodiscard]] Result<ParameterRecord<ParameterT>, Error> record() noexcept
{
    return frontend::Operation<detail::RecordFrontend, ParameterT>::call();
}

template <Parameter ParameterT> [[nodiscard]] Result<void, Error> save() noexcept
{
    using DeclaredPersistence = typename detail::DeclaredPersistence<ParameterT>::type;
    static_assert(
        [] {
            if constexpr (std::is_same_v<DeclaredPersistence, NoPolicy>) {
                return true;
            } else {
                using Persistence = detail::PersistenceTraits<DeclaredPersistence>;
                return Persistence::persistent;
            }
        }(),
        "SOLAR_DIAGNOSTIC_PARAMETER_SAVE_POLICY: typed save requires independent persistent "
        "storage");
    return frontend::Operation<detail::SaveFrontend<false>, ParameterT>::call();
}

template <Parameter ParameterT> [[nodiscard]] Result<void, Error> try_save() noexcept
{
    using DeclaredPersistence = typename detail::DeclaredPersistence<ParameterT>::type;
    static_assert(
        [] {
            if constexpr (std::is_same_v<DeclaredPersistence, NoPolicy>) {
                return true;
            } else {
                using Persistence = detail::PersistenceTraits<DeclaredPersistence>;
                return Persistence::persistent;
            }
        }(),
        "SOLAR_DIAGNOSTIC_PARAMETER_SAVE_POLICY: typed try_save requires independent "
        "persistent storage");
    return frontend::Operation<detail::SaveFrontend<true>, ParameterT>::call();
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<PersistenceReport, Error> save_all() noexcept
{
    return detail::save_all_for<Application, false>();
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<PersistenceReport, Error> try_save_all() noexcept
{
    return detail::save_all_for<Application, true>();
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<PersistenceReport, Error> flush() noexcept
{
    return detail::flush_for<Application, false>();
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<PersistenceReport, Error> try_flush() noexcept
{
    return detail::flush_for<Application, true>();
}

template <typename Observer, Parameter ParameterT, typename RouteTag = DefaultChangeRouteTag,
          typename Application = DefaultApplication>
[[nodiscard]] Result<ChangeRecord, Error> change_record() noexcept
{
    using System = bound_system_t<Application>;
    if constexpr (!enabled) {
        return fail<Error>(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
    } else {
        using Lookup = detail::FindChange<Observer, ParameterT, RouteTag,
                                          typename System::ParameterFacility::ChangeTypes>;
        if constexpr (Lookup::found) {
            return detail::change_record<System, typename Lookup::type>();
        } else {
            static_assert(!frontend::strict,
                          "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_PARAMETER_CHANGE_QUERY: queried "
                          "parameter change route is absent from the bound catalog");
            return fail<Error>({.status = solar::Status::NotFound,
                                .reason = Reason::NotRegistered,
                                .operation = Operation::Query});
        }
    }
}

template <Parameter... ParametersT>
[[nodiscard]] Result<Snapshot<ParametersT...>, Error> snapshot() noexcept
{
    return detail::snapshot_for<DefaultApplication, false, ParametersT...>();
}

template <Parameter... ParametersT>
[[nodiscard]] Result<Snapshot<ParametersT...>, Error> try_snapshot() noexcept
{
    return detail::snapshot_for<DefaultApplication, true, ParametersT...>();
}

template <Parameter... ParametersT>
[[nodiscard]] Result<TransactionUpdate, Error>
set_all(Assignment<ParametersT>... assignments) noexcept
{
    return detail::set_all_for<DefaultApplication, false>(
        std::tuple<Assignment<ParametersT>...>{std::move(assignments)...});
}

template <Parameter... ParametersT>
[[nodiscard]] Result<TransactionUpdate, Error>
try_set_all(Assignment<ParametersT>... assignments) noexcept
{
    return detail::set_all_for<DefaultApplication, true>(
        std::tuple<Assignment<ParametersT>...>{std::move(assignments)...});
}

template <typename Application = DefaultApplication>
[[nodiscard]] constexpr auto descriptors() noexcept
{
    return detail::DescriptorAccess<Application>::all();
}

template <Parameter ParameterT, typename Application = DefaultApplication>
[[nodiscard]] auto descriptor() noexcept
{
    return detail::DescriptorAccess<Application>::template one<ParameterT>();
}

template <typename Application> struct Of
{
    template <Parameter ParameterT>
    [[nodiscard]] static Result<typename ParameterT::Value, Error> get() noexcept
    {
        return frontend::Operation<detail::GetFrontend<false>, ParameterT, Application>::call();
    }

    template <Parameter ParameterT>
    [[nodiscard]] static Result<typename ParameterT::Value, Error> try_get() noexcept
    {
        return frontend::Operation<detail::GetFrontend<true>, ParameterT, Application>::call();
    }

    template <Parameter ParameterT>
    [[nodiscard]] static Result<typename ParameterT::Value, Error> get_isr() noexcept
    {
        return detail::get_isr_for<Application, ParameterT>();
    }

    template <Parameter ParameterT>
    [[nodiscard]] static Result<Update<ParameterT>, Error>
    set(typename ParameterT::Value value) noexcept
    {
        using DeclaredAccess = typename detail::DeclaredAccess<ParameterT>::type;
        static_assert(
            [] {
                if constexpr (std::is_same_v<DeclaredAccess, NoPolicy>) {
                    return true;
                } else {
                    return detail::AccessTraits<DeclaredAccess>::writable &&
                           !detail::AccessTraits<DeclaredAccess>::privileged;
                }
            }(),
            "SOLAR_DIAGNOSTIC_PARAMETER_SET_ACCESS: ordinary set requires a writable, "
            "non-privileged declaration");
        return frontend::Operation<detail::SetFrontend<false>, ParameterT, Application>::call(
            std::move(value));
    }

    template <Parameter ParameterT>
    [[nodiscard]] static Result<Update<ParameterT>, Error>
    try_set(typename ParameterT::Value value) noexcept
    {
        using DeclaredAccess = typename detail::DeclaredAccess<ParameterT>::type;
        static_assert(
            [] {
                if constexpr (std::is_same_v<DeclaredAccess, NoPolicy>) {
                    return true;
                } else {
                    return detail::AccessTraits<DeclaredAccess>::writable &&
                           !detail::AccessTraits<DeclaredAccess>::privileged;
                }
            }(),
            "SOLAR_DIAGNOSTIC_PARAMETER_SET_ACCESS: ordinary try_set requires a writable, "
            "non-privileged declaration");
        return frontend::Operation<detail::SetFrontend<true>, ParameterT, Application>::call(
            std::move(value));
    }

    template <Parameter ParameterT, typename Authority>
    [[nodiscard]] static Result<Update<ParameterT>, Error> set(typename ParameterT::Value value,
                                                               const Authority& authority) noexcept
    {
        return detail::privileged_set<Application, ParameterT>(std::move(value), authority);
    }

    template <Parameter ParameterT>
    [[nodiscard]] static Result<Update<ParameterT>, Error> reset() noexcept
    {
        return frontend::Operation<detail::ResetFrontend, ParameterT, Application>::call();
    }

    template <Parameter ParameterT>
    [[nodiscard]] static Result<ParameterRecord<ParameterT>, Error> record() noexcept
    {
        return frontend::Operation<detail::RecordFrontend, ParameterT, Application>::call();
    }

    template <Parameter ParameterT> [[nodiscard]] static Result<void, Error> save() noexcept
    {
        return frontend::Operation<detail::SaveFrontend<false>, ParameterT, Application>::call();
    }

    template <Parameter ParameterT> [[nodiscard]] static Result<void, Error> try_save() noexcept
    {
        return frontend::Operation<detail::SaveFrontend<true>, ParameterT, Application>::call();
    }

    template <Parameter... ParametersT>
    [[nodiscard]] static Result<Snapshot<ParametersT...>, Error> snapshot() noexcept
    {
        return detail::snapshot_for<Application, false, ParametersT...>();
    }

    template <Parameter... ParametersT>
    [[nodiscard]] static Result<Snapshot<ParametersT...>, Error> try_snapshot() noexcept
    {
        return detail::snapshot_for<Application, true, ParametersT...>();
    }

    template <Parameter... ParametersT>
    [[nodiscard]] static Result<TransactionUpdate, Error>
    set_all(Assignment<ParametersT>... assignments) noexcept
    {
        return detail::set_all_for<Application, false>(
            std::tuple<Assignment<ParametersT>...>{std::move(assignments)...});
    }

    template <Parameter... ParametersT>
    [[nodiscard]] static Result<TransactionUpdate, Error>
    try_set_all(Assignment<ParametersT>... assignments) noexcept
    {
        return detail::set_all_for<Application, true>(
            std::tuple<Assignment<ParametersT>...>{std::move(assignments)...});
    }

    [[nodiscard]] static Result<PersistenceReport, Error> save_all() noexcept
    {
        return detail::save_all_for<Application, false>();
    }

    [[nodiscard]] static Result<PersistenceReport, Error> try_save_all() noexcept
    {
        return detail::save_all_for<Application, true>();
    }

    [[nodiscard]] static Result<PersistenceReport, Error> flush() noexcept
    {
        return detail::flush_for<Application, false>();
    }

    [[nodiscard]] static Result<PersistenceReport, Error> try_flush() noexcept
    {
        return detail::flush_for<Application, true>();
    }

    [[nodiscard]] static constexpr auto descriptors() noexcept
    {
        return detail::DescriptorAccess<Application>::all();
    }

    template <Parameter ParameterT> [[nodiscard]] static auto descriptor() noexcept
    {
        return detail::DescriptorAccess<Application>::template one<ParameterT>();
    }

    template <typename Observer, Parameter ParameterT, typename RouteTag = DefaultChangeRouteTag>
    [[nodiscard]] static Result<ChangeRecord, Error> change_record() noexcept
    {
        return parameters::change_record<Observer, ParameterT, RouteTag, Application>();
    }
};

} // namespace solar::parameters
