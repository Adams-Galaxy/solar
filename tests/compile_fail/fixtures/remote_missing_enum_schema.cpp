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
template <> struct solar::remote::Schema<Value>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.Value"};
    using Fields = remote::Fields<Field<1, "mode", &Value::mode>>;
    static constexpr std::size_t max_encoded_size = 8;
    static constexpr Codec codec = Codec::Cbor;
};
static_assert(solar::remote::validate_schema<Value>());
