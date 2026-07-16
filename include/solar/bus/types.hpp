#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "solar/catalog/catalog.hpp"
#include "solar/component.hpp"
#include "solar/core/status.hpp"

namespace solar::bus
{

struct Tag
{};

struct MessageTag
{};

struct SubscriptionTag
{};

struct MessageIdentityDomain
{};

struct SubscriptionIdentityDomain
{};

using Id = StableId<MessageIdentityDomain>;
using MessageLocalId = LocalId<MessageTag>;
using SubscriptionLocalId = LocalId<SubscriptionTag>;

struct Descriptor
{
    std::string_view name;
    std::string_view description{};
    std::optional<Id> stable_id{};
    std::uint16_t version{1};
};

struct SubscriptionDescriptor
{
    std::string_view name;
    std::uint16_t version{1};
};

using MessageDescriptorView = catalog::BasicDescriptorView<MessageTag, Descriptor>;
using SubscriptionCatalogView =
    catalog::BasicDescriptorView<SubscriptionTag, SubscriptionDescriptor>;

enum class DeliveryKind : std::uint8_t
{
    Inline,
    InlineIsr,
    Queued,
    Latest,
    Coalesced,
};

enum class OverflowKind : std::uint8_t
{
    None,
    Reject,
    DropNewest,
    DropOldest,
    Wait,
};

enum class StopKind : std::uint8_t
{
    Drain,
    CancelPending,
};

enum class Operation : std::uint8_t
{
    Emit,
    TryEmit,
    TryEmitIsr,
    Query,
    Initialize,
    Stop,
};

enum class Reason : std::uint8_t
{
    NotReady,
    Disabled,
    NotRegistered,
    InvalidContext,
    RouteRejected,
    RouteTimedOut,
    ExecutorUnavailable,
    InlineHandlerFailed,
    InternalInvariant,
};

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::InternalInvariant};
    Operation operation{Operation::Emit};
    MessageLocalId message{};
    SubscriptionLocalId subscription{};
    std::uint16_t attempted_routes{};
    std::uint16_t accepted_routes{};
    std::uint16_t rejected_routes{};
    std::uint16_t dropped_routes{};
    int native_error{};
};

struct SubscriptionView
{
    SubscriptionLocalId local_id{};
    MessageLocalId message{};
    component::LocalId subscriber{};
    OwnerView origin_owner{};
    OriginKind origin{OriginKind::Direct};
    std::string_view message_name{};
    DeliveryKind delivery{DeliveryKind::Inline};
    OverflowKind overflow{OverflowKind::None};
    StopKind stop{StopKind::Drain};
    component::LocalId executor{};
    std::size_t capacity{};
    std::size_t payload_bytes{};
    bool isr_compatible{};
};

struct RouteRecord
{
    SubscriptionLocalId subscription{};
    MessageLocalId message{};
    component::LocalId subscriber{};
    DeliveryKind delivery{DeliveryKind::Inline};
    Status last_status{Status::NotReady};
    std::uint64_t considered{};
    std::uint64_t accepted{};
    std::uint64_t delivered{};
    std::uint64_t replacements{};
    std::uint64_t coalesced{};
    std::uint64_t dropped_newest{};
    std::uint64_t dropped_oldest{};
    std::uint64_t rejected{};
    std::uint64_t timed_out{};
    std::uint64_t handler_failed{};
    std::uint64_t executor_unavailable{};
    std::uint64_t cancelled{};
    std::uint32_t pending{};
    std::uint32_t pending_high_water{};
    std::uint32_t in_flight{};
    std::int64_t last_accept_tick{};
    std::int64_t last_delivery_tick{};
    Status first_handler_failure{Status::Ok};
    Status last_handler_failure{Status::Ok};
    bool has_handler_failure{};
    bool accepting{};
    bool draining{};
    bool quiescent{true};
};

struct RecordPage
{
    std::size_t offset{};
    std::size_t count{};
    std::size_t total{};

    [[nodiscard]] constexpr bool has_more() const noexcept
    {
        return offset + count < total;
    }
};

} // namespace solar::bus
