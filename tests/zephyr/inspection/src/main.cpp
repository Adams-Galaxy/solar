#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct Samples
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "samples"};
};

struct CustomRecord
{
    std::uint32_t value{};
};

struct CustomCollection
{
    using Record = CustomRecord;
    using Query = solar::inspection::BasicQuery;

    static constexpr solar::inspection::Descriptor descriptor{
        .name = "fixture.custom",
        .description = "Custom application-owned records",
        .stable_id = solar::inspection::Id{0xF1700001U},
        .subsystem = solar::inspection::Subsystem::Custom,
        .capabilities =
            solar::inspection::capability(solar::inspection::OperationCapability::Query),
        .consistency_modes =
            solar::inspection::consistency(solar::inspection::Consistency::StablePage),
        .synchronization = solar::inspection::Synchronization::None,
        .context = solar::inspection::Context::Any,
        .cost = solar::inspection::Cost::LinearPage,
        .maximum_page = 4,
        .record_size = sizeof(Record),
        .query_size = sizeof(Query),
    };
};

struct Navigation
{
    using Metrics = solar::metrics::Metrics<Samples>;
    using Inspections = solar::inspection::Collections<CustomCollection>;

    static constexpr solar::component::Descriptor descriptor{.name = "navigation"};
};

using RobotSystem = solar::System<solar::Blueprint<solar::Facilities<Navigation>>>;

struct UnbootedSamples
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "unbooted.samples"};
};

struct UnbootedNavigation
{
    using Metrics = solar::metrics::Metrics<UnbootedSamples>;
    static constexpr solar::component::Descriptor descriptor{.name = "unbooted.navigation"};
};

struct UnbootedApplication
{};

using UnbootedSystem = solar::System<solar::Blueprint<solar::Facilities<UnbootedNavigation>>>;

} // namespace fixture

template <> struct solar::inspection::Provider<fixture::CustomCollection>
{
    static solar::Result<solar::inspection::PageResult, solar::inspection::Error>
    query(const fixture::CustomCollection::Query& query,
          std::span<fixture::CustomCollection::Record> output) noexcept
    {
        constexpr std::array records{fixture::CustomRecord{11}, fixture::CustomRecord{22},
                                     fixture::CustomRecord{33}};
        const auto offset = static_cast<std::size_t>(query.page.cursor.offset);
        if (offset > records.size()) {
            return solar::fail<solar::inspection::Error>({
                .status = solar::Status::Invalid,
                .reason = solar::inspection::Reason::InvalidRequest,
                .operation = solar::inspection::Operation::Query,
            });
        }
        const auto count = (std::min)(output.size(), records.size() - offset);
        std::copy_n(records.begin() + offset, count, output.begin());
        return solar::inspection::PageResult{
            .written = count,
            .next = {.offset = static_cast<std::uint32_t>(offset + count)},
            .has_more = offset + count < records.size(),
            .consistency = solar::inspection::PageConsistency::StablePage,
            .freshness = solar::inspection::Freshness::Current,
        };
    }
};

SOLAR_BIND_SYSTEM(fixture::RobotSystem);
SOLAR_BIND_SYSTEM_FOR(fixture::UnbootedApplication, fixture::UnbootedSystem);

static_assert(fixture::RobotSystem::InspectionCatalog::contains<solar::inspection::Components>);
static_assert(
    fixture::RobotSystem::InspectionCatalog::contains<solar::inspection::LifecycleComponents>);
static_assert(
    fixture::RobotSystem::InspectionCatalog::contains<solar::inspection::ExecutionRegistrations>);
static_assert(fixture::RobotSystem::InspectionCatalog::contains<solar::inspection::MetricValues>);
static_assert(fixture::RobotSystem::InspectionCatalog::contains<fixture::CustomCollection>);
static_assert(solar::contains_v<solar::inspection::Facility, fixture::RobotSystem::Builtins>);

ZTEST(inspection, test_catalog_lookup_and_visit)
{
    const auto collections = solar::inspection::collections();
    zassert_equal(collections.size(), 5);

    constexpr auto custom =
        fixture::RobotSystem::InspectionCatalog::Entry<fixture::CustomCollection>::local_id;
    const auto found = solar::inspection::find(custom);
    zassert_true(found.has_value());
    zassert_equal(found->get().descriptor.stable_id.value, 0xF1700001U);
    constexpr auto navigation = fixture::RobotSystem::Catalogs::Of<solar::component::Tag>::Entry<
        fixture::Navigation>::local_id;
    zassert_equal(found->get().owner.kind, solar::OwnerKind::Component);
    zassert_equal(found->get().owner.component.value, navigation.value);
    zassert_equal(found->get().origin, solar::OriginKind::Contribution);

    const auto by_stable = solar::inspection::find(solar::inspection::Id{0xF1700001U});
    zassert_true(by_stable.has_value());
    zassert_equal(by_stable->get().local_id.value, custom.value);

    bool visited{};
    auto visit = solar::inspection::visit(custom, [&](auto identity) {
        using Collection = typename decltype(identity)::type;
        visited = std::is_same_v<Collection, fixture::CustomCollection>;
    });
    zassert_true(visit.has_value());
    zassert_true(visited);

    auto missing = solar::inspection::find(solar::inspection::LocalId{60000});
    zassert_false(missing.has_value());
    zassert_equal(missing.error().reason, solar::inspection::Reason::NotFound);
}

