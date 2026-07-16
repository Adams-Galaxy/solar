#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "solar/remote/catalog.hpp"
#include "solar/remote/protocol.hpp"

namespace solar::remote::manifest
{

enum class RecordKind : std::uint8_t
{
    Schema = 1,
    Field = 2,
    Data = 3,
    Action = 4,
    Topic = 5,
    Stream = 6,
    Link = 7,
};

enum class ScalarKind : std::uint8_t
{
    Boolean = 1,
    Unsigned = 2,
    Signed = 3,
    Float32 = 4,
    Float64 = 5,
    Enumeration = 6,
    Text = 7,
    Bytes = 8,
};

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
    using type = Status;
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

template <typename List> struct DataSchemas;
template <typename... Types> struct DataSchemas<TypeList<Types...>>
{
    using type = TypeList<typename Types::Value...>;
};

template <typename List> struct ActionSchemas;
template <typename... Types> struct ActionSchemas<TypeList<Types...>>
{
    using type = TypeList<typename ActionRequest<Types>::type..., typename ActionResponse<Types>::type...,
                          typename ActionError<Types>::type...>;
};

template <typename List> struct ValueSchemas;
template <typename... Types> struct ValueSchemas<TypeList<Types...>>
{
    using type = TypeList<typename Types::Value...>;
};

template <typename T> consteval ScalarKind scalar_kind()
{
    using Value = remote::detail::optional_value_t<T>;
    if constexpr (std::is_same_v<Value, bool>) {
        return ScalarKind::Boolean;
    } else if constexpr (std::is_enum_v<Value>) {
        return ScalarKind::Enumeration;
    } else if constexpr (std::unsigned_integral<Value>) {
        return ScalarKind::Unsigned;
    } else if constexpr (std::signed_integral<Value>) {
        return ScalarKind::Signed;
    } else if constexpr (std::is_same_v<Value, float>) {
        return ScalarKind::Float32;
    } else if constexpr (std::is_same_v<Value, double>) {
        return ScalarKind::Float64;
    } else if constexpr (remote::detail::IsBoundedText<Value>::value) {
        return ScalarKind::Text;
    } else {
        return ScalarKind::Bytes;
    }
}

template <typename Value> consteval SchemaShape schema_shape()
{
    if constexpr (requires { Schema<Value>::shape; }) {
        return Schema<Value>::shape;
    }
    return SchemaShape::Object;
}

template <typename FieldsT> struct FieldCount;
template <typename... FieldsT>
struct FieldCount<Fields<FieldsT...>> : std::integral_constant<std::size_t, sizeof...(FieldsT)>
{};

template <typename List> struct SchemaFieldCount;
template <typename... Types> struct SchemaFieldCount<TypeList<Types...>>
    : std::integral_constant<std::size_t,
                             (FieldCount<typename Schema<Types>::Fields>::value + ... + 0U)>
{};

template <typename List> consteval std::size_t schema_text_size();
template <typename... Types> consteval std::size_t schema_text_size(TypeList<Types...>)
{
    return ((Schema<Types>::descriptor.name.size() + Schema<Types>::descriptor.description.size()) +
            ... + 0U);
}

template <typename List> consteval std::size_t endpoint_text_size();
template <typename... Types> consteval std::size_t endpoint_text_size(TypeList<Types...>)
{
    return ((Types::descriptor.name.size() + Types::descriptor.description.size()) + ... + 0U);
}

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

struct Writer
{
    std::span<std::byte> output;
    std::size_t offset{};

