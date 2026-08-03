#include <cassert>
#include <string_view>

#include <solar/module.hpp>

namespace external_example
{
struct Identity
{
    static constexpr std::string_view name = "example.counter";
};
struct Counter
{
    solar::Result<void> initialize() noexcept
    {
        value = 1;
        return {};
    }
    solar::Result<void> start() noexcept
    {
        running = true;
        return {};
    }
    solar::Result<void> stop() noexcept
    {
        running = false;
        return {};
    }
    int value{};
    bool running{};
};
struct Application;
using StaticCounter = solar::module::StaticOwner<Application, Identity, Counter>;
} // namespace external_example

int main()
{
    using external_example::StaticCounter;
    static_assert(solar::module::Identified<external_example::Identity>);
    assert(StaticCounter::initialize());
    assert(StaticCounter::start());
    StaticCounter::instance().value = 9;
    assert(StaticCounter::instance_const().value == 9);
    assert(&StaticCounter::instance() == &StaticCounter::storage);
    assert(StaticCounter::stop());
    assert(StaticCounter::deinitialize());
}
