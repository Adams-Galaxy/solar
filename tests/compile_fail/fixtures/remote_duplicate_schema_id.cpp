#define CONFIG_SOLAR_REMOTE 1
#define CONFIG_SOLAR_REMOTE_MAX_SCHEMAS 16
#define CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS 16

#include <solar/remote.hpp>
#include <solar/system.hpp>

struct FirstValue { int value; };
struct SecondValue { int value; };

template <> struct solar::remote::Schema<FirstValue>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{7}, .name = "bad.First"};
    using Fields = remote::Fields<Field<1, &FirstValue::value>>;
    static constexpr std::size_t max_encoded_size = 8;
    static constexpr Codec codec = Codec::Cbor;
};

template <> struct solar::remote::Schema<SecondValue>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{7}, .name = "bad.Second"};
    using Fields = remote::Fields<Field<1, &SecondValue::value>>;
    static constexpr std::size_t max_encoded_size = 8;
    static constexpr Codec codec = Codec::Cbor;
};

struct FirstData
{
    static constexpr solar::remote::DataDescriptor descriptor{.id = solar::remote::DataId{1},
                                                               .name = "bad.first"};
    using Value = FirstValue;
    using Capabilities = solar::remote::Capabilities<solar::remote::Watch<>>;
};
struct SecondData
{
    static constexpr solar::remote::DataDescriptor descriptor{.id = solar::remote::DataId{2},
                                                               .name = "bad.second"};
    using Value = SecondValue;
    using Capabilities = solar::remote::Capabilities<solar::remote::Watch<>>;
};
struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "bad.component"};
    using RemoteData = solar::remote::ContributeData<FirstData, SecondData>;
};
using BadSystem = solar::System<solar::Blueprint<solar::Facilities<Component>>>;
constexpr auto bad_manifest = solar::remote::manifest::Image<BadSystem>::bytes;
