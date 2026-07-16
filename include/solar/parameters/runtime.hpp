#pragma once

#include "solar/parameters/facility.hpp"

#if defined(CONFIG_SOLAR_PARAMETERS)

#include <array>
#include <chrono>
#include <span>
#include <tuple>
#include <type_traits>

#include "solar/execution/runtime.hpp"
#include "solar/lifecycle/engine.hpp"

namespace solar::parameters::detail
{

template <typename Needle, typename List> struct TypeIndex;

template <typename Needle, typename... Tail>
struct TypeIndex<Needle, TypeList<Needle, Tail...>> : std::integral_constant<std::size_t, 0>
{};

template <typename Needle, typename Head, typename... Tail>
struct TypeIndex<Needle, TypeList<Head, Tail...>>
    : std::integral_constant<std::size_t, 1 + TypeIndex<Needle, TypeList<Tail...>>::value>
{};

template <typename Needle, typename List>
inline constexpr std::size_t type_index_v = TypeIndex<Needle, List>::value;

template <typename System, typename ParameterT>
[[nodiscard]] constexpr Error make_error(Operation operation, Status status, Reason reason) noexcept
{
    Error error{
        .status = status,
        .reason = reason,
        .operation = operation,
        .parameter = System::ParameterCatalog::template Entry<ParameterT>::local_id,
    };
    constexpr auto stable_id = descriptor_traits<Tag, ParameterT>::descriptor.stable_id;
    if constexpr (stable_id.has_value()) {
        error.stable_id = stable_id->raw();
    }
    return error;
}

#if defined(CONFIG_SOLAR_PARAMETERS_PERSISTENCE)

template <typename ParameterT> [[nodiscard]] constexpr persistence::Key persistence_key() noexcept
{
    return {
        .kind = persistence::RecordKind::Parameter,
        .stable_id = descriptor_traits<Tag, ParameterT>::descriptor.stable_id->raw(),
    };
}

template <typename ParameterT>
[[nodiscard]] Result<std::size_t> encode_persistent_value(const typename ParameterT::Value& value,
                                                          std::span<std::byte> output) noexcept
{
    using Codec = codec_for_t<ParameterT>;
    static_assert(CodecForValue<Codec, typename ParameterT::Value>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_CODEC_CONTRACT: codec must expose bounded "
                  "encoded_size and typed encode/decode operations");
    static_assert(persistence::detail::envelope_size + Codec::encoded_size <=
                      CONFIG_SOLAR_PARAMETERS_MAX_ENCODED_RECORD_BYTES,
                  "SOLAR_DIAGNOSTIC_PARAMETER_ENCODED_RECORD_SIZE: encoded record exceeds the "
                  "configured hard ceiling");

    std::array<std::byte, Codec::encoded_size> payload{};
    auto encoded = Codec::encode(value, payload);
    if (!encoded || *encoded != Codec::encoded_size) {
        return fail(encoded ? Status::ProtocolError : encoded.error());
    }
    return persistence::detail::encode_record(
        persistence_key<ParameterT>(), descriptor_traits<Tag, ParameterT>::descriptor.version,
        payload, output);
}

template <typename ParameterT, typename Store>
[[nodiscard]] Result<void> persist_value(const typename ParameterT::Value& value,
                                         bool erase) noexcept
{
    if (erase) {
        return Store::erase(persistence_key<ParameterT>());
    }
    std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_ENCODED_RECORD_BYTES> record{};
    auto encoded = encode_persistent_value<ParameterT>(value, record);
    if (!encoded) {
        return fail(encoded.error());
    }
    return Store::save(persistence_key<ParameterT>(),
                       std::span<const std::byte>{record}.first(*encoded));
}

template <typename Group>
inline constexpr std::size_t group_payload_size_v =
    []<typename... MembersT>(TypeList<MembersT...>) {
        return (codec_for_t<MembersT>::encoded_size + ... + std::size_t{0});
    }(typename GroupTraits<Group>::Members{});

template <typename Group> [[nodiscard]] constexpr persistence::Key group_key() noexcept
{
    return {
        .kind = persistence::RecordKind::Group,
        .stable_id = Group::stable_id.raw(),
    };
}

template <typename Group, typename Provider>
[[nodiscard]] Result<std::size_t> encode_group_payload(std::span<std::byte> output,
                                                       Provider&& provider) noexcept
{
    static_assert(persistence::detail::envelope_size + group_payload_size_v<Group> <=
                      CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES,
                  "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_RECORD_SIZE: transactional group record "
                  "exceeds the configured hard ceiling");
    if (output.size() < group_payload_size_v<Group>) {
        return fail(Status::NoBuffer);
    }
    Result<void> result{};
    std::size_t offset{};
    for_each_type<typename GroupTraits<Group>::Members>([&]<typename ParameterT> {
        using Codec = codec_for_t<ParameterT>;
        static_assert(CodecForValue<Codec, typename ParameterT::Value>,
                      "SOLAR_DIAGNOSTIC_PARAMETER_GROUP_CODEC: every transactional member "
                      "requires a bounded codec");
        if (result) {
            auto value = provider.template operator()<ParameterT>();
            if (!value) {
                result = fail(value.error());
                return;
            }
            auto encoded = Codec::encode(*value, output.subspan(offset, Codec::encoded_size));
            if (!encoded || *encoded != Codec::encoded_size) {
                result = fail(encoded ? Status::ProtocolError : encoded.error());
                return;
            }
            offset += Codec::encoded_size;
        }
    });
    if (!result) {
        return fail(result.error());
    }
    return offset;
}

template <typename Group, typename Provider>
[[nodiscard]] Result<void> persist_group_image(Provider&& provider) noexcept
{
    using Traits = GroupTraits<Group>;
    std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES> payload{};
    auto payload_size = encode_group_payload<Group>(payload, std::forward<Provider>(provider));
    if (!payload_size) {
        return fail(payload_size.error());
    }
    std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES> record{};
    auto encoded = persistence::detail::encode_record(
        group_key<Group>(), Group::version,
        std::span<const std::byte>{payload}.first(*payload_size), record);
    if (!encoded) {
        return fail(encoded.error());
    }
    return Traits::Store::save(group_key<Group>(),
                               std::span<const std::byte>{record}.first(*encoded));
}

template <std::size_t Index = 0, typename Group, typename Values>
[[nodiscard]] Result<void> decode_group_values(std::span<const std::byte> payload, Values& values,
                                               std::size_t& offset) noexcept
{
    using Members = typename GroupTraits<Group>::Members;
    if constexpr (Index == list_size_v<Members>) {
        return offset == payload.size() ? Result<void>{}
                                        : Result<void>{fail(Status::MessageTooLarge)};
    } else {
        using ParameterT = type_at_t<Index, Members>;
        using Codec = codec_for_t<ParameterT>;
        if (offset + Codec::encoded_size > payload.size()) {
            return fail(Status::MessageTooLarge);
        }
        auto decoded = Codec::decode(payload.subspan(offset, Codec::encoded_size));
        if (!decoded) {
            return fail(decoded.error());
        }
        std::get<Index>(values) = *decoded;
        offset += Codec::encoded_size;
        return decode_group_values<Index + 1, Group>(payload, values, offset);
    }
}

template <std::size_t Index = 0, typename Architecture, typename Group, typename Values>
[[nodiscard]] Result<void> normalize_group_values(Values& values) noexcept
{
    using Members = typename GroupTraits<Group>::Members;
    if constexpr (Index == list_size_v<Members>) {
        return {};
    } else {
        using ParameterT = type_at_t<Index, Members>;
        using Policies =
            ParameterPolicies<ParameterT, typename Architecture::ConfigurationPolicies>;
        auto normalized = normalize<typename Policies::Validation>(std::get<Index>(values));
        if (!normalized) {
            return fail(Status::Invalid);
        }
        std::get<Index>(values) = normalized->value;
        return normalize_group_values<Index + 1, Architecture, Group>(values);
    }
}

template <typename Members> struct GroupValues;

template <typename... MembersT> struct GroupValues<TypeList<MembersT...>>
{
    using type = std::tuple<typename MembersT::Value...>;

    [[nodiscard]] static constexpr type defaults()
    {
        return type{typename MembersT::Value{MembersT::default_value}...};
    }
};

template <typename Group>
using group_values_t = typename GroupValues<typename GroupTraits<Group>::Members>::type;

template <typename Group, typename Values, typename = void> struct GroupMigration
{
    static constexpr bool available = false;
};

template <typename Group, typename Values>
struct GroupMigration<Group, Values, std::void_t<typename Group::Migration>>
{
    static constexpr bool available =
        requires(std::uint16_t version, std::span<const std::byte> payload) {
            { Group::Migration::migrate(version, payload) } -> std::same_as<Result<Values>>;
        };

