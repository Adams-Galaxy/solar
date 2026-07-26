#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

#include "solar/remote/catalog.hpp"
#include "solar/remote/packed.hpp"
#include "solar/remote/protocol.hpp"
#include "solar/remote/sha256.hpp"

namespace solar::remote::manifest
{

inline constexpr std::uint16_t format_version = 2;
inline constexpr std::size_t image_header_size = 16;
inline constexpr std::size_t record_header_size = 4;

enum class RecordKind : std::uint8_t
{
    Schema = 1,
    Field = 2,
    Data = 3,
    Action = 4,
    Topic = 5,
    Stream = 6,
    Link = 7,
    EnumValue = 8,
    Capability = 9,
    InStreamGroup = 10,
};

enum class RecordFlags : std::uint8_t
{
    None = 0,
    Required = 1U << 0,
};

enum class ValueKind : std::uint8_t
{
    Boolean = 1,
    Unsigned = 2,
    Signed = 3,
    Float = 4,
    Enumeration = 5,
    Text = 6,
    Bytes = 7,
    SchemaReference = 8,
};

enum class FieldFlags : std::uint8_t
{
    None = 0,
    Required = 1U << 0,
    Deprecated = 1U << 1,
};

enum class EnumFlags : std::uint8_t
{
    None = 0,
    Open = 1U << 0,
};

enum class EndpointDomain : std::uint8_t
{
    Data = 1,
    Action = 2,
    Topic = 3,
    Stream = 4,
};

enum class DeliveryKind : std::uint8_t
{
    None = 0,
    Latest = 1,
    QueueDropOldest = 2,
    QueueDropNewest = 3,
    QueueReject = 4,
    Reliable = 5,
};

enum class CapabilityFlags : std::uint8_t
{
    None = 0,
    Cancellation = 1U << 0,
    BatchedFraming = 1U << 1,
};

enum class InStreamFlags : std::uint8_t
{
    None = 0,
    ExplicitOpen = 1U << 0,
    OnOpen = 1U << 1,
    OnClose = 1U << 2,
    Exclusive = 1U << 3,
};

enum class ReplacementKind : std::uint8_t
{
    None = 0,
    Replace = 1,
    Reject = 2,
};

[[nodiscard]] constexpr std::uint8_t bits(RecordFlags value) noexcept
{
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] constexpr std::uint8_t bits(FieldFlags value) noexcept
{
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] constexpr std::uint8_t bits(EnumFlags value) noexcept
{
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] constexpr std::uint8_t bits(CapabilityFlags value) noexcept
{
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] constexpr std::uint8_t bits(InStreamFlags value) noexcept
{
    return static_cast<std::uint8_t>(value);
}

namespace detail
{

template <typename Entries> struct Declarations;

template <typename... Entries> struct Declarations<TypeList<Entries...>>
{
    using type = TypeList<typename Entries::Declaration...>;
};

template <typename T, typename = void> struct ActionRequest
{
    using type = Empty;
};

template <typename T> struct ActionRequest<T, std::void_t<typename T::Request>>
{
    using type = typename T::Request;
};

template <typename T, typename = void> struct ActionResponse
{
    using type = Empty;
};

template <typename T> struct ActionResponse<T, std::void_t<typename T::Response>>
{
    using type = typename T::Response;
};

template <typename T, typename = void> struct ActionError
{
    using type = solar::Error;
};

template <typename T> struct ActionError<T, std::void_t<typename T::Error>>
{
    using type = typename T::Error;
};

template <typename T, typename = void> struct ActionAccess
{
    using type = Requires<>;
};

template <typename T> struct ActionAccess<T, std::void_t<typename T::Access>>
{
    using type = typename T::Access;
};

template <typename T, typename = void> struct LinkGrants
{
    using type = Requires<>;
};

template <typename T> struct LinkGrants<T, std::void_t<typename T::Grants>>
{
    using type = typename T::Grants;
};

template <typename T, typename = void> struct TopicPublication
{
    using type = Watch<Latest, MultipleProducers>;
};

template <typename T> struct TopicPublication<T, std::void_t<typename T::Publication>>
{
    using type = typename T::Publication;
};

template <typename T> struct PermissionMask;

template <Permission... Grants> struct PermissionMask<Requires<Grants...>>
{
    static constexpr std::uint8_t value =
        (std::uint8_t{} | ... |
         static_cast<std::uint8_t>(1U << (static_cast<std::uint8_t>(Grants) - 1U)));
};

template <typename T> struct CapabilityMask;

template <typename... CapabilityTypes> struct CapabilityMask<Capabilities<CapabilityTypes...>>
{
    static consteval bool unique()
    {
        constexpr std::array<std::uint8_t, sizeof...(CapabilityTypes)> kinds{
            static_cast<std::uint8_t>(CapabilityTypes::kind)...};
        for (std::size_t left{}; left < kinds.size(); ++left) {
            for (std::size_t right = left + 1; right < kinds.size(); ++right) {
                if (kinds[left] == kinds[right]) {
                    return false;
                }
            }
        }
        return true;
    }

    static constexpr std::uint8_t value =
        (std::uint8_t{} | ... |
         static_cast<std::uint8_t>(1U << (static_cast<std::uint8_t>(CapabilityTypes::kind) - 1U)));
};

template <typename List, typename Value, template <typename, typename> typename Less>
struct InsertSorted;

template <typename Value, template <typename, typename> typename Less>
struct InsertSorted<TypeList<>, Value, Less>
{
    using type = TypeList<Value>;
};

template <typename Head, typename... Tail, typename Value,
          template <typename, typename> typename Less>
struct InsertSorted<TypeList<Head, Tail...>, Value, Less>
{
  private:
    using Remaining = typename InsertSorted<TypeList<Tail...>, Value, Less>::type;

