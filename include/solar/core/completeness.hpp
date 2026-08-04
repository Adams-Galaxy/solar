#pragma once

#include <type_traits>

namespace solar
{

/**
 * True once T is a complete type at the point this is instantiated.
 *
 * Deferred (function- or class-template) instantiation happens once per
 * translation unit, generally no earlier than the end of it. A type that is
 * only forward-declared where some template is first named, but is defined
 * later in the same translation unit, is reported complete here once that
 * later definition has been seen -- exactly the timing self-referential,
 * whole-application metafunctions such as `application::Specification` rely
 * on to remain valid both as part of the real application translation unit
 * and when a single header naming them is compiled on its own.
 */
template <typename T, typename = void> struct is_complete : std::false_type
{};
template <typename T> struct is_complete<T, std::void_t<decltype(sizeof(T))>> : std::true_type
{};
template <typename T> inline constexpr bool is_complete_v = is_complete<T>::value;

} // namespace solar
