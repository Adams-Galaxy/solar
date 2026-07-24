#include <solar/remote.hpp>

struct Value
{
    solar::remote::BoundedText<16> text;
};

template <> struct solar::remote::Schema<Value>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.Value"};
    using Fields = remote::Fields<Field<1, "text", &Value::text>>;
    static constexpr std::size_t max_encoded_size = 18;
    static constexpr Codec codec = Codec::Packed;
};

static_assert(solar::remote::packed::encoded_size<Value> != 0);