  public:
    using type = std::conditional_t<Less<Value, Head>::value, TypeList<Value, Head, Tail...>,
                                    concat_t<TypeList<Head>, Remaining>>;
};

template <typename List, template <typename, typename> typename Less> struct Sort;

template <template <typename, typename> typename Less> struct Sort<TypeList<>, Less>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail, template <typename, typename> typename Less>
struct Sort<TypeList<Head, Tail...>, Less>
{
    using type =
        typename InsertSorted<typename Sort<TypeList<Tail...>, Less>::type, Head, Less>::type;
};

template <typename Left, typename Right>
struct SchemaIdLess
    : std::bool_constant<(Schema<Left>::descriptor.id.value < Schema<Right>::descriptor.id.value)>
{};

template <typename Left, typename Right>
struct EndpointIdLess : std::bool_constant<(Left::descriptor.id.value < Right::descriptor.id.value)>
{};

template <typename Left, typename Right>
struct CapabilityKindLess : std::bool_constant<(static_cast<std::uint8_t>(Left::kind) <
                                                static_cast<std::uint8_t>(Right::kind))>
{};

template <typename Left, typename Right>
struct EnumValueLess
    : std::bool_constant<(
          static_cast<std::underlying_type_t<std::remove_cv_t<decltype(Left::value)>>>(
              Left::value) <
          static_cast<std::underlying_type_t<std::remove_cv_t<decltype(Right::value)>>>(
              Right::value))>
{};

template <typename List, template <typename, typename> typename Less>
using sort_t = typename Sort<List, Less>::type;

template <typename List> struct DataSchemas;

template <typename... Types> struct DataSchemas<TypeList<Types...>>
{
    using type = TypeList<typename Types::Value...>;
};

template <typename List> struct ActionSchemas;

template <typename... Types> struct ActionSchemas<TypeList<Types...>>
{
    using type =
        TypeList<typename ActionRequest<Types>::type..., typename ActionResponse<Types>::type...,
                 typename ActionError<Types>::type...>;
};

template <typename List> struct ValueSchemas;

template <typename... Types> struct ValueSchemas<TypeList<Types...>>
{
    using type = TypeList<typename Types::Value...>;
};

template <typename T> struct IsObjectSchema : std::bool_constant<ObjectSchemaType<T>>
{};

template <typename T> struct IsEnumerationSchema : std::bool_constant<EnumerationSchemaType<T>>
{};

template <typename FieldT> struct FieldEnumType
{
    using Member = remote::detail::optional_value_t<remote::detail::field_member_t<FieldT>>;
    using type = std::conditional_t<EnumerationSchemaType<Member>, TypeList<Member>, TypeList<>>;
};

template <typename FieldsT> struct FieldEnumTypes;

template <typename... FieldTypes> struct FieldEnumTypes<Fields<FieldTypes...>>
{
    using type = unique_t<concat_t<typename FieldEnumType<FieldTypes>::type...>>;
};

template <typename SchemaT> struct SchemaEnumTypes
{
    using type = typename FieldEnumTypes<typename Schema<SchemaT>::Fields>::type;
};

template <typename List> struct ReferencedEnums;

template <typename... Schemas> struct ReferencedEnums<TypeList<Schemas...>>
{
    using type = unique_t<concat_t<typename SchemaEnumTypes<Schemas>::type...>>;
};

template <typename FieldT> struct FieldStatusType
{
    using Member = remote::detail::optional_value_t<remote::detail::field_member_t<FieldT>>;
    using type = std::conditional_t<StatusSchemaType<Member>, TypeList<Member>, TypeList<>>;
};

template <typename FieldsT> struct FieldStatusTypes;

template <typename... FieldTypes> struct FieldStatusTypes<Fields<FieldTypes...>>
{
    using type = unique_t<concat_t<typename FieldStatusType<FieldTypes>::type...>>;
};

template <typename List> struct ReferencedStatusSchemas;

template <typename... Schemas> struct ReferencedStatusSchemas<TypeList<Schemas...>>
{
    using type =
        unique_t<concat_t<typename FieldStatusTypes<typename Schema<Schemas>::Fields>::type...>>;
};

template <typename Needles, typename Haystack> struct AllContained;

template <typename T>
inline constexpr bool builtin_schema_v = requires { requires Schema<T>::builtin; };

template <typename... Needles, typename Haystack>
struct AllContained<TypeList<Needles...>, Haystack>
    : std::bool_constant<((contains_v<Needles, Haystack> || builtin_schema_v<Needles>) && ...)>
{};

template <typename... Types> consteval bool unique_schema_ids(TypeList<Types...>)
{
    constexpr std::array<std::uint32_t, sizeof...(Types)> ids{
        Schema<Types>::descriptor.id.value...};
    for (std::size_t left{}; left < ids.size(); ++left) {
        for (std::size_t right = left + 1; right < ids.size(); ++right) {
            if (ids[left] == ids[right]) {
                return false;
            }
        }
    }
    return true;
}

template <typename... Types> consteval bool unique_schema_names(TypeList<Types...>)
{
    constexpr std::array<std::string_view, sizeof...(Types)> names{
        Schema<Types>::descriptor.name...};
    for (std::size_t left{}; left < names.size(); ++left) {
        for (std::size_t right = left + 1; right < names.size(); ++right) {
            if (names[left] == names[right]) {
                return false;
            }
        }
    }
    return true;
}

template <typename SchemaT> consteval bool validate_one_schema()
{
    if constexpr (EnumerationSchemaType<SchemaT>) {
        return validate_enum_schema<SchemaT>();
    } else {
        return validate_schema<SchemaT>();
    }
}

template <typename... Types> consteval bool validate_schemas(TypeList<Types...>)
{
    return (validate_one_schema<Types>() && ...);
}

template <typename FieldsT> struct FieldCount;

template <typename... FieldsT>
struct FieldCount<Fields<FieldsT...>> : std::integral_constant<std::size_t, sizeof...(FieldsT)>
{};

template <typename List> struct SchemaFieldCount;

template <typename... Types>
struct SchemaFieldCount<TypeList<Types...>>
    : std::integral_constant<std::size_t,
                             (FieldCount<typename Schema<Types>::Fields>::value + ... + 0U)>
{};

template <typename ValuesT> struct EnumValueCount;

template <typename... Values>
struct EnumValueCount<EnumValues<Values...>>
    : std::integral_constant<std::size_t, sizeof...(Values)>
{};

template <typename List> struct SchemaEnumValueCount;

template <typename... Types>
struct SchemaEnumValueCount<TypeList<Types...>>
    : std::integral_constant<std::size_t,
                             (EnumValueCount<typename Schema<Types>::Values>::value + ... + 0U)>
{};

template <typename List> consteval std::size_t schema_text_size();

template <typename... Types> consteval std::size_t schema_text_size(TypeList<Types...>)
{
    return ((Schema<Types>::descriptor.name.size() + Schema<Types>::descriptor.description.size()) +
            ... + 0U);
}

template <typename FieldsT> struct FieldTextSize;

template <typename... FieldTypes>
struct FieldTextSize<Fields<FieldTypes...>>
    : std::integral_constant<std::size_t,
                             ((FieldTypes::name.size() + FieldTypes::description.size() +
                               FieldTypes::unit.size()) +
                              ... + 0U)>
{};

template <typename List> consteval std::size_t field_text_size();

template <typename... Types> consteval std::size_t field_text_size(TypeList<Types...>)
{
    return (FieldTextSize<typename Schema<Types>::Fields>::value + ... + 0U);
}

template <typename ValuesT> struct EnumTextSize;

template <typename... Values>
struct EnumTextSize<EnumValues<Values...>>
    : std::integral_constant<std::size_t,
                             ((Values::name.size() + Values::description.size()) + ... + 0U)>
{};

template <typename List> consteval std::size_t enum_text_size();

template <typename... Types> consteval std::size_t enum_text_size(TypeList<Types...>)
{
    return (EnumTextSize<typename Schema<Types>::Values>::value + ... + 0U);
}

template <typename List> consteval std::size_t endpoint_text_size();

template <typename... Types> consteval std::size_t endpoint_text_size(TypeList<Types...>)
{
    return ((Types::descriptor.name.size() + Types::descriptor.description.size()) + ... + 0U);
}

template <typename T> struct BoundedCapacity : std::integral_constant<std::uint32_t, 0>
{};

template <std::size_t Capacity>
struct BoundedCapacity<BoundedText<Capacity>>
    : std::integral_constant<std::uint32_t, static_cast<std::uint32_t>(Capacity)>
{
    static_assert(Capacity <= UINT32_MAX);
};

template <std::size_t Capacity>
struct BoundedCapacity<BoundedBytes<Capacity>>
    : std::integral_constant<std::uint32_t, static_cast<std::uint32_t>(Capacity)>
{
    static_assert(Capacity <= UINT32_MAX);
};

template <typename T> consteval ValueKind value_kind()
{
    using Value = remote::detail::optional_value_t<T>;
    if constexpr (std::is_same_v<Value, bool>) {
        return ValueKind::Boolean;
    } else if constexpr (EnumerationSchemaType<Value>) {
        return ValueKind::Enumeration;
    } else if constexpr (StatusSchemaType<Value>) {
        return ValueKind::SchemaReference;
    } else if constexpr (std::unsigned_integral<Value>) {
        return ValueKind::Unsigned;
    } else if constexpr (std::signed_integral<Value>) {
        return ValueKind::Signed;
    } else if constexpr (std::floating_point<Value>) {
        return ValueKind::Float;
    } else if constexpr (remote::detail::IsBoundedText<Value>::value) {
        return ValueKind::Text;
    } else {
        return ValueKind::Bytes;
    }
}

template <typename T> consteval std::uint16_t bit_width()
{
    using Value = remote::detail::optional_value_t<T>;
    if constexpr (std::is_same_v<Value, bool>) {
        return 8;
    } else if constexpr (std::integral<Value> || std::floating_point<Value>) {
        static_assert(sizeof(Value) * 8U <= UINT16_MAX);
        return static_cast<std::uint16_t>(sizeof(Value) * 8U);
    } else {
        return 0;
    }
}

template <typename T> consteval TypeId referenced_type()
{
    using Value = remote::detail::optional_value_t<T>;
    if constexpr (EnumerationSchemaType<Value> || StatusSchemaType<Value>) {
        return Schema<Value>::descriptor.id;
    }
    return {};
}

template <typename Value, typename FieldT> consteval std::uint32_t packed_offset()
{
    if constexpr (Schema<Value>::codec != Codec::Packed) {
        return UINT32_MAX;
    } else {
        std::uint32_t offset{};
        bool found{};
        []<typename... FieldsT>(std::uint32_t& current, bool& matched, Fields<FieldsT...>) {
            ((matched ? void()
              : std::same_as<FieldT, FieldsT>
                  ? static_cast<void>(matched = true)
                  : static_cast<void>(
                        current += sizeof(
                            packed::detail::wire_type_t<remote::detail::field_member_t<FieldsT>>))),
             ...);
        }(offset, found, typename Schema<Value>::Fields{});
        return offset;
    }
}

struct Writer
{
    std::span<std::byte> output;
    std::size_t offset{};

