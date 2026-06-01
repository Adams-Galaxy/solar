#pragma once

#include <cstddef>

#include "solar/entry/profile.hpp"
#include "solar/rtos/this_thread.hpp"

namespace solar::entry
{

struct SimulatedRunOptions
{
    /// Sleep interval used by the outer host runner while services run.
    Milliseconds loop_interval{1};
    /// Optional deterministic iteration cap for host tests. Zero means forever.
    std::size_t max_iterations = 0;
};

template <typename Profile>
/**
 * @brief Construct, boot, and supervise a profile in a host/sim process.
 */
int run(SimulatedRunOptions options = {})
{
    const Status preflight_status = preflight<Profile>();
    if (preflight_status != Status::Ok)
    {
        return 1;
    }

    const Status facility_status = init_facilities<Profile>();
    if (facility_status != Status::Ok)
    {
        return 1;
    }

    const Status facility_start_status = start_facilities<Profile>();
    if (facility_start_status != Status::Ok)
    {
        return 1;
    }

    typename Profile::System system{};
    const Status boot_status = boot<Profile>(system);
    if (boot_status != Status::Ok)
    {
        return exit_code<Profile>(system);
    }

    std::size_t iterations = 0;
    while (!finished<Profile>(system))
    {
        if (options.max_iterations != 0 && iterations >= options.max_iterations)
        {
            break;
        }

        ++iterations;

        if (options.loop_interval.count() > 0)
        {
            rtos::ThisThread::sleep_for(options.loop_interval);
        }
    }

    return exit_code<Profile>(system);
}

} // namespace solar::entry
