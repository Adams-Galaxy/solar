#include <solar/remote.hpp>

struct Inner
{
    std::uint16_t value;
};

template <> struct solar::remote::Schema<Inner>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.Inner"};
    static constexpr SchemaShape shape = SchemaShape::Record;
    using Fields = remote::Fields<Field<1, "value", &Inner::value>>;
};

struct Outer
{
    Inner inner;
};

template <> struct solar::remote::Schema<Outer>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{3}, .name = "bad.Outer"};
    static constexpr SchemaShape shape = SchemaShape::Record;
    using Fields = remote::Fields<Field<1, "inner", &Outer::inner>>;
};

static_assert(solar::remote::validate_record_schema<Outer>());