    constexpr void u8(std::uint8_t value)
    {
        output[offset++] = static_cast<std::byte>(value);
    }

    constexpr void u16(std::uint16_t value)
    {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8U));
    }

    constexpr void u32(std::uint32_t value)
    {
        u16(static_cast<std::uint16_t>(value));
        u16(static_cast<std::uint16_t>(value >> 16U));
    }

    constexpr void u64(std::uint64_t value)
    {
        u32(static_cast<std::uint32_t>(value));
        u32(static_cast<std::uint32_t>(value >> 32U));
    }

    constexpr void text(std::string_view value)
    {
        for (char character : value) {
            u8(static_cast<std::uint8_t>(character));
        }
    }

    constexpr void record(RecordKind kind, std::uint16_t size,
                          RecordFlags flags = RecordFlags::Required)
    {
        static_assert(record_header_size == 4);
        u8(static_cast<std::uint8_t>(kind));
        u8(bits(flags));
        u16(size);
    }
};

template <typename T> consteval SchemaShape schema_shape()
{
    if constexpr (EnumerationSchemaType<T>) {
        return SchemaShape::Enumeration;
    } else if constexpr (requires { Schema<T>::shape; }) {
        return Schema<T>::shape;
    }
    return SchemaShape::Object;
}

template <typename T> consteval std::uint8_t schema_codec()
{
    if constexpr (EnumerationSchemaType<T>) {
        return 0;
    } else {
        return static_cast<std::uint8_t>(Schema<T>::codec);
    }
}

template <typename T> consteval std::uint32_t schema_maximum()
{
    if constexpr (EnumerationSchemaType<T>) {
        return 0;
    } else {
        static_assert(Schema<T>::max_encoded_size <= UINT32_MAX);
        return static_cast<std::uint32_t>(Schema<T>::max_encoded_size);
    }
}

template <typename T> consteval ValueKind enum_underlying_kind()
{
    if constexpr (!EnumerationSchemaType<T>) {
        return static_cast<ValueKind>(0);
    } else if constexpr (std::is_signed_v<std::underlying_type_t<T>>) {
        return ValueKind::Signed;
    } else {
        return ValueKind::Unsigned;
    }
}

template <typename T> consteval std::uint16_t enum_underlying_width()
{
    if constexpr (!EnumerationSchemaType<T>) {
        return 0;
    } else {
        return static_cast<std::uint16_t>(sizeof(std::underlying_type_t<T>) * 8U);
    }
}

template <typename T> consteval EnumFlags enum_flags()
{
    if constexpr (EnumerationSchemaType<T> &&
                  remote::detail::enum_openness<T>() == EnumOpenness::Open) {
        return EnumFlags::Open;
    }
    return EnumFlags::None;
}

template <typename T> consteval std::size_t schema_record_size()
{
    return 24 + Schema<T>::descriptor.name.size() + Schema<T>::descriptor.description.size();
}

