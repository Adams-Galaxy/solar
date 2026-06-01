#pragma once

#include <cstdint>

#include "solar/core/boot.hpp"
#include "solar/core/status.hpp"
#include "solar/remote/codec.hpp"
#include "solar/remote/protocol.hpp"
#include "solar/remote/schema.hpp"

namespace solar::remote::generated
{

struct Empty
{
    static constexpr TypeId Type = Id<"solar.Empty">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 0;

    static bool encode(Writer &, Empty const &)
    {
        return true;
    }

    static bool decode(Reader &, Empty &)
    {
        return true;
    }
};

struct HelloRequest
{
    static constexpr TypeId Type = Id<"solar.HelloRequest">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 132;

    BoundedString<64> client_name{};
    BoundedString<64> client_version{};

    static bool encode(Writer &writer, HelloRequest const &value)
    {
        return writer.write_string(value.client_name) && writer.write_string(value.client_version);
    }

    static bool decode(Reader &reader, HelloRequest &value)
    {
        return reader.read_string(value.client_name) && reader.read_string(value.client_version);
    }
};

struct HelloResponse
{
    static constexpr TypeId Type = Id<"solar.HelloResponse">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 18;

    std::uint16_t protocol_version = ProtocolVersion;
    std::uint16_t frame_max_bytes = 1024;
    std::uint16_t heartbeat_ms = 1000;
    std::uint16_t session_timeout_ms = 5000;
    std::uint16_t method_count = 0;
    std::uint16_t topic_count = 0;
    std::uint16_t observable_count = 0;
    std::uint16_t type_count = 0;

    static bool encode(Writer &writer, HelloResponse const &value)
    {
        return writer.write_scalar(value.protocol_version) && writer.write_scalar(value.frame_max_bytes) &&
               writer.write_scalar(value.heartbeat_ms) && writer.write_scalar(value.session_timeout_ms) &&
               writer.write_scalar(value.method_count) && writer.write_scalar(value.topic_count) &&
               writer.write_scalar(value.observable_count) && writer.write_scalar(value.type_count);
    }

    static bool decode(Reader &reader, HelloResponse &value)
    {
        return reader.read_scalar(value.protocol_version) && reader.read_scalar(value.frame_max_bytes) &&
               reader.read_scalar(value.heartbeat_ms) && reader.read_scalar(value.session_timeout_ms) &&
               reader.read_scalar(value.method_count) && reader.read_scalar(value.topic_count) &&
               reader.read_scalar(value.observable_count) && reader.read_scalar(value.type_count);
    }
};

struct ListRequest
{
    static constexpr TypeId Type = Id<"solar.ListRequest">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 4;

    std::uint16_t offset = 0;
    std::uint16_t limit = 16;

    static bool encode(Writer &writer, ListRequest const &value)
    {
        return writer.write_scalar(value.offset) && writer.write_scalar(value.limit);
    }

    static bool decode(Reader &reader, ListRequest &value)
    {
        return reader.read_scalar(value.offset) && reader.read_scalar(value.limit);
    }
};

struct RemoteSummary
{
    static constexpr TypeId Type = Id<"solar.RemoteSummary">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 10;

    std::uint16_t methods = 0;
    std::uint16_t topics = 0;
    std::uint16_t observables = 0;
    std::uint16_t types = 0;
    std::uint16_t components = 0;

    static bool encode(Writer &writer, RemoteSummary const &value)
    {
        return writer.write_scalar(value.methods) && writer.write_scalar(value.topics) &&
               writer.write_scalar(value.observables) &&
               writer.write_scalar(value.types) && writer.write_scalar(value.components);
    }

    static bool decode(Reader &reader, RemoteSummary &value)
    {
        return reader.read_scalar(value.methods) && reader.read_scalar(value.topics) &&
               reader.read_scalar(value.observables) &&
               reader.read_scalar(value.types) && reader.read_scalar(value.components);
    }
};

struct MethodInfo
{
    static constexpr TypeId Type = Id<"solar.MethodInfo">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 82;

