#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "solar/core/spin_mutex.hpp"
#include "solar/core/status.hpp"

namespace solar::persistence
{

template <typename T>
concept Storage = requires(T value, std::uint32_t key, std::span<const std::byte> input,
                           std::span<std::byte> output) {
    { value.write(key, input) } -> std::same_as<Result<void>>;
    { value.read(key, output) } -> std::same_as<Result<std::size_t>>;
};

/** Fixed-slot storage with atomic-per-call replacement and explicit capacity. */
template <std::size_t Slots, std::size_t MaximumBytes> class MemoryStorage
{
  public:
    [[nodiscard]] Result<void> initialize()
    {
        SpinGuard lock{mutex_};
        entries_ = {};
        return {};
    }
    [[nodiscard]] Result<void> write(std::uint32_t key, std::span<const std::byte> input)
    {
        SpinGuard lock{mutex_};
        if (input.size() > MaximumBytes)
            return fail<solar::Error>({.status = Status::NoSpace});
        Entry* target{};
        for (auto& entry : entries_) {
            if (entry.used && entry.key == key) {
                target = &entry;
                break;
            }
            if (!entry.used && target == nullptr)
                target = &entry;
        }
        if (target == nullptr)
            return fail<solar::Error>({.status = Status::NoSpace});
        std::copy(input.begin(), input.end(), target->bytes.begin());
        // Publish metadata last so a backend following this contract never
        // exposes a partially copied replacement.
        target->key = key;
        target->size = input.size();
        target->used = true;
        return {};
    }
    [[nodiscard]] Result<std::size_t> read(std::uint32_t key,
                                           std::span<std::byte> output) const
    {
        SpinGuard lock{mutex_};
        for (const auto& entry : entries_)
            if (entry.used && entry.key == key) {
                if (output.size() < entry.size)
                    return fail<solar::Error>({.status = Status::NoSpace});
                std::copy_n(entry.bytes.begin(), entry.size, output.begin());
                return entry.size;
            }
        return fail<solar::Error>({.status = Status::NotFound});
    }

  private:
    struct Entry
    {
        std::uint32_t key{};
        std::size_t size{};
        bool used{};
        std::array<std::byte, MaximumBytes> bytes{};
    };
    std::array<Entry, Slots> entries_{};
    mutable SpinMutex mutex_{};
};

/** Static facade over the same bounded storage implementation. */
template <typename Application, std::size_t Slots, std::size_t MaximumBytes>
struct StaticMemoryStorage
{
    inline static MemoryStorage<Slots, MaximumBytes> storage{};
    [[nodiscard]] static Result<void> initialize()
    {
        return storage.initialize();
    }
    [[nodiscard]] static Result<void> write(std::uint32_t key,
                                            std::span<const std::byte> input)
    {
        return storage.write(key, input);
    }
    [[nodiscard]] static Result<std::size_t> read(std::uint32_t key,
                                                  std::span<std::byte> output)
    {
        return storage.read(key, output);
    }
};

} // namespace solar::persistence
