#include <solar/remote.hpp>

struct LogEntry
{
    std::uint32_t sequence;
    solar::BoundedText<64> message;
};

template <> struct solar::remote::Schema<LogEntry>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "bad.LogEntry"};
    static constexpr SchemaShape shape = SchemaShape::Record;
    using Fields = remote::Fields<Field<1, "sequence", &LogEntry::sequence>,
                                  Field<2, "message", &LogEntry::message>>;
};

static_assert(solar::remote::validate_record_schema<LogEntry>());
