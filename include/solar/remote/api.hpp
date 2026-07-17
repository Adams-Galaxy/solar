#pragma once

#include "solar/lifecycle/protocol.hpp"
#include "solar/remote/declaration.hpp"
#include "solar/remote/runtime.hpp"
#include "solar/system/frontend.hpp"

namespace solar::remote::detail
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

struct WriteDataFrontend
{
    using CatalogTag = DataTag;
    template <typename DataT> using Signature = Result<WriteReceipt, Error>(typename DataT::Value);

    template <typename System, typename DataT>
    [[nodiscard]] static Result<WriteReceipt, Error> invoke(typename DataT::Value value) noexcept
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        if constexpr (has_push_v<DataT>) {
            return write_data<System, DataT>(std::move(value));
        } else {
            (void)value;
            return fail<Error>(
                {Status::NotSupported, Reason::UnsupportedOperation, Operation::Publish});
        }
#else
        (void)value;
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::Publish));
#endif
    }

    template <typename System, typename DataT> static consteval void validate()
    {
        static_assert(System::RemoteDataCatalog::template contains<DataT>);
    }

    template <typename DataT>
    [[nodiscard]] static Result<WriteReceipt, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Publish));
    }
};

struct DataInterestFrontend
{
    using CatalogTag = DataTag;
    template <typename> using Signature = bool();

    template <typename System, typename DataT> [[nodiscard]] static bool invoke() noexcept
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        bool interested{};
        if constexpr (has_push_v<DataT>) {
            interested = interested || interested_in_data<System, DataT>();
        }
        if constexpr (has_loaned_v<DataT>) {
            interested = interested || loan_state<System, DataT>().interested_sessions.load(
                                           std::memory_order_acquire) != 0;
        }
        if constexpr (has_watch_v<DataT>) {
            interested = interested || watch_state<System, DataT>().interested_sessions.load(
                                           std::memory_order_acquire) != 0;
        }
        return interested;
#else
        return false;
#endif
    }

    template <typename System, typename DataT> static consteval void validate()
    {
        static_assert(System::RemoteDataCatalog::template contains<DataT>);
    }

    template <typename> [[nodiscard]] static bool unavailable(frontend::Error) noexcept
    {
        return false;
    }
};

struct PublishDataWatchFrontend
{
    using CatalogTag = DataTag;
    template <typename DataT> using Signature = Result<WriteReceipt, Error>(typename DataT::Value);

    template <typename System, typename DataT>
    [[nodiscard]] static Result<WriteReceipt, Error> invoke(typename DataT::Value value) noexcept
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        if constexpr (has_watch_v<DataT>) {
            return publish_watch<System, DataT>(std::move(value));
        }
#endif
        (void)value;
        return fail<Error>(
            {Status::NotSupported, Reason::UnsupportedOperation, Operation::Publish});
    }

    template <typename System, typename DataT> static consteval void validate()
    {
        static_assert(System::RemoteDataCatalog::template contains<DataT>);
    }

    template <typename DataT>
    [[nodiscard]] static Result<WriteReceipt, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Publish));
    }
};

struct PublishTopicFrontend
{
    using CatalogTag = TopicTag;
    template <typename TopicT>
    using Signature = Result<WriteReceipt, Error>(typename TopicT::Value);

    template <typename System, typename TopicT>
    [[nodiscard]] static Result<WriteReceipt, Error> invoke(typename TopicT::Value value) noexcept
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        return publish_topic<System, TopicT>(std::move(value));
#else
        (void)value;
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::Publish));
#endif
    }

    template <typename System, typename TopicT> static consteval void validate()
    {
        static_assert(System::RemoteTopicCatalog::template contains<TopicT>);
    }

    template <typename TopicT>
    [[nodiscard]] static Result<WriteReceipt, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Publish));
    }
};

struct TopicInterestFrontend
{
    using CatalogTag = TopicTag;
    template <typename> using Signature = bool();

    template <typename System, typename TopicT> [[nodiscard]] static bool invoke() noexcept
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        return topic_state<System, TopicT>().interested_sessions.load(std::memory_order_acquire) !=
               0;
#else
        return false;
#endif
    }

    template <typename System, typename TopicT> static consteval void validate()
    {
        static_assert(System::RemoteTopicCatalog::template contains<TopicT>);
    }

    template <typename> [[nodiscard]] static bool unavailable(frontend::Error) noexcept
    {
        return false;
    }
};

struct TryLoanFrontend
{
    using CatalogTag = DataTag;
    template <typename DataT> using Signature = Result<Loan<DataT>, Error>();