    std::uint32_t id = 0;
    std::uint32_t request_type = 0;
    std::uint32_t response_type = 0;
    std::uint16_t version = 1;
    BoundedString<64> name{};

    static bool encode(Writer &writer, MethodInfo const &value)
    {
        return writer.write_scalar(value.id) && writer.write_scalar(value.request_type) &&
               writer.write_scalar(value.response_type) && writer.write_scalar(value.version) &&
               writer.write_string(value.name);
    }

    static bool decode(Reader &reader, MethodInfo &value)
    {
        return reader.read_scalar(value.id) && reader.read_scalar(value.request_type) &&
               reader.read_scalar(value.response_type) && reader.read_scalar(value.version) &&
               reader.read_string(value.name);
    }
};

struct MethodListResponse
{
    static constexpr TypeId Type = Id<"solar.MethodListResponse">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 660;

    BoundedArray<MethodInfo, 8> methods{};

    static bool encode(Writer &writer, MethodListResponse const &value)
    {
        if (!writer.write_scalar(value.methods.size))
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.methods.size; ++i)
        {
            if (!MethodInfo::encode(writer, value.methods.data[i]))
            {
                return false;
            }
        }
        return true;
    }

    static bool decode(Reader &reader, MethodListResponse &value)
    {
        if (!reader.read_scalar(value.methods.size) || value.methods.size > value.methods.data.size())
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.methods.size; ++i)
        {
            if (!MethodInfo::decode(reader, value.methods.data[i]))
            {
                return false;
            }
        }
        return true;
    }
};

struct TopicInfo
{
    static constexpr TypeId Type = Id<"solar.TopicInfo">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 78;

    std::uint32_t id = 0;
    std::uint32_t payload_type = 0;
    std::uint8_t direction = 0;
    std::uint8_t policy = 0;
    std::uint16_t version = 1;
    BoundedString<64> name{};

    static bool encode(Writer &writer, TopicInfo const &value)
    {
        return writer.write_scalar(value.id) && writer.write_scalar(value.payload_type) &&
               writer.write_scalar(value.direction) && writer.write_scalar(value.policy) &&
               writer.write_scalar(value.version) && writer.write_string(value.name);
    }

    static bool decode(Reader &reader, TopicInfo &value)
    {
        return reader.read_scalar(value.id) && reader.read_scalar(value.payload_type) &&
               reader.read_scalar(value.direction) && reader.read_scalar(value.policy) &&
               reader.read_scalar(value.version) && reader.read_string(value.name);
    }
};

struct TopicListResponse
{
    static constexpr TypeId Type = Id<"solar.TopicListResponse">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 628;

    BoundedArray<TopicInfo, 8> topics{};

    static bool encode(Writer &writer, TopicListResponse const &value)
    {
        if (!writer.write_scalar(value.topics.size))
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.topics.size; ++i)
        {
            if (!TopicInfo::encode(writer, value.topics.data[i]))
            {
                return false;
            }
        }
        return true;
    }

    static bool decode(Reader &reader, TopicListResponse &value)
    {
        if (!reader.read_scalar(value.topics.size) || value.topics.size > value.topics.data.size())
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.topics.size; ++i)
        {
            if (!TopicInfo::decode(reader, value.topics.data[i]))
            {
                return false;
            }
        }
        return true;
    }
};

struct ObservableInfo
{
    static constexpr TypeId Type = Id<"solar.ObservableInfo">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 82;

    std::uint32_t id = 0;
    std::uint32_t payload_type = 0;
    std::uint8_t mode = 0;
    std::uint8_t policy = 0;
    std::uint16_t min_interval_ms = 0;
    std::uint16_t max_interval_ms = 0;
    std::uint16_t version = 1;
    BoundedString<64> name{};

