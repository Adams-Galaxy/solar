#include <array>

#include <solar/solar.hpp>

namespace fixture
{

struct ValidCollection
{
    using Record = std::uint32_t;
    using Query = solar::inspection::BasicQuery;
    static constexpr solar::inspection::Descriptor descriptor{
        .name = "valid",
        .stable_id = solar::inspection::Id{0xF1700101U},
        .maximum_page = 1,
        .record_size = sizeof(Record),
        .query_size = sizeof(Query),
    };
};

struct OtherCollection : ValidCollection
{
    static constexpr solar::inspection::Descriptor descriptor{
        .name = "other",
        .stable_id = solar::inspection::Id{0xF1700102U},
        .maximum_page = 1,
        .record_size = sizeof(Record),
        .query_size = sizeof(Query),
    };
};

#if SOLAR_FAIL_CASE == 1
struct InvalidCollection
{
    using Query = solar::inspection::BasicQuery;
    static constexpr solar::inspection::Descriptor descriptor{
        .name = "invalid",
        .stable_id = solar::inspection::Id{0xF1700103U},
        .maximum_page = 1,
        .record_size = 1,
        .query_size = sizeof(Query),
    };
};
using SelectedCollection = InvalidCollection;
#else
using SelectedCollection = ValidCollection;
#endif

struct Component
{
#if SOLAR_FAIL_CASE == 4
    using Inspections = solar::inspection::Collections<SelectedCollection, OtherCollection>;
#elif SOLAR_FAIL_CASE == 1 || SOLAR_FAIL_CASE == 2
    using Inspections = solar::inspection::Collections<SelectedCollection>;
#endif
    static constexpr solar::component::Descriptor descriptor{.name = "component"};
};

using System = solar::System<solar::Blueprint<solar::Facilities<Component>>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

#if SOLAR_FAIL_CASE == 2
void provider_missing()
{
    std::array<fixture::ValidCollection::Record, 1> output{};
    (void)solar::inspection::query<fixture::ValidCollection>({}, output);
}
#elif SOLAR_FAIL_CASE == 3
void collection_missing()
{
    std::array<fixture::OtherCollection::Record, 1> output{};
    (void)solar::inspection::query<fixture::OtherCollection>({}, output);
}
#endif

int main()
{
    return fixture::System::valid ? 0 : 1;
}
