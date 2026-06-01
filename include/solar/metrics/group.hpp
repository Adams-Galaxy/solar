#pragma once

#include "solar/metrics/catalog.hpp"

namespace solar::metrics
{

/**
 * @brief Bind a user-authored metric group facade to a concrete metrics store.
 *
 * GroupT should expose `using Metrics = solar::metrics::List<...>` and static
 * template methods such as `observe<StoreT>(...)`. Solar does not implicitly
 * update grouped metrics; the group is the explicit domain API that chooses
 * which descriptors receive each observation.
 */
template <typename StoreT, typename GroupT>
struct BoundGroup
{
    using Metrics = typename GroupT::Metrics;

    template <typename... Args>
    static decltype(auto) observe(Args &&...args)
    {
        return GroupT::template observe<StoreT>(static_cast<Args &&>(args)...);
    }

    template <typename... Args>
    static decltype(auto) inc(Args &&...args)
    {
        return GroupT::template inc<StoreT>(static_cast<Args &&>(args)...);
    }

    template <typename... Args>
    static decltype(auto) record(Args &&...args)
    {
        return GroupT::template record<StoreT>(static_cast<Args &&>(args)...);
    }
};

} // namespace solar::metrics
