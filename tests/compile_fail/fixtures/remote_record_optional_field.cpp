#include <optional>

#include <solar/remote.hpp>

struct Point
{
    std::uint16_t distance_mm;
    std::optional<std::uint8_t> confidence;
};

template <> struct solar::remote::Schema<Point>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.Point"};
    static constexpr SchemaShape shape = SchemaShape::Record;
    using Fields = remote::Fields<Field<1, "distance_mm", &Point::distance_mm>,
                                  Field<2, "confidence", &Point::confidence>>;
};

static_assert(solar::remote::validate_record_schema<Point>());