    template <typename System, typename DataT>
    [[nodiscard]] static Result<Loan<DataT>, Error> invoke() noexcept
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        if constexpr (has_loaned_v<DataT>) {
            return try_loan_data<System, DataT>();
        }
#endif
        return fail<Error>(
            {Status::NotSupported, Reason::UnsupportedOperation, Operation::Publish});
    }

    template <typename System, typename DataT> static consteval void validate()
    {
        static_assert(System::RemoteDataCatalog::template contains<DataT>);
    }

    template <typename DataT>
    [[nodiscard]] static Result<Loan<DataT>, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Publish));
    }
};

struct CommitLoanFrontend
{
    using CatalogTag = DataTag;
    template <typename DataT>
    using Signature = Result<WriteReceipt, Error>(Loan<DataT>&&, std::size_t);

    template <typename System, typename DataT>
    [[nodiscard]] static Result<WriteReceipt, Error> invoke(Loan<DataT>&& loan,
                                                            std::size_t size) noexcept
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
        if constexpr (has_loaned_v<DataT>) {
            return commit_loan_data<System, DataT>(std::move(loan), size);
        }
#endif
        (void)loan;
        (void)size;
        return fail<Error>(
            {Status::NotSupported, Reason::UnsupportedOperation, Operation::Publish});
    }

    template <typename System, typename DataT> static consteval void validate()
    {
        static_assert(System::RemoteDataCatalog::template contains<DataT>);
    }

    template <typename DataT>
    [[nodiscard]] static Result<WriteReceipt, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Publish));
    }
};

struct WriteDataFromIsrFrontend
{
    using CatalogTag = DataTag;
    template <typename DataT> using Signature = Result<WriteReceipt, Error>(typename DataT::Value);

    template <typename System, typename DataT>
    [[nodiscard]] static Result<WriteReceipt, Error> invoke(typename DataT::Value value) noexcept
    {
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE) &&                                         \
    defined(CONFIG_SOLAR_REMOTE_ISR_PUBLICATION)
        if constexpr (has_push_v<DataT>) {
            return write_data<System, DataT, true>(std::move(value));
        } else {
            (void)value;
            return fail<Error>(
                {Status::NotSupported, Reason::UnsupportedOperation, Operation::Publish});
        }
#else
        (void)value;
        return fail<Error>(frontend_error(frontend::Error::Disabled, Operation::Publish));
#endif
    }

    template <typename System, typename DataT> static consteval void validate()
    {
        static_assert(System::RemoteDataCatalog::template contains<DataT>);
        if constexpr (has_push_v<DataT>) {
            static_assert(std::is_trivially_copyable_v<typename DataT::Value> &&
                              std::is_trivially_destructible_v<typename DataT::Value>,
                          "SOLAR_DIAGNOSTIC_REMOTE_ISR_VALUE: ISR publication requires a "
                          "trivially copyable and trivially destructible Value");
        }
    }

    template <typename DataT>
    [[nodiscard]] static Result<WriteReceipt, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Publish));
    }
};

template <typename System, typename Application> void bind_remote_frontends() noexcept
{
    if constexpr (available) {
        frontend::bind_catalog<System, WriteDataFrontend, Application>();
        frontend::bind_catalog<System, DataInterestFrontend, Application>();
        frontend::bind_catalog<System, WriteDataFromIsrFrontend, Application>();
        frontend::bind_catalog<System, TryLoanFrontend, Application>();
        frontend::bind_catalog<System, CommitLoanFrontend, Application>();
        frontend::bind_catalog<System, PublishDataWatchFrontend, Application>();
    } else {
        frontend::bind_disabled<WriteDataFrontend, Application>();
        frontend::bind_disabled<DataInterestFrontend, Application>();
        frontend::bind_disabled<WriteDataFromIsrFrontend, Application>();
        frontend::bind_disabled<TryLoanFrontend, Application>();
        frontend::bind_disabled<CommitLoanFrontend, Application>();
        frontend::bind_disabled<PublishDataWatchFrontend, Application>();
    }
}

template <typename System, typename Application> void bind_remote_topic_frontends() noexcept
{
    if constexpr (available) {
        frontend::bind_catalog<System, PublishTopicFrontend, Application>();
        frontend::bind_catalog<System, TopicInterestFrontend, Application>();
    } else {
        frontend::bind_disabled<PublishTopicFrontend, Application>();
        frontend::bind_disabled<TopicInterestFrontend, Application>();
    }
}

} // namespace solar::remote::detail

