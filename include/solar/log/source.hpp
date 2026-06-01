#pragma once

#include <cstddef>

#include "solar/core.hpp"

namespace solar::log
{

/**
 * @brief Type-level list of valid category names for a log source.
 */
template <typename... CategoryNames>
struct Categories
{
    static constexpr std::size_t size = sizeof...(CategoryNames);
};

/**
 * @brief Named log source with an optional category vocabulary.
 */
template <typename NameT, typename CategoryList = Categories<>>
struct Source
{
    using Name = NameT;
    using CategoryListType = CategoryList;

    static constexpr const char *name()
    {
        return NameT::c_str();
    }
};

/**
 * @brief Default application log source used when no source is specified.
 */
struct DefaultSource : Source<solar::Name<"app">>
{
};

/**
 * @brief Empty category marker.
 */
struct NoCategory
{
    static constexpr const char *c_str()
    {
        return "";
    }
};

} // namespace solar::log
