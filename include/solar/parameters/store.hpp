#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

namespace solar::parameters
{

/** A closed, generated or handwritten set of parameter declarations. */
template <typename... Declarations> struct Schema
{
    static_assert(unique_types_v<TypeList<Declarations...>>,
                  "SOLAR_PARAMETERS_DUPLICATE_DECLARATION: schema entries must be unique");
    using Entries = TypeList<Declarations...>;
};

template <typename T>
concept StoreDeclaration = requires {
    typename T::Value;
    { T::default_value } -> std::convertible_to<typename T::Value>;
    { T::name } -> std::convertible_to<const char*>;
};

template <typename T> struct IsSchema : std::false_type
{};

template <typename... Declarations> struct IsSchema<Schema<Declarations...>> : std::true_type
{};

template <typename T>
concept SchemaType = IsSchema<T>::value;

template <typename Value> struct StoreUpdate
{
    Value previous_value{};
    Value effective_value{};
    std::uint64_t revision{};
};

/** One typed value staged for an atomic multi-parameter update. */
template <StoreDeclaration Declaration> struct ValueAssignment
{
    using DeclarationType = Declaration;
    typename Declaration::Value value;
};

struct TransactionResult
{
    std::uint64_t revision{};
    std::size_t updated{};
};

template <StoreDeclaration Declaration> struct ParameterSnapshotEntry
{
    typename Declaration::Value value{};
};

template <StoreDeclaration... Declarations> struct ParameterSnapshot
{
    std::tuple<ParameterSnapshotEntry<Declarations>...> values{};
    std::uint64_t transaction_revision{};

    template <typename Declaration> [[nodiscard]] const auto& get() const noexcept
    {
        static_assert(contains_v<Declaration, TypeList<Declarations...>>);
        return std::get<ParameterSnapshotEntry<Declaration>>(values).value;
    }
};

namespace store_detail
{

class SpinMutex
{
  public:
    void lock() noexcept
    {
        while (locked_.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() noexcept
    {
        locked_.clear(std::memory_order_release);
    }

  private:
    std::atomic_flag locked_ = ATOMIC_FLAG_INIT;
};

class Guard
{
  public:
    explicit Guard(SpinMutex& mutex) noexcept : mutex_(mutex)
    {
        mutex_.lock();
    }

    ~Guard()
    {
        mutex_.unlock();
    }

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

  private:
    SpinMutex& mutex_;
};

template <StoreDeclaration Declaration> struct Slot
{
    using DeclarationType = Declaration;
    using Value = typename Declaration::Value;

    Value value{Declaration::default_value};
    std::uint64_t revision{};
};

template <typename Declaration>
[[nodiscard]] constexpr bool valid(const typename Declaration::Value& value) noexcept
{
    if constexpr (requires { Declaration::minimum; }) {
        if (value < static_cast<typename Declaration::Value>(Declaration::minimum)) {
            return false;
        }
    }
    if constexpr (requires { Declaration::maximum; }) {
        if (value > static_cast<typename Declaration::Value>(Declaration::maximum)) {
            return false;
        }
    }
    if constexpr (requires { Declaration::validate(value); }) {
        return Declaration::validate(value);
    }
    return true;
}

template <typename SchemaT> struct Storage;

template <StoreDeclaration... Declarations> struct Storage<Schema<Declarations...>>
{
    using Slots = std::tuple<Slot<Declarations>...>;

    template <typename Function> static void for_each(Slots& slots, Function&& function)
    {
        std::apply([&function](auto&... slot) { (function(slot), ...); }, slots);
    }

    template <typename Function> static void for_each_const(const Slots& slots, Function&& function)
    {
        std::apply([&function](const auto&... slot) { (function(slot), ...); }, slots);
    }
};

template <typename AssignmentT> struct AssignmentDeclaration;

template <StoreDeclaration Declaration> struct AssignmentDeclaration<ValueAssignment<Declaration>>
{
    using type = Declaration;
};

} // namespace store_detail

/**
 * Explicitly owned, bounded parameter storage.
 *
 * Store has no dependency on System, Remote, persistence, or a codec. Reads and
 * writes are serialized. A transaction validates every staged value before it
 * changes any slot, then commits all values while holding the same lock.
 */
template <SchemaType SchemaT> class Store
{
  public:
    using SchemaType = SchemaT;
    using Dependencies = TypeList<>;

    Store() = default;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    [[nodiscard]] Result<void> initialize() noexcept
    {
        store_detail::Guard lock{mutex_};
        Storage::for_each(slots_, []<typename SlotT>(SlotT& slot) {
            using Declaration = typename SlotT::DeclarationType;
            slot.value = Declaration::default_value;
            slot.revision = 0;
        });
        transaction_revision_ = 0;
        initialized_ = true;
        active_ = false;
        return {};
    }

    [[nodiscard]] Result<void> start() noexcept
    {
        store_detail::Guard lock{mutex_};
        if (!initialized_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        if (active_) {
            return fail<solar::Error>({.status = Status::Already});
        }
        active_ = true;
        return {};
    }

    [[nodiscard]] Result<void> stop() noexcept
    {
        store_detail::Guard lock{mutex_};
        if (!initialized_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        active_ = false;
        return {};
    }

    [[nodiscard]] Result<void> deinitialize() noexcept
    {
        store_detail::Guard lock{mutex_};
        active_ = false;
        initialized_ = false;
        return {};
    }

    template <StoreDeclaration Declaration>
    [[nodiscard]] Result<typename Declaration::Value> get() const noexcept
    {
        require_member<Declaration>();
        store_detail::Guard lock{mutex_};
        if (!initialized_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        return slot<Declaration>().value;
    }

    template <StoreDeclaration Declaration, typename Value>
    [[nodiscard]] Result<StoreUpdate<typename Declaration::Value>> set(Value&& value) noexcept
    {
        require_member<Declaration>();
        static_assert(std::same_as<std::remove_cvref_t<Value>, typename Declaration::Value>,
                      "SOLAR_PARAMETERS_INCOMPATIBLE_VALUE: set requires the parameter's exact "
                      "Value type");
        if (!store_detail::valid<Declaration>(value)) {
            return fail<solar::Error>({.status = Status::Invalid});
        }

        store_detail::Guard lock{mutex_};
        if (!initialized_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        auto& target = slot<Declaration>();
        const auto previous = target.value;
        target.value = std::forward<Value>(value);
        ++target.revision;
        ++transaction_revision_;
        return StoreUpdate<typename Declaration::Value>{
            .previous_value = previous,
            .effective_value = target.value,
            .revision = target.revision,
        };
    }

    template <typename... Assignments>
    [[nodiscard]] Result<TransactionResult> set_many(Assignments&&... assignments) noexcept
        requires(sizeof...(Assignments) > 0)
    {
        using AssignmentTypes = TypeList<std::remove_cvref_t<Assignments>...>;
        using Declarations = TypeList<typename store_detail::AssignmentDeclaration<
            std::remove_cvref_t<Assignments>>::type...>;
        static_assert(unique_types_v<Declarations>,
                      "SOLAR_PARAMETERS_DUPLICATE_TRANSACTION_ENTRY: a transaction may stage "
                      "each parameter once");
        static_assert(list_size_v<AssignmentTypes> == list_size_v<Declarations>);
        (require_member<typename store_detail::AssignmentDeclaration<
             std::remove_cvref_t<Assignments>>::type>(),
         ...);

        if (!(store_detail::valid<typename store_detail::AssignmentDeclaration<
                  std::remove_cvref_t<Assignments>>::type>(assignments.value) &&
              ...)) {
            return fail<solar::Error>({.status = Status::Invalid});
        }

        store_detail::Guard lock{mutex_};
        if (!initialized_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        const auto revision = ++transaction_revision_;
        (commit_assignment(std::forward<Assignments>(assignments)), ...);
        return TransactionResult{.revision = revision, .updated = sizeof...(Assignments)};
    }

    /** Read several values and their common transaction revision atomically. */
    template <StoreDeclaration... Declarations>
    [[nodiscard]] Result<ParameterSnapshot<Declarations...>> snapshot() const noexcept
    {
        static_assert(unique_types_v<TypeList<Declarations...>>);
        (require_member<Declarations>(), ...);
        store_detail::Guard lock{mutex_};
        if (!initialized_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        return ParameterSnapshot<Declarations...>{
            .values =
                std::tuple<ParameterSnapshotEntry<Declarations>...>{
                    ParameterSnapshotEntry<Declarations>{slot<Declarations>().value}...},
            .transaction_revision = transaction_revision_,
        };
    }

    /** Explicit dynamic boundary for inspection and transport adapters. */
    template <typename Visitor>
    [[nodiscard]] Result<void> visit(std::uint32_t id, Visitor&& visitor) const
    {
        store_detail::Guard lock{mutex_};
        if (!initialized_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        bool found{};
        Storage::for_each_const(slots_, [&]<typename SlotT>(const SlotT& value) {
            using Declaration = typename SlotT::DeclarationType;
            static_assert(
                requires { Declaration::id; },
                "SOLAR_PARAMETERS_DYNAMIC_ID_REQUIRED: dynamic inspection requires "
                "generated IDs");
            if (!found && Declaration::id == id) {
                visitor(Declaration{}, value.value, value.revision);
                found = true;
            }
        });
        return found ? Result<void>{} : fail<solar::Error>({.status = Status::NotFound});
    }

    template <StoreDeclaration Declaration>
    [[nodiscard]] Result<std::uint64_t> revision() const noexcept
    {
        require_member<Declaration>();
        store_detail::Guard lock{mutex_};
        if (!initialized_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        return slot<Declaration>().revision;
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        store_detail::Guard lock{mutex_};
        return initialized_;
    }

    [[nodiscard]] bool active() const noexcept
    {
        store_detail::Guard lock{mutex_};
        return active_;
    }

  private:
    using Storage = store_detail::Storage<SchemaT>;

    template <typename Declaration> static consteval void require_member()
    {
        static_assert(contains_v<Declaration, typename SchemaT::Entries>,
                      "SOLAR_PARAMETERS_NOT_IN_SCHEMA: parameter is absent from the closed "
                      "application schema");
    }

    template <typename Declaration> auto& slot() noexcept
    {
        return std::get<store_detail::Slot<Declaration>>(slots_);
    }

    template <typename Declaration> const auto& slot() const noexcept
    {
        return std::get<store_detail::Slot<Declaration>>(slots_);
    }

    template <StoreDeclaration Declaration>
    void commit_assignment(ValueAssignment<Declaration> assignment) noexcept
    {
        auto& target = slot<Declaration>();
        target.value = std::move(assignment.value);
        ++target.revision;
    }

    mutable store_detail::SpinMutex mutex_{};
    typename Storage::Slots slots_{};
    std::uint64_t transaction_revision_{};
    bool initialized_{};
    bool active_{};
};

/** Canonical application facade forwarding to exactly one Store object. */
template <typename Application, SchemaType SchemaT> struct StaticStore
{
    using ApplicationType = Application;
    using SchemaType = SchemaT;
    using Dependencies = TypeList<>;

    inline static Store<SchemaT> storage{};

    [[nodiscard]] static Result<void> initialize() noexcept
    {
        return storage.initialize();
    }

    [[nodiscard]] static Result<void> start() noexcept
    {
        return storage.start();
    }

    [[nodiscard]] static Result<void> stop() noexcept
    {
        return storage.stop();
    }

    [[nodiscard]] static Result<void> deinitialize() noexcept
    {
        return storage.deinitialize();
    }

    template <StoreDeclaration Declaration>
    [[nodiscard]] static Result<typename Declaration::Value> get() noexcept
    {
        return storage.template get<Declaration>();
    }

    template <StoreDeclaration Declaration, typename Value>
    [[nodiscard]] static auto set(Value&& value) noexcept
    {
        return storage.template set<Declaration>(std::forward<Value>(value));
    }

    template <typename... Assignments>
    [[nodiscard]] static auto set_many(Assignments&&... assignments) noexcept
    {
        return storage.set_many(std::forward<Assignments>(assignments)...);
    }

    template <StoreDeclaration... Declarations> [[nodiscard]] static auto snapshot() noexcept
    {
        return storage.template snapshot<Declarations...>();
    }

    template <typename Visitor>
    [[nodiscard]] static Result<void> visit(std::uint32_t id, Visitor&& visitor)
    {
        return storage.visit(id, std::forward<Visitor>(visitor));
    }

    template <StoreDeclaration Declaration>
    [[nodiscard]] static Result<std::uint64_t> revision() noexcept
    {
        return storage.template revision<Declaration>();
    }
};

} // namespace solar::parameters
