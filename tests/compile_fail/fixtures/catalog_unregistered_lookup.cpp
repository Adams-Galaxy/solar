#include "catalog_fixture.hpp"

struct NotRegistered
{};

using MissingEntry = catalog_fixture::AlphaCatalog::Entry<NotRegistered>;

static_assert(sizeof(MissingEntry) != 0);
