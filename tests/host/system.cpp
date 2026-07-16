#include <cassert>
#include <type_traits>

#include "system_fixture.hpp"

namespace fixture = system_fixture;

using ExpectedBuiltins = solar::TypeList<fixture::SupportFacility, fixture::DemandFacility,
                                         fixture::AlwaysFacility, fixture::ExplicitFacility>;
using ExpectedComponents =
    solar::TypeList<fixture::Sensor, fixture::UserFacility, fixture::SupportFacility,
                    fixture::DemandFacility, fixture::AlwaysFacility, fixture::ExplicitFacility,
                    fixture::Controller, fixture::Worker>;

static_assert(fixture::RobotSystem::valid);
static_assert(fixture::EmptySystem::valid);
static_assert(std::is_same_v<fixture::RobotSystem::Builtins, ExpectedBuiltins>);
static_assert(std::is_same_v<fixture::RobotSystem::Components, ExpectedComponents>);
static_assert(
    std::is_same_v<fixture::RobotSystem::Components, fixture::ReorderedSystem::Components>);
static_assert(std::is_same_v<fixture::RobotSystem::Catalogs, fixture::ReorderedSystem::Catalogs>);
static_assert(std::is_same_v<solar::bound_system_t<>, fixture::RobotSystem>);
static_assert(
    std::is_same_v<solar::bound_system_t<fixture::TestApplication>, fixture::RobotSystem>);
static_assert(std::is_same_v<fixture::RobotSystem::graph::Category<fixture::Sensor>,
                             solar::category::Device>);
static_assert(std::is_same_v<fixture::RobotSystem::graph::Category<fixture::DemandFacility>,
                             solar::category::Facility>);
static_assert(std::is_same_v<fixture::RobotSystem::graph::Category<fixture::Controller>,
                             solar::category::Service>);
static_assert(std::is_same_v<fixture::RobotSystem::graph::Category<fixture::Worker>,
                             solar::category::Executor>);
static_assert(std::is_same_v<fixture::RobotSystem::graph::Dependencies<fixture::Controller>,
                             fixture::Controller::Dependencies::Entries>);
static_assert(fixture::RobotSystem::catalog::Of<catalog_fixture::alpha::Tag>::size == 3);
static_assert(fixture::RobotSystem::catalog::Of<catalog_fixture::beta::Tag>::size == 0);
static_assert(std::is_same_v<fixture::RobotSystem::configuration::Policy<
                                 catalog_fixture::alpha::Tag, fixture::StorageAxis>,
                             fixture::BufferedStorage>);
static_assert(std::is_same_v<fixture::RobotSystem::configuration::EffectivePolicy<
                                 catalog_fixture::alpha::Tag, fixture::StorageAxis, solar::NoPolicy,
                                 fixture::KconfigStorage>,
                             fixture::BufferedStorage>);
static_assert(std::is_same_v<fixture::RobotSystem::configuration::EffectivePolicy<
                                 catalog_fixture::alpha::Tag, fixture::StorageAxis,
                                 fixture::DeclarationStorage, fixture::KconfigStorage>,
                             fixture::DeclarationStorage>);
static_assert(std::is_same_v<fixture::RobotSystem::ExecutionRegistrations,
                             solar::TypeList<fixture::ControlJob>>);

using DefaultOperation = solar::frontend::Operation<fixture::ControlPolicy, fixture::ControlValue>;
using TaggedOperation = typename solar::frontend::Of<fixture::TestApplication>::template Operation<
    fixture::ControlPolicy, fixture::ControlValue>;
static_assert(
    std::is_same_v<decltype(DefaultOperation::call(1)), decltype(TaggedOperation::call(1))>);

int main()
{
    solar::frontend::reset_catalog_for_test<fixture::RobotSystem, fixture::ControlPolicy>();

    const auto before_binding = fixture::RelaxedClient::increment(1);
    assert(!before_binding);
    assert(before_binding.error() == solar::frontend::Error::NotReady);

    solar::frontend::bind_catalog<fixture::RobotSystem, fixture::ControlPolicy>();
    const auto inline_result = fixture::call_from_header_without_root(4);
    assert(inline_result && *inline_result == 4);

    const auto direct_result = DefaultOperation::call(3);
    assert(direct_result && *direct_result == 7);
    auto* canonical_state =
        &fixture::RobotSystem::StateSlot<fixture::ControlValue, fixture::ControlState, int>::value;
    assert(fixture::state_address_from_other_translation_unit() == canonical_state);

    const auto other_translation_unit = fixture::call_from_other_translation_unit(2);
    assert(other_translation_unit && *other_translation_unit == 9);

    using AbsentOperation =
        solar::frontend::Operation<fixture::ControlPolicy, fixture::AbsentValue>;
    const auto absent = AbsentOperation::call(1);
    assert(!absent);
    assert(absent.error() == solar::frontend::Error::NotRegistered);

    solar::frontend::bind_catalog<fixture::RobotSystem, fixture::ControlPolicy,
                                  fixture::TestApplication>();
    const auto tagged = TaggedOperation::call(1);
    assert(tagged && *tagged == 10);

    solar::frontend::bind_disabled<fixture::ControlPolicy, fixture::DisabledApplication>();
    using DisabledOperation =
        solar::frontend::Operation<fixture::ControlPolicy, fixture::ControlValue,
                                   fixture::DisabledApplication>;
    const auto disabled = DisabledOperation::call(1);
    assert(!disabled);
    assert(disabled.error() == solar::frontend::Error::Disabled);
}
