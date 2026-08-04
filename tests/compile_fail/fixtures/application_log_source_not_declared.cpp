#define CONFIG_SOLAR_LOG 1
#define CONFIG_SOLAR_LOG_MAX_RECORD_BYTES 64
#define CONFIG_SOLAR_LOG_MAX_STRING_BYTES 32
#define CONFIG_SOLAR_LOG_MAX_HEXDUMP_BYTES 32

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
// application type before any device header that logs against it.
struct Application;

struct DeclaredDevice
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.declared"};
};

// Never added to Application::Devices below -- the omission under test.
struct UndeclaredDevice
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.undeclared"};

    static void log_something() noexcept
    {
        solar::log::For<Application>::info<UndeclaredDevice>("unreachable");
    }
};

struct Application
{
    using Contract = fixture::Contract;
    using Devices = solar::Devices<DeclaredDevice>;
};

} // namespace fixture

int main()
{
    fixture::UndeclaredDevice::log_something();
}
