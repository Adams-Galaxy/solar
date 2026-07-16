#pragma once

#include <concepts>
#include <type_traits>

#include "solar/inspection/types.hpp"

namespace solar::inspection
{

template <typename Collection>
concept CollectionType =
    requires {
        typename Collection::Record;
        typename Collection::Query;
        { Collection::descriptor } -> std::convertible_to<const Descriptor&>;
    } && std::is_trivially_destructible_v<typename Collection::Record> &&
    std::is_trivially_destructible_v<typename Collection::Query>;

template <typename Collection> struct Provider
{};

template <typename Record> struct TextFormatter
{};

} // namespace solar::inspection