    [[nodiscard]] static Result<Values> migrate(std::uint16_t version,
                                                std::span<const std::byte> payload) noexcept
    {
        return Group::Migration::migrate(version, payload);
    }
};

template <typename Architecture, typename ParameterT>
void record_load_failure(Status status, Reason reason, std::uint16_t observed_version = 0) noexcept
{
    using Facility = Facility<Architecture>;
    constexpr auto parameter_index = type_index_v<ParameterT, typename Architecture::Parameters>;
    Facility::template slot<ParameterT>.mutate_record([&](auto& record) {
        record.last_error = {
            .status = status,
            .reason = reason,
            .operation = Operation::Initialize,
            .parameter = LocalId{static_cast<LocalId::Representation>(parameter_index)},
            .stable_id = descriptor_traits<Tag, ParameterT>::descriptor.stable_id->raw(),
            .expected_version = descriptor_traits<Tag, ParameterT>::descriptor.version,
            .observed_version = observed_version,
        };
        record.stored_version = observed_version;
        record.load_source = LoadSource::Default;
        record.load_outcome = LoadOutcome::DefaultAfterFailure;
    });
}

template <typename Architecture, typename ParameterT>
[[nodiscard]] Result<void> load_failure(Status status, Reason reason,
                                        std::uint16_t observed_version = 0) noexcept
{
    using Policies = ParameterPolicies<ParameterT, typename Architecture::ConfigurationPolicies>;
    record_load_failure<Architecture, ParameterT>(status, reason, observed_version);
    if constexpr (std::is_same_v<typename Policies::LoadFailure, load::FailBoot>) {
        return fail(status);
    }
    return {};
}

template <typename Architecture, typename ParameterT>
[[nodiscard]] Result<void> load_persistent_parameter() noexcept
{
    using Facility = Facility<Architecture>;
    using Policies = ParameterPolicies<ParameterT, typename Architecture::ConfigurationPolicies>;
    using Persistence = PersistenceTraits<typename Policies::Persistence>;
    using Store = typename Persistence::Store;
    using Codec = codec_for_t<ParameterT>;

    if constexpr (!Persistence::persistent || Persistence::kind == PersistenceKind::Transactional) {
        return {};
    } else {
        std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_ENCODED_RECORD_BYTES> buffer{};
        auto loaded = Store::load(persistence_key<ParameterT>(), buffer);
        if (!loaded) {
            if (loaded.error() == Status::NotFound) {
                Facility::template slot<ParameterT>.mutate_record([](auto& record) {
                    record.load_source = LoadSource::Default;
                    record.load_outcome = LoadOutcome::MissingDefault;
                    record.persistence = PersistenceState::Clean;
                });
                return {};
            }
            return load_failure<Architecture, ParameterT>(loaded.error(),
                                                          Reason::PersistenceFailed);
        }
        if (*loaded > buffer.size()) {
            return load_failure<Architecture, ParameterT>(Status::MessageTooLarge,
                                                          Reason::CorruptRecord);
        }
        auto decoded =
            persistence::detail::decode_record(std::span<const std::byte>{buffer}.first(*loaded));
        if (!decoded || decoded->key.kind != persistence::RecordKind::Parameter ||
            decoded->key.stable_id != persistence_key<ParameterT>().stable_id) {
            return load_failure<Architecture, ParameterT>(
                decoded ? Status::ProtocolError : decoded.error(), Reason::CorruptRecord);
        }

        Result<typename ParameterT::Value> value = fail(Status::ProtocolError);
        LoadSource source = LoadSource::Store;
        if (decoded->version == descriptor_traits<Tag, ParameterT>::descriptor.version) {
            value = Codec::decode(decoded->payload);
        } else if constexpr (IS_ENABLED(CONFIG_SOLAR_PARAMETERS_MIGRATION) &&
                             requires(std::uint16_t version, std::span<const std::byte> payload) {
                                 {
                                     ParameterT::Migration::migrate(version, payload)
                                 } -> std::same_as<Result<typename ParameterT::Value>>;
                             }) {
            if (decoded->version < descriptor_traits<Tag, ParameterT>::descriptor.version) {
                value = ParameterT::Migration::migrate(decoded->version, decoded->payload);
                source = LoadSource::Migration;
            }
        }
        if (!value) {
            const auto reason = value.error() == Status::ProtocolError ? Reason::VersionMismatch
                                                                       : Reason::CodecFailed;
            return load_failure<Architecture, ParameterT>(value.error(), reason, decoded->version);
        }
        auto normalized = normalize<typename Policies::Validation>(*value);
        if (!normalized) {
            return load_failure<Architecture, ParameterT>(
                Status::Invalid, Reason::ValidationRejected, decoded->version);
        }
        Facility::template slot<ParameterT>.mark_loaded(
            normalized->value, source,
            source == LoadSource::Migration ? LoadOutcome::Migrated : LoadOutcome::Loaded,
            decoded->version);
        if (source == LoadSource::Migration) {
            Facility::template slot<ParameterT>.mark_dirty(0, false);
        }
        return {};
    }
}

template <typename Architecture, typename Group>
[[nodiscard]] Result<void> group_load_failure(Status status, Reason reason,
                                              std::uint16_t observed_version = 0) noexcept
{
    bool fail_boot{};
    for_each_type<typename GroupTraits<Group>::Members>([&]<typename ParameterT> {
        using Policies =
            ParameterPolicies<ParameterT, typename Architecture::ConfigurationPolicies>;
        record_load_failure<Architecture, ParameterT>(status, reason, observed_version);
        fail_boot = fail_boot || std::is_same_v<typename Policies::LoadFailure, load::FailBoot>;
    });
    return fail_boot ? Result<void>{fail(status)} : Result<void>{};
}

template <typename Architecture, typename Group>
[[nodiscard]] Result<void> load_persistent_group() noexcept
{
    using Facility = Facility<Architecture>;
    using Traits = GroupTraits<Group>;
    std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES> buffer{};
    auto loaded = Traits::Store::load(group_key<Group>(), buffer);
    if (!loaded) {
        if (loaded.error() == Status::NotFound) {
            for_each_type<typename Traits::Members>([]<typename ParameterT> {
                Facility::template slot<ParameterT>.mutate_record([](auto& record) {
                    record.load_source = LoadSource::Default;
                    record.load_outcome = LoadOutcome::MissingDefault;
                    record.persistence = PersistenceState::Clean;
                });
            });
            return {};
        }
        return group_load_failure<Architecture, Group>(loaded.error(), Reason::PersistenceFailed);
    }
    if (*loaded > buffer.size()) {
        return group_load_failure<Architecture, Group>(Status::MessageTooLarge,
                                                       Reason::CorruptRecord);
    }
    auto decoded =
        persistence::detail::decode_record(std::span<const std::byte>{buffer}.first(*loaded));
    if (!decoded || decoded->key.kind != persistence::RecordKind::Group ||
        decoded->key.stable_id != group_key<Group>().stable_id) {
        return group_load_failure<Architecture, Group>(
            decoded ? Status::ProtocolError : decoded.error(), Reason::CorruptRecord);
    }

    auto values = GroupValues<typename Traits::Members>::defaults();
    LoadSource source = LoadSource::Store;
    Result<void> decoded_values = fail(Status::ProtocolError);
    if (decoded->version == Group::version) {
        std::size_t offset{};
        decoded_values = decode_group_values<0, Group>(decoded->payload, values, offset);
    } else if constexpr (IS_ENABLED(CONFIG_SOLAR_PARAMETERS_MIGRATION) &&
                         GroupMigration<Group, group_values_t<Group>>::available) {
        if (decoded->version < Group::version) {
            auto migrated = GroupMigration<Group, group_values_t<Group>>::migrate(decoded->version,
                                                                                  decoded->payload);
            if (migrated) {
                values = *migrated;
                decoded_values = {};
                source = LoadSource::Migration;
            } else {
                decoded_values = fail(migrated.error());
            }
        }
    }
    if (!decoded_values) {
        return group_load_failure<Architecture, Group>(
            decoded_values.error(),
            decoded->version == Group::version ? Reason::CodecFailed : Reason::VersionMismatch,
            decoded->version);
    }
    auto normalized = normalize_group_values<0, Architecture, Group>(values);
    if (!normalized) {
        return group_load_failure<Architecture, Group>(Status::Invalid, Reason::ValidationRejected,
                                                       decoded->version);
    }
    [&]<std::size_t... Index>(std::index_sequence<Index...>) {
        (Facility::template slot<type_at_t<Index, typename Traits::Members>>.mark_loaded(
             std::get<Index>(values), source,
             source == LoadSource::Migration ? LoadOutcome::Migrated : LoadOutcome::Loaded,
             decoded->version),
         ...);
    }(std::make_index_sequence<list_size_v<typename Traits::Members>>{});
    if (source == LoadSource::Migration) {
        Facility::template group_state<Group>.mark_dirty();
    }
    return {};
}

template <typename Architecture> [[nodiscard]] Result<void> initialize_persistence() noexcept
{
    Result<void> result{};
    for_each_type<typename Architecture::Stores>([&]<typename Store> {
        if (result) {
            result = Store::initialize();
        }
    });
    if (!result) {
        return result;
    }
    for_each_type<typename Architecture::Parameters>([&]<typename ParameterT> {
        if (result) {
            result = load_persistent_parameter<Architecture, ParameterT>();
        }
    });
    for_each_type<typename Architecture::Groups>([&]<typename Group> {
        if (result) {
            result = load_persistent_group<Architecture, Group>();
        }
    });
    return result;
}

#else

template <typename Architecture> [[nodiscard]] Result<void> initialize_persistence() noexcept
{
    return {};
}

#endif

template <typename System, typename ParameterT>
[[nodiscard]] Result<typename ParameterT::Value, Error> read_parameter(bool no_wait) noexcept
{
    using Facility = typename System::ParameterFacility;
    const auto operation = no_wait ? Operation::TryGet : Operation::Get;
    if (kernel::in_isr()) {
        return fail(
            make_error<System, ParameterT>(operation, Status::Invalid, Reason::InvalidContext));
    }
    if (!Facility::ready.load(std::memory_order_acquire)) {
        return fail(make_error<System, ParameterT>(operation, Status::NotReady, Reason::NotReady));
    }
    auto value = Facility::template slot<ParameterT>.read(no_wait);
    if (!value) {
        const auto status = value.error();
        return fail(make_error<System, ParameterT>(
            operation, status,
            status == Status::WouldBlock ? Reason::WouldBlock : Reason::InternalInvariant));
    }
    return *value;
}

template <typename Handler, typename ParameterT>
[[nodiscard]] Result<void> invoke_change_handler(const Change<ParameterT>& change) noexcept
{
    using Return = decltype(Handler::changed(change));
    if constexpr (std::same_as<Return, void>) {
        Handler::changed(change);
        return {};
    } else if constexpr (std::same_as<Return, Status>) {
        const auto status = Handler::changed(change);
        return status == Status::Ok ? Result<void>{} : Result<void>{fail(status)};
    } else {
        return Handler::changed(change);
    }
}

template <typename System, typename ChangeT>
[[nodiscard]] Status
invoke_change(const Change<typename change_traits<ChangeT>::ParameterType>& change) noexcept
{
    using Facility = typename System::ParameterFacility;
    using Traits = change_traits<ChangeT>;
    auto& state = Facility::template change_state<ChangeT>;
    state.begin_invoke();
    auto result = invoke_change_handler<typename Traits::HandlerType>(change);
    const auto status = result ? Status::Ok : result.error();
    state.finish_invoke(change.revision, status);
    if (!result) {
        Facility::template slot<typename Traits::ParameterType>.mutate_record([&](auto& record) {
            ++record.change_failures;
            record.last_error = make_error<System, typename Traits::ParameterType>(
                Operation::ActivateChanges, status, Reason::ChangeHandlerFailed);
        });
    }
    return status;
}

template <typename System, typename ParameterT>
[[nodiscard]] std::size_t dispatch_change(const Change<ParameterT>& change, bool deferred) noexcept
{
    using Facility = typename System::ParameterFacility;
    std::size_t failures{};
    for_each_type<typename Facility::ChangeTypes>([&]<typename ChangeT> {
        using Traits = change_traits<ChangeT>;
        if constexpr (std::is_same_v<ParameterT, typename Traits::ParameterType>) {
            if (deferred) {
                Facility::template change_state<ChangeT>.defer(change);
            } else if (invoke_change<System, ChangeT>(change) != Status::Ok) {
                ++failures;
            }
        }
    });
    return failures;
}

template <typename System, typename ParameterT>
[[nodiscard]] Result<void, Error>
prepare_immediate_persistence(const typename ParameterT::Value& value, bool reset) noexcept
{
    using Facility = typename System::ParameterFacility;
    using Policies = typename Facility::template Policies<ParameterT>;
    using Persistence = PersistenceTraits<typename Policies::Persistence>;
    if constexpr (Persistence::kind == PersistenceKind::Immediate) {
#if defined(CONFIG_SOLAR_PARAMETERS_PERSISTENCE)
        auto persistence_guard = kernel::lock_guard(Facility::persistence_gate);
        if (!persistence_guard) {
            return fail(make_error<System, ParameterT>(Operation::Save, persistence_guard.error(),
                                                       Reason::PersistenceUnavailable));
        }
        auto persisted = persist_value<ParameterT, typename Persistence::Store>(value, reset);
        if (!persisted) {
            Facility::template slot<ParameterT>.mark_persistence_failure(
                persisted.error(), reset ? Operation::Reset : Operation::Save);
            return fail(make_error<System, ParameterT>(reset ? Operation::Reset : Operation::Save,
                                                       persisted.error(),
                                                       Reason::PersistenceFailed));
        }
#else
        static_assert(solar::detail::dependent_false_v<ParameterT>,
                      "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_DISABLED: immediate persistence "
                      "requires CONFIG_SOLAR_PARAMETERS_PERSISTENCE");
#endif
    } else if constexpr (Persistence::kind == PersistenceKind::Transactional) {
#if defined(CONFIG_SOLAR_PARAMETERS_PERSISTENCE)
        using Group = typename Persistence::Group;
        using GroupCommit = typename GroupTraits<Group>::CommitTraits;
        if constexpr (GroupCommit::kind == PersistenceKind::Immediate) {
            auto persistence_guard = kernel::lock_guard(Facility::persistence_gate);
            if (!persistence_guard) {
                return fail(make_error<System, ParameterT>(
                    Operation::Save, persistence_guard.error(), Reason::PersistenceUnavailable));
            }
            auto persisted = persist_group_image<Group>([&]<typename MemberT>() {
                if constexpr (std::is_same_v<MemberT, ParameterT>) {
                    return Result<typename MemberT::Value>{value};
                } else {
                    return Facility::template slot<MemberT>.peek();
                }
            });
            if (!persisted) {
                Facility::template slot<ParameterT>.mark_persistence_failure(
                    persisted.error(), reset ? Operation::Reset : Operation::Save);
                return fail(
                    make_error<System, ParameterT>(reset ? Operation::Reset : Operation::Save,
                                                   persisted.error(), Reason::PersistenceFailed));
            }
        }
#endif
    }
    return {};
}

template <typename System, typename ParameterT>
[[nodiscard]] PersistenceState mark_committed_persistence(std::uint64_t revision,
                                                          bool reset) noexcept
{
    using Facility = typename System::ParameterFacility;
    using Policies = typename Facility::template Policies<ParameterT>;
    using Persistence = PersistenceTraits<typename Policies::Persistence>;
    auto& slot = Facility::template slot<ParameterT>;
    if constexpr (Persistence::kind == PersistenceKind::Volatile) {
        return PersistenceState::Volatile;
    } else if constexpr (Persistence::kind == PersistenceKind::Immediate) {
        slot.mark_persisted(revision);
        return PersistenceState::Clean;
    } else if constexpr (Persistence::kind == PersistenceKind::Deferred) {
        slot.mark_dirty(revision, reset, kernel::Deadline::after(Persistence::delay.duration()));
        return reset ? PersistenceState::ResetPending : PersistenceState::Scheduled;
    } else if constexpr (Persistence::kind == PersistenceKind::Transactional) {
        using Group = typename Persistence::Group;
        using GroupCommit = typename GroupTraits<Group>::CommitTraits;
        if constexpr (GroupCommit::kind == PersistenceKind::Immediate) {
            slot.mark_persisted(revision);
            return PersistenceState::Clean;
        } else if constexpr (GroupCommit::kind == PersistenceKind::Deferred) {
            const auto due = kernel::Deadline::after(GroupCommit::delay.duration());
            slot.mark_dirty(revision, reset, due);
            Facility::template group_state<Group>.mark_dirty(due);
            return reset ? PersistenceState::ResetPending : PersistenceState::Scheduled;
        } else {
            slot.mark_dirty(revision, reset);
            Facility::template group_state<Group>.mark_dirty();
            return reset ? PersistenceState::ResetPending : PersistenceState::Dirty;
        }
    } else {
        slot.mark_dirty(revision, reset);
        return reset ? PersistenceState::ResetPending : PersistenceState::Dirty;
    }
}

template <typename System> [[nodiscard]] Result<void> schedule_deferred_persistence() noexcept
{
    using Facility = typename System::ParameterFacility;
    if constexpr (!System::ParameterArchitecture::has_deferred) {
        return {};
    } else {
        if (!Facility::persistence_active.load(std::memory_order_acquire)) {
            return {};
        }
        std::optional<kernel::Deadline> earliest{};
        for_each_type<typename Facility::ParameterTypes>([&]<typename ParameterT> {
            using Policies = typename Facility::template Policies<ParameterT>;
            if constexpr (EffectiveDeferred<typename Policies::Persistence>::value) {
                auto due = Facility::template slot<ParameterT>.next_due();
                if (due && (!earliest || *due < *earliest)) {
                    earliest = *due;
                }
            }
        });
        for_each_type<typename Facility::Groups>([&]<typename Group> {
            using Commit = typename GroupTraits<Group>::CommitTraits;
            if constexpr (Commit::deferred) {
                auto work = Facility::template group_state<Group>.work();
                if (work.dirty && work.due && (!earliest || *work.due < *earliest)) {
                    earliest = *work.due;
                }
            }
        });
        if (!earliest) {
            return {};
        }
        const auto remaining = earliest->remaining();
        const auto delay = remaining.is_no_wait()
                               ? std::chrono::nanoseconds{0}
                               : std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     kernel::from_ticks(remaining.native_handle().ticks));
        auto scheduled =
            execution::detail::schedule_registration<System,
                                                     typename Facility::DeferredRegistration>(delay,
                                                                                              true);
        if (!scheduled) {
            return fail(scheduled.error().status);
        }
        return {};
    }
}

