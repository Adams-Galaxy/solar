#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "solar/core.hpp"

namespace solar::metrics
{

/**
 * @brief Store and report the last observed value.
 */
struct Last
{
    template <typename ValueT>
    class Storage
    {
    public:
        using Output = ValueT;

        void reset()
        {
            value_ = {};
            count_ = 0;
        }

        void observe(ValueT value)
        {
            value_ = value;
            ++count_;
        }

        Output value() const
        {
            return value_;
        }

        std::uint64_t count() const
        {
            return count_;
        }

    private:
        ValueT value_{};
        std::uint64_t count_ = 0;
    };
};

/**
 * @brief Store and report the largest observed value.
 */
struct Max
{
    template <typename ValueT>
    class Storage
    {
    public:
        using Output = ValueT;

        void reset()
        {
            value_ = {};
            count_ = 0;
        }

        void observe(ValueT value)
        {
            if (count_ == 0 || value > value_)
            {
                value_ = value;
            }
            ++count_;
        }

        Output value() const
        {
            return value_;
        }

        std::uint64_t count() const
        {
            return count_;
        }

    private:
        ValueT value_{};
        std::uint64_t count_ = 0;
    };
};

/**
 * @brief Mean of the last N samples.
 */
template <std::size_t N>
struct WindowMean
{
    static_assert(N > 0, "WindowMean requires a non-zero window");

    template <typename ValueT>
    class Storage
    {
    public:
        using Output = double;

        void reset()
        {
            samples_ = {};
            head_ = 0;
            size_ = 0;
            total_ = 0.0;
        }

        void observe(ValueT value)
        {
            const double sample = static_cast<double>(value);
            if (size_ < N)
            {
                samples_[head_] = sample;
                total_ += sample;
                head_ = (head_ + 1U) % N;
                ++size_;
                return;
            }

            total_ -= samples_[head_];
            samples_[head_] = sample;
            total_ += sample;
            head_ = (head_ + 1U) % N;
        }

        Output value() const
        {
            return size_ == 0 ? 0.0 : total_ / static_cast<double>(size_);
        }

        std::uint64_t count() const
        {
            return size_;
        }

    private:
        std::array<double, N> samples_{};
        std::size_t head_ = 0;
        std::size_t size_ = 0;
        double total_ = 0.0;
    };
};

/**
 * @brief Exponential moving average with alpha = Numerator / Denominator.
 */
template <std::uint32_t Numerator, std::uint32_t Denominator>
struct Ema
{
    static_assert(Denominator > 0, "Ema denominator must be non-zero");
    static_assert(Numerator <= Denominator, "Ema alpha must be between 0 and 1");

    template <typename ValueT>
    class Storage
    {
    public:
        using Output = double;

        void reset()
        {
            filtered_ = 0.0;
            count_ = 0;
        }

        void observe(ValueT value)
        {
            const double sample = static_cast<double>(value);
            if (count_ == 0)
            {
                filtered_ = sample;
            }
            else
            {
                constexpr double alpha = static_cast<double>(Numerator) / static_cast<double>(Denominator);
                filtered_ = (alpha * sample) + ((1.0 - alpha) * filtered_);
            }
            ++count_;
        }

        Output value() const
        {
            return filtered_;
        }

        std::uint64_t count() const
        {
            return count_;
        }

    private:
        double filtered_ = 0.0;
        std::uint64_t count_ = 0;
    };
};

template <typename PolicyT, typename ValueT>
/**
 * @brief Concept for metric sample reducers.
 *
 * A policy is a type with `Storage<ValueT>`; the storage owns deterministic
 * state and exposes `reset`, `observe`, and `value`.
 */
concept SamplePolicy = requires(typename PolicyT::template Storage<ValueT> storage, ValueT sample) {
    storage.reset();
    storage.observe(sample);
    storage.value();
};

template <typename PolicyT, typename ValueT>
using PolicyStorageT = typename PolicyT::template Storage<ValueT>;

} // namespace solar::metrics
