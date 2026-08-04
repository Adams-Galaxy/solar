#define CONFIG_SOLAR_REMOTE 1
#define SOLAR_HAS_GENERATED_APPLICATION 1

#include <solar/application.hpp>

namespace fixture
{

struct Contract
{
    using Parameters = solar::TypeList<>;
    using Data = solar::TypeList<>;
    using Actions = solar::TypeList<>;
    using OutputStreams = solar::TypeList<>;
    using InputStreams = solar::TypeList<>;
    using Events = solar::TypeList<>;
    using Metrics = solar::TypeList<>;
};

// Forward-declared, as the real generated headers forward-declare the
// application type before any device/service header that references it.
struct Application;

// Deliberately has no RemoteLinks -- the omission under test. A real
// platform's console/link headers would normally be included here.
struct Platform
{};

struct Application
{
    using Contract = fixture::Contract;
    using Platform = fixture::Platform;
};

} // namespace fixture

template <> struct solar::application::GeneratedTraits<fixture::Application>
{
    static constexpr bool available = true;
    static constexpr bool requires_parameters = false;
    static constexpr bool requires_remote = true;
    static constexpr std::string_view project_name = "fixture";
    using Project = void;
    using Contract = fixture::Contract;
    using ParameterSchema = solar::parameters::Schema<>;
};

template <> struct solar::application::GeneratedRemoteTraits<fixture::Application>
{
    static constexpr bool available = true;
    // No Contract member: an application that requires Remote but has no
    // RemoteLinks never reaches the point of needing one, so this fixture
    // does not need to model it.
};

using Spec = solar::application::Specification<fixture::Application>;
static_assert(std::is_void_v<typename Spec::Composition> || true); // forces full instantiation

int main() {}
