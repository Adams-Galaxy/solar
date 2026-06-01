#pragma once

#include "solar/entry/profile.hpp"
#include "solar/rtos/this_thread.hpp"

/**
 * @brief Define Arduino `setup()`/`loop()` for a Solar profile.
 *
 * The macro keeps Arduino-specific entry glue out of robot profiles. Solar owns
 * boot in `setup()`, while service threads own ongoing behavior.
 */
#define SOLAR_ARDUINO_ENTRY(ProfileType)          \
    namespace                                     \
    {                                             \
        ProfileType::System solar_entry_system{}; \
    }                                             \
                                                  \
    void setup()                                  \
    {                                             \
        if (::solar::entry::init_facilities<ProfileType>() == ::solar::Status::Ok && \
            ::solar::entry::start_facilities<ProfileType>() == ::solar::Status::Ok) \
        {                                         \
            (void)::solar::entry::boot<ProfileType>(solar_entry_system); \
        }                                         \
    }                                             \
                                                  \
    void loop()                                   \
    {                                             \
        (void)solar_entry_system;                 \
        ::solar::rtos::ThisThread::yield();       \
    }
