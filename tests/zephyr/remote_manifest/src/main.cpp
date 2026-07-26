#include <cstddef>
#include <cstdint>

#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{
struct Reading
{
    std::uint32_t value{};
};

struct ManualControl
{
    static constexpr solar::remote::InStreamGroupDescriptor descriptor{
        .id = solar::remote::InStreamGroupId{0x4401},
        .name = "fixture.manual-control",
        .description = "Mutually exclusive manual control inputs",
    };
};

inline void consume(const Reading&) {}
inline void opened(const solar::remote::InStreamOpenContext&) {}
inline void closed(const solar::remote::InStreamCloseContext&) {}

struct Current
{
    static constexpr solar::remote::DataDescriptor descriptor{.id = solar::remote::DataId{0x2201},
                                                              .name = "fixture.current"};
    using Value = Reading;
    using Capabilities = solar::remote::Capabilities<solar::remote::Watch<>>;
};

struct Command
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0x2202},
        .name = "fixture.command",
    };
    using Value = Reading;
    using Capabilities = solar::remote::Capabilities<solar::remote::InStream<
        &consume, solar::remote::OnOpen<&opened>, solar::remote::OnClose<&closed>,
        solar::remote::Exclusive<ManualControl, solar::remote::Replace>,
        solar::remote::ReliableWindow<2>, solar::remote::MaxRate<50>>>;
};

struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.component"};
    using RemoteData = solar::remote::ContributeData<Current, Command>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Component>>>;
} // namespace fixture

template <> struct solar::remote::Schema<fixture::Reading>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{0x3301}, .name = "fixture.Reading"};
    using Fields = remote::Fields<Field<1, "value", &fixture::Reading::value>>;
    static constexpr std::size_t max_encoded_size = 8;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_remote_manifest, test_bound_catalog_drives_manifest)
{
    using Image = solar::remote::manifest::Image<fixture::System>;
    static_assert(Image::data_count == 2);
    static_assert(Image::schema_count == 1);
    static_assert(Image::in_stream_group_count == 1);
    zassert_equal(Image::bytes[0], std::byte{'S'});
    zassert_equal(Image::bytes[3], std::byte{'M'});
    zassert_equal(Image::bytes[4], std::byte{2});
    constexpr std::array<std::byte, 32> zero_digest{};
    zassert_true(Image::digest != zero_digest);
}

ZTEST_SUITE(solar_remote_manifest, nullptr, nullptr, nullptr, nullptr, nullptr);
