#include <solar/remote.hpp>
enum class Mode : unsigned char
{
    off,
    on
};
template <> struct solar::remote::Schema<Mode>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.Mode"};
    static constexpr SchemaShape shape = SchemaShape::Enumeration;
    using Values = remote::EnumValues<remote::EnumValue<Mode::off, "same">,
                                      remote::EnumValue<Mode::on, "same">>;
};
static_assert(solar::remote::validate_enum_schema<Mode>());
