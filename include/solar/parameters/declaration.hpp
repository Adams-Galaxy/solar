#pragma once

#include <concepts>
#include <type_traits>

#include "solar/parameters/catalog.hpp"
#include "solar/parameters/policy.hpp"

namespace solar::parameters
{

template <typename Declaration>
concept Parameter =
    requires {
        typename Declaration::Value;
        { descriptor_traits<Tag, Declaration>::descriptor } -> std::same_as<const Descriptor&>;
        { Declaration::default_value } -> std::convertible_to<typename Declaration::Value>;
    } && std::is_object_v<typename Declaration::Value> &&
    !std::is_reference_v<typename Declaration::Value> &&
    std::copy_constructible<typename Declaration::Value> &&
    std::is_copy_assignable_v<typename Declaration::Value> &&
    std::equality_comparable<typename Declaration::Value>;

} // namespace solar::parameters
