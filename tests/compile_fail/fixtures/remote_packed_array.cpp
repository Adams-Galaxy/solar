#include <solar/remote.hpp>

struct PackedArray
{
    solar::remote::Array<std::uint16_t, 4> values;
};

template <> struct solar::remote::Schema<PackedArray>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.PackedArray"};
    using Fields = remote::Fields<Field<1, "values", &PackedArray::values>>;
    static constexpr std::size_t max_encoded_size = 8;
    static constexpr Codec codec = Codec::Packed;
};

static_assert(solar::remote::packed::encoded_size<PackedArray> != 0);