template <typename Architecture, typename ParameterT>
[[nodiscard]] Result<bool> persist_parameter_slot(bool force, bool no_wait = false) noexcept
{
    using Facility = Facility<Architecture>;
    using Policies = typename Facility::template Policies<ParameterT>;
    using Persistence = PersistenceTraits<typename Policies::Persistence>;
    if constexpr (!Persistence::persistent || Persistence::kind == PersistenceKind::Transactional) {
        return false;
    } else {
#if defined(CONFIG_SOLAR_PARAMETERS_PERSISTENCE)
        auto write_guard =
            kernel::unique_lock(Facility::write_gate,
                                no_wait ? kernel::Timeout::no_wait() : kernel::Timeout::forever());
        if (!write_guard) {
            return fail(write_guard.error());
        }
        auto value = Facility::template slot<ParameterT>.peek();
        if (!value) {
            return fail(value.error());
        }
        auto work = Facility::template slot<ParameterT>.persistence_work(*value);
        (void)(*write_guard).unlock();
        if (!work.dirty || (!force && work.due && !work.due->expired())) {
            return false;
        }

        auto persistence_guard =
            kernel::lock_guard(Facility::persistence_gate,
                               no_wait ? kernel::Timeout::no_wait() : kernel::Timeout::forever());
        if (!persistence_guard) {
            return fail(persistence_guard.error());
        }
        auto persisted =
            persist_value<ParameterT, typename Persistence::Store>(work.value, work.erase);
        Facility::template slot<ParameterT>.finish_persistence(
            work.revision, persisted ? Status::Ok : persisted.error());
        if (!persisted) {
            return fail(persisted.error());
        }
        return true;
#else
        static_assert(solar::detail::dependent_false_v<ParameterT>,
                      "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_DISABLED: persistence operation "
                      "requires CONFIG_SOLAR_PARAMETERS_PERSISTENCE");
#endif
    }
}