    static bool encode(Writer &writer, ObservableInfo const &value)
    {
        return writer.write_scalar(value.id) && writer.write_scalar(value.payload_type) &&
               writer.write_scalar(value.mode) && writer.write_scalar(value.policy) &&
               writer.write_scalar(value.min_interval_ms) && writer.write_scalar(value.max_interval_ms) &&
               writer.write_scalar(value.version) && writer.write_string(value.name);
    }

    static bool decode(Reader &reader, ObservableInfo &value)
    {
        return reader.read_scalar(value.id) && reader.read_scalar(value.payload_type) &&
               reader.read_scalar(value.mode) && reader.read_scalar(value.policy) &&
               reader.read_scalar(value.min_interval_ms) && reader.read_scalar(value.max_interval_ms) &&
               reader.read_scalar(value.version) && reader.read_string(value.name);
    }
};

struct ObservableListResponse
{
    static constexpr TypeId Type = Id<"solar.ObservableListResponse">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 660;

    BoundedArray<ObservableInfo, 8> observables{};

    static bool encode(Writer &writer, ObservableListResponse const &value)
    {
        if (!writer.write_scalar(value.observables.size))
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.observables.size; ++i)
        {
            if (!ObservableInfo::encode(writer, value.observables.data[i]))
            {
                return false;
            }
        }
        return true;
    }

    static bool decode(Reader &reader, ObservableListResponse &value)
    {
        if (!reader.read_scalar(value.observables.size) || value.observables.size > value.observables.data.size())
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.observables.size; ++i)
        {
            if (!ObservableInfo::decode(reader, value.observables.data[i]))
            {
                return false;
            }
        }
        return true;
    }
};

struct SubscribeRequest
{
    static constexpr TypeId Type = Id<"solar.SubscribeRequest">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 7;

    std::uint32_t target_id = 0;
    std::uint16_t interval_ms = 0;
    std::uint8_t flags = 0;

    static bool encode(Writer &writer, SubscribeRequest const &value)
    {
        return writer.write_scalar(value.target_id) && writer.write_scalar(value.interval_ms) &&
               writer.write_scalar(value.flags);
    }

    static bool decode(Reader &reader, SubscribeRequest &value)
    {
        return reader.read_scalar(value.target_id) && reader.read_scalar(value.interval_ms) &&
               reader.read_scalar(value.flags);
    }
};

struct SubscribeResponse
{
    static constexpr TypeId Type = Id<"solar.SubscribeResponse">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 5;

    std::uint16_t subscription_id = 0;
    std::uint16_t interval_ms = 0;
    std::uint8_t flags = 0;

    static bool encode(Writer &writer, SubscribeResponse const &value)
    {
        return writer.write_scalar(value.subscription_id) && writer.write_scalar(value.interval_ms) &&
               writer.write_scalar(value.flags);
    }

    static bool decode(Reader &reader, SubscribeResponse &value)
    {
        return reader.read_scalar(value.subscription_id) && reader.read_scalar(value.interval_ms) &&
               reader.read_scalar(value.flags);
    }
};

struct UnsubscribeRequest
{
    static constexpr TypeId Type = Id<"solar.UnsubscribeRequest">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 2;

    std::uint16_t subscription_id = 0;

    static bool encode(Writer &writer, UnsubscribeRequest const &value)
    {
        return writer.write_scalar(value.subscription_id);
    }

    static bool decode(Reader &reader, UnsubscribeRequest &value)
    {
        return reader.read_scalar(value.subscription_id);
    }
};

struct TypeInfo
{
    static constexpr TypeId Type = Id<"solar.TypeInfo">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 76;

    std::uint32_t id = 0;
    std::uint16_t version = 1;
    std::uint16_t max_size = 0;
    BoundedString<64> name{};

    static bool encode(Writer &writer, TypeInfo const &value)
    {
        return writer.write_scalar(value.id) && writer.write_scalar(value.version) &&
               writer.write_scalar(value.max_size) && writer.write_string(value.name);
    }

