#include "catalog_fixture.hpp"

namespace catalog_fixture
{

const alpha::DescriptorView* descriptor_data_from_other_translation_unit()
{
    return AlphaCatalog::descriptors().data();
}

} // namespace catalog_fixture
