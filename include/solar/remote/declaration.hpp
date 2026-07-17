#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#include "solar/core/type_list.hpp"
#include "solar/remote/types.hpp"

namespace solar::remote
{

template <typename Value> struct Schema;

template <FieldId Id, auto Member, bool Required = true> struct Field
{
    static_assert(Id != 0, "SOLAR_DIAGNOSTIC_REMOTE_FIELD_ZERO_ID: field ID zero is reserved");
    static_assert(std::is_member_object_pointer_v<decltype(Member)>,
                  "SOLAR_DIAGNOSTIC_REMOTE_FIELD_MEMBER: Field requires a data-member pointer");

    static constexpr FieldId id = Id;
    static constexpr auto member = Member;
    static constexpr bool required = Required;
};

template <typename... FieldTypes> struct Fields
{
    using Entries = TypeList<FieldTypes...>;
    static constexpr std::size_t size = sizeof...(FieldTypes);
};

namespace detail
{

template <typename T> struct IsFields : std::false_type
{};

template <typename... Entries> struct IsFields<Fields<Entries...>> : std::true_type
{};

template <typename T> inline constexpr bool is_fields_v = IsFields<T>::value;

template <typename T> struct IsOptional : std::false_type
{};

template <typename T> struct IsOptional<std::optional<T>> : std::true_type
{};

template <typename T> inline constexpr bool is_optional_v = IsOptional<T>::value;

template <typename T> struct OptionalValue
{
    using type = T;
};

template <typename T> struct OptionalValue<std::optional<T>>
{
    using type = T;
};

template <typename T> using optional_value_t = typename OptionalValue<T>::type;

template <typename T> struct IsBoundedText : std::false_type
{};

template <std::size_t Capacity> struct IsBoundedText<BoundedText<Capacity>> : std::true_type
{};

template <typename T> struct IsBoundedBytes : std::false_type
{};

template <std::size_t Capacity> struct IsBoundedBytes<BoundedBytes<Capacity>> : std::true_type
{};

template <typename T>
inline constexpr bool supported_scalar_v =
    std::is_same_v<T, bool> || std::integral<T> || std::floating_point<T> || std::is_enum_v<T> ||
    IsBoundedText<T>::value || IsBoundedBytes<T>::value;

template <typename FieldT> struct FieldMember;

template <FieldId Id, typename Owner, typename Member, Member Owner::* Pointer, bool Required>
struct FieldMember<Field<Id, Pointer, Required>>
{
    using OwnerType = Owner;
    using type = Member;
};

template <typename FieldT> using field_member_t = typename FieldMember<FieldT>::type;

template <typename... FieldTypes> consteval bool unique_field_ids(Fields<FieldTypes...>)
{
    constexpr std::array<FieldId, sizeof...(FieldTypes)> ids{FieldTypes::id...};
    for (std::size_t left{}; left < ids.size(); ++left) {
        for (std::size_t right = left + 1; right < ids.size(); ++right) {
            if (ids[left] == ids[right]) {
                return false;
            }
        }
    }
    return true;
}

template <typename... FieldTypes> consteval bool ordered_field_ids(Fields<FieldTypes...>)
{
    constexpr std::array<FieldId, sizeof...(FieldTypes)> ids{FieldTypes::id...};
    for (std::size_t index = 1; index < ids.size(); ++index) {
        if (ids[index - 1] >= ids[index]) {
            return false;
        }
    }
    return true;
}

template <typename... FieldTypes> consteval bool supported_fields(Fields<FieldTypes...>)
{
    return (supported_scalar_v<optional_value_t<field_member_t<FieldTypes>>> && ...);
}

} // namespace detail

template <typename Value>
concept SchemaType =
    requires {
        { Schema<Value>::descriptor } -> std::convertible_to<SchemaDescriptor>;
        typename Schema<Value>::Fields;
        { Schema<Value>::max_encoded_size } -> std::convertible_to<std::size_t>;
        { Schema<Value>::codec } -> std::convertible_to<Codec>;
    } && detail::is_fields_v<typename Schema<Value>::Fields> && std::is_object_v<Value> &&
    std::is_default_constructible_v<Value> && std::is_copy_constructible_v<Value>;

template <typename Value> consteval bool validate_schema()
{
    static_assert(SchemaType<Value>,
                  "SOLAR_DIAGNOSTIC_REMOTE_MISSING_SCHEMA: external value requires Schema<T>");
    using FieldList = typename Schema<Value>::Fields;
    static_assert(detail::unique_field_ids(FieldList{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_FIELD_ID: Schema field IDs must be unique");
    static_assert(detail::ordered_field_ids(FieldList{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_FIELD_ORDER: Schema fields must be ordered by ID");
    static_assert(detail::supported_fields(FieldList{}),
                  "SOLAR_DIAGNOSTIC_REMOTE_UNSUPPORTED_FIELD: Schema contains an unsupported or "
                  "unbounded field type");
    static_assert(Schema<Value>::descriptor.id.value != 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_MISSING_SCHEMA_ID: Schema requires a nonzero TypeId");
    static_assert(!Schema<Value>::descriptor.name.empty(),
                  "SOLAR_DIAGNOSTIC_REMOTE_EMPTY_SCHEMA_NAME: Schema name must not be empty");
    static_assert(Schema<Value>::descriptor.version != 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_SCHEMA_VERSION: Schema version zero is reserved");
    static_assert(Schema<Value>::max_encoded_size != 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_UNBOUNDED_SCHEMA: max_encoded_size must be nonzero");
#if defined(CONFIG_SOLAR_REMOTE_MAX_SCHEMA_FIELDS)
    static_assert(FieldList::size <= CONFIG_SOLAR_REMOTE_MAX_SCHEMA_FIELDS,
                  "SOLAR_DIAGNOSTIC_REMOTE_SCHEMA_FIELD_CEILING: Schema exceeds "
                  "CONFIG_SOLAR_REMOTE_MAX_SCHEMA_FIELDS");
#endif
#if defined(CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES)
    static_assert(Schema<Value>::max_encoded_size <= CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES,
                  "SOLAR_DIAGNOSTIC_REMOTE_SCHEMA_SIZE_CEILING: Schema exceeds "
                  "CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES");
#endif
    return true;
}

template <typename... CapabilitiesT> struct Capabilities
{
    using Entries = TypeList<CapabilitiesT...>;
};

template <auto Reader> struct Query
{
    static constexpr auto reader = Reader;
    static constexpr Capability kind = Capability::Query;
};

template <auto Writer> struct Update
{
    static constexpr auto writer = Writer;
    static constexpr Capability kind = Capability::Update;
};

template <typename... Policies> struct Watch
{
    using PolicyTypes = TypeList<Policies...>;
    static constexpr Capability kind = Capability::Watch;
};

struct Push
{};

template <auto Reader, typename... Policies> struct Poll
{
    static constexpr auto reader = Reader;
    using PolicyTypes = TypeList<Policies...>;
};

template <std::size_t Slots, std::size_t Bytes, std::size_t Alignment = alignof(std::max_align_t)>
struct LoanedPool
{
    static_assert(Slots > 0 && Bytes > 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_EMPTY_LOAN_POOL: LoanedPool dimensions must be "
                  "positive");
    static_assert((Alignment & (Alignment - 1U)) == 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_LOAN_ALIGNMENT: LoanedPool alignment must be a power "
                  "of two");
    static constexpr std::size_t slots = Slots;
    static constexpr std::size_t bytes = Bytes;
    static constexpr std::size_t alignment = Alignment;
};

template <typename PoolPolicy> struct Loaned
{
    using Pool = PoolPolicy;
};
struct Packed
{};
struct Cbor
{};
struct Inline
{};

template <typename Target> struct On
{
    using TargetType = Target;
};

template <typename Target> struct Mailbox
{
    using TargetType = Target;
};

template <typename Acquisition, typename... Policies> struct OutStream
{
    using AcquisitionType = Acquisition;
    using PolicyTypes = TypeList<Policies...>;
    static constexpr Capability kind = Capability::OutStream;
};

template <auto Consumer, typename... Policies> struct InStream
{
    static constexpr auto consumer = Consumer;
    using PolicyTypes = TypeList<Policies...>;
    static constexpr Capability kind = Capability::InStream;
};

template <std::uint32_t Hertz> struct MaxRate
{
    static constexpr std::uint32_t hertz = Hertz;
};

template <std::size_t Count> struct Batch
{
    static_assert(Count > 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_ZERO_BATCH: Remote Batch count must be positive");
    static constexpr std::size_t count = Count;
};

struct DropOldest
{};
struct DropNewest
{};
struct Reject
{};

template <std::size_t Depth, typename Overflow = DropOldest> struct Queue
{
    static_assert(Depth > 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_ZERO_QUEUE: Remote Queue depth must be positive");
    static constexpr std::size_t depth = Depth;
    using OverflowPolicy = Overflow;
};

struct Latest
{};
template <std::size_t Count> struct ReliableWindow
{
    static_assert(Count > 0,
                  "SOLAR_DIAGNOSTIC_REMOTE_EMPTY_RELIABLE_WINDOW: ReliableWindow must contain "
                  "at least one inbound slot");
    static constexpr std::size_t count = Count;
};
struct RejectWhenBusy
{};
struct SingleProducer
{};
struct MultipleProducers
{};

namespace detail
{

template <typename T> struct IsOn : std::false_type
{
    using type = void;
};

template <typename Target> struct IsOn<On<Target>> : std::true_type
{
    using type = Target;
};

template <typename... Policies> struct PollTarget
{
    using type = void;
};

template <typename Head, typename... Tail> struct PollTarget<Head, Tail...>
{
    using type = std::conditional_t<IsOn<Head>::value, typename IsOn<Head>::type,
                                    typename PollTarget<Tail...>::type>;
};

template <typename T> struct PollAcquisition
{
    static constexpr bool valid = false;
};

template <auto Reader, typename... Policies> struct PollAcquisition<Poll<Reader, Policies...>>
{
    static constexpr bool valid = true;
    static constexpr auto reader = Reader;
    using Target = typename PollTarget<Policies...>::type;
};

} // namespace detail

template <Permission... Grants> struct Requires
{
    static constexpr std::array permissions{Grants...};
};

namespace detail
{

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
template <typename T, typename = void> struct ActionExecution
{
    using type = void;
};
template <typename T> struct ActionExecution<T, std::void_t<typename T::Execution>>
{
    using type = typename T::Execution;
};

template <typename T> using action_request_t = typename ActionRequest<T>::type;
template <typename T> using action_response_t = typename ActionResponse<T>::type;
template <typename T> using action_error_t = typename ActionError<T>::type;
template <typename T> using action_access_t = typename ActionAccess<T>::type;
template <typename T> using action_execution_t = typename ActionExecution<T>::type;

struct ResponderToken
{
    std::uint16_t link{};
    std::uint32_t request{};
    std::uint32_t epoch{};
};

template <typename ActionT> struct ResponderFactory;

template <typename Access> struct PermissionMask;
template <Permission... Grants> struct PermissionMask<Requires<Grants...>>
{
    static constexpr std::uint8_t value =
        (std::uint8_t{} | ... |
         static_cast<std::uint8_t>(1U << (static_cast<unsigned>(Grants) - 1U)));
};

} // namespace detail

template <typename ActionT> class Responder
{
  public:
    using Response = detail::action_response_t<ActionT>;
    using Error = detail::action_error_t<ActionT>;

    Responder(const Responder&) = delete;
    Responder& operator=(const Responder&) = delete;

    Responder(Responder&& other) noexcept
        : token_(other.token_), success_(other.success_), failure_(other.failure_),
          abandon_(other.abandon_), cancelled_(other.cancelled_), active_(other.active_)
    {
        other.active_ = false;
    }

    Responder& operator=(Responder&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }
        abandon();
        token_ = other.token_;
        success_ = other.success_;
        failure_ = other.failure_;
        abandon_ = other.abandon_;
        cancelled_ = other.cancelled_;
        active_ = other.active_;
        other.active_ = false;
        return *this;
    }

    ~Responder()
    {
        abandon();
    }

    [[nodiscard]] Result<void> complete(Response response) noexcept
    {
        if (!active_ || success_ == nullptr) {
            return fail<solar::Error>({.status = solar::Status::NotReady});
        }
        active_ = false;
        return success_(token_, std::move(response));
    }

    [[nodiscard]] Result<void> complete() noexcept
        requires std::same_as<Response, Empty>
    {
        return complete(Empty{});
    }

    [[nodiscard]] Result<void> reject(Error error) noexcept
    {
        if (!active_ || failure_ == nullptr) {
            return fail<solar::Error>({.status = solar::Status::NotReady});
        }
        active_ = false;
        return failure_(token_, std::move(error));
    }

    [[nodiscard]] bool cancelled() const noexcept
    {
        return !active_ || cancelled_ == nullptr || cancelled_(token_);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return active_ && !cancelled();
    }

  private:
    using Success = Result<void> (*)(detail::ResponderToken, Response&&) noexcept;
    using Failure = Result<void> (*)(detail::ResponderToken, Error&&) noexcept;
    using Abandon = void (*)(detail::ResponderToken) noexcept;
    using Cancelled = bool (*)(detail::ResponderToken) noexcept;

    friend struct detail::ResponderFactory<ActionT>;

    Responder(detail::ResponderToken token, Success success, Failure failure, Abandon abandon,
              Cancelled cancelled) noexcept
        : token_(token), success_(success), failure_(failure), abandon_(abandon),
          cancelled_(cancelled), active_(true)
    {}

    void abandon() noexcept
    {
        if (active_ && abandon_ != nullptr) {
            active_ = false;
            abandon_(token_);
        }
    }

    detail::ResponderToken token_{};
    Success success_{};
    Failure failure_{};
    Abandon abandon_{};
    Cancelled cancelled_{};
    bool active_{};
};

namespace detail
{

template <typename ActionT> struct ResponderFactory
{
    [[nodiscard]] static Responder<ActionT>
    make(ResponderToken token, typename Responder<ActionT>::Success success,
         typename Responder<ActionT>::Failure failure, typename Responder<ActionT>::Abandon abandon,
         typename Responder<ActionT>::Cancelled cancelled) noexcept
    {
        return Responder<ActionT>{token, success, failure, abandon, cancelled};
    }
};

} // namespace detail

template <typename T>
concept Data = requires {
    typename T::Value;
    typename T::Capabilities;
    { T::descriptor } -> std::convertible_to<DataDescriptor>;
} && SchemaType<typename T::Value>;

template <typename T>
concept Action = requires {
    { T::descriptor } -> std::convertible_to<ActionDescriptor>;
};

template <typename T>
concept Topic = requires {
    typename T::Value;
    { T::descriptor } -> std::convertible_to<TopicDescriptor>;
} && SchemaType<typename T::Value>;

template <typename T>
concept Stream = requires {
    typename T::Value;
    { T::descriptor } -> std::convertible_to<StreamDescriptor>;
} && SchemaType<typename T::Value>;

template <> struct Schema<Empty>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{1}, .name = "solar.Empty"};
    using Fields = remote::Fields<>;
    static constexpr std::size_t max_encoded_size = 1;
    static constexpr Codec codec = Codec::Cbor;
};

template <> struct Schema<Status>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{2}, .name = "solar.Status"};
    using Fields = remote::Fields<>;
    static constexpr std::size_t max_encoded_size = 2;
    static constexpr Codec codec = Codec::Cbor;
    static constexpr SchemaShape shape = SchemaShape::StatusCode;
};

template <> struct Schema<solar::Error>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{3}, .name = "solar.Error"};
    using Fields = remote::Fields<remote::Field<1, &solar::Error::status>,
                                  remote::Field<2, &solar::Error::native>>;
    static constexpr std::size_t max_encoded_size = 16;
    static constexpr Codec codec = Codec::Cbor;
};

} // namespace solar::remote
