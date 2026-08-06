#pragma once

#include "solar/core/status.hpp"

namespace solar::metrics
{

/** Read-only inspection view over one typed metric. */
template <typename Metrics, typename Metric> struct Inspect
{
    [[nodiscard]] static auto read(Metrics& metrics)
    {
        return metrics.template get<Metric>();
    }

    [[nodiscard]] static auto read()
        requires requires { Metrics::template get<Metric>(); }
    {
        return Metrics::template get<Metric>();
    }
};

/** Pulls one typed snapshot from a metric store into an explicit sink. */
template <typename Metrics, typename Metric> struct Export
{
    template <typename Sink> [[nodiscard]] static Result<void> write(Metrics& metrics, Sink& sink)
    {
        auto sample = metrics.template get<Metric>();
        if (!sample) {
            return fail<solar::Error>(sample.error());
        }
        return sink.write(*sample);
    }

    template <typename Sink>
    [[nodiscard]] static Result<void> write(Sink& sink)
        requires requires { Metrics::template get<Metric>(); }
    {
        auto sample = Metrics::template get<Metric>();
        if (!sample) {
            return fail<solar::Error>(sample.error());
        }
        return sink.write(*sample);
    }
};

} // namespace solar::metrics
