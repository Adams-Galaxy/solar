#include <solar/solar.hpp>

struct Registered
{
    static constexpr solar::component::Descriptor descriptor{.name = "registered"};
};

struct Unregistered
{
    static constexpr solar::log::SourceDescriptor descriptor{.name = "unregistered"};
};

struct UnregisteredDomain
{
    static constexpr solar::log::DomainDescriptor descriptor{.name = "unregistered-domain"};
};

struct InvalidSink
{
};

using CompileSystem = solar::System<solar::Blueprint<
    solar::Facilities<Registered>,
#if SOLAR_FAIL_CASE == 4
    solar::log::Configuration<solar::log::Sinks<solar::log::To<InvalidSink>>>
#else
    solar::log::Configuration<solar::log::Sinks<solar::log::To<solar::log::RetainedHistory>>>
#endif
    >>;

SOLAR_BIND_SYSTEM(CompileSystem);

#if SOLAR_FAIL_CASE == 1
auto result = solar::log::info<Unregistered>("bad source");
#elif SOLAR_FAIL_CASE == 2
auto result = solar::log::info<Registered>("missing {} argument");
#elif SOLAR_FAIL_CASE == 3
auto result = solar::log::info<Registered, UnregisteredDomain>("bad domain");
#elif SOLAR_FAIL_CASE == 4
static_assert(CompileSystem::valid);
#endif

int main()
{
    return 0;
}
