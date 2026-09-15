#include <solar/remote.hpp>

struct Opaque
{
    int value;
};

struct Point
{
    std::uint16_t distance_mm;
    Opaque payload;
};

template <> struct solar::remote::Schema<Point>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.Point"};
    static constexpr SchemaShape shape = SchemaShape::Record;
    using Fields = remote::Fields<Field<1, "distance_mm", &Point::distance_mm>,
                                  Field<2, "payload", &Point::payload>>;
};

static_assert(solar::remote::validate_record_schema<Point>());
