#include "catalog_fixture.hpp"

struct ExtensionTag
{};

struct ExtensionEntry
{};

struct ExtensionOwner
{
    using Contributions = solar::Contribution<ExtensionTag, ExtensionEntry>;
};

using BadCatalogs = solar::collect_catalog_set_t<solar::TypeList<catalog_fixture::alpha::Tag>,
                                                 solar::TypeList<ExtensionOwner>>;

static_assert(BadCatalogs::size == 1);
