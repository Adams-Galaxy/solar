#define CONFIG_SOLAR_REMOTE 1
#define CONFIG_SOLAR_REMOTE_MAX_SCHEMAS 16
#define CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS 16

#include <solar/remote.hpp>
#include <solar/system.hpp>

struct Value { int value; };
template <> struct solar::remote::Schema<Value>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{7}, .name = "bad.Value"};
    using Fields = remote::Fields<Field<1, &Value::value>>;
    static constexpr std::size_t max_encoded_size = 8;
    static constexpr Codec codec = Codec::Cbor;
};
struct Data
{
    static constexpr solar::remote::DataDescriptor descriptor{.id = solar::remote::DataId{1},
                                                               .name = "bad.data"};
    using Value = ::Value;
    using Capabilities =
        solar::remote::Capabilities<solar::remote::Watch<>, solar::remote::Watch<>>;
};
struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "bad.component"};
    using RemoteData = solar::remote::ContributeData<Data>;
};
using BadSystem = solar::System<solar::Blueprint<solar::Facilities<Component>>>;
constexpr auto bad_manifest = solar::remote::manifest::Image<BadSystem>::bytes;
