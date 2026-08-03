#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <utility>

#include "solar/core/status.hpp"

namespace solar::log
{

/** Explicitly joins retained storage to zero or more immediate sinks. */
template <typename Store, typename... Sinks> class Capture
{
  public:
    Capture(Store& store, Sinks&... sinks) noexcept : store_{store}, sinks_{&sinks...} {}

    template <typename Record> [[nodiscard]] Result<void> write(Record record)
    {
        if (auto retained = store_.append(record); !retained) {
            return retained;
        }
        Result<void> result{};
        std::apply(
            [&](auto*... sinks) { ((result ? result = sinks->write(record) : result), ...); },
            sinks_);
        return result;
    }

  private:
    Store& store_;
    std::tuple<Sinks*...> sinks_;
};

/** Encodes records into an explicitly owned persistence backend. */
template <typename Storage, typename Codec, std::uint32_t Key, std::size_t MaximumBytes>
class PersistenceSink
{
  public:
    explicit PersistenceSink(Storage& storage) noexcept : storage_{storage} {}

    template <typename Record> [[nodiscard]] Result<void> write(const Record& record) noexcept
    {
        std::array<std::byte, MaximumBytes> bytes{};
        auto encoded = Codec::encode(record, bytes);
        if (!encoded) {
            return fail<solar::Error>(encoded.error());
        }
        return storage_.write(Key, std::span{bytes}.first(*encoded));
    }

  private:
    Storage& storage_;
};

} // namespace solar::log
