#pragma once

#include <atomic>

#include "solar/core/status.hpp"

namespace solar::remote
{

/**
 * Explicit adapter that exports a typed module value through a Remote Stream.
 * It can be used as a log sink, metric exporter, or synchronous event observer.
 */
template <typename Runtime, typename Stream> struct StreamSink
{
    using Value = typename Stream::Value;

    [[nodiscard]] static Result<void> write(const Value& value)
    {
        auto published = Runtime::template publish<Stream>(value);
        if (!published) {
            failures.fetch_add(1, std::memory_order_relaxed);
            return fail<solar::Error>({.status = published.error().status});
        }
        return {};
    }

    static void observe(const Value& value)
    {
        (void)write(value);
    }

    inline static std::atomic_uint32_t failures{};
};

} // namespace solar::remote
