#include <type_traits>

#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct Application;

struct Configured
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "policy.configured"};
    static constexpr Value default_value = 2;
};

struct Explicit
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "policy.explicit"};
    static constexpr Value default_value = 1;
    using Validation = solar::parameters::OneOf<1, 3>;
    using Access = solar::parameters::ReadWrite;
    using External = solar::parameters::LocalOnly;
    using Storage = solar::parameters::Atomic;
    using LoadFailure = solar::parameters::load::UseDefaultAndReport;
};

using System = solar::System<solar::Blueprint<
    solar::Parameters<Configured, Explicit>,
    solar::parameters::Configuration<
        solar::parameters::DefaultValidation<solar::parameters::Range<0, 10>>,
        solar::parameters::DefaultAccess<solar::parameters::ReadOnly>,
        solar::parameters::DefaultExternal<solar::parameters::ExternallyWritable<>>,
        solar::parameters::DefaultStorage<solar::parameters::MutexStorage>>>>;

using ConfiguredPolicies = System::ParameterFacility::Policies<Configured>;
using ExplicitPolicies = System::ParameterFacility::Policies<Explicit>;

static_assert(std::is_same_v<ConfiguredPolicies::Validation, solar::parameters::Range<0, 10>>);
static_assert(std::is_same_v<ConfiguredPolicies::Access, solar::parameters::ReadOnly>);
static_assert(
    std::is_same_v<ConfiguredPolicies::External, solar::parameters::ExternallyWritable<>>);
static_assert(std::is_same_v<ConfiguredPolicies::Storage, solar::parameters::MutexStorage>);

static_assert(std::is_same_v<ExplicitPolicies::Validation, solar::parameters::OneOf<1, 3>>);
static_assert(std::is_same_v<ExplicitPolicies::Access, solar::parameters::ReadWrite>);
static_assert(std::is_same_v<ExplicitPolicies::External, solar::parameters::LocalOnly>);
static_assert(std::is_same_v<ExplicitPolicies::Storage, solar::parameters::Atomic>);
static_assert(
    std::is_same_v<ExplicitPolicies::LoadFailure, solar::parameters::load::UseDefaultAndReport>);

#if defined(CONFIG_SOLAR_PARAMETERS_DEFAULT_LOAD_FAIL_BOOT)
static_assert(std::is_same_v<ConfiguredPolicies::LoadFailure, solar::parameters::load::FailBoot>);
static_assert(std::is_same_v<System::ParameterFacility::PersistenceStopPolicy,
                             solar::parameters::stop::CancelPending>);
#else
static_assert(
    std::is_same_v<ConfiguredPolicies::LoadFailure, solar::parameters::load::UseDefaultAndReport>);
static_assert(std::is_same_v<System::ParameterFacility::PersistenceStopPolicy,
                             solar::parameters::stop::FlushDeferred>);
#endif

} // namespace fixture

SOLAR_BIND_SYSTEM_FOR(fixture::Application, fixture::System);

ZTEST(solar_parameters_policy, test_effective_policies_boot_normally)
{
    using Parameters = solar::parameters::Of<fixture::Application>;

    auto boot = fixture::System::boot<fixture::Application>();
    zassert_true(boot.has_value());

    auto configured = Parameters::get<fixture::Configured>();
    zassert_true(configured.has_value());
    zassert_equal(*configured, fixture::Configured::default_value);

    auto explicit_update = Parameters::set<fixture::Explicit>(3);
    zassert_true(explicit_update.has_value());
    auto snapshot = Parameters::snapshot<fixture::Configured, fixture::Explicit>();
    zassert_true(snapshot.has_value());
    zassert_equal(snapshot->get<fixture::Explicit>(), 3);
    zassert_equal(Parameters::descriptors().size(), 2);

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
}

ZTEST_SUITE(solar_parameters_policy, nullptr, nullptr, nullptr, nullptr, nullptr);
