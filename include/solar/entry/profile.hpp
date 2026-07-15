#pragma once

#include <concepts>
#include <type_traits>

#include "solar/core.hpp"

namespace solar::entry
{

namespace detail
{

template <typename Profile>
concept HasPreflight = requires {
    Profile::preflight();
};

template <typename Profile>
concept HasAwake = requires(BootReport const &report) {
    Profile::awake(report);
};

template <typename Profile>
concept HasFailed = requires(BootReport const &report) {
    Profile::failed(report);
};

template <typename Profile>
concept HasFinished = requires {
    { Profile::finished() } -> std::convertible_to<bool>;
};

template <typename Profile>
concept HasExitCode = requires {
    { Profile::exit_code() } -> std::convertible_to<int>;
};

template <typename Profile, typename = void>
struct SystemFacilitiesOf
{
    using type = solar::Facilities<>;
};

template <typename Profile>
struct SystemFacilitiesOf<Profile, std::void_t<typename Profile::System::Facilities>>
{
    using type = typename Profile::System::Facilities;
};

template <typename Profile, typename = void>
struct SystemEntryFacilitiesOf : SystemFacilitiesOf<Profile>
{
};

template <typename Profile>
struct SystemEntryFacilitiesOf<Profile, std::void_t<typename Profile::System::EntryFacilities>>
{
    using type = typename Profile::System::EntryFacilities;
};

template <typename Profile, typename = void>
struct FacilitiesOf : SystemEntryFacilitiesOf<Profile>
{
};

template <typename Profile>
struct FacilitiesOf<Profile, std::void_t<typename Profile::Facilities>>
{
    using type = typename Profile::Facilities;
};

template <typename Facility>
concept HasFacilityInit = requires {
    Facility::init();
};

template <typename Facility>
concept HasFacilityStart = requires {
    Facility::start();
};

template <typename Facility>
concept HasFacilityStop = requires {
    Facility::stop();
};

template <typename ReturnT>
Status normalize_hook_status(ReturnT &&value)
{
    using Raw = std::remove_cvref_t<ReturnT>;
    if constexpr (std::is_same_v<Raw, Status>)
    {
        return value;
    }
    else if constexpr (std::is_same_v<Raw, Result<void>>)
    {
        return value.status();
    }
    else if constexpr (std::is_convertible_v<Raw, bool>)
    {
        return value ? Status::Ok : Status::Error;
    }
    else
    {
        return Status::Ok;
    }
}

template <typename Facility>
Status call_facility_init()
{
    if constexpr (HasFacilityInit<Facility>)
    {
        if constexpr (std::is_void_v<decltype(Facility::init())>)
        {
            Facility::init();
            return Status::Ok;
        }
        else
        {
            return normalize_hook_status(Facility::init());
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename Facility>
Status call_facility_start()
{
    if constexpr (HasFacilityStart<Facility>)
    {
        if constexpr (std::is_void_v<decltype(Facility::start())>)
        {
            Facility::start();
            return Status::Ok;
        }
        else
        {
            return normalize_hook_status(Facility::start());
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename Facility>
Status call_facility_stop()
{
    if constexpr (HasFacilityStop<Facility>)
    {
        if constexpr (std::is_void_v<decltype(Facility::stop())>)
        {
            Facility::stop();
            return Status::Ok;
        }
        else
        {
            return normalize_hook_status(Facility::stop());
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename FacilitiesT>
struct FacilityRunner;

template <typename... FacilityTypes>
struct FacilityRunner<solar::Facilities<FacilityTypes...>>
{
    static Status init()
    {
        Status status = Status::Ok;
        ((status == Status::Ok ? status = call_facility_init<FacilityTypes>() : status), ...);
        return status;
    }

    static Status start()
    {
        Status status = Status::Ok;
        ((status == Status::Ok ? status = call_facility_start<FacilityTypes>() : status), ...);
        return status;
    }

    static Status stop()
    {
        Status status = Status::Ok;
        ((status == Status::Ok ? status = call_facility_stop<FacilityTypes>() : status), ...);
        return status;
    }
};

} // namespace detail

template <typename Profile>
/**
 * @brief Run a profile's optional preflight hook.
 *
 * Preflight happens before `System` construction in Solar-owned entry helpers.
 * It is intended for project-specific setup that must happen before graph boot.
 */
Status preflight()
{
    if constexpr (detail::HasPreflight<Profile>)
    {
        if constexpr (std::is_void_v<decltype(Profile::preflight())>)
        {
            Profile::preflight();
            return Status::Ok;
        }
        else
        {
            return detail::normalize_hook_status(Profile::preflight());
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename Profile>
/**
 * @brief Initialize profile-level static facilities.
 */
Status init_facilities()
{
    using Facilities = typename detail::FacilitiesOf<Profile>::type;
    return detail::FacilityRunner<Facilities>::init();
}

template <typename Profile>
/**
 * @brief Start profile-level static facilities.
 */
Status start_facilities()
{
    using Facilities = typename detail::FacilitiesOf<Profile>::type;
    return detail::FacilityRunner<Facilities>::start();
}

template <typename Profile>
/**
 * @brief Stop profile-level static facilities.
 */
Status stop_facilities()
{
    using Facilities = typename detail::FacilitiesOf<Profile>::type;
    return detail::FacilityRunner<Facilities>::stop();
}

template <typename Profile>
/**
 * @brief Boot a constructed profile system and dispatch awake/failed hooks.
 */
Status boot()
{
    using System = typename Profile::System;
    const Result<BootReport> result = System::boot();
    const Status status = result.status();
    const BootReport &report = System::boot_report();
    if (status == Status::Ok)
    {
        if constexpr (detail::HasAwake<Profile>)
        {
            Profile::awake(report);
        }
    }
    else
    {
        if constexpr (detail::HasFailed<Profile>)
        {
            Profile::failed(report);
        }
    }
    return status;
}

template <typename Profile>
/**
 * @brief Query an optional profile completion policy.
 */
bool finished()
{
    if constexpr (detail::HasFinished<Profile>)
    {
        return Profile::finished();
    }
    else
    {
        return false;
    }
}

template <typename Profile>
/**
 * @brief Query an optional profile exit code policy.
 */
int exit_code()
{
    if constexpr (detail::HasExitCode<Profile>)
    {
        return Profile::exit_code();
    }
    else
    {
        return Profile::System::boot_report().ok() ? 0 : 1;
    }
}

} // namespace solar::entry