namespace solar::remote
{

template <Data DataT>
[[nodiscard]] Result<WriteReceipt, Error> write(typename DataT::Value value) noexcept
{
    return frontend::Operation<detail::WriteDataFrontend, DataT>::call(std::move(value));
}

template <Data DataT>
[[nodiscard]] Result<WriteReceipt, Error> publish(typename DataT::Value value) noexcept
{
    return frontend::Operation<detail::PublishDataWatchFrontend, DataT>::call(std::move(value));
}

template <Topic TopicT>
[[nodiscard]] Result<WriteReceipt, Error> publish(typename TopicT::Value value) noexcept
{
    return frontend::Operation<detail::PublishTopicFrontend, TopicT>::call(std::move(value));
}

template <Data DataT> [[nodiscard]] bool interested() noexcept
{
    return frontend::Operation<detail::DataInterestFrontend, DataT>::call();
}

template <Topic TopicT> [[nodiscard]] bool interested() noexcept
{
    return frontend::Operation<detail::TopicInterestFrontend, TopicT>::call();
}

template <Data DataT>
[[nodiscard]] Result<WriteReceipt, Error> write_from_isr(typename DataT::Value value) noexcept
{
    return frontend::Operation<detail::WriteDataFromIsrFrontend, DataT>::call(std::move(value));
}

template <Data DataT> [[nodiscard]] Result<Loan<DataT>, Error> try_loan() noexcept
{
    return frontend::Operation<detail::TryLoanFrontend, DataT>::call();
}

template <Data DataT>
[[nodiscard]] Result<WriteReceipt, Error> commit(Loan<DataT>&& loan, std::size_t size) noexcept
{
    return frontend::Operation<detail::CommitLoanFrontend, DataT>::call(std::move(loan), size);
}

namespace records
{

template <typename Application = DefaultApplication> [[nodiscard]] ServiceRecord service() noexcept
{
    using System = bound_system_t<Application>;
    if constexpr (!System::RemoteArchitecture::demanded) {
        return {};
    } else {
        return System::RemoteService::record();
    }
}

template <remote::Link LinkT, typename Application = DefaultApplication>
[[nodiscard]] LinkRecord link() noexcept
{
    using System = bound_system_t<Application>;
    static_assert(System::RemoteLinkCatalog::template contains<LinkT>,
                  "SOLAR_DIAGNOSTIC_REMOTE_LINK_RECORD: queried link is absent from the bound "
                  "Remote catalog");
    constexpr auto index = static_cast<std::uint16_t>(
        System::RemoteLinkCatalog::template Entry<LinkT>::local_id.value);
    return System::RemoteService::template link_record<LinkT, index>();
}

template <typename Application = DefaultApplication> [[nodiscard]] auto links() noexcept
{
    using System = bound_system_t<Application>;
    return []<typename... LinkTypes>(TypeList<LinkTypes...>) {
        return std::array<LinkRecord, sizeof...(LinkTypes)>{link<LinkTypes, Application>()...};
    }(typename System::RemoteArchitecture::Links{});
}

} // namespace records

} // namespace solar::remote

template <> struct solar::lifecycle::ApplicationBindingProtocol<solar::remote::DataTag>
{
    template <typename System, typename Application> static void bind() noexcept
    {
        solar::remote::detail::bind_remote_frontends<System, Application>();
    }
};

template <> struct solar::lifecycle::ApplicationBindingProtocol<solar::remote::TopicTag>
{
    template <typename System, typename Application> static void bind() noexcept
    {
        solar::remote::detail::bind_remote_topic_frontends<System, Application>();
    }
};

template <> struct solar::lifecycle::CatalogActivationProtocol<solar::remote::DataTag>
{
    template <typename System>
    static constexpr bool participates = System::RemoteArchitecture::demanded;

    template <typename> [[nodiscard]] static solar::Result<void> commit() noexcept
    {
        return {};
    }

    template <typename System>
    [[nodiscard]] static solar::lifecycle::Failure failure(solar::lifecycle::Operation operation,
                                                           solar::Status status) noexcept
    {
        return {
            .component = System::Catalogs::template Of<solar::component::Tag>::template Entry<
                typename System::RemoteFacility>::local_id,
            .category = solar::lifecycle::ComponentCategory::Facility,
            .operation = operation,
            .status = status,
            .primary = true,
        };
    }

    template <typename System> static void activate() noexcept
    {
        if constexpr (System::RemoteArchitecture::demanded) {
            System::RemoteFacility::template activate_runtime<System>();
        }
    }
};
