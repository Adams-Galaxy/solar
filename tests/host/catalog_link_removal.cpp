#include "catalog_fixture.hpp"

static_assert(catalog_fixture::AlphaCatalog::size == 6);

int main()
{
    return catalog_fixture::AlphaCatalog::size == 6 ? 0 : 1;
}