ZTEST(inspection, test_bounded_pages_and_cursor_containment)
{
    std::array<solar::inspection::Components::Record, 1> first{};
    auto page = solar::inspection::query<solar::inspection::Components>(
        {.page = {.limit = first.size()}}, first);
    zassert_true(page.has_value());
    zassert_equal(page->written, 1);
    zassert_true(page->has_more);
    zassert_true(page->next.collection.valid());

    std::array<fixture::CustomRecord, 2> custom{};
    auto custom_page = solar::inspection::query<fixture::CustomCollection>(
        {.page = {.limit = custom.size()}}, custom);
    zassert_true(custom_page.has_value());
    zassert_equal(custom_page->written, 2);
    zassert_equal(custom[0].value, 11);
    zassert_equal(custom[1].value, 22);
    zassert_true(custom_page->has_more);

    auto second = solar::inspection::query<fixture::CustomCollection>(
        {.page = {.cursor = custom_page->next, .limit = custom.size()}}, custom);
    zassert_true(second.has_value());
    zassert_equal(second->written, 1);
    zassert_equal(custom[0].value, 33);
    zassert_false(second->has_more);

    auto wrong_cursor = page->next;
    auto rejected = solar::inspection::query<fixture::CustomCollection>(
        {.page = {.cursor = wrong_cursor, .limit = 1}}, custom);
    zassert_false(rejected.has_value());
    zassert_equal(rejected.error().reason, solar::inspection::Reason::StaleCursor);

    auto oversized =
        solar::inspection::query<fixture::CustomCollection>({.page = {.limit = 5}}, custom);
    zassert_false(oversized.has_value());
    zassert_equal(oversized.error().reason, solar::inspection::Reason::NoSpace);
}

ZTEST(inspection, test_canonical_runtime_adapters)
{
    std::array<solar::metrics::MetricViewRecord, 1> unavailable_metrics{};
    auto unavailable =
        solar::inspection::Of<fixture::UnbootedApplication>::query<solar::inspection::MetricValues>(
            {.page = {.limit = unavailable_metrics.size()}}, unavailable_metrics);
    zassert_false(unavailable.has_value());
    zassert_equal(unavailable.error().reason, solar::inspection::Reason::Unavailable);

    auto boot = solar::boot();
    zassert_true(boot.has_value());

    std::array<solar::lifecycle::ComponentRecord, 2> lifecycle_records{};
    auto lifecycle_page = solar::inspection::query<solar::inspection::LifecycleComponents>(
        {.page = {.limit = lifecycle_records.size()}}, lifecycle_records);
    zassert_true(lifecycle_page.has_value());
    zassert_equal(lifecycle_records[0].state, solar::lifecycle::ComponentState::Running);

    auto increment = solar::metrics::inc<fixture::Samples>();
    zassert_true(increment.has_value());
    std::array<solar::metrics::MetricViewRecord, 1> metric_values{};
    auto metric_page = solar::inspection::query<solar::inspection::MetricValues>(
        {.page = {.limit = metric_values.size()}}, metric_values);
    zassert_true(metric_page.has_value());
    zassert_equal(metric_page->written, 1);
    zassert_equal(metric_values[0].metric.value, 0);
    zassert_equal(std::get<std::uint64_t>(metric_values[0].value), 1);

    std::array<solar::execution::RegistrationRecord, 1> registrations{};
    auto execution_page = solar::inspection::query<solar::inspection::ExecutionRegistrations>(
        {.page = {.limit = registrations.size()}}, registrations);
    zassert_true(execution_page.has_value());
    zassert_equal(execution_page->written, 0);

    std::array<char, 4> text{};
    auto formatted = solar::inspection::format_text(solar::inspection::collections()[0], text);
    zassert_true(formatted.has_value());
    zassert_true(formatted->truncated);
    zassert_true(formatted->required > formatted->written);

    std::array<std::byte, 32> cbor{};
    auto encoded = solar::inspection::encode_cbor(lifecycle_records[0], cbor);
    zassert_false(encoded.has_value());
    zassert_equal(encoded.error().reason, solar::inspection::Reason::Disabled);

    auto stop = solar::stop();
    zassert_true(stop.has_value());
}

ZTEST_SUITE(inspection, nullptr, nullptr, nullptr, nullptr, nullptr);
