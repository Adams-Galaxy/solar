#include <solar/remote.hpp>
enum class Mode : unsigned char
{
    off
};
template <> struct solar::remote::Schema<Mode>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.Mode"};
    static constexpr SchemaShape shape = SchemaShape::Enumeration;
    using Values = remote::EnumValues<>;
};
static_assert(solar::remote::validate_enum_schema<Mode>());