    static bool decode(Reader &reader, TypeInfo &value)
    {
        return reader.read_scalar(value.id) && reader.read_scalar(value.version) &&
               reader.read_scalar(value.max_size) && reader.read_string(value.name);
    }
};

struct TypeListResponse
{
    static constexpr TypeId Type = Id<"solar.TypeListResponse">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 612;

    BoundedArray<TypeInfo, 8> types{};

    static bool encode(Writer &writer, TypeListResponse const &value)
    {
        if (!writer.write_scalar(value.types.size))
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.types.size; ++i)
        {
            if (!TypeInfo::encode(writer, value.types.data[i]))
            {
                return false;
            }
        }
        return true;
    }

    static bool decode(Reader &reader, TypeListResponse &value)
    {
        if (!reader.read_scalar(value.types.size) || value.types.size > value.types.data.size())
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.types.size; ++i)
        {
            if (!TypeInfo::decode(reader, value.types.data[i]))
            {
                return false;
            }
        }
        return true;
    }
};

struct ComponentInfo
{
    static constexpr TypeId Type = Id<"solar.ComponentInfo">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 132;

    BoundedString<64> name{};
    BoundedString<32> kind{};
    std::uint8_t state = 0;

    static bool encode(Writer &writer, ComponentInfo const &value)
    {
        return writer.write_string(value.name) && writer.write_string(value.kind) && writer.write_scalar(value.state);
    }

    static bool decode(Reader &reader, ComponentInfo &value)
    {
        return reader.read_string(value.name) && reader.read_string(value.kind) && reader.read_scalar(value.state);
    }
};

struct ComponentListResponse
{
    static constexpr TypeId Type = Id<"solar.ComponentListResponse">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 1076;

    BoundedArray<ComponentInfo, 8> components{};

    static bool encode(Writer &writer, ComponentListResponse const &value)
    {
        if (!writer.write_scalar(value.components.size))
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.components.size; ++i)
        {
            if (!ComponentInfo::encode(writer, value.components.data[i]))
            {
                return false;
            }
        }
        return true;
    }

    static bool decode(Reader &reader, ComponentListResponse &value)
    {
        if (!reader.read_scalar(value.components.size) || value.components.size > value.components.data.size())
        {
            return false;
        }
        for (std::uint16_t i = 0; i < value.components.size; ++i)
        {
            if (!ComponentInfo::decode(reader, value.components.data[i]))
            {
                return false;
            }
        }
        return true;
    }
};

struct BootReportResponse
{
    static constexpr TypeId Type = Id<"solar.BootReportResponse">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 40;

    std::uint8_t status = 0;
    std::uint8_t phase = 0;
    BoundedString<32> component{};

    static bool encode(Writer &writer, BootReportResponse const &value)
    {
        return writer.write_scalar(value.status) && writer.write_scalar(value.phase) && writer.write_string(value.component);
    }

    static bool decode(Reader &reader, BootReportResponse &value)
    {
        return reader.read_scalar(value.status) && reader.read_scalar(value.phase) && reader.read_string(value.component);
    }
};

struct ErrorResponse
{
    static constexpr TypeId Type = Id<"solar.ErrorResponse">::value;
    static constexpr std::uint16_t Version = 1;
    static constexpr std::uint16_t MaxSize = 78;

    std::uint16_t error_code = 0;
    std::uint8_t status = 0;
    std::uint32_t target_id = 0;
    BoundedString<64> message{};

    static bool encode(Writer &writer, ErrorResponse const &value)
    {
        return writer.write_scalar(value.error_code) && writer.write_scalar(value.status) &&
               writer.write_scalar(value.target_id) && writer.write_string(value.message);
    }

    static bool decode(Reader &reader, ErrorResponse &value)
    {
        return reader.read_scalar(value.error_code) && reader.read_scalar(value.status) &&
               reader.read_scalar(value.target_id) && reader.read_string(value.message);
    }
};