template <typename Architecture, typename Group>
[[nodiscard]] Result<bool> persist_group_slot(bool force, bool no_wait = false) noexcept
{
#if defined(CONFIG_SOLAR_PARAMETERS_PERSISTENCE)
    using Facility = Facility<Architecture>;
    auto work = Facility::template group_state<Group>.work();
    if (!work.dirty || (!force && work.due && !work.due->expired())) {
        return false;
    }

    std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES> payload{};
    std::size_t payload_size{};
    {
        auto write_guard =
            kernel::lock_guard(Facility::write_gate,
                               no_wait ? kernel::Timeout::no_wait() : kernel::Timeout::forever());
        if (!write_guard) {
            return fail(write_guard.error());
        }
        auto encoded = encode_group_payload<Group>(payload, []<typename ParameterT>() {
            return Facility::template slot<ParameterT>.peek();
        });
        if (!encoded) {
            return fail(encoded.error());
        }
        payload_size = *encoded;
    }

    auto persistence_guard =
        kernel::lock_guard(Facility::persistence_gate,
                           no_wait ? kernel::Timeout::no_wait() : kernel::Timeout::forever());
    if (!persistence_guard) {
        return fail(persistence_guard.error());
    }
    std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES> record{};
    auto encoded = persistence::detail::encode_record(
        group_key<Group>(), Group::version, std::span<const std::byte>{payload}.first(payload_size),
        record);
    Result<void> persisted =
        encoded ? GroupTraits<Group>::Store::save(
                      group_key<Group>(), std::span<const std::byte>{record}.first(*encoded))
                : Result<void>{fail(encoded.error())};
    const bool cleared =
        Facility::template group_state<Group>.finish(work.revision, persisted.has_value());
    for_each_type<typename GroupTraits<Group>::Members>([&]<typename ParameterT> {
        auto value = Facility::template slot<ParameterT>.peek();
        if (value) {
            auto member_work = Facility::template slot<ParameterT>.persistence_work(*value);
            if (!persisted || cleared) {
                Facility::template slot<ParameterT>.finish_persistence(
                    member_work.revision, persisted ? Status::Ok : persisted.error());
            }
        }
    });
    if (!persisted) {
        return fail(persisted.error());
    }
    return true;
#else
    static_assert(solar::detail::dependent_false_v<Group>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_PERSISTENCE_DISABLED: transactional persistence "
                  "requires CONFIG_SOLAR_PARAMETERS_PERSISTENCE");
    return fail(Status::NotSupported);
#endif
}

template <typename Architecture>
[[nodiscard]] Result<std::size_t> persist_deferred_parameters(bool force,
                                                              bool no_wait = false) noexcept
{
    using Facility = Facility<Architecture>;
    std::optional<Status> first_error{};
    std::size_t saved{};
    for_each_type<typename Architecture::Parameters>([&]<typename ParameterT> {
        using Policies = typename Facility::template Policies<ParameterT>;
        using Persistence = PersistenceTraits<typename Policies::Persistence>;
        if constexpr (Persistence::deferred) {
            auto persisted = persist_parameter_slot<Architecture, ParameterT>(force, no_wait);
            if (!persisted) {
                if (!first_error) {
                    first_error = persisted.error();
                }
            } else {
                saved += *persisted ? 1U : 0U;
            }
        }
    });
    for_each_type<typename Architecture::Groups>([&]<typename Group> {
        using Commit = typename GroupTraits<Group>::CommitTraits;
        if constexpr (Commit::deferred) {
            auto persisted = persist_group_slot<Architecture, Group>(force, no_wait);
            if (!persisted) {
                if (!first_error) {
                    first_error = persisted.error();
                }
            } else {
                saved += *persisted ? 1U : 0U;
            }
        }
    });
    if (first_error) {
        return fail(*first_error);
    }
    return saved;
}

template <typename Facility> [[nodiscard]] Result<void> schedule_next_deferred() noexcept
{
    if constexpr (!Facility::Architecture::has_deferred) {
        return {};
    } else {
        if (!Facility::persistence_active.load(std::memory_order_acquire) ||
            Facility::schedule_persistence == nullptr) {
            return {};
        }
        std::optional<kernel::Deadline> earliest{};
        for_each_type<typename Facility::ParameterTypes>([&]<typename ParameterT> {
            using Policies = typename Facility::template Policies<ParameterT>;
            if constexpr (EffectiveDeferred<typename Policies::Persistence>::value) {
                auto due = Facility::template slot<ParameterT>.next_due();
                if (due && (!earliest || *due < *earliest)) {
                    earliest = *due;
                }
            }
        });
        for_each_type<typename Facility::Groups>([&]<typename Group> {
            using Commit = typename GroupTraits<Group>::CommitTraits;
            if constexpr (Commit::deferred) {
                auto work = Facility::template group_state<Group>.work();
                if (work.dirty && work.due && (!earliest || *work.due < *earliest)) {
                    earliest = *work.due;
                }
            }
        });
        if (!earliest) {
            return {};
        }
        const auto remaining = earliest->remaining();
        const auto delay = remaining.is_no_wait()
                               ? std::chrono::nanoseconds{0}
                               : std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     kernel::from_ticks(remaining.native_handle().ticks));
        return Facility::schedule_persistence(delay);
    }
}

