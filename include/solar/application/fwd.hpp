#pragma once

#include <string_view>

namespace solar::parameters
{
template <typename... Declarations> struct Schema;
}
namespace solar::log
{
template <typename Application> struct For;
}

namespace solar::application
{

/** Generated defaults for one canonical application identity. */
template <typename Application> struct GeneratedTraits
{
    static constexpr bool available = false;
    static constexpr bool requires_parameters = false;
    static constexpr bool requires_remote = false;
    static constexpr std::string_view project_name{};
    using Project = void;
    using Contract = void;
    using ParameterSchema = parameters::Schema<>;
};

template <typename Application> struct GeneratedRemoteTraits
{
    static constexpr bool available = false;
};

} // namespace solar::application
