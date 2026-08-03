#include <array>
#include <cassert>
#include <type_traits>

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

inline std::array<int, 8> lifecycle{};
inline std::size_t lifecycle_size{};
inline void record(int value)
{
    lifecycle[lifecycle_size++] = value;
}

struct ConventionalDevice
{
    static constexpr const char* name = "fixture.device";
    static solar::Result<void> init()
    {
        record(1);
        return {};
    }
    static solar::Result<void> start()
    {
        record(3);
        return {};
    }
    static solar::Result<void> stop()
    {
        record(5);
        return {};
    }
    static solar::Result<void> deinit()
    {
        record(7);
        return {};
    }
};

struct Service
{
    static constexpr const char* name = "fixture.service";
    using Endpoints = solar::Endpoints<>;
    static solar::Result<void> initialize()
    {
        record(2);
        return {};
    }
    static solar::Result<void> deinitialize()
    {
        record(6);
        return {};
    }
};

struct Application
{
    using Contract = fixture::Contract;
    using Devices = solar::Devices<ConventionalDevice>;
    using Services = solar::Services<solar::Run<Service, solar::Stack<4096>, solar::Priority<3>>>;
};

using Specification = solar::application::Specification<Application>;
using DeviceModule = solar::AsModule<ConventionalDevice>;
using Runner = solar::execution::ServiceRunner<Application, Service, 4096, 3>;
using Expected = solar::Compose<solar::Contract<Contract>, solar::Own<DeviceModule, Runner>,
                                solar::Components<Service>>;
static_assert(std::is_same_v<solar::application::Composition<Application>, Expected>);
static_assert(std::is_void_v<solar::application::Parameters<Application>>);
static_assert(std::is_same_v<solar::application::composition_t<Application>, Expected>);

} // namespace fixture

int main()
{
    using System = solar::System<fixture::Application>;
    assert(System::boot());
    assert(fixture::lifecycle_size == 3);
    assert(
        (fixture::lifecycle[0] == 1 && fixture::lifecycle[1] == 2 && fixture::lifecycle[2] == 3));
    assert(System::shutdown());
    assert(fixture::lifecycle_size == 6);
    assert(
        (fixture::lifecycle[3] == 5 && fixture::lifecycle[4] == 6 && fixture::lifecycle[5] == 7));
}