template <typename System, typename ParameterT>
[[nodiscard]] Result<Update<ParameterT>, Error> set_parameter(typename ParameterT::Value candidate,
                                                              bool no_wait, UpdateOrigin origin,
                                                              bool reset) noexcept
{
    using Facility = typename System::ParameterFacility;
    using Policies = typename Facility::template Policies<ParameterT>;
    const auto operation =
        reset ? Operation::Reset : (no_wait ? Operation::TrySet : Operation::Set);

    if (kernel::in_isr()) {
        return fail(
            make_error<System, ParameterT>(operation, Status::Invalid, Reason::InvalidContext));
    }
    const auto system_state = lifecycle::Engine<System>::state();
    if (!Facility::ready.load(std::memory_order_acquire) ||
        !Facility::mutation_open.load(std::memory_order_acquire) ||
        system_state == lifecycle::SystemState::Stopping ||
        system_state == lifecycle::SystemState::RollingBack ||
        system_state == lifecycle::SystemState::Stopped ||
        system_state == lifecycle::SystemState::Failed) {
        return fail(make_error<System, ParameterT>(operation, Status::NotReady, Reason::NotReady));
    }

    auto normalized = normalize<typename Policies::Validation>(candidate);
    if (!normalized) {
        Facility::template slot<ParameterT>.mutate_record([&](auto& record) {
            ++record.rejected;
            record.last_error = make_error<System, ParameterT>(operation, Status::Invalid,
                                                               Reason::ValidationRejected);
        });
        return fail(
            make_error<System, ParameterT>(operation, Status::Invalid, Reason::ValidationRejected));
    }

    auto gate = kernel::unique_lock(Facility::write_gate, no_wait ? kernel::Timeout::no_wait()
                                                                  : kernel::Timeout::forever());
    if (!gate) {
        const auto status = gate.error();
        return fail(make_error<System, ParameterT>(
            operation, status,
            status == Status::WouldBlock ? Reason::WouldBlock : Reason::InternalInvariant));
    }

    auto& slot = Facility::template slot<ParameterT>;
    auto old = slot.peek(no_wait);
    if (!old) {
        return fail(make_error<System, ParameterT>(
            operation, old.error(),
            old.error() == Status::WouldBlock ? Reason::WouldBlock : Reason::InternalInvariant));
    }

    auto before = slot.copy_record(*old);
    if (*old == normalized->value) {
        auto persistence = before.persistence;
        if (reset) {
            auto persisted =
                prepare_immediate_persistence<System, ParameterT>(normalized->value, true);
            if (!persisted) {
                return fail(persisted.error());
            }
            persistence = mark_committed_persistence<System, ParameterT>(before.revision, true);
        }
        slot.mutate_record([&](auto& record) {
            ++record.unchanged;
            if (normalized->adjusted) {
                ++record.validation_adjustments;
            }
            record.last_origin = origin;
        });
        (void)(*gate).unlock();
        if (reset) {
            if constexpr (EffectiveDeferred<typename Policies::Persistence>::value) {
                auto scheduled = schedule_deferred_persistence<System>();
                if (!scheduled) {
                    slot.mark_schedule_failure(scheduled.error());
                    persistence = PersistenceState::Failed;
                }
            }
        }
        return Update<ParameterT>{
            .effective_value = normalized->value,
            .revision = before.revision,
            .persistence = persistence,
            .changed = false,
            .adjusted = normalized->adjusted,
        };
    }

    auto persisted = prepare_immediate_persistence<System, ParameterT>(normalized->value, reset);
    if (!persisted) {
        return fail(persisted.error());
    }

    const auto write_status = slot.write(normalized->value);
    if (write_status != Status::Ok) {
        return fail(
            make_error<System, ParameterT>(operation, write_status, Reason::InternalInvariant));
    }
    const auto revision = before.revision + 1U;
    slot.mutate_record([&](auto& record) {
        record.value = normalized->value;
        record.revision = revision;
        ++record.updates;
        if (normalized->adjusted) {
            ++record.validation_adjustments;
        }
        record.last_origin = origin;
        record.last_error = make_error<System, ParameterT>(operation, Status::Ok, Reason::None);
    });

    auto persistence = mark_committed_persistence<System, ParameterT>(revision, reset);

    (void)(*gate).unlock();
    if constexpr (EffectiveDeferred<typename Policies::Persistence>::value) {
        auto scheduled = schedule_deferred_persistence<System>();
        if (!scheduled) {
            slot.mark_schedule_failure(scheduled.error());
            persistence = PersistenceState::Failed;
        }
    }
    const bool deferred = system_state != lifecycle::SystemState::Running &&
                          !Facility::activating_changes.load(std::memory_order_acquire);
    const Change<ParameterT> change{
        .old_value = *old,
        .new_value = normalized->value,
        .revision = revision,
        .origin = origin,
        .adjusted = normalized->adjusted,
        .transaction = origin == UpdateOrigin::LocalTransaction,
        .reset = reset,
    };
    const auto failures = dispatch_change<System>(change, deferred);
    return Update<ParameterT>{
        .effective_value = normalized->value,
        .revision = revision,
        .persistence = persistence,
        .change_failures = failures,
        .changed = true,
        .adjusted = normalized->adjusted,
        .changes_deferred = deferred,
    };
}

template <typename System, typename ParameterT>
[[nodiscard]] Result<ParameterRecord<ParameterT>, Error> parameter_record() noexcept
{
    using Facility = typename System::ParameterFacility;
    if (!Facility::ready.load(std::memory_order_acquire)) {
        return fail(
            make_error<System, ParameterT>(Operation::Query, Status::NotReady, Reason::NotReady));
    }
    auto gate = kernel::lock_guard(Facility::write_gate);
    if (!gate) {
        return fail(make_error<System, ParameterT>(Operation::Query, gate.error(),
                                                   Reason::InternalInvariant));
    }
    auto value = Facility::template slot<ParameterT>.peek();
    if (!value) {
        return fail(make_error<System, ParameterT>(Operation::Query, value.error(),
                                                   Reason::InternalInvariant));
    }
    auto record = Facility::template slot<ParameterT>.copy_record(*value);
    record.ready = true;
    return record;
}

template <typename Observer, typename ParameterT, typename RouteTag, typename Changes>
struct FindChange;

template <typename Observer, typename ParameterT, typename RouteTag>
struct FindChange<Observer, ParameterT, RouteTag, TypeList<>>
{
    static constexpr bool found = false;
    using type = void;
};

template <typename Observer, typename ParameterT, typename RouteTag, typename Head,
          typename... Tail>
struct FindChange<Observer, ParameterT, RouteTag, TypeList<Head, Tail...>>
{
    using Traits = change_traits<Head>;
    static constexpr bool matches = std::is_same_v<Observer, typename Traits::ObserverType> &&
                                    std::is_same_v<ParameterT, typename Traits::ParameterType> &&
                                    std::is_same_v<RouteTag, typename Traits::RouteTagType>;
    using Remaining = FindChange<Observer, ParameterT, RouteTag, TypeList<Tail...>>;
    static constexpr bool found = matches || Remaining::found;
    using type = std::conditional_t<matches, Head, typename Remaining::type>;
};

template <typename System, typename ChangeT> [[nodiscard]] ChangeRecord change_record() noexcept
{
    using Facility = typename System::ParameterFacility;
    using Traits = change_traits<ChangeT>;
    auto record = Facility::template change_state<ChangeT>.copy();
    record.change = System::ParameterChangeCatalog::template Entry<ChangeT>::local_id;
    record.parameter =
        System::ParameterCatalog::template Entry<typename Traits::ParameterType>::local_id;
    record.observer = System::Catalogs::template Of<component::Tag>::template Entry<
        typename Traits::ObserverType>::local_id;
    return record;
}

template <typename System, typename Tuple, std::size_t Index>
[[nodiscard]] Result<void, Error> read_snapshot_values(Tuple&, bool) noexcept
{
    return {};
}

template <typename System, typename Tuple, std::size_t Index, typename Head, typename... Tail>
[[nodiscard]] Result<void, Error> read_snapshot_values(Tuple& values, bool no_wait) noexcept
{
    auto value = System::ParameterFacility::template slot<Head>.peek(no_wait);
    if (!value) {
        return fail(make_error<System, Head>(
            Operation::Snapshot, value.error(),
            value.error() == Status::WouldBlock ? Reason::WouldBlock : Reason::InternalInvariant));
    }
    std::get<Index>(values) = *value;
    if constexpr (sizeof...(Tail) != 0) {
        return read_snapshot_values<System, Tuple, Index + 1, Tail...>(values, no_wait);
    }
    return {};
}

template <typename System, typename... ParametersT>
[[nodiscard]] Result<Snapshot<ParametersT...>, Error> snapshot_parameters(bool no_wait) noexcept
{
    using Facility = typename System::ParameterFacility;
    if (kernel::in_isr()) {
        return fail(Error{.status = Status::Invalid,
                          .reason = Reason::InvalidContext,
                          .operation = Operation::Snapshot});
    }
    if (!Facility::ready.load(std::memory_order_acquire)) {
        return fail(Error{.status = Status::NotReady,
                          .reason = Reason::NotReady,
                          .operation = Operation::Snapshot});
    }
    auto gate = kernel::lock_guard(Facility::write_gate, no_wait ? kernel::Timeout::no_wait()
                                                                 : kernel::Timeout::forever());
    if (!gate) {
        return fail(Error{.status = gate.error(),
                          .reason = gate.error() == Status::WouldBlock ? Reason::WouldBlock
                                                                       : Reason::InternalInvariant,
                          .operation = Operation::Snapshot});
    }
    std::tuple<typename ParametersT::Value...> values{
        typename ParametersT::Value{ParametersT::default_value}...};
    auto read = read_snapshot_values<System, decltype(values), 0, ParametersT...>(values, no_wait);
    if (!read) {
        return fail(read.error());
    }
    return Snapshot<ParametersT...>{std::move(values)};
}

