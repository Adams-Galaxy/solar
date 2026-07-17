#pragma once

#include <algorithm>
#include <functional>
#include <span>

#include "solar/inspection/provider.hpp"
#include "solar/system/binding.hpp"

namespace solar::inspection
{

namespace detail
{

template <typename System> struct DescriptorAccess
{
    using Catalog = typename System::InspectionCatalog;

    [[nodiscard]] static constexpr std::span<const DescriptorView> all() noexcept
    {
        return Catalog::descriptors();
    }
};

template <typename System, typename Visitor, typename... Entries>
[[nodiscard]] Result<void, Error> visit_entry(LocalId collection, Visitor&& visitor,
                                              TypeList<Entries...>)
{
    bool matched{};
    ((collection == Entries::local_id
          ? (std::invoke(std::forward<Visitor>(visitor),
                         std::type_identity<typename Entries::Declaration>{}),
             matched = true, void())
          : void()),
     ...);
    if (!matched) {
        return fail<Error>({.status = solar::Status::NotFound,
                            .reason = Reason::NotFound,
                            .operation = Operation::Visit,
                            .collection = collection});
    }
    return {};
}

} // namespace detail

template <typename Application = DefaultApplication> struct Of
{
    using System = bound_system_t<Application>;
    using Catalog = typename System::InspectionCatalog;

    [[nodiscard]] static constexpr std::span<const DescriptorView> collections() noexcept
    {
        return detail::DescriptorAccess<System>::all();
    }

    template <CollectionType Collection>
    [[nodiscard]] static constexpr const DescriptorView& describe() noexcept
    {
        static_assert(Catalog::template contains<Collection>,
                      "SOLAR_DIAGNOSTIC_INSPECTION_COLLECTION_NOT_REGISTERED: collection is "
                      "absent from the bound Inspection catalog");
        return Catalog::descriptors()[Catalog::template Entry<Collection>::local_id.index()];
    }

    [[nodiscard]] static Result<std::reference_wrapper<const DescriptorView>, Error>
    find(LocalId collection) noexcept
    {
        const auto descriptors = collections();
        if (!collection.valid() || collection.index() >= descriptors.size()) {
            return fail<Error>({.status = solar::Status::NotFound,
                                .reason = Reason::NotFound,
                                .operation = Operation::Find,
                                .collection = collection});
        }
        return std::cref(descriptors[collection.index()]);
    }

    [[nodiscard]] static Result<std::reference_wrapper<const DescriptorView>, Error>
    find(Id stable_id) noexcept
    {
        const auto descriptors = collections();
        const auto found =
            std::find_if(descriptors.begin(), descriptors.end(), [&](const auto& value) {
                return value.descriptor.stable_id == stable_id;
            });
        if (found == descriptors.end()) {
            return fail<Error>({.status = solar::Status::NotFound,
                                .reason = Reason::NotFound,
                                .operation = Operation::Find});
        }
        return std::cref(*found);
    }

    template <CollectionType Collection>
    [[nodiscard]] static Result<PageResult, Error>
    query(const typename Collection::Query& request,
          std::span<typename Collection::Record> destination) noexcept
    {
        static_assert(Catalog::template contains<Collection>,
                      "SOLAR_DIAGNOSTIC_INSPECTION_COLLECTION_NOT_REGISTERED: queried collection "
                      "is absent from the bound Inspection catalog");
        constexpr auto collection = Catalog::template Entry<Collection>::local_id;
        if (request.page.cursor.collection.valid() &&
            request.page.cursor.collection != collection) {
            return fail<Error>({.status = solar::Status::Invalid,
                                .reason = Reason::StaleCursor,
                                .operation = Operation::Query,
                                .collection = collection});
        }
        const auto requested = request.page.limit == 0 ? destination.size() : request.page.limit;
        if (requested > Collection::descriptor.maximum_page) {
            return fail<Error>({.status = solar::Status::NoSpace,
                                .reason = Reason::NoSpace,
                                .operation = Operation::Query,
                                .collection = collection,
                                .detail = static_cast<std::uint32_t>(requested)});
        }
        const auto capacity = (std::min)(requested, destination.size());
        auto result = detail::query_provider<System, Collection>(
            request, destination.first(capacity), collection);
        if (!result) {
            return result;
        }
        if (result->written > capacity) {
            return fail<Error>({.status = solar::Status::Error,
                                .reason = Reason::SourceFailed,
                                .operation = Operation::Query,
                                .collection = collection,
                                .detail = static_cast<std::uint32_t>(result->written)});
        }
        if (!result->next.collection.valid()) {
            result->next.collection = collection;
        }
        return result;
    }

    template <typename Visitor>
    [[nodiscard]] static Result<void, Error> visit(LocalId collection, Visitor&& visitor)
    {
        return detail::visit_entry<System>(collection, std::forward<Visitor>(visitor),
                                           typename Catalog::EntryTypes{});
    }
};

template <typename Application = DefaultApplication>
[[nodiscard]] constexpr std::span<const DescriptorView> collections() noexcept
{
    return Of<Application>::collections();
}

template <CollectionType Collection, typename Application = DefaultApplication>
[[nodiscard]] constexpr const DescriptorView& describe() noexcept
{
    return Of<Application>::template describe<Collection>();
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<std::reference_wrapper<const DescriptorView>, Error>
find(LocalId collection) noexcept
{
    return Of<Application>::find(collection);
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<std::reference_wrapper<const DescriptorView>, Error>
find(Id stable_id) noexcept
{
    return Of<Application>::find(stable_id);
}

template <CollectionType Collection, typename Application = DefaultApplication>
[[nodiscard]] Result<PageResult, Error>
query(const typename Collection::Query& request,
      std::span<typename Collection::Record> destination) noexcept
{
    return Of<Application>::template query<Collection>(request, destination);
}

template <typename Visitor, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> visit(LocalId collection, Visitor&& visitor)
{
    return Of<Application>::visit(collection, std::forward<Visitor>(visitor));
}

} // namespace solar::inspection
