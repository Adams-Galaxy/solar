#include <solar/remote.hpp>
struct Value
{
    int value;
};
template <> struct solar::remote::Schema<Value>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.Value"};
    using Fields = remote::Fields<
        Field<1, "value", &Value::value, remote::Description<"one">, remote::Description<"two">>>;
    static constexpr std::size_t max_encoded_size = 8;
    static constexpr Codec codec = Codec::Cbor;
};
static_assert(solar::remote::validate_schema<Value>());
static_assert(solar::remote::Field<1, "value", &Value::value, solar::remote::Description<"one">,
                                   solar::remote::Description<"two">>::description.empty());