template <typename ParameterT> struct TransactionState
{
    using ParameterType = ParameterT;
    using Value = typename ParameterT::Value;

    Normalized<Value> normalized{.value = Value{ParameterT::default_value}};
    Value old_value{ParameterT::default_value};
    ParameterRecord<ParameterT> before{.value = Value{ParameterT::default_value}};
    bool changed{};
};

template <std::size_t Index = 0, typename System, typename Assignments, typename States>
[[nodiscard]] Result<void, Error> normalize_transaction(const Assignments& assignments,
                                                        States& states) noexcept
{
    if constexpr (Index == std::tuple_size_v<Assignments>) {
        return {};
    } else {
        using AssignmentT = std::tuple_element_t<Index, Assignments>;
        using ParameterT = typename AssignmentT::ParameterType;
        using Policies = typename System::ParameterFacility::template Policies<ParameterT>;
        auto normalized =
            normalize<typename Policies::Validation>(std::get<Index>(assignments).value);
        if (!normalized) {
            return fail(make_error<System, ParameterT>(Operation::Transaction, Status::Invalid,
                                                       Reason::ValidationRejected));
        }
        std::get<Index>(states).normalized = *normalized;
        return normalize_transaction<Index + 1, System>(assignments, states);
    }
}

template <std::size_t Index = 0, typename System, typename States>
[[nodiscard]] Result<void, Error> capture_transaction(States& states, bool no_wait) noexcept
{
    if constexpr (Index == std::tuple_size_v<States>) {
        return {};
    } else {
        using State = std::tuple_element_t<Index, States>;
        using ParameterT = typename State::ParameterType;
        auto& state = std::get<Index>(states);
        auto& slot = System::ParameterFacility::template slot<ParameterT>;
        auto old = slot.peek(no_wait);
        if (!old) {
            return fail(make_error<System, ParameterT>(Operation::Transaction, old.error(),
                                                       old.error() == Status::WouldBlock
                                                           ? Reason::WouldBlock
                                                           : Reason::InternalInvariant));
        }
        state.old_value = *old;
        state.before = slot.copy_record(*old);
        state.changed = !(state.old_value == state.normalized.value);
        return capture_transaction<Index + 1, System>(states, no_wait);
    }
}

template <std::size_t Index = 0, typename System, typename States>
[[nodiscard]] Result<void, Error> write_transaction_values(States& states) noexcept
{
    if constexpr (Index == std::tuple_size_v<States>) {
        return {};
    } else {
        using State = std::tuple_element_t<Index, States>;
        using ParameterT = typename State::ParameterType;
        auto& state = std::get<Index>(states);
        if (state.changed) {
            const auto status =
                System::ParameterFacility::template slot<ParameterT>.write(state.normalized.value);
            if (status != Status::Ok) {
                return fail(make_error<System, ParameterT>(Operation::Transaction, status,
                                                           Reason::InternalInvariant));
            }
        }
        return write_transaction_values<Index + 1, System>(states);
    }
}

template <typename System, typename... States>
void restore_transaction_values(std::tuple<States...>& states) noexcept
{
    std::
        apply(
            []<typename... StateT>(StateT&... state) {
                ((void)(state.changed
                        ? System::ParameterFacility::template slot<
                              typename StateT::ParameterType>.write(state.old_value)
                        : Status::Ok),
             ...);
            },
            states);
}

template <typename ParameterT, typename States>
[[nodiscard]] auto& transaction_state(States& states) noexcept
{
    return std::get<TransactionState<ParameterT>>(states);
}

template <typename System, typename... States>
void update_transaction_records(std::tuple<States...>& states) noexcept
{
    std::apply(
        []<typename... StateT>(StateT&... state) {
            (
                [&] {
                    using ParameterT = typename StateT::ParameterType;
                    auto& slot = System::ParameterFacility::template slot<ParameterT>;
                    slot.mutate_record([&](auto& record) {
                        if (state.changed) {
                            record.value = state.normalized.value;
                            record.revision = state.before.revision + 1U;
                            ++record.updates;
                        } else {
                            ++record.unchanged;
                        }
                        if (state.normalized.adjusted) {
                            ++record.validation_adjustments;
                        }
                        record.last_origin = UpdateOrigin::LocalTransaction;
                        record.last_error = make_error<System, ParameterT>(
                            Operation::Transaction, Status::Ok, Reason::None);
                    });
                }(),
                ...);
        },
        states);
}

template <typename System, typename... States>
[[nodiscard]] Result<void, Error>
prepare_transaction_persistence(std::tuple<States...>& states) noexcept
{
    using Facility = typename System::ParameterFacility;
    Result<void, Error> result{};
    std::apply(
        [&](States&... state) {
            (
                [&] {
                    using ParameterT = typename States::ParameterType;
                    using Persistence = PersistenceTraits<
                        typename Facility::template Policies<ParameterT>::Persistence>;
                    if constexpr (Persistence::kind == PersistenceKind::Immediate) {
                        if (result && state.changed) {
                            result = prepare_immediate_persistence<System, ParameterT>(
                                state.normalized.value, false);
                        }
                    }
                }(),
                ...);
        },
        states);

#if defined(CONFIG_SOLAR_PARAMETERS_PERSISTENCE)
    for_each_type<typename Facility::Groups>([&]<typename Group> {
        using Commit = typename GroupTraits<Group>::CommitTraits;
        constexpr bool touched =
            ((contains_v<typename States::ParameterType, typename GroupTraits<Group>::Members>) ||
             ...);
        if constexpr (Commit::kind == PersistenceKind::Immediate && touched) {
            if (!result) {
                return;
            }
            auto persistence_guard = kernel::lock_guard(Facility::persistence_gate);
            if (!persistence_guard) {
                result = fail(Error{.status = persistence_guard.error(),
                                    .reason = Reason::PersistenceUnavailable,
                                    .operation = Operation::Transaction,
                                    .group_id = Group::stable_id.raw()});
                return;
            }
            auto persisted = persist_group_image<Group>([&]<typename MemberT>() {
                if constexpr ((std::is_same_v<MemberT, typename States::ParameterType> || ...)) {
                    return Result<typename MemberT::Value>{
                        transaction_state<MemberT>(states).normalized.value};
                } else {
                    return Facility::template slot<MemberT>.peek();
                }
            });
            if (!persisted) {
                result = fail(Error{.status = persisted.error(),
                                    .reason = Reason::PersistenceFailed,
                                    .operation = Operation::Transaction,
                                    .group_id = Group::stable_id.raw()});
            }
        }
    });
#endif
    return result;
}

template <typename System, typename... States>
void mark_transaction_persistence(std::tuple<States...>& states) noexcept
{
    std::apply(
        []<typename... StateT>(StateT&... state) {
            (
                [&] {
                    using ParameterT = typename StateT::ParameterType;
                    if (state.changed) {
                        (void)mark_committed_persistence<System, ParameterT>(
                            state.before.revision + 1U, false);
                    }
                }(),
                ...);
        },
        states);
}

template <typename System, typename... ParameterTypes>
[[nodiscard]] TransactionUpdate
dispatch_transaction_changes(std::tuple<TransactionState<ParameterTypes>...>& states,
                             bool deferred) noexcept
{
    using Facility = typename System::ParameterFacility;
    TransactionUpdate update{};
    ((update.changed += transaction_state<ParameterTypes>(states).changed ? 1U : 0U,
      update.adjusted += transaction_state<ParameterTypes>(states).normalized.adjusted ? 1U : 0U),
     ...);
    update.changes_deferred = deferred && update.changed != 0;

    for_each_type<typename Facility::ChangeTypes>([&]<typename ChangeT> {
        using Traits = change_traits<ChangeT>;
        using ParameterT = typename Traits::ParameterType;
        if constexpr ((std::is_same_v<ParameterT, ParameterTypes> || ...)) {
            auto& state = transaction_state<ParameterT>(states);
            if (state.changed) {
                const Change<ParameterT> change{
                    .old_value = state.old_value,
                    .new_value = state.normalized.value,
                    .revision = state.before.revision + 1U,
                    .origin = UpdateOrigin::LocalTransaction,
                    .adjusted = state.normalized.adjusted,
                    .transaction = true,
                };
                if (deferred) {
                    Facility::template change_state<ChangeT>.defer(change);
                } else if (invoke_change<System, ChangeT>(change) != Status::Ok) {
                    ++update.change_failures;
                }
            }
        }
    });
    return update;
}