    constexpr void u8(std::uint8_t value) { output[offset++] = static_cast<std::byte>(value); }
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
    constexpr void text(std::string_view value)
    {
        for (char character : value) {
            u8(static_cast<std::uint8_t>(character));
        }
    }
};

template <typename FieldT> consteval void emit_field(Writer& writer, TypeId schema)
{
    using Member = remote::detail::field_member_t<FieldT>;
    static_assert(sizeof(remote::detail::optional_value_t<Member>) <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_FIELD_SIZE: field metadata width exceeds uint16");
    writer.u8(static_cast<std::uint8_t>(RecordKind::Field));
    writer.u8(static_cast<std::uint8_t>(scalar_kind<Member>()));
    writer.u16(FieldT::id);
    writer.u32(schema.value);
    writer.u16(static_cast<std::uint16_t>(sizeof(remote::detail::optional_value_t<Member>)));
    writer.u8(FieldT::required ? 1U : 0U);
    writer.u8(0);
}

template <typename Value, typename... FieldTypes>
consteval void emit_fields(Writer& writer, Fields<FieldTypes...>)
{
    (emit_field<FieldTypes>(writer, Schema<Value>::descriptor.id), ...);
}

template <typename Value> consteval void emit_schema(Writer& writer)
{
    static_assert(validate_schema<Value>());
    constexpr auto descriptor = Schema<Value>::descriptor;
    static_assert(descriptor.name.size() <= UINT16_MAX &&
                      descriptor.description.size() <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_DESCRIPTOR_SIZE: schema text exceeds manifest width");
    writer.u8(static_cast<std::uint8_t>(RecordKind::Schema));
    writer.u8(static_cast<std::uint8_t>(Schema<Value>::codec) |
              (static_cast<std::uint8_t>(schema_shape<Value>()) << 4U));
    writer.u16(descriptor.version);
    writer.u32(descriptor.id.value);
    writer.u32(static_cast<std::uint32_t>(Schema<Value>::max_encoded_size));
    writer.u16(static_cast<std::uint16_t>(descriptor.name.size()));
    writer.u16(static_cast<std::uint16_t>(descriptor.description.size()));
    writer.text(descriptor.name);
    writer.text(descriptor.description);
    emit_fields<Value>(writer, typename Schema<Value>::Fields{});
}

template <typename... Types> consteval void emit_schemas(Writer& writer, TypeList<Types...>)
{
    (emit_schema<Types>(writer), ...);
}

template <RecordKind Kind, typename Type> consteval void emit_value_endpoint(Writer& writer)
{
    constexpr auto descriptor = Type::descriptor;
    static_assert(descriptor.id.value != 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_MISSING_ENDPOINT_ID: endpoint ID must be nonzero");
    static_assert(validate_schema<typename Type::Value>());
    static_assert(descriptor.name.size() <= UINT16_MAX &&
                      descriptor.description.size() <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_DESCRIPTOR_SIZE: endpoint text exceeds manifest width");
    writer.u8(static_cast<std::uint8_t>(Kind));
    if constexpr (Kind == RecordKind::Data) {
        static_assert(CapabilityMask<typename Type::Capabilities>::unique(),
                      "SOLAR_DIAGNOSTIC_REMOTE_CAPABILITY_COLLISION: Data repeats a capability");
        writer.u8(CapabilityMask<typename Type::Capabilities>::value);
    } else {
        writer.u8(static_cast<std::uint8_t>(Schema<typename Type::Value>::codec));
    }
    writer.u16(descriptor.version);
    writer.u32(descriptor.id.value);
    writer.u32(Schema<typename Type::Value>::descriptor.id.value);
    writer.u16(static_cast<std::uint16_t>(descriptor.name.size()));
    writer.u16(static_cast<std::uint16_t>(descriptor.description.size()));
    writer.text(descriptor.name);
    writer.text(descriptor.description);
}

template <typename ActionT> consteval void emit_action(Writer& writer)
{
    using Request = typename ActionRequest<ActionT>::type;
    using Response = typename ActionResponse<ActionT>::type;
    using DomainError = typename ActionError<ActionT>::type;
    static_assert(validate_schema<Request>() && validate_schema<Response>() &&
                  validate_schema<DomainError>());
    constexpr auto descriptor = ActionT::descriptor;
    static_assert(descriptor.id.value != 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_MISSING_ENDPOINT_ID: Action ID must be nonzero");
    static_assert(descriptor.name.size() <= UINT16_MAX &&
                      descriptor.description.size() <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_DESCRIPTOR_SIZE: Action text exceeds manifest width");
    writer.u8(static_cast<std::uint8_t>(RecordKind::Action));
    writer.u8(PermissionMask<typename ActionAccess<ActionT>::type>::value);
    writer.u16(descriptor.version);
    writer.u32(descriptor.id.value);
    writer.u32(Schema<Request>::descriptor.id.value);
    writer.u32(Schema<Response>::descriptor.id.value);
    writer.u32(Schema<DomainError>::descriptor.id.value);
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

template <typename... Types> consteval void emit_actions(Writer& writer, TypeList<Types...>)
{
    (emit_action<Types>(writer), ...);
}

template <typename LinkT> consteval void emit_link(Writer& writer)
{
    constexpr auto descriptor = LinkT::descriptor;
    static_assert(descriptor.name.size() <= UINT16_MAX &&
                      descriptor.description.size() <= UINT16_MAX,
                  "SOLAR_DIAGNOSTIC_REMOTE_DESCRIPTOR_SIZE: link text exceeds manifest width");
    writer.u8(static_cast<std::uint8_t>(RecordKind::Link));
    writer.u8(0);
    writer.u16(descriptor.version);
    writer.u32(descriptor.id.value);
    writer.u32(0);
    writer.u16(static_cast<std::uint16_t>(descriptor.name.size()));
    writer.u16(static_cast<std::uint16_t>(descriptor.description.size()));
    writer.text(descriptor.name);
    writer.text(descriptor.description);
}

template <typename... Types> consteval void emit_links(Writer& writer, TypeList<Types...>)
{
    (emit_link<Types>(writer), ...);
}

} // namespace detail

template <typename System> struct Image
{
    using AuthoredSchemas =
        typename detail::Declarations<typename System::RemoteSchemaCatalog::EntryTypes>::type;
    using Data = typename detail::Declarations<typename System::RemoteDataCatalog::EntryTypes>::type;
    using Actions =
        typename detail::Declarations<typename System::RemoteActionCatalog::EntryTypes>::type;
    using Topics =
        typename detail::Declarations<typename System::RemoteTopicCatalog::EntryTypes>::type;
    using Streams =
        typename detail::Declarations<typename System::RemoteStreamCatalog::EntryTypes>::type;
    using Links = typename detail::Declarations<typename System::RemoteLinkCatalog::EntryTypes>::type;
    using Schemas = unique_t<concat_t<AuthoredSchemas, typename detail::DataSchemas<Data>::type,
                                      typename detail::ActionSchemas<Actions>::type,
                                      typename detail::ValueSchemas<Topics>::type,
                                      typename detail::ValueSchemas<Streams>::type>>;
    static_assert(detail::unique_schema_ids(Schemas{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_SCHEMA_ID: effective schemas share a TypeId");
    static_assert(detail::unique_schema_names(Schemas{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_SCHEMA_NAME: effective schemas share a name");

    static constexpr std::size_t schema_count = list_size_v<Schemas>;
    static constexpr std::size_t data_count = list_size_v<Data>;
    static constexpr std::size_t action_count = list_size_v<Actions>;
    static constexpr std::size_t topic_count = list_size_v<Topics>;
    static constexpr std::size_t stream_count = list_size_v<Streams>;
    static constexpr std::size_t link_count = list_size_v<Links>;
    static constexpr std::size_t field_count = detail::SchemaFieldCount<Schemas>::value;
    static constexpr std::size_t record_count = schema_count + field_count + data_count +
                                                action_count + topic_count + stream_count + link_count;
    static constexpr std::size_t byte_count =
        16 + schema_count * 16 + field_count * 12 + data_count * 16 + action_count * 24 +
        topic_count * 16 + stream_count * 16 + link_count * 16 +
        detail::schema_text_size(Schemas{}) + detail::endpoint_text_size(Data{}) +
        detail::endpoint_text_size(Actions{}) + detail::endpoint_text_size(Topics{}) +
        detail::endpoint_text_size(Streams{}) + detail::endpoint_text_size(Links{});
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
        writer.u16(1);
        writer.u8(protocol::major_version);
        writer.u8(protocol::minor_version);
        writer.u16(static_cast<std::uint16_t>(record_count));
        writer.u16(0);
        writer.u32(static_cast<std::uint32_t>(byte_count));
        detail::emit_schemas(writer, Schemas{});
        detail::emit_value_endpoints<RecordKind::Data>(writer, Data{});
        detail::emit_actions(writer, Actions{});
        detail::emit_value_endpoints<RecordKind::Topic>(writer, Topics{});
        detail::emit_value_endpoints<RecordKind::Stream>(writer, Streams{});
        detail::emit_links(writer, Links{});
        return output;
    }();
};

} // namespace solar::remote::manifest

#define SOLAR_DETAIL_REMOTE_JOIN_(LEFT, RIGHT) LEFT##RIGHT
#define SOLAR_DETAIL_REMOTE_JOIN(LEFT, RIGHT) SOLAR_DETAIL_REMOTE_JOIN_(LEFT, RIGHT)
#if defined(__ZEPHYR__)
#define SOLAR_DETAIL_REMOTE_MANIFEST_SECTION "._solar_remote_manifest.static.image"
#else
#define SOLAR_DETAIL_REMOTE_MANIFEST_SECTION ".solar.remote.manifest"
#endif
#define SOLAR_REMOTE_EMIT_MANIFEST(SYSTEM)                                                     \
    [[gnu::used, gnu::section(SOLAR_DETAIL_REMOTE_MANIFEST_SECTION)]] static constexpr auto       \
        SOLAR_DETAIL_REMOTE_JOIN(solar_remote_manifest_, __COUNTER__) =                          \
            solar::remote::manifest::Image<SYSTEM>::bytes
