#pragma once

#include <type_traits>

#include "solar/entry/profile.hpp"

namespace solar::entry
{

/**
 * @brief Run a Solar profile from a Zephyr application `main()`.
 *
 * Zephyr owns kernel startup, scheduling, interrupt routing, and board
 * bring-up. Solar owns profile facility setup, static graph boot, and
 * dispatching the profile's optional `awake(system, report)` or
 * `failed(system, report)` callbacks.
 */
template <typename Profile>
int run_zephyr()
{
    Status status = preflight<Profile>();
    if (status != Status::Ok)
    {
        return static_cast<int>(status);
    }

    status = init_facilities<Profile>();
    if (status != Status::Ok)
    {
        return static_cast<int>(status);
    }

    status = start_facilities<Profile>();
    if (status != Status::Ok)
    {
        return static_cast<int>(status);
    }

    status = boot<Profile>();
    if (status != Status::Ok)
    {
        return exit_code<Profile>();
    }

    return exit_code<Profile>();
}

} // namespace solar::entry
