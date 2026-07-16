#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct InitCount
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "init-count"};
};

struct Producer
{
    static constexpr solar::component::Descriptor descriptor{.name = "producer"};
    using Metrics = solar::metrics::Metrics<InitCount>;

    [[nodiscard]] static solar::Result<void> init() noexcept
    {
        auto updated = solar::metrics::inc<InitCount>();
        return updated ? solar::Result<void>{}
                       : solar::Result<void>{solar::fail(updated.error().status)};
    }
};

using System = solar::System<solar::Blueprint<solar::Facilities<Producer>>>;
using EmptySystem = solar::System<solar::Blueprint<>>;

static_assert(!solar::contains_v<EmptySystem::MetricFacility, typename EmptySystem::Builtins>);

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_metrics_availability, test_binding_and_lifecycle_windows)
{
    auto before_boot = solar::metrics::inc<fixture::InitCount>();
    zassert_false(before_boot.has_value());
    zassert_equal(before_boot.error().reason, solar::metrics::Reason::NotReady);

    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    auto after_init = solar::metrics::get<fixture::InitCount>();
    zassert_true(after_init.has_value());
    zassert_equal(after_init->value, 1);

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
    auto after_stop = solar::metrics::get<fixture::InitCount>();
    zassert_false(after_stop.has_value());
    zassert_equal(after_stop.error().reason, solar::metrics::Reason::NotReady);
}

ZTEST_SUITE(solar_metrics_availability, nullptr, nullptr, nullptr, nullptr, nullptr);
