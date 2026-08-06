#pragma once

#include "solar/core/status.hpp"

namespace solar::supervisor
{

/** Evaluates one external health probe and records it in a Monitor. */
template <typename Monitor, typename Check, typename Probe> struct Evaluate
{
    [[nodiscard]] static Result<void> poll()
    {
        auto condition = Probe::condition();
        if (!condition) {
            return fail<solar::Error>(condition.error());
        }
        return Monitor::template report<Check>(*condition);
    }
};

} // namespace solar::supervisor