template <typename System, typename... ParameterTypes>
[[nodiscard]] Result<TransactionUpdate, Error>
set_all_parameters(std::tuple<Assignment<ParameterTypes>...> assignments,
                   bool no_wait = false) noexcept
{
    using Facility = typename System::ParameterFacility;
    static_assert((System::ParameterCatalog::template contains<ParameterTypes> && ...),
                  "SOLAR_DIAGNOSTIC_PARAMETER_TRANSACTION_NOT_REGISTERED: transaction contains "
                  "an unregistered parameter");
    static_assert(unique_types_v<TypeList<ParameterTypes...>>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_TRANSACTION_DUPLICATE: transaction assigns one "
                  "parameter more than once");
    static_assert(
        (AccessTraits<typename Facility::template Policies<ParameterTypes>::Access>::writable &&
         ...),
        "SOLAR_DIAGNOSTIC_PARAMETER_TRANSACTION_READ_ONLY: transaction contains a read-only "
        "parameter");
    static_assert(
        (!AccessTraits<typename Facility::template Policies<ParameterTypes>::Access>::privileged &&
         ...),
        "SOLAR_DIAGNOSTIC_PARAMETER_TRANSACTION_PRIVILEGED: ordinary transaction contains a "
        "privileged parameter");
    static_assert(
        sizeof...(ParameterTypes) == 1 ||
            ((PersistenceTraits<
                  typename Facility::template Policies<ParameterTypes>::Persistence>::kind !=
              PersistenceKind::Immediate) &&
             ...),
        "SOLAR_DIAGNOSTIC_PARAMETER_TRANSACTION_IMMEDIATE_DURABILITY: a multi-parameter RAM "
        "transaction cannot imply atomic durability across independent immediate records");

    if (kernel::in_isr()) {
        return fail(Error{.status = Status::Invalid,
                          .reason = Reason::InvalidContext,
                          .operation = Operation::Transaction});
    }
    const auto system_state = lifecycle::Engine<System>::state();
    if (!Facility::ready.load(std::memory_order_acquire) ||
        !Facility::mutation_open.load(std::memory_order_acquire) ||
        system_state == lifecycle::SystemState::Stopping ||
        system_state == lifecycle::SystemState::RollingBack ||
        system_state == lifecycle::SystemState::Stopped ||
        system_state == lifecycle::SystemState::Failed) {
        return fail(Error{.status = Status::NotReady,
                          .reason = Reason::NotReady,
                          .operation = Operation::Transaction});
    }

    std::tuple<TransactionState<ParameterTypes>...> states{};
    auto normalized = normalize_transaction<0, System>(assignments, states);
    if (!normalized) {
        return fail(normalized.error());
    }

    auto gate = kernel::unique_lock(Facility::write_gate, no_wait ? kernel::Timeout::no_wait()
                                                                  : kernel::Timeout::forever());
    if (!gate) {
        return fail(Error{.status = gate.error(),
                          .reason = gate.error() == Status::WouldBlock ? Reason::WouldBlock
                                                                       : Reason::InternalInvariant,
                          .operation = Operation::Transaction});
    }
    auto captured = capture_transaction<0, System>(states, no_wait);
    if (!captured) {
        return fail(captured.error());
    }
    auto prepared = prepare_transaction_persistence<System>(states);
    if (!prepared) {
        return fail(prepared.error());
    }
    auto written = write_transaction_values<0, System>(states);
    if (!written) {
        restore_transaction_values<System>(states);
        return fail(written.error());
    }
    update_transaction_records<System>(states);
    mark_transaction_persistence<System>(states);
    (void)(*gate).unlock();

    if constexpr ((EffectiveDeferred<
                       typename Facility::template Policies<ParameterTypes>::Persistence>::value ||
                   ...)) {
        auto scheduled = schedule_deferred_persistence<System>();
        if (!scheduled) {
            for_each_type<TypeList<ParameterTypes...>>([&]<typename ParameterT> {
                if constexpr (EffectiveDeferred<typename Facility::template Policies<
                                  ParameterT>::Persistence>::value) {
                    Facility::template slot<ParameterT>.mark_schedule_failure(scheduled.error());
                }
            });
        }
    }

    const bool deferred = system_state != lifecycle::SystemState::Running &&
                          !Facility::activating_changes.load(std::memory_order_acquire);
    return dispatch_transaction_changes<System>(states, deferred);
}

template <typename System, typename ParameterT>
[[nodiscard]] Result<void, Error> save_parameter(bool no_wait) noexcept
{
    using Facility = typename System::ParameterFacility;
    using Policies = typename Facility::template Policies<ParameterT>;
    using Persistence = PersistenceTraits<typename Policies::Persistence>;
    static_assert(System::ParameterCatalog::template contains<ParameterT>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_SAVE_NOT_REGISTERED: saved parameter is absent "
                  "from the bound catalog");
    if constexpr (!Persistence::persistent) {
        return fail(make_error<System, ParameterT>(Operation::Save, Status::NotSupported,
                                                   Reason::PersistenceUnavailable));
    }
    if (kernel::in_isr()) {
        return fail(make_error<System, ParameterT>(Operation::Save, Status::Invalid,
                                                   Reason::InvalidContext));
    }
    if (!Facility::ready.load(std::memory_order_acquire)) {
        return fail(
            make_error<System, ParameterT>(Operation::Save, Status::NotReady, Reason::NotReady));
    }
    if constexpr (Persistence::kind == PersistenceKind::Transactional) {
        using Group = typename Persistence::Group;
        auto persisted = persist_group_slot<typename Facility::Architecture, Group>(true, no_wait);
        if (!persisted) {
            return fail(make_error<System, ParameterT>(Operation::Save, persisted.error(),
                                                       persisted.error() == Status::WouldBlock
                                                           ? Reason::WouldBlock
                                                           : Reason::PersistenceFailed));
        }
    } else if constexpr (Persistence::persistent) {
        auto persisted =
            persist_parameter_slot<typename Facility::Architecture, ParameterT>(true, no_wait);
        if (!persisted) {
            return fail(make_error<System, ParameterT>(Operation::Save, persisted.error(),
                                                       persisted.error() == Status::WouldBlock
                                                           ? Reason::WouldBlock
                                                           : Reason::PersistenceFailed));
        }
    }
    return {};
}

template <typename System>
[[nodiscard]] Result<PersistenceReport, Error> save_all_parameters(bool no_wait) noexcept
{
    using Facility = typename System::ParameterFacility;
    if (kernel::in_isr()) {
        return fail(Error{.status = Status::Invalid,
                          .reason = Reason::InvalidContext,
                          .operation = Operation::SaveAll});
    }
    if (!Facility::ready.load(std::memory_order_acquire)) {
        return fail(Error{.status = Status::NotReady,
                          .reason = Reason::NotReady,
                          .operation = Operation::SaveAll});
    }
    PersistenceReport report{};
    std::optional<Error> first_error{};
    for_each_type<typename Facility::ParameterTypes>([&]<typename ParameterT> {
        using Policies = typename Facility::template Policies<ParameterT>;
        using Persistence = PersistenceTraits<typename Policies::Persistence>;
        if constexpr (Persistence::persistent &&
                      Persistence::kind != PersistenceKind::Transactional) {
            ++report.visited;
            auto persisted =
                persist_parameter_slot<typename Facility::Architecture, ParameterT>(true, no_wait);
            if (!persisted) {
                ++report.failed;
                if (!first_error) {
                    first_error = make_error<System, ParameterT>(
                        Operation::SaveAll, persisted.error(),
                        persisted.error() == Status::WouldBlock ? Reason::WouldBlock
                                                                : Reason::PersistenceFailed);
                }
            } else if (*persisted) {
                ++report.saved;
            } else {
                ++report.clean;
            }
        }
    });
    for_each_type<typename Facility::Groups>([&]<typename Group> {
        ++report.visited;
        auto persisted = persist_group_slot<typename Facility::Architecture, Group>(true, no_wait);
        if (!persisted) {
            ++report.failed;
            if (!first_error) {
                first_error = Error{.status = persisted.error(),
                                    .reason = persisted.error() == Status::WouldBlock
                                                  ? Reason::WouldBlock
                                                  : Reason::PersistenceFailed,
                                    .operation = Operation::SaveAll,
                                    .group_id = Group::stable_id.raw()};
            }
        } else if (*persisted) {
            ++report.saved;
        } else {
            ++report.clean;
        }
    });
    if (first_error) {
        return fail(*first_error);
    }
    return report;
}