template <typename T> consteval void emit_schema(Writer& writer)
{
    static_assert(validate_one_schema<T>());
    constexpr auto descriptor = Schema<T>::descriptor;
    constexpr auto size = schema_record_size<T>();
    static_assert(size <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_SIZE: Schema record exceeds uint16");
    static_assert(descriptor.name.size() <= UINT16_MAX &&
                      descriptor.description.size() <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_DESCRIPTOR_SIZE: schema text exceeds manifest width");
    writer.record(RecordKind::Schema, static_cast<std::uint16_t>(size));
    writer.u32(descriptor.id.value);
    writer.u16(descriptor.version);
    writer.u8(static_cast<std::uint8_t>(schema_shape<T>()));
    writer.u8(schema_codec<T>());
    writer.u32(schema_maximum<T>());
    writer.u8(static_cast<std::uint8_t>(enum_underlying_kind<T>()));
    writer.u8(bits(enum_flags<T>()));
    writer.u16(enum_underlying_width<T>());
    writer.u16(static_cast<std::uint16_t>(descriptor.name.size()));
    writer.u16(static_cast<std::uint16_t>(descriptor.description.size()));
    writer.text(descriptor.name);
    writer.text(descriptor.description);
}

template <typename... Types> consteval void emit_schemas(Writer& writer, TypeList<Types...>)
{
    (emit_schema<Types>(writer), ...);
}

template <typename Value, typename FieldT> consteval std::size_t field_record_size()
{
    return 34 + FieldT::name.size() + FieldT::description.size() + FieldT::unit.size();
}

template <typename Value, typename FieldT> consteval void emit_field(Writer& writer)
{
    using Member = remote::detail::field_member_t<FieldT>;
    using Unwrapped = remote::detail::optional_value_t<Member>;
    constexpr auto size = field_record_size<Value, FieldT>();
    static_assert(size <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_SIZE: Field record exceeds uint16");
    static_assert(FieldT::name.size() <= UINT16_MAX && FieldT::description.size() <= UINT16_MAX &&
                      FieldT::unit.size() <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_DESCRIPTOR_SIZE: field text exceeds manifest width");
    auto flags = FieldFlags::None;
    if constexpr (FieldT::required) {
        flags = static_cast<FieldFlags>(bits(flags) | bits(FieldFlags::Required));
    }
    if constexpr (FieldT::deprecated) {
        flags = static_cast<FieldFlags>(bits(flags) | bits(FieldFlags::Deprecated));
    }
    writer.record(RecordKind::Field, static_cast<std::uint16_t>(size));
    writer.u32(Schema<Value>::descriptor.id.value);
    writer.u32(referenced_type<Member>().value);
    writer.u16(FieldT::id);
    writer.u32(packed_offset<Value, FieldT>());
    writer.u8(static_cast<std::uint8_t>(value_kind<Member>()));
    writer.u8(bits(flags));
    if constexpr (EnumerationSchemaType<Unwrapped>) {
        writer.u16(enum_underlying_width<Unwrapped>());
    } else if constexpr (StatusSchemaType<Unwrapped>) {
        writer.u16(static_cast<std::uint16_t>(sizeof(std::underlying_type_t<Unwrapped>) * 8U));
    } else {
        writer.u16(bit_width<Member>());
    }
    writer.u32(BoundedCapacity<Unwrapped>::value);
    writer.u16(static_cast<std::uint16_t>(FieldT::name.size()));
    writer.u16(static_cast<std::uint16_t>(FieldT::description.size()));
    writer.u16(static_cast<std::uint16_t>(FieldT::unit.size()));
    writer.u16(0);
    writer.text(FieldT::name);
    writer.text(FieldT::description);
    writer.text(FieldT::unit);
}

template <typename Value, typename... FieldTypes>
consteval void emit_fields_for(Writer& writer, Fields<FieldTypes...>)
{
    (emit_field<Value, FieldTypes>(writer), ...);
}

template <typename... Types> consteval void emit_fields(Writer& writer, TypeList<Types...>)
{
    (emit_fields_for<Types>(writer, typename Schema<Types>::Fields{}), ...);
}

template <typename Enum, typename ValueT> consteval std::size_t enum_value_record_size()
{
    return 24 + ValueT::name.size() + ValueT::description.size();
}

template <typename Enum, typename ValueT> consteval void emit_enum_value(Writer& writer)
{
    constexpr auto size = enum_value_record_size<Enum, ValueT>();
    static_assert(size <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_SIZE: EnumValue record exceeds "
                  "uint16");
    static_assert(ValueT::name.size() <= UINT16_MAX && ValueT::description.size() <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_DESCRIPTOR_SIZE: enum value text exceeds manifest "
                  "width");
    using Underlying = std::underlying_type_t<Enum>;
    using Unsigned = std::make_unsigned_t<Underlying>;
    writer.record(RecordKind::EnumValue, static_cast<std::uint16_t>(size));
    writer.u32(Schema<Enum>::descriptor.id.value);
    writer.u64(
        static_cast<std::uint64_t>(static_cast<Unsigned>(static_cast<Underlying>(ValueT::value))));
    writer.u8(ValueT::deprecated ? bits(FieldFlags::Deprecated) : 0);
    writer.u8(0);
    writer.u8(0);
    writer.u8(0);
    writer.u16(static_cast<std::uint16_t>(ValueT::name.size()));
    writer.u16(static_cast<std::uint16_t>(ValueT::description.size()));
    writer.text(ValueT::name);
    writer.text(ValueT::description);
}

template <typename Enum, typename... Values>
consteval void emit_enum_values_for(Writer& writer, EnumValues<Values...>)
{
    using Sorted = sort_t<TypeList<Values...>, EnumValueLess>;
    []<typename... SortedValues>(Writer& output, TypeList<SortedValues...>) consteval {
        (emit_enum_value<Enum, SortedValues>(output), ...);
    }(writer, Sorted{});
}

template <typename... Types> consteval void emit_enum_values(Writer& writer, TypeList<Types...>)
{
    (emit_enum_values_for<Types>(writer, typename Schema<Types>::Values{}), ...);
}

template <typename Endpoint> consteval void validate_endpoint_text()
{
    static_assert(Endpoint::descriptor.name.size() <= UINT16_MAX &&
                      Endpoint::descriptor.description.size() <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_DESCRIPTOR_SIZE: endpoint text exceeds manifest "
                  "width");
}

template <typename DataT> consteval std::size_t data_record_size()
{
    return 20 + DataT::descriptor.name.size() + DataT::descriptor.description.size();
}

template <typename DataT> consteval void emit_data(Writer& writer)
{
    static_assert(validate_schema<typename DataT::Value>());
    static_assert(CapabilityMask<typename DataT::Capabilities>::unique(),
                  "SOLAR_DIAGNOSTIC_REMOTE_CAPABILITY_COLLISION: Data repeats a capability");
    validate_endpoint_text<DataT>();
    constexpr auto descriptor = DataT::descriptor;
    constexpr auto size = data_record_size<DataT>();
    static_assert(size <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_SIZE: Data record exceeds uint16");
    writer.record(RecordKind::Data, static_cast<std::uint16_t>(size));
    writer.u32(descriptor.id.value);
    writer.u32(Schema<typename DataT::Value>::descriptor.id.value);
    writer.u16(descriptor.version);
    writer.u8(CapabilityMask<typename DataT::Capabilities>::value);
    writer.u8(0);
    writer.u16(static_cast<std::uint16_t>(descriptor.name.size()));
    writer.u16(static_cast<std::uint16_t>(descriptor.description.size()));
    writer.text(descriptor.name);
    writer.text(descriptor.description);
}

template <typename... Types> consteval void emit_data_records(Writer& writer, TypeList<Types...>)
{
    (emit_data<Types>(writer), ...);
}

template <typename ActionT> consteval std::size_t action_record_size()
{
    return 28 + ActionT::descriptor.name.size() + ActionT::descriptor.description.size();
}

template <typename ActionT> consteval void emit_action(Writer& writer)
{
    using Request = typename ActionRequest<ActionT>::type;
    using Response = typename ActionResponse<ActionT>::type;
    using DomainError = typename ActionError<ActionT>::type;
    static_assert(validate_schema<Request>() && validate_schema<Response>() &&
                  validate_schema<DomainError>());
    validate_endpoint_text<ActionT>();
    constexpr auto descriptor = ActionT::descriptor;
    constexpr auto size = action_record_size<ActionT>();
    static_assert(size <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_SIZE: Action record exceeds uint16");
    writer.record(RecordKind::Action, static_cast<std::uint16_t>(size));
    writer.u32(descriptor.id.value);
    writer.u32(Schema<Request>::descriptor.id.value);
    writer.u32(Schema<Response>::descriptor.id.value);
    writer.u32(Schema<DomainError>::descriptor.id.value);
    writer.u16(descriptor.version);
    writer.u8(PermissionMask<typename ActionAccess<ActionT>::type>::value);
    writer.u8(0);
    writer.u16(static_cast<std::uint16_t>(descriptor.name.size()));
    writer.u16(static_cast<std::uint16_t>(descriptor.description.size()));
    writer.text(descriptor.name);
    writer.text(descriptor.description);
}

template <typename... Types> consteval void emit_actions(Writer& writer, TypeList<Types...>)
{
    (emit_action<Types>(writer), ...);
}

template <RecordKind Kind, typename EndpointT> consteval std::size_t value_record_size()
{
    static_assert(Kind == RecordKind::Topic || Kind == RecordKind::Stream);
    return 20 + EndpointT::descriptor.name.size() + EndpointT::descriptor.description.size();
}

template <RecordKind Kind, typename EndpointT> consteval void emit_value_endpoint(Writer& writer)
{
    static_assert(validate_schema<typename EndpointT::Value>());
    validate_endpoint_text<EndpointT>();
    constexpr auto descriptor = EndpointT::descriptor;
    constexpr auto size = value_record_size<Kind, EndpointT>();
    static_assert(size <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_SIZE: value endpoint record exceeds "
                  "uint16");
    writer.record(Kind, static_cast<std::uint16_t>(size));
    writer.u32(descriptor.id.value);
    writer.u32(Schema<typename EndpointT::Value>::descriptor.id.value);
    writer.u16(descriptor.version);
    writer.u8(static_cast<std::uint8_t>(Schema<typename EndpointT::Value>::codec));
    writer.u8(0);
    writer.u16(static_cast<std::uint16_t>(descriptor.name.size()));
    writer.u16(static_cast<std::uint16_t>(descriptor.description.size()));
    writer.text(descriptor.name);
    writer.text(descriptor.description);
}

template <RecordKind Kind, typename... Types>
consteval void emit_value_endpoints(Writer& writer, TypeList<Types...>)
{
    (emit_value_endpoint<Kind, Types>(writer), ...);
}

template <typename LinkT> consteval std::size_t link_record_size()
{
    return 16 + LinkT::descriptor.name.size() + LinkT::descriptor.description.size();
}

template <typename LinkT> consteval void emit_link(Writer& writer)
{
    validate_endpoint_text<LinkT>();
    constexpr auto descriptor = LinkT::descriptor;
    constexpr auto size = link_record_size<LinkT>();
    static_assert(size <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_SIZE: Link record exceeds uint16");
    writer.record(RecordKind::Link, static_cast<std::uint16_t>(size));
    writer.u32(descriptor.id.value);
    writer.u16(descriptor.version);
    writer.u8(PermissionMask<typename LinkGrants<LinkT>::type>::value);
    writer.u8(0);
    writer.u16(static_cast<std::uint16_t>(descriptor.name.size()));
    writer.u16(static_cast<std::uint16_t>(descriptor.description.size()));
    writer.text(descriptor.name);
    writer.text(descriptor.description);
}

template <typename... Types> consteval void emit_links(Writer& writer, TypeList<Types...>)
{
    (emit_link<Types>(writer), ...);
}

template <typename T> struct PolicyRate : std::integral_constant<std::uint32_t, 0>
{};

template <std::uint32_t Hertz>
struct PolicyRate<MaxRate<Hertz>> : std::integral_constant<std::uint32_t, Hertz>
{};

template <typename T> struct PolicyBatch : std::integral_constant<std::uint16_t, 0>
{};

template <std::size_t Count>
struct PolicyBatch<Batch<Count>>
    : std::integral_constant<std::uint16_t, static_cast<std::uint16_t>(Count)>
{
    static_assert(Count <= UINT16_MAX);
};

template <typename T> struct PolicyWindow : std::integral_constant<std::uint16_t, 0>
{};

template <std::size_t Count>
struct PolicyWindow<ReliableWindow<Count>>
    : std::integral_constant<std::uint16_t, static_cast<std::uint16_t>(Count)>
{
    static_assert(Count <= UINT16_MAX);
};

template <typename... Policies> struct FirstExclusivePolicy
{
    using type = void;
};

template <typename Head, typename... Tail>
struct FirstExclusivePolicy<Head, Tail...>
{
    using type =
        std::conditional_t<remote::detail::IsExclusive<Head>::value, Head,
                           typename FirstExclusivePolicy<Tail...>::type>;
};

template <typename... Policies> struct InStreamMetadata
{
    static constexpr std::size_t open_count =
        (std::size_t{} + ... + static_cast<std::size_t>(remote::detail::IsOnOpen<Policies>::value));
    static constexpr std::size_t close_count =
        (std::size_t{} + ... + static_cast<std::size_t>(remote::detail::IsOnClose<Policies>::value));
    static constexpr std::size_t exclusive_count =
        (std::size_t{} + ... +
         static_cast<std::size_t>(remote::detail::IsExclusive<Policies>::value));
    static_assert(open_count <= 1,
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM_OPEN: InStream declares more "
                  "than one OnOpen callback");
    static_assert(close_count <= 1,
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM_CLOSE: InStream declares more "
                  "than one OnClose callback");
    static_assert(exclusive_count <= 1,
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM_EXCLUSIVE: InStream declares "
                  "more than one Exclusive policy");

    using ExclusivePolicy = typename FirstExclusivePolicy<Policies...>::type;
    using Group = std::conditional_t<exclusive_count == 0, void,
                                     typename remote::detail::IsExclusive<ExclusivePolicy>::Group>;
    using Behavior =
        std::conditional_t<exclusive_count == 0, void,
                           typename remote::detail::IsExclusive<ExclusivePolicy>::Behavior>;

    static constexpr InStreamFlags flags = static_cast<InStreamFlags>(
        bits(InStreamFlags::ExplicitOpen) |
        (open_count != 0 ? bits(InStreamFlags::OnOpen) : 0U) |
        (close_count != 0 ? bits(InStreamFlags::OnClose) : 0U) |
        (exclusive_count != 0 ? bits(InStreamFlags::Exclusive) : 0U));
    static constexpr ReplacementKind replacement =
        exclusive_count == 0
            ? ReplacementKind::None
            : (std::same_as<Behavior, Replace> ? ReplacementKind::Replace
                                               : ReplacementKind::Reject);
    static constexpr std::uint32_t group_id = [] {
        if constexpr (exclusive_count == 0) {
            return std::uint32_t{};
        } else {
            static_assert(requires { Group::descriptor; },
                          "SOLAR_DIAGNOSTIC_REMOTE_IN_STREAM_GROUP_DESCRIPTOR: exclusive group "
                          "requires an InStreamGroupDescriptor");
            static_assert(
                std::convertible_to<decltype(Group::descriptor), InStreamGroupDescriptor>,
                "SOLAR_DIAGNOSTIC_REMOTE_IN_STREAM_GROUP_DESCRIPTOR: exclusive group descriptor "
                "has the wrong type");
            static_assert(Group::descriptor.id.value != 0,
                          "SOLAR_DIAGNOSTIC_REMOTE_IN_STREAM_GROUP_ZERO_ID: exclusive group ID "
                          "must be nonzero");
            static_assert(!Group::descriptor.name.empty(),
                          "SOLAR_DIAGNOSTIC_REMOTE_IN_STREAM_GROUP_EMPTY_NAME: exclusive group "
                          "name must not be empty");
            return Group::descriptor.id.value;
        }
    }();
};

struct NoInStreamMetadata
{
    static constexpr InStreamFlags flags{InStreamFlags::None};
    static constexpr ReplacementKind replacement{ReplacementKind::None};
    static constexpr std::uint32_t group_id{};
};

template <typename CapabilityT> struct CapabilityInStreamMetadata
{
    using type = NoInStreamMetadata;
};

template <auto Consumer, typename... Policies>
struct CapabilityInStreamMetadata<InStream<Consumer, Policies...>>
{
    using type = InStreamMetadata<Policies...>;
};

template <typename T>
struct QueueDelivery : std::integral_constant<DeliveryKind, DeliveryKind::None>
{};

template <std::size_t Depth>
struct QueueDelivery<Queue<Depth, DropOldest>>
    : std::integral_constant<DeliveryKind, DeliveryKind::QueueDropOldest>
{};

template <std::size_t Depth>
struct QueueDelivery<Queue<Depth, DropNewest>>
    : std::integral_constant<DeliveryKind, DeliveryKind::QueueDropNewest>
{};

template <std::size_t Depth>
struct QueueDelivery<Queue<Depth, Reject>>
    : std::integral_constant<DeliveryKind, DeliveryKind::QueueReject>
{};

template <typename... Policies> consteval std::uint32_t policy_rate()
{
    std::uint32_t value{};
    ((value = PolicyRate<Policies>::value != 0 ? PolicyRate<Policies>::value : value), ...);
    return value;
}

template <typename... Policies> consteval std::uint16_t policy_batch()
{
    std::uint16_t value{1};
    ((value = PolicyBatch<Policies>::value != 0 ? PolicyBatch<Policies>::value : value), ...);
    return value;
}

template <typename... Policies> consteval std::uint16_t policy_window()
{
    std::uint16_t value{};
    ((value = PolicyWindow<Policies>::value != 0 ? PolicyWindow<Policies>::value : value), ...);
    return value;
}

template <typename... Policies> consteval DeliveryKind policy_delivery()
{
    DeliveryKind value{DeliveryKind::None};
    ((value = std::same_as<Policies, Latest>       ? DeliveryKind::Latest
              : PolicyWindow<Policies>::value != 0 ? DeliveryKind::Reliable
              : QueueDelivery<Policies>::value != DeliveryKind::None
                  ? QueueDelivery<Policies>::value
                  : value),
     ...);
    return value;
}

template <typename CapabilityT> struct CapabilityPolicy;

struct StreamPublication
{
    static constexpr Capability kind = Capability::OutStream;
};

template <typename T> struct LoanedAcquisition : std::false_type
{};

template <typename Pool> struct LoanedAcquisition<Loaned<Pool>> : std::true_type
{};

template <auto Reader> struct CapabilityPolicy<Query<Reader>>
{
    static constexpr Permission permission = Permission::Observe;
    static constexpr std::uint32_t rate{};
    static constexpr std::uint16_t batch{1};
    static constexpr std::uint16_t window{};
    static constexpr DeliveryKind delivery{DeliveryKind::None};
    static constexpr CapabilityFlags flags{CapabilityFlags::Cancellation};
};

template <auto WriterT> struct CapabilityPolicy<Update<WriterT>>
{
    static constexpr Permission permission = Permission::Configure;
    static constexpr std::uint32_t rate{};
    static constexpr std::uint16_t batch{1};
    static constexpr std::uint16_t window{};
    static constexpr DeliveryKind delivery{DeliveryKind::None};
    static constexpr CapabilityFlags flags{CapabilityFlags::Cancellation};
};

template <typename... Policies> struct CapabilityPolicy<Watch<Policies...>>
{
    static constexpr Permission permission = Permission::Observe;
    static constexpr std::uint32_t rate = policy_rate<Policies...>();
    static constexpr std::uint16_t batch = policy_batch<Policies...>();
    static constexpr std::uint16_t window{};
    static constexpr DeliveryKind delivery = [] {
        constexpr auto declared = policy_delivery<Policies...>();
        return declared == DeliveryKind::None ? DeliveryKind::QueueDropOldest : declared;
    }();
    static constexpr CapabilityFlags flags{CapabilityFlags::None};
};

template <typename Acquisition, typename... Policies>
struct CapabilityPolicy<OutStream<Acquisition, Policies...>>
{
    static constexpr Permission permission = Permission::Observe;
    static constexpr std::uint32_t rate = policy_rate<Policies...>();
    static constexpr std::uint16_t batch = policy_batch<Policies...>();
    static constexpr std::uint16_t window{};
    static constexpr DeliveryKind delivery = [] {
        constexpr auto declared = policy_delivery<Policies...>();
        if constexpr (declared != DeliveryKind::None) {
            return declared;
        } else if constexpr (std::same_as<Acquisition, Push>) {
            return DeliveryKind::QueueDropOldest;
        } else if constexpr (LoanedAcquisition<Acquisition>::value) {
            return DeliveryKind::QueueReject;
        } else {
            return DeliveryKind::Latest;
        }
    }();
    static constexpr CapabilityFlags flags =
        std::same_as<Acquisition, Push> ? CapabilityFlags::BatchedFraming : CapabilityFlags::None;
};

template <auto Consumer, typename... Policies>
struct CapabilityPolicy<InStream<Consumer, Policies...>>
{
    static constexpr Permission permission = Permission::Control;
    static constexpr std::uint32_t rate = policy_rate<Policies...>();
    static constexpr std::uint16_t batch = policy_batch<Policies...>();
    static constexpr std::uint16_t window = policy_window<Policies...>();
    static constexpr DeliveryKind delivery = policy_delivery<Policies...>();
    static constexpr CapabilityFlags flags{CapabilityFlags::Cancellation};
};

template <> struct CapabilityPolicy<StreamPublication>
{
    static constexpr Permission permission = Permission::Observe;
    static constexpr std::uint32_t rate{};
    static constexpr std::uint16_t batch{1};
    static constexpr std::uint16_t window{};
    static constexpr DeliveryKind delivery{DeliveryKind::Latest};
    static constexpr CapabilityFlags flags{CapabilityFlags::None};
};

template <Permission PermissionT> consteval std::uint8_t permission_bit()
{
    return static_cast<std::uint8_t>(1U << (static_cast<std::uint8_t>(PermissionT) - 1U));
}

inline constexpr std::size_t capability_record_size = 28;

template <EndpointDomain Domain, typename EndpointT, typename CapabilityT>
consteval void emit_capability(Writer& writer)
{
    using Policy = CapabilityPolicy<CapabilityT>;
    using Inbound = typename CapabilityInStreamMetadata<CapabilityT>::type;
    writer.record(RecordKind::Capability, static_cast<std::uint16_t>(capability_record_size));
    writer.u8(static_cast<std::uint8_t>(Domain));
    writer.u8(static_cast<std::uint8_t>(CapabilityT::kind));
    writer.u8(permission_bit<Policy::permission>());
    writer.u8(static_cast<std::uint8_t>(Schema<typename EndpointT::Value>::codec));
    writer.u32(EndpointT::descriptor.id.value);
    writer.u32(Policy::rate);
    writer.u16(Policy::batch);
    writer.u16(Policy::window);
    writer.u8(static_cast<std::uint8_t>(Policy::delivery));
    writer.u8(bits(Policy::flags));
    writer.u8(bits(Inbound::flags));
    writer.u8(static_cast<std::uint8_t>(Inbound::replacement));
    writer.u32(Inbound::group_id);
}

template <typename DataT, typename... CapabilityTypes>
consteval void emit_data_capabilities_for(Writer& writer, Capabilities<CapabilityTypes...>)
{
    using Sorted = sort_t<TypeList<CapabilityTypes...>, CapabilityKindLess>;
    []<typename... SortedCapabilities>(Writer& output, TypeList<SortedCapabilities...>) consteval {
        (emit_capability<EndpointDomain::Data, DataT, SortedCapabilities>(output), ...);
    }(writer, Sorted{});
}

template <typename... DataTypes>
consteval void emit_data_capabilities(Writer& writer, TypeList<DataTypes...>)
{
    (emit_data_capabilities_for<DataTypes>(writer, typename DataTypes::Capabilities{}), ...);
}

template <typename TopicT> consteval void emit_topic_capability(Writer& writer)
{
    using Publication = typename TopicPublication<TopicT>::type;
    emit_capability<EndpointDomain::Topic, TopicT, Publication>(writer);
}

template <typename... TopicTypes>
consteval void emit_topic_capabilities(Writer& writer, TypeList<TopicTypes...>)
{
    (emit_topic_capability<TopicTypes>(writer), ...);
}

template <typename StreamT> consteval void emit_stream_capability(Writer& writer)
{
    emit_capability<EndpointDomain::Stream, StreamT, StreamPublication>(writer);
}

template <typename... StreamTypes>
consteval void emit_stream_capabilities(Writer& writer, TypeList<StreamTypes...>)
{
    (emit_stream_capability<StreamTypes>(writer), ...);
}

template <typename CapabilityT> struct CapabilityInStreamGroups
{
    using type = TypeList<>;
};

template <auto Consumer, typename... Policies>
struct CapabilityInStreamGroups<InStream<Consumer, Policies...>>
{
  private:
    using Metadata = InStreamMetadata<Policies...>;

  public:
    using type = std::conditional_t<Metadata::exclusive_count == 0, TypeList<>,
                                    TypeList<typename Metadata::Group>>;
};

template <typename CapabilitiesT> struct CapabilityListInStreamGroups;

template <typename... CapabilityTypes>
struct CapabilityListInStreamGroups<Capabilities<CapabilityTypes...>>
{
    using type =
        unique_t<concat_t<typename CapabilityInStreamGroups<CapabilityTypes>::type...>>;
};

template <typename DataListT> struct DataInStreamGroups;

template <typename... DataTypes> struct DataInStreamGroups<TypeList<DataTypes...>>
{
    using type =
        unique_t<concat_t<typename CapabilityListInStreamGroups<
            typename DataTypes::Capabilities>::type...>>;
};

template <typename... GroupTypes> consteval bool unique_group_ids(TypeList<GroupTypes...>)
{
    constexpr std::array<std::uint32_t, sizeof...(GroupTypes)> ids{
        GroupTypes::descriptor.id.value...};
    for (std::size_t left{}; left < ids.size(); ++left) {
        for (std::size_t right = left + 1; right < ids.size(); ++right) {
            if (ids[left] == ids[right]) {
                return false;
            }
        }
    }
    return true;
}

template <typename... GroupTypes> consteval bool unique_group_names(TypeList<GroupTypes...>)
{
    constexpr std::array<std::string_view, sizeof...(GroupTypes)> names{
        GroupTypes::descriptor.name...};
    for (std::size_t left{}; left < names.size(); ++left) {
        for (std::size_t right = left + 1; right < names.size(); ++right) {
            if (names[left] == names[right]) {
                return false;
            }
        }
    }
    return true;
}

template <typename GroupT> consteval std::size_t in_stream_group_record_size()
{
    return 16 + GroupT::descriptor.name.size() + GroupT::descriptor.description.size();
}

template <typename GroupT> consteval void emit_in_stream_group(Writer& writer)
{
    constexpr auto descriptor = GroupT::descriptor;
    constexpr auto size = in_stream_group_record_size<GroupT>();
    static_assert(size <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_SIZE: inbound stream group record "
                  "exceeds uint16");
    writer.record(RecordKind::InStreamGroup, static_cast<std::uint16_t>(size));
    writer.u32(descriptor.id.value);
    writer.u16(descriptor.version);
    writer.u16(static_cast<std::uint16_t>(descriptor.name.size()));
    writer.u16(static_cast<std::uint16_t>(descriptor.description.size()));
    writer.u16(0);
    writer.text(descriptor.name);
    writer.text(descriptor.description);
}

template <typename... GroupTypes>
consteval void emit_in_stream_groups(Writer& writer, TypeList<GroupTypes...>)
{
    (emit_in_stream_group<GroupTypes>(writer), ...);
}

template <typename... GroupTypes>
consteval std::size_t in_stream_group_text_size(TypeList<GroupTypes...>)
{
    return (std::size_t{} + ... +
            (GroupTypes::descriptor.name.size() + GroupTypes::descriptor.description.size()));
}

template <typename List> struct DataCapabilityCount;

template <typename... DataTypes>
struct DataCapabilityCount<TypeList<DataTypes...>>
    : std::integral_constant<std::size_t, (list_size_v<typename DataTypes::Capabilities::Entries> +
                                           ... + std::size_t{0})>
{};

} // namespace detail

template <typename System> struct Image
{
    using AuthoredSchemas =
        typename detail::Declarations<typename System::RemoteSchemaCatalog::EntryTypes>::type;
    using Data = detail::sort_t<
        typename detail::Declarations<typename System::RemoteDataCatalog::EntryTypes>::type,
        detail::EndpointIdLess>;
    using Actions = detail::sort_t<
        typename detail::Declarations<typename System::RemoteActionCatalog::EntryTypes>::type,
        detail::EndpointIdLess>;
    using Topics = detail::sort_t<
        typename detail::Declarations<typename System::RemoteTopicCatalog::EntryTypes>::type,
        detail::EndpointIdLess>;
    using Streams = detail::sort_t<
        typename detail::Declarations<typename System::RemoteStreamCatalog::EntryTypes>::type,
        detail::EndpointIdLess>;
    using Links = detail::sort_t<
        typename detail::Declarations<typename System::RemoteLinkCatalog::EntryTypes>::type,
        detail::EndpointIdLess>;
    using InStreamGroups = detail::sort_t<typename detail::DataInStreamGroups<Data>::type,
                                          detail::EndpointIdLess>;
    using InitialCandidateSchemas =
        unique_t<concat_t<AuthoredSchemas, typename detail::DataSchemas<Data>::type,
                          typename detail::ActionSchemas<Actions>::type,
                          typename detail::ValueSchemas<Topics>::type,
                          typename detail::ValueSchemas<Streams>::type>>;
    using InitialObjectSchemas = filter_t<InitialCandidateSchemas, detail::IsObjectSchema>;
    using InitialReferencedEnums = typename detail::ReferencedEnums<InitialObjectSchemas>::type;
    using CandidateSchemas =
        unique_t<concat_t<InitialCandidateSchemas, InitialReferencedEnums,
                          typename detail::ReferencedStatusSchemas<InitialObjectSchemas>::type>>;
    using CandidateObjectSchemas = filter_t<CandidateSchemas, detail::IsObjectSchema>;
    using CandidateEnumSchemas = filter_t<CandidateSchemas, detail::IsEnumerationSchema>;
    using ReferencedEnums = typename detail::ReferencedEnums<CandidateObjectSchemas>::type;

    static_assert(detail::AllContained<ReferencedEnums, AuthoredSchemas>::value,
                  "SOLAR_DIAGNOSTIC_REMOTE_UNBOUND_ENUM_SCHEMA: enum field schema must be "
                  "present in the effective Remote schema catalog");

    using Schemas = detail::sort_t<unique_t<concat_t<CandidateObjectSchemas, CandidateEnumSchemas>>,
                                   detail::SchemaIdLess>;
    using ObjectSchemas = filter_t<Schemas, detail::IsObjectSchema>;
    using EnumSchemas = filter_t<Schemas, detail::IsEnumerationSchema>;

    static_assert(detail::validate_schemas(Schemas{}));
    static_assert(detail::unique_schema_ids(Schemas{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_SCHEMA_ID: effective schemas share a TypeId");
    static_assert(detail::unique_schema_names(Schemas{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_SCHEMA_NAME: effective schemas share a "
                  "name");
    static_assert(detail::unique_group_ids(InStreamGroups{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM_GROUP_ID: inbound stream groups "
                  "share a stable ID");
    static_assert(detail::unique_group_names(InStreamGroups{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_IN_STREAM_GROUP_NAME: inbound stream groups "
                  "share a name");

    static constexpr std::size_t schema_count = list_size_v<Schemas>;
    static constexpr std::size_t data_count = list_size_v<Data>;
    static constexpr std::size_t action_count = list_size_v<Actions>;
    static constexpr std::size_t topic_count = list_size_v<Topics>;
    static constexpr std::size_t stream_count = list_size_v<Streams>;
    static constexpr std::size_t link_count = list_size_v<Links>;
    static constexpr std::size_t in_stream_group_count = list_size_v<InStreamGroups>;
    static constexpr std::size_t field_count = detail::SchemaFieldCount<ObjectSchemas>::value;
    static constexpr std::size_t enum_value_count =
        detail::SchemaEnumValueCount<EnumSchemas>::value;
    static constexpr std::size_t capability_count =
        detail::DataCapabilityCount<Data>::value + topic_count + stream_count;
    static constexpr std::size_t record_count = schema_count + field_count + enum_value_count +
                                                data_count + action_count + topic_count +
                                                stream_count + capability_count + link_count +
                                                in_stream_group_count;

    static constexpr std::size_t byte_count =
        image_header_size + schema_count * 24 + field_count * 34 + enum_value_count * 24 +
        data_count * 20 + action_count * 28 + topic_count * 20 + stream_count * 20 +
        capability_count * detail::capability_record_size + link_count * 16 +
        in_stream_group_count * 16 +
        detail::schema_text_size(Schemas{}) + detail::field_text_size(ObjectSchemas{}) +
        detail::enum_text_size(EnumSchemas{}) + detail::endpoint_text_size(Data{}) +
        detail::endpoint_text_size(Actions{}) + detail::endpoint_text_size(Topics{}) +
        detail::endpoint_text_size(Streams{}) + detail::endpoint_text_size(Links{}) +
        detail::in_stream_group_text_size(InStreamGroups{});

    static_assert(record_count <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_CEILING: manifest record count exceeds "
                  "the protocol format");
    static_assert(byte_count <= UINT32_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_SIZE_CEILING: manifest exceeds the protocol "
                  "format");

    inline static constexpr auto bytes = [] consteval {
        std::array<std::byte, byte_count> output{};
        detail::Writer writer{output};
        writer.text("SLRM");
        writer.u16(format_version);
        writer.u8(protocol::major_version);
        writer.u8(protocol::minor_version);
        writer.u16(static_cast<std::uint16_t>(record_count));
        writer.u16(0);
        writer.u32(static_cast<std::uint32_t>(byte_count));
        detail::emit_schemas(writer, Schemas{});
        detail::emit_fields(writer, ObjectSchemas{});
        detail::emit_enum_values(writer, EnumSchemas{});
        detail::emit_data_records(writer, Data{});
        detail::emit_actions(writer, Actions{});
        detail::emit_value_endpoints<RecordKind::Topic>(writer, Topics{});
        detail::emit_value_endpoints<RecordKind::Stream>(writer, Streams{});
        detail::emit_data_capabilities(writer, Data{});
        detail::emit_topic_capabilities(writer, Topics{});
        detail::emit_stream_capabilities(writer, Streams{});
        detail::emit_links(writer, Links{});
        detail::emit_in_stream_groups(writer, InStreamGroups{});
        if (writer.offset != output.size()) {
            __builtin_abort();
        }
        return output;
    }();
    inline static constexpr auto digest = remote::detail::sha256(std::span{bytes});
};

} // namespace solar::remote::manifest

#define SOLAR_DETAIL_REMOTE_JOIN_(LEFT, RIGHT) LEFT##RIGHT
#define SOLAR_DETAIL_REMOTE_JOIN(LEFT, RIGHT) SOLAR_DETAIL_REMOTE_JOIN_(LEFT, RIGHT)
#if defined(__ZEPHYR__)
#define SOLAR_DETAIL_REMOTE_MANIFEST_SECTION "._solar_remote_manifest.static.image"
#elif defined(__APPLE__)
#define SOLAR_DETAIL_REMOTE_MANIFEST_SECTION "__DATA,__solar_remote"
#else
#define SOLAR_DETAIL_REMOTE_MANIFEST_SECTION ".solar.remote.manifest"
#endif
#define SOLAR_REMOTE_EMIT_MANIFEST(SYSTEM)                                                         \
    [[gnu::used, gnu::section(SOLAR_DETAIL_REMOTE_MANIFEST_SECTION)]] static constexpr auto        \
    SOLAR_DETAIL_REMOTE_JOIN(solar_remote_manifest_, __COUNTER__) =                                \
        solar::remote::manifest::Image<SYSTEM>::bytes
