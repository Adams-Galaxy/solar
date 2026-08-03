#define CONFIG_SOLAR_REMOTE 1
#define CONFIG_SOLAR_REMOTE_MAX_SCHEMAS 16
#define CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS 16
#include <solar/remote.hpp>
enum class Mode : unsigned char
{
    off,
    on
};
struct Value
{
    Mode mode;
};
template <> struct solar::remote::Schema<Mode>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{6}, .name = "bad.Mode"};
    static constexpr SchemaShape shape = SchemaShape::Enumeration;
    using Values =
        remote::EnumValues<remote::EnumValue<Mode::off, "off">, remote::EnumValue<Mode::on, "on">>;
};
template <> struct solar::remote::Schema<Value>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{7}, .name = "bad.Value"};
    using Fields = remote::Fields<Field<1, "mode", &Value::mode>>;
    static constexpr std::size_t max_encoded_size = 8;
    static constexpr Codec codec = Codec::Cbor;
};
struct Data
{
    static constexpr solar::remote::DataDescriptor descriptor{.id = solar::remote::DataId{1},
                                                              .name = "bad.data"};
    using Value = ::Value;
    using Capabilities = solar::remote::Capabilities<solar::remote::Watch<>>;
};
using BadArchitecture =
    solar::remote::Architecture<solar::TypeList<Value>, solar::TypeList<Data>, solar::TypeList<>,
                                solar::TypeList<>, solar::TypeList<>, solar::TypeList<>,
                                solar::TypeList<>, solar::TypeList<>>;
using BadRemote = solar::remote::RuntimeContext<BadArchitecture>;
constexpr auto bad_manifest = solar::remote::manifest::Image<BadRemote>::bytes;