template <typename System>
[[nodiscard]] Result<PersistenceReport, Error> flush_parameters(bool no_wait) noexcept
{
    using Facility = typename System::ParameterFacility;
    if constexpr (!Facility::Architecture::has_deferred) {
        return PersistenceReport{};
    } else {
        if (kernel::in_isr()) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidContext,
                              .operation = Operation::Flush});
        }
        if (!Facility::ready.load(std::memory_order_acquire)) {
            return fail(Error{.status = Status::NotReady,
                              .reason = Reason::NotReady,
                              .operation = Operation::Flush});
        }
        auto cancelled =
            execution::detail::cancel_registration<System, typename Facility::DeferredRegistration>(
                false);
        if (!cancelled && cancelled.error().status != Status::NotReady) {
            return fail(Error{.status = cancelled.error().status,
                              .reason = Reason::PersistenceUnavailable,
                              .operation = Operation::Flush});
        }
        if (no_wait) {
            if (!execution::detail::registration_quiescent<
                    System, typename Facility::DeferredRegistration>()) {
                return fail(Error{.status = Status::WouldBlock,
                                  .reason = Reason::WouldBlock,
                                  .operation = Operation::Flush});
            }
        } else {
            const auto deadline = kernel::Deadline::after(persistence_timeout);
            if (!execution::detail::wait_registration_quiescent<
                    System, typename Facility::DeferredRegistration>(deadline)) {
                return fail(Error{.status = Status::Timeout,
                                  .reason = Reason::PersistenceUnavailable,
                                  .operation = Operation::Flush});
            }
        }
        PersistenceReport report{};
        std::optional<Error> first_error{};
        for_each_type<typename Facility::ParameterTypes>([&]<typename ParameterT> {
            using Policies = typename Facility::template Policies<ParameterT>;
            using Persistence = PersistenceTraits<typename Policies::Persistence>;
            if constexpr (Persistence::deferred) {
                ++report.visited;
                auto persisted =
                    persist_parameter_slot<typename Facility::Architecture, ParameterT>(true,
                                                                                        no_wait);
                if (!persisted) {
                    ++report.failed;
                    if (!first_error) {
                        first_error = make_error<System, ParameterT>(
                            Operation::Flush, persisted.error(),
                            persisted.error() == Status::WouldBlock ? Reason::WouldBlock
                                                                    : Reason::PersistenceFailed);
                    }
                } else if (*persisted) {
                    ++report.saved;
                } else {
                    ++report.clean;
                }
            }
        });
        for_each_type<typename Facility::Groups>([&]<typename Group> {
            using Commit = typename GroupTraits<Group>::CommitTraits;
            if constexpr (Commit::deferred) {
                ++report.visited;
                auto persisted =
                    persist_group_slot<typename Facility::Architecture, Group>(true, no_wait);
                if (!persisted) {
                    ++report.failed;
                    if (!first_error) {
                        first_error = Error{.status = persisted.error(),
                                            .reason = persisted.error() == Status::WouldBlock
                                                          ? Reason::WouldBlock
                                                          : Reason::PersistenceFailed,
                                            .operation = Operation::Flush,
                                            .group_id = Group::stable_id.raw()};
                    }
                } else if (*persisted) {
                    ++report.saved;
                } else {
                    ++report.clean;
                }
            }
        });
        if (first_error) {
            return fail(*first_error);
        }
        return report;
    }
}

template <typename System> [[nodiscard]] Result<void> initialize_facility() noexcept
{
    using Facility = typename System::ParameterFacility;
    (void)Facility{};
    return {};
}

} // namespace solar::parameters::detail

namespace solar::parameters
{

template <typename Architecture> Result<void> Facility<Architecture>::init() noexcept
{
    ready.store(false, std::memory_order_release);
    mutation_open.store(false, std::memory_order_release);
    activating_changes.store(false, std::memory_order_release);
    persistence_active.store(false, std::memory_order_release);
    schedule_persistence = nullptr;
    for_each_type<ParameterTypes>([]<typename ParameterT> {
        constexpr auto index = detail::type_index_v<ParameterT, ParameterTypes>;
        slot<ParameterT>.initialize(LocalId{static_cast<LocalId::Representation>(index)});
    });
    for_each_type<ChangeTypes>([]<typename ChangeT> {
        using ParameterT = typename change_traits<ChangeT>::ParameterType;
        constexpr auto change_index = detail::type_index_v<ChangeT, ChangeTypes>;
        constexpr auto parameter_index = detail::type_index_v<ParameterT, ParameterTypes>;
        change_state<ChangeT>.initialize(
            ChangeLocalId{static_cast<ChangeLocalId::Representation>(change_index)},
            LocalId{static_cast<LocalId::Representation>(parameter_index)},
            component::LocalId{static_cast<component::LocalId::Representation>(
                detail::type_index_v<typename change_traits<ChangeT>::ObserverType,
                                     typename Architecture::ComponentTypes>)});
    });
    for_each_type<Groups>([]<typename Group> { group_state<Group>.initialize(); });
    auto persistence = detail::initialize_persistence<Architecture>();
    if (!persistence) {
        return persistence;
    }
    ready.store(true, std::memory_order_release);
    mutation_open.store(true, std::memory_order_release);
    return {};
}

template <typename Architecture> Result<void> Facility<Architecture>::start() noexcept
{
    mutation_open.store(true, std::memory_order_release);
    return {};
}

template <typename Architecture> Result<void> Facility<Architecture>::stop() noexcept
{
    mutation_open.store(false, std::memory_order_release);
    persistence_active.store(false, std::memory_order_release);
    if constexpr (Architecture::has_deferred &&
                  std::is_same_v<PersistenceStopPolicy, stop::FlushDeferred>) {
        auto flushed = detail::persist_deferred_parameters<Architecture>(true);
        if (!flushed) {
            return fail(flushed.error());
        }
    }
    return {};
}

template <typename Architecture> Result<void> Facility<Architecture>::deinit() noexcept
{
    mutation_open.store(false, std::memory_order_release);
    ready.store(false, std::memory_order_release);
    persistence_active.store(false, std::memory_order_release);
    schedule_persistence = nullptr;
    return {};
}

template <typename Architecture> Result<void> Facility<Architecture>::run_deferred() noexcept
{
    auto persisted = detail::persist_deferred_parameters<Architecture>(false);
    if (!persisted) {
        return fail(persisted.error());
    }
    return detail::schedule_next_deferred<Facility>();
}

template <typename Architecture>
template <typename System>
Result<void> Facility<Architecture>::activate_changes() noexcept
{
    activating_changes.store(true, std::memory_order_release);
    Status status = Status::Ok;
    for_each_type<ChangeTypes>([&]<typename ChangeT> {
        auto pending = change_state<ChangeT>.take_pending();
        if (pending && !(pending->old_value == pending->new_value)) {
            const auto current = detail::invoke_change<System, ChangeT>(*pending);
            if (status == Status::Ok && current != Status::Ok) {
                status = current;
            }
        }
    });
    activating_changes.store(false, std::memory_order_release);
    return status == Status::Ok ? Result<void>{} : Result<void>{fail(status)};
}

template <typename Architecture>
template <typename System>
void Facility<Architecture>::activate_runtime() noexcept
{
    if constexpr (Architecture::has_deferred) {
        schedule_persistence = +[](std::chrono::nanoseconds delay) noexcept -> Result<void> {
            auto scheduled =
                execution::detail::schedule_registration<System, DeferredRegistration>(delay, true);
            return scheduled ? Result<void>{} : Result<void>{fail(scheduled.error().status)};
        };
        persistence_active.store(true, std::memory_order_release);
        (void)detail::schedule_deferred_persistence<System>();
    }
}

} // namespace solar::parameters

#else

namespace solar::parameters::detail
{

[[nodiscard]] constexpr Error disabled_error(Operation operation) noexcept
{
    return {
        .status = Status::NotSupported,
        .reason = Reason::Disabled,
        .operation = operation,
    };
}

template <typename System, typename ParameterT>
[[nodiscard]] Result<typename ParameterT::Value, Error> read_parameter(bool) noexcept
{
    return fail(disabled_error(Operation::Get));
}

template <typename System, typename ParameterT>
[[nodiscard]] Result<Update<ParameterT>, Error> set_parameter(typename ParameterT::Value, bool,
                                                              UpdateOrigin, bool) noexcept
{
    return fail(disabled_error(Operation::Set));
}

template <typename System, typename ParameterT>
[[nodiscard]] Result<ParameterRecord<ParameterT>, Error> parameter_record() noexcept
{
    return fail(disabled_error(Operation::Query));
}

template <typename Observer, typename ParameterT, typename RouteTag, typename Changes>
struct FindChange;

template <typename Observer, typename ParameterT, typename RouteTag>
struct FindChange<Observer, ParameterT, RouteTag, TypeList<>>
{
    static constexpr bool found = false;
    using type = void;
};

template <typename System, typename ChangeT> [[nodiscard]] ChangeRecord change_record() noexcept
{
    return {};
}

template <typename System, typename... ParametersT>
[[nodiscard]] Result<Snapshot<ParametersT...>, Error> snapshot_parameters(bool) noexcept
{
    return fail(disabled_error(Operation::Snapshot));
}

template <typename System, typename... ParameterTypes>
[[nodiscard]] Result<TransactionUpdate, Error>
set_all_parameters(std::tuple<Assignment<ParameterTypes>...>, bool = false) noexcept
{
    return fail(disabled_error(Operation::Transaction));
}

template <typename System, typename ParameterT>
[[nodiscard]] Result<void, Error> save_parameter(bool) noexcept
{
    return fail(disabled_error(Operation::Save));
}

template <typename System>
[[nodiscard]] Result<PersistenceReport, Error> save_all_parameters(bool) noexcept
{
    return fail(disabled_error(Operation::SaveAll));
}

template <typename System>
[[nodiscard]] Result<PersistenceReport, Error> flush_parameters(bool) noexcept
{
    return fail(disabled_error(Operation::Flush));
}

} // namespace solar::parameters::detail

#endif
