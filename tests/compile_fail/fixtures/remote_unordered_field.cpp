#include <solar/remote.hpp>

struct Value
{
    int first;
    int second;
};

template <> struct solar::remote::Schema<Value>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.Value"};
    using Fields = remote::Fields<Field<2, &Value::second>, Field<1, &Value::first>>;
    static constexpr std::size_t max_encoded_size = 16;
    static constexpr Codec codec = Codec::Cbor;
};

static_assert(solar::remote::validate_schema<Value>());
