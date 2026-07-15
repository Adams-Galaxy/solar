#pragma once

#include <cstddef>

#include "solar/core.hpp"

namespace solar::facilities
{

/**
 * @brief Thin passive inspection helper for graph and observability snapshots.
 *
 * Inspection is intentionally not a large service. It provides convenient
 * accessors while the authoritative state remains the static system graph and
 * observability facilities.
 */
class Inspection
{
public:
    using Name = solar::Name<"inspection">;

    template <typename ContextT>
    Status init(ContextT &)
    {
        return Status::Ok;
    }

    template <typename SystemT>
    static constexpr std::size_t component_count()
    {
        return SystemT::graph::components().size();
    }

    template <typename LoggerT>
    auto logger_stats() const
    {
        return LoggerT::stats();
    }
};

} // namespace solar::facilities
