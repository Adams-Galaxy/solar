#pragma once

#include "solar/lifecycle/protocol.hpp"
#include "solar/log/runtime.hpp"
#include "solar/system/frontend.hpp"

namespace solar::log::detail
{

template <Operation CaptureOperation> struct CaptureFrontend
{
    using CatalogTag = SourceTag;
    template <typename SourceT>
    using Signature = Result<Receipt, Error>(const CaptureRequest& request);

    template <typename System, typename SourceT>
    [[nodiscard]] static Result<Receipt, Error> invoke(const CaptureRequest& request) noexcept
    {
        return capture<System, SourceT>(request, CaptureOperation);
    }

    template <typename SourceT>
    [[nodiscard]] static Result<Receipt, Error> unavailable(frontend::Error error) noexcept
    {
        switch (error) {
        case frontend::Error::NotReady:
            return fail(Error{.status = Status::NotReady,
                              .reason = Reason::NotReady,
                              .operation = CaptureOperation});
        case frontend::Error::Disabled:
            return fail(Error{.status = Status::NotSupported,
                              .reason = Reason::Disabled,
                              .operation = CaptureOperation});
        case frontend::Error::NotRegistered:
            return fail(Error{.status = Status::NotFound,
                              .reason = Reason::NotRegistered,
                              .operation = CaptureOperation});
        }
        return fail(Error{.status = Status::Error,
                          .reason = Reason::InternalInvariant,
                          .operation = CaptureOperation});
    }
};

template <typename System, typename Application> void bind_frontends() noexcept
{
    if constexpr (available) {
        frontend::bind_catalog<System, CaptureFrontend<Operation::Capture>, Application>();
        frontend::bind_catalog<System, CaptureFrontend<Operation::TryCapture>, Application>();
        frontend::bind_catalog<System, CaptureFrontend<Operation::IsrCapture>, Application>();
    } else {
        frontend::bind_disabled<CaptureFrontend<Operation::Capture>, Application>();
        frontend::bind_disabled<CaptureFrontend<Operation::TryCapture>, Application>();
        frontend::bind_disabled<CaptureFrontend<Operation::IsrCapture>, Application>();
    }
}

} // namespace solar::log::detail

template <> struct solar::lifecycle::ApplicationBindingProtocol<solar::log::SourceTag>
{
    template <typename System, typename Application> static void bind() noexcept
    {
        solar::log::detail::bind_frontends<System, Application>();
    }
};

template <> struct solar::lifecycle::CatalogActivationProtocol<solar::log::SourceTag>
{
    template <typename System>
    static constexpr bool participates =
        solar::log::available && System::LogSourceCatalog::size != 0;

    template <typename> [[nodiscard]] static solar::Result<void> commit() noexcept
    {
        return {};
    }

    template <typename System>
    [[nodiscard]] static solar::lifecycle::Failure failure(solar::lifecycle::Operation operation,
                                                           solar::Status status) noexcept
    {
        using Facility = typename System::LogFacility;
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
        if constexpr (solar::log::available) {
            System::LogFacility::template activate_runtime<System>();
        }
    }
};
