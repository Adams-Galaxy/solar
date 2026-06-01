#pragma once

#include <cstddef>

#include "solar/core.hpp"
#include "solar/rtos/queue.hpp"

namespace solar::services
{

/**
 * @brief Graph-owned fixed-depth typed queue.
 *
 * Channels are passive graph entries for bounded communication between tasks,
 * services, and app code. They intentionally use static capacity and typed
 * payloads instead of a runtime bus registry.
 */
template <typename NameT, typename PayloadT, std::size_t Depth>
class Channel
{
public:
    using Name = NameT;
    using Payload = PayloadT;

    static_assert(Depth > 0, "Solar channels require a non-zero depth");

    Status publish(const PayloadT &payload)
    {
        return queue_.try_send(payload);
    }

    Result<PayloadT> try_receive()
    {
        PayloadT payload{};
        const Status status = queue_.try_receive(payload);
        if (status != Status::Ok)
        {
            return status;
        }
        return payload;
    }

    std::size_t size() const
    {
        return queue_.size();
    }

    static constexpr std::size_t capacity()
    {
        return Depth;
    }

private:
    rtos::Queue<PayloadT, Depth> queue_{};
};

} // namespace solar::services
