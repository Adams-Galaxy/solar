#pragma once

#include <solar/execution/registration.hpp>
#include <solar/system.hpp>

#include "system_api_fixture.hpp"

namespace system_fixture
{

template <typename... Declarations> struct AlphaSection
{};

struct SensorReading
{
    static constexpr catalog_fixture::alpha::Descriptor descriptor{
        .name = "system.sensor_reading",
        .stable_id = catalog_fixture::alpha::Id{0x2003},
    };
};

struct Sensor
{
    static constexpr solar::component::Descriptor descriptor{.name = "sensor"};
    using Alphas = catalog_fixture::alpha::Contribute<SensorReading>;
};

struct UserFacility
{
    static constexpr solar::component::Descriptor descriptor{.name = "user_facility"};
};

struct SupportFacility
{
    static constexpr solar::component::Descriptor descriptor{.name = "support_facility"};
};

struct DemandFacility
{
    static constexpr solar::component::Descriptor descriptor{.name = "demand_facility"};
};

struct AlwaysFacility
{
    static constexpr solar::component::Descriptor descriptor{.name = "always_facility"};
};

struct UnusedFacility
{
    static constexpr solar::component::Descriptor descriptor{.name = "unused_facility"};
};

struct ExplicitFacility
{
    static constexpr solar::component::Descriptor descriptor{.name = "explicit_facility"};
};

struct Controller
{
    static constexpr solar::component::Descriptor descriptor{.name = "controller"};
    using Dependencies = solar::Dependencies<Sensor, UserFacility, SupportFacility>;
    using Alphas = catalog_fixture::alpha::Contribute<ControlValue>;
};

struct Worker
{
    static constexpr solar::component::Descriptor descriptor{.name = "worker"};
};

struct ControlBehavior
{
    static void execute() {}
};

using ControlJob =
    solar::execution::OnDemand<"control-job", ControlBehavior, solar::execution::SystemWorkQueue>;

struct StorageAxis;
struct BufferedStorage;
struct KconfigStorage;
struct DeclarationStorage;

} // namespace system_fixture

template <typename... Declarations>
struct solar::section_traits<system_fixture::AlphaSection<Declarations...>>
{
    using Key = solar::section_key::Catalog<catalog_fixture::alpha::Tag>;
    using CatalogTag = catalog_fixture::alpha::Tag;
    using Entries = solar::TypeList<Declarations...>;
    static constexpr solar::SectionRole role = solar::SectionRole::SubsystemCatalog;
};

template <>
struct solar::subsystem_policy_traits<catalog_fixture::alpha::Tag, system_fixture::BufferedStorage>
{
    static constexpr bool recognized = true;
    using Axis = system_fixture::StorageAxis;
};

template <> struct solar::builtin_traits<system_fixture::SupportFacility>
{
    static constexpr bool enabled = true;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<>;

    template <typename CatalogSet> static constexpr bool demanded = false;
};

template <> struct solar::builtin_traits<system_fixture::DemandFacility>
{
    static constexpr bool enabled = true;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<system_fixture::SupportFacility>;

    template <typename CatalogSet>
    static constexpr bool demanded =
        CatalogSet::template Of<catalog_fixture::alpha::Tag>::size != 0;
};

template <> struct solar::builtin_traits<system_fixture::AlwaysFacility>
{
    static constexpr bool enabled = true;
    static constexpr bool always_present = true;
    using Requirements = solar::TypeList<>;

    template <typename CatalogSet> static constexpr bool demanded = false;
};

template <> struct solar::builtin_traits<system_fixture::UnusedFacility>
{
    static constexpr bool enabled = true;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<>;

    template <typename CatalogSet> static constexpr bool demanded = false;
};

template <> struct solar::builtin_traits<system_fixture::ExplicitFacility>
{
    static constexpr bool enabled = true;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<>;

    template <typename CatalogSet> static constexpr bool demanded = false;
};

namespace system_fixture
{

using RobotBlueprint = solar::Blueprint<
    solar::SubsystemConfiguration<catalog_fixture::alpha::Tag, BufferedStorage>,
    solar::Services<Controller>,
    solar::BuiltinCandidates<SupportFacility, DemandFacility, AlwaysFacility, UnusedFacility>,
    solar::Execution<ControlJob>, solar::Executors<Worker>,
    AlphaSection<catalog_fixture::AlphaDirect>, solar::Facilities<UserFacility>,
    solar::Builtins<ExplicitFacility>, solar::ExtensionTags<catalog_fixture::beta::Tag>,
    solar::Devices<Sensor>>;

using ReorderedBlueprint = solar::Blueprint<
    solar::Devices<Sensor>, solar::ExtensionTags<catalog_fixture::beta::Tag>,
    solar::Builtins<ExplicitFacility>, solar::Facilities<UserFacility>,
    AlphaSection<catalog_fixture::AlphaDirect>, solar::Executors<Worker>,
    solar::Execution<ControlJob>,
    solar::BuiltinCandidates<SupportFacility, DemandFacility, AlwaysFacility, UnusedFacility>,
    solar::Services<Controller>,
    solar::SubsystemConfiguration<catalog_fixture::alpha::Tag, BufferedStorage>>;

using RobotSystem = solar::System<RobotBlueprint>;
using ReorderedSystem = solar::System<ReorderedBlueprint>;
using EmptySystem = solar::System<solar::Blueprint<>>;

struct TestApplication;
struct DisabledApplication;

int* state_address_from_other_translation_unit();
solar::Result<int, solar::frontend::Error> call_from_other_translation_unit(int amount);
solar::Result<int, solar::frontend::Error> call_from_header_without_root(int amount);

} // namespace system_fixture

SOLAR_BIND_SYSTEM(system_fixture::RobotSystem);
SOLAR_BIND_SYSTEM_FOR(system_fixture::TestApplication, system_fixture::RobotSystem);
