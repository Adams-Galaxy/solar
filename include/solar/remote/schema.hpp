#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "solar/core/fixed_string.hpp"

namespace solar::remote
{

using TypeId = std::uint32_t;
using MethodId = std::uint32_t;
using TopicId = std::uint32_t;
using ObservableId = std::uint32_t;

/**
 * @brief Direction of a Remote topic relative to the firmware device.
 */
enum class Direction : std::uint8_t
{
    DeviceToHost = 1,
    HostToDevice = 2,
    Bidirectional = 3,
};

/**
 * @brief Queueing/drop behavior requested by a Remote topic.
 */
enum class TopicPolicy : std::uint8_t
{
    LatestOnly = 1,
    QueueDropOldest = 2,
    QueueDropNewest = 3,
};

/**
 * @brief Whether an observable supports immediate snapshots, sampled streams, or both.
 */
enum class ObservableMode : std::uint8_t
{
    Snapshot = 1,
    Sampled = 2,
    SnapshotAndSampled = 3,
};

/**
 * @brief Delivery policy requested by an observable stream.
 */
enum class ObservablePolicy : std::uint8_t
{
    LatestOnly = 1,
    Reliable = 2,
};

/**
 * @brief Runtime descriptor for a Remote schema type.
 */
struct TypeDescriptor
{
    TypeId id = 0;
    const char *name = "";
    std::uint16_t version = 1;
    std::uint16_t max_size = 0;
};

/**
 * @brief Runtime descriptor for a Remote request/response method.
 */
struct MethodDescriptor
{
    MethodId id = 0;
    const char *name = "";
    TypeId request_type = 0;
    TypeId response_type = 0;
    std::uint16_t version = 1;
};

/**
 * @brief Runtime descriptor for a Remote pub/sub topic.
 */
struct TopicDescriptor
{
    TopicId id = 0;
    const char *name = "";
    TypeId payload_type = 0;
    Direction direction = Direction::DeviceToHost;
    TopicPolicy policy = TopicPolicy::LatestOnly;
    std::uint16_t version = 1;
};

/**
 * @brief Runtime descriptor for a Remote observable value.
 */
struct ObservableDescriptor
{
    ObservableId id = 0;
    const char *name = "";
    TypeId payload_type = 0;
    ObservableMode mode = ObservableMode::SnapshotAndSampled;
    ObservablePolicy policy = ObservablePolicy::LatestOnly;
    std::uint16_t min_interval_ms = 100;
    std::uint16_t max_interval_ms = 5000;
    std::uint16_t version = 1;
};

constexpr std::uint32_t fnv1a32(std::string_view text)
{
    std::uint32_t value = 0x811C9DC5u;
    for (char ch : text)
    {
        value ^= static_cast<std::uint8_t>(ch);
        value *= 0x01000193u;
    }
    return value;
}

template <FixedString Text, std::uint16_t Version = 1>
/**
 * @brief Stable 32-bit Remote ID derived from name and schema version.
 */
struct Id
{
    static constexpr std::string_view name = Text.view();
    static constexpr std::uint16_t version = Version;
    static constexpr std::uint32_t value = fnv1a32(name) ^ (static_cast<std::uint32_t>(Version) * 0x9E3779B9u);
};

template <typename... Entries>
/**
 * @brief Type-level catalog of Remote schema types.
 */
struct Types
{
    using SolarCatalogKind = Types;
    static constexpr std::size_t size = sizeof...(Entries);
};

/**
 * @brief Marker requesting that a service use the catalogs collected by System.
 */
struct UseSystemCatalog
{
};

template <typename... Entries>
/**
 * @brief Type-level catalog of Remote methods.
 */
struct Methods
{
    using SolarCatalogKind = Methods;
    static constexpr std::size_t size = sizeof...(Entries);
};

template <typename... Entries>
/**
 * @brief Type-level catalog of Remote topics.
 */
struct Topics
{
    using SolarCatalogKind = Topics;
    static constexpr std::size_t size = sizeof...(Entries);
};

template <typename... Entries>
/**
 * @brief Type-level catalog of Remote observables.
 */
struct Observables
{
    using SolarCatalogKind = Observables;
    static constexpr std::size_t size = sizeof...(Entries);
};

template <typename NameT, typename RequestT, typename ResponseT, auto Handler = nullptr, std::uint16_t Version = 1>
/**
 * @brief Static request/response method descriptor.
 */
struct Method
{
    using Name = NameT;
    using Request = RequestT;
    using Response = ResponseT;
    static constexpr auto handler = Handler;
    static constexpr std::uint16_t version = Version;
    static constexpr MethodId id = fnv1a32(NameT::view()) ^ (static_cast<std::uint32_t>(Version) * 0x9E3779B9u);
};

template <typename NameT, typename PayloadT, Direction DirectionValue, TopicPolicy PolicyValue = TopicPolicy::LatestOnly, std::uint16_t Version = 1>
/**
 * @brief Static pub/sub topic descriptor.
 */
struct Topic
{
    using Name = NameT;
    using Payload = PayloadT;
    static constexpr Direction direction = DirectionValue;
    static constexpr TopicPolicy policy = PolicyValue;
    static constexpr std::uint16_t version = Version;
    static constexpr TopicId id = fnv1a32(NameT::view()) ^ (static_cast<std::uint32_t>(Version) * 0x9E3779B9u);
};

template <typename NameT,
          typename PayloadT,
          ObservableMode ModeValue = ObservableMode::SnapshotAndSampled,
          ObservablePolicy PolicyValue = ObservablePolicy::LatestOnly,
          std::uint16_t MinIntervalMs = 100,
          std::uint16_t MaxIntervalMs = 5000,
          auto SnapshotHandler = nullptr,
          auto SampleHandler = nullptr,
          std::uint16_t Version = 1>
/**
 * @brief Static observable descriptor for snapshot and/or sampled values.
 */
struct Observable
{
    using Name = NameT;
    using Payload = PayloadT;
    static constexpr ObservableMode mode = ModeValue;
    static constexpr ObservablePolicy policy = PolicyValue;
    static constexpr std::uint16_t min_interval_ms = MinIntervalMs;
    static constexpr std::uint16_t max_interval_ms = MaxIntervalMs;
    static constexpr auto snapshot_handler = SnapshotHandler;
    static constexpr auto sample_handler = SampleHandler;
    static constexpr std::uint16_t version = Version;
    static constexpr ObservableId id = fnv1a32(NameT::view()) ^ (static_cast<std::uint32_t>(Version) * 0x9E3779B9u);
};

template <typename... Entries>
struct Expose
{
    using EntriesList = Types<Entries...>;
};

} // namespace solar::remote