inline constexpr TypeDescriptor CoreTypes[] = {
    {Empty::Type, "solar.Empty", Empty::Version, Empty::MaxSize},
    {HelloRequest::Type, "solar.HelloRequest", HelloRequest::Version, HelloRequest::MaxSize},
    {HelloResponse::Type, "solar.HelloResponse", HelloResponse::Version, HelloResponse::MaxSize},
    {ListRequest::Type, "solar.ListRequest", ListRequest::Version, ListRequest::MaxSize},
    {RemoteSummary::Type, "solar.RemoteSummary", RemoteSummary::Version, RemoteSummary::MaxSize},
    {MethodInfo::Type, "solar.MethodInfo", MethodInfo::Version, MethodInfo::MaxSize},
    {MethodListResponse::Type, "solar.MethodListResponse", MethodListResponse::Version, MethodListResponse::MaxSize},
    {TopicInfo::Type, "solar.TopicInfo", TopicInfo::Version, TopicInfo::MaxSize},
    {TopicListResponse::Type, "solar.TopicListResponse", TopicListResponse::Version, TopicListResponse::MaxSize},
    {ObservableInfo::Type, "solar.ObservableInfo", ObservableInfo::Version, ObservableInfo::MaxSize},
    {ObservableListResponse::Type, "solar.ObservableListResponse", ObservableListResponse::Version, ObservableListResponse::MaxSize},
    {SubscribeRequest::Type, "solar.SubscribeRequest", SubscribeRequest::Version, SubscribeRequest::MaxSize},
    {SubscribeResponse::Type, "solar.SubscribeResponse", SubscribeResponse::Version, SubscribeResponse::MaxSize},
    {UnsubscribeRequest::Type, "solar.UnsubscribeRequest", UnsubscribeRequest::Version, UnsubscribeRequest::MaxSize},
    {TypeInfo::Type, "solar.TypeInfo", TypeInfo::Version, TypeInfo::MaxSize},
    {TypeListResponse::Type, "solar.TypeListResponse", TypeListResponse::Version, TypeListResponse::MaxSize},
    {ComponentInfo::Type, "solar.ComponentInfo", ComponentInfo::Version, ComponentInfo::MaxSize},
    {ComponentListResponse::Type, "solar.ComponentListResponse", ComponentListResponse::Version, ComponentListResponse::MaxSize},
    {BootReportResponse::Type, "solar.BootReportResponse", BootReportResponse::Version, BootReportResponse::MaxSize},
    {ErrorResponse::Type, "solar.ErrorResponse", ErrorResponse::Version, ErrorResponse::MaxSize},
};

inline constexpr MethodDescriptor CoreMethods[] = {
    {Id<"solar.hello">::value, "solar.hello", HelloRequest::Type, HelloResponse::Type, 1},
    {Id<"solar.remote.summary">::value, "solar.remote.summary", Empty::Type, RemoteSummary::Type, 1},
    {Id<"solar.remote.methods.list">::value, "solar.remote.methods.list", ListRequest::Type, MethodListResponse::Type, 1},
    {Id<"solar.remote.topics.list">::value, "solar.remote.topics.list", ListRequest::Type, TopicListResponse::Type, 1},
    {Id<"solar.remote.observables.list">::value, "solar.remote.observables.list", ListRequest::Type, ObservableListResponse::Type, 1},
    {Id<"solar.remote.types.list">::value, "solar.remote.types.list", ListRequest::Type, TypeListResponse::Type, 1},
    {Id<"solar.graph.components.list">::value, "solar.graph.components.list", ListRequest::Type, ComponentListResponse::Type, 1},
    {Id<"solar.boot.report">::value, "solar.boot.report", Empty::Type, BootReportResponse::Type, 1},
};

} // namespace solar::remote::generated
