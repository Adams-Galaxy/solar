#include <zephyr/ztest.h>

#include "catalog_fixture.hpp"

namespace fixture = catalog_fixture;

static_assert(fixture::AlphaCatalog::size == 6);
static_assert(fixture::AlphaCatalog::Entry<fixture::AlphaOwned>::local_id.value == 2);
static_assert(
    std::is_same_v<typename fixture::ResolvedBetaSource::Declaration, fixture::AlphaOwned>);

ZTEST(solar_catalog, test_static_descriptor_views)
{
    const auto descriptors = fixture::AlphaCatalog::descriptors();

    zassert_equal(descriptors.size(), fixture::AlphaCatalog::size);
    if constexpr (solar::catalog::descriptor_strings_enabled) {
        zassert_equal(descriptors[2].descriptor.name, "alpha.owned");
    } else {
        zassert_true(descriptors[2].descriptor.name.empty());
    }
    zassert_equal(descriptors[2].owner.kind, solar::OwnerKind::Component);
    zassert_equal(descriptors[2].owner.component.value, 0);
}

ZTEST(solar_catalog, test_bounded_lookup)
{
    const auto found = fixture::AlphaCatalog::find(fixture::alpha::Id{0x1003});
    const auto missing = fixture::AlphaCatalog::find(solar::LocalId<fixture::alpha::Tag>{99});

    zassert_true(found.has_value());
    zassert_equal(found->get().local_id.value, 2);
    zassert_false(missing.has_value());
    zassert_equal(missing.error(), solar::catalog::LookupError::UnknownLocalId);
}

ZTEST_SUITE(solar_catalog, nullptr, nullptr, nullptr, nullptr, nullptr);
