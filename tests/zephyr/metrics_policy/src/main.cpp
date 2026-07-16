#include <type_traits>

#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct Configured
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "policy.configured"};
};

struct Explicit : Configured
{
    using Concurrency = solar::metrics::concurrency::Atomic;
    using Overflow = solar::metrics::overflow::Saturate;
    using Numeric = solar::metrics::numeric::RejectNonFinite;
    using Timestamps = solar::metrics::timestamps::Enabled;
    static constexpr solar::metrics::Descriptor descriptor{.name = "policy.explicit"};
};

using System = solar::System<solar::Blueprint<
    solar::Metrics<Configured, Explicit>,
    solar::metrics::Configuration<
        solar::metrics::DefaultConcurrency<solar::metrics::concurrency::MutexProtected>,
        solar::metrics::DefaultOverflow<solar::metrics::overflow::Wrap>,
        solar::metrics::DefaultNumeric<solar::metrics::numeric::PreserveNonFinite>,
        solar::metrics::DefaultTimestamps<solar::metrics::timestamps::Disabled>>>>;

using ConfiguredPolicies = System::MetricFacility::Policies<Configured>;
using ExplicitPolicies = System::MetricFacility::Policies<Explicit>;

static_assert(
    std::is_same_v<ConfiguredPolicies::Concurrency, solar::metrics::concurrency::MutexProtected>);
static_assert(std::is_same_v<ConfiguredPolicies::Overflow, solar::metrics::overflow::Wrap>);
static_assert(
    std::is_same_v<ConfiguredPolicies::Numeric, solar::metrics::numeric::PreserveNonFinite>);
static_assert(std::is_same_v<ConfiguredPolicies::Timestamps, solar::metrics::timestamps::Disabled>);

static_assert(std::is_same_v<ExplicitPolicies::Concurrency, solar::metrics::concurrency::Atomic>);
static_assert(std::is_same_v<ExplicitPolicies::Overflow, solar::metrics::overflow::Saturate>);
static_assert(std::is_same_v<ExplicitPolicies::Numeric, solar::metrics::numeric::RejectNonFinite>);
static_assert(std::is_same_v<ExplicitPolicies::Timestamps, solar::metrics::timestamps::Enabled>);

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_metrics_policy, test_typed_precedence_boots_and_updates)
{
    zassert_true(fixture::System::boot().has_value());

    zassert_true(solar::metrics::add<fixture::Configured>(3).has_value());
    zassert_true(solar::metrics::add<fixture::Explicit>(4).has_value());
    zassert_equal(solar::metrics::get<fixture::Configured>()->value, 3);
    zassert_equal(solar::metrics::get<fixture::Explicit>()->value, 4);

    auto configured = solar::metrics::catalog::descriptor<fixture::Configured>();
    auto explicit_metric = solar::metrics::catalog::descriptor<fixture::Explicit>();
    zassert_equal(configured->get().concurrency, solar::metrics::ConcurrencyKind::MutexProtected);
    zassert_equal(configured->get().overflow, solar::metrics::OverflowKind::Wrap);
    zassert_false(configured->get().timestamped);
    zassert_equal(explicit_metric->get().concurrency, solar::metrics::ConcurrencyKind::Atomic);
    zassert_equal(explicit_metric->get().overflow, solar::metrics::OverflowKind::Saturate);
    zassert_true(explicit_metric->get().timestamped);

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(solar_metrics_policy, nullptr, nullptr, nullptr, nullptr, nullptr);
