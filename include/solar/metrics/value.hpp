#pragma once

#include <cstdint>
#include <type_traits>

#include "solar/core.hpp"
#include "solar/metrics/catalog.hpp"

namespace solar::metrics
{

enum class ValueType : std::uint8_t
{
    I64,
    U64,
    F64,
    Bool,
};

/**
 * @brief Small numeric union used by metric snapshots.
 *
 * Snapshots intentionally expose one primary value. Policy-specific details can
 * be layered separately without inflating this common payload.
 */
struct Value
{
    ValueType type = ValueType::I64;
    union
    {
        std::int64_t i64;
        std::uint64_t u64;
        double f64;
        bool boolean;
    };

    constexpr Value() : i64(0) {}

    static constexpr Value from(std::int64_t value)
    {
        Value out{};
        out.type = ValueType::I64;
        out.i64 = value;
        return out;
    }

    static constexpr Value from(std::uint64_t value)
    {
        Value out{};
        out.type = ValueType::U64;
        out.u64 = value;
        return out;
    }

    static constexpr Value from(double value)
    {
        Value out{};
        out.type = ValueType::F64;
        out.f64 = value;
        return out;
    }

    static constexpr Value from(bool value)
    {
        Value out{};
        out.type = ValueType::Bool;
        out.boolean = value;
        return out;
    }

    constexpr double as_f64() const
    {
        switch (type)
        {
        case ValueType::I64:
            return static_cast<double>(i64);
        case ValueType::U64:
            return static_cast<double>(u64);
        case ValueType::F64:
            return f64;
        case ValueType::Bool:
            return boolean ? 1.0 : 0.0;
        }
        return 0.0;
    }
};

/**
 * @brief Convert a scalar C++ value into the snapshot value union.
 */
template <typename T>
constexpr Value make_value(T value)
{
    using Raw = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Raw, bool>)
    {
        return Value::from(value);
    }
    else if constexpr (std::is_floating_point_v<Raw>)
    {
        return Value::from(static_cast<double>(value));
    }
    else if constexpr (std::is_unsigned_v<Raw>)
    {
        return Value::from(static_cast<std::uint64_t>(value));
    }
    else
    {
        return Value::from(static_cast<std::int64_t>(value));
    }
}

struct Snapshot
{
    std::uint32_t id = 0;
    const char *name = nullptr;
    Kind kind = Kind::Sample;
    const char *unit = nullptr;
    Value value{};
};

} // namespace solar::metrics
