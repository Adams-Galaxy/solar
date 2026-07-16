#include <cstddef>
#include <cstdint>

#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{
struct Reading { std::uint32_t value{}; };

struct Current
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0x2201}, .name = "fixture.current"};
    using Value = Reading;
    using Capabilities = solar::remote::Capabilities<solar::remote::Watch<>>;
};

struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.component"};
    using RemoteData = solar::remote::ContributeData<Current>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Component>>>;
} // namespace fixture

template <> struct solar::remote::Schema<fixture::Reading>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{0x3301},
                                                  .name = "fixture.Reading"};
    using Fields = remote::Fields<Field<1, &fixture::Reading::value>>;
    static constexpr std::size_t max_encoded_size = 8;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_remote_manifest, test_bound_catalog_drives_manifest)
{
    using Image = solar::remote::manifest::Image<fixture::System>;
    static_assert(Image::data_count == 1);
    static_assert(Image::schema_count == 1);
    zassert_equal(Image::bytes[0], std::byte{'S'});
    zassert_equal(Image::bytes[3], std::byte{'M'});
}

ZTEST_SUITE(solar_remote_manifest, nullptr, nullptr, nullptr, nullptr, nullptr);
