#include <zephyr/ztest.h>

#include <solar/metrics.hpp>
#include <solar/solar.hpp>

namespace fixture
{

struct DisabledCount
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "disabled-count"};
};

using System = solar::System<solar::Blueprint<>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_metrics_disabled, test_disabled_frontends_are_explicit_and_storage_free)
{
    static_assert(!solar::metrics::enabled);

    auto before_boot = solar::metrics::inc<fixture::DisabledCount>();
    zassert_false(before_boot.has_value());
    zassert_equal(before_boot.error().reason, solar::metrics::Reason::NotReady);

    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    auto disabled = solar::metrics::inc<fixture::DisabledCount>();
    zassert_false(disabled.has_value());
    zassert_equal(disabled.error().reason, solar::metrics::Reason::Disabled);
    zassert_true(solar::metrics::catalog::descriptors().empty());

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(solar_metrics_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
