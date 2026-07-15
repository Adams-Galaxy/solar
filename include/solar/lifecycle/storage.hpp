#pragma once

#include <array>
#include <cstddef>

#include "solar/core/lifecycle.hpp"
#include "solar/core/status.hpp"
#include "solar/kernel/mutex.hpp"

namespace solar::lifecycle
{

/**
 * @brief Fixed-capacity, mutex-protected lifecycle state for one System.
 *
 * Every public operation acquires the same internal mutex, so no external lock
 * may be held when calling Storage. Queries return copies and are safe from
 * service threads. Storage never invokes component code; orchestration must
 * release all lifecycle locks before calling a component hook.
 */
template <std::size_t Capacity>
class Storage
{
public:
    using Records = std::array<LifecycleRecord, Capacity>;

    Storage() = default;
    explicit Storage(const Records &records) : records_(records) {}

    Status initialize(const Records &records)
    {
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            if (!records[i].component.id.valid())
            {
                return Status::Invalid;
            }

            for (std::size_t j = i + 1; j < records.size(); ++j)
            {
                if (records[i].component.id == records[j].component.id)
                {
                    return Status::Already;
                }
            }
        }

        const Status lock_status = mutex_.lock();
        if (lock_status != Status::Ok)
        {
            return lock_status;
        }

        records_ = records;
        system_state_ = SystemState::Dormant;
        const Status unlock_status = mutex_.unlock();
        return unlock_status;
    }

    Result<SystemState> system_state()
    {
        const Status lock_status = mutex_.lock();
        if (lock_status != Status::Ok)
        {
            return lock_status;
        }

        const SystemState state = system_state_;
        const Status unlock_status = mutex_.unlock();
        return unlock_status == Status::Ok ? Result<SystemState>{state}
                                           : Result<SystemState>{unlock_status};
    }

    Status transition_system(SystemState state)
    {
        const Status lock_status = mutex_.lock();
        if (lock_status != Status::Ok)
        {
            return lock_status;
        }

        Status result = Status::Ok;
        if (!can_transition(system_state_, state))
        {
            result = Status::Invalid;
        }
        else
        {
            system_state_ = state;
        }

        const Status unlock_status = mutex_.unlock();
        return result == Status::Ok ? unlock_status : result;
    }

    Result<Records> records()
    {
        const Status lock_status = mutex_.lock();
        if (lock_status != Status::Ok)
        {
            return lock_status;
        }

        const Records copy = records_;
        const Status unlock_status = mutex_.unlock();
        return unlock_status == Status::Ok ? Result<Records>{copy}
                                           : Result<Records>{unlock_status};
    }

    Result<LifecycleRecord> record(ComponentId id)
    {
        const Status lock_status = mutex_.lock();
        if (lock_status != Status::Ok)
        {
            return lock_status;
        }

        const LifecycleRecord *found = find(id);
        Result<LifecycleRecord> result = found == nullptr
                                             ? Result<LifecycleRecord>{Status::NotFound}
                                             : Result<LifecycleRecord>{*found};

        const Status unlock_status = mutex_.unlock();
        return unlock_status == Status::Ok ? result
                                           : Result<LifecycleRecord>{unlock_status};
    }

    Status transition(ComponentId id,
                      LifecycleState state,
                      LifecycleOperation operation,
                      Status operation_status = Status::Ok)
    {
        const Status lock_status = mutex_.lock();
        if (lock_status != Status::Ok)
        {
            return lock_status;
        }

        LifecycleRecord *found = find(id);
        Status result = Status::Ok;

        if (found == nullptr)
        {
            result = Status::NotFound;
        }
        else if (!can_transition(found->state, state))
        {
            result = Status::Invalid;
        }
        else if (operation_status != Status::Ok && state != LifecycleState::Failed)
        {
            result = Status::Invalid;
        }
        else
        {
            found->state = state;
            found->last_operation = operation;
            found->last_status = operation_status;
            ++found->transition_count;

            if (state == LifecycleState::Initialized && operation_status == Status::Ok)
            {
                found->initialized_successfully = true;
            }
            if (state == LifecycleState::Running && operation_status == Status::Ok)
            {
                found->started_successfully = true;
            }
            if (operation == LifecycleOperation::Deinit && operation_status == Status::Ok &&
                (state == LifecycleState::Deinitialized || state == LifecycleState::Failed))
            {
                found->deinitialized_successfully = true;
            }

            if (operation_status != Status::Ok && !found->first_failure.present())
            {
                found->first_failure = {operation, operation_status};
            }
        }

        const Status unlock_status = mutex_.unlock();
        return result == Status::Ok ? unlock_status : result;
    }

private:
    LifecycleRecord *find(ComponentId id)
    {
        for (auto &record : records_)
        {
            if (record.component.id == id)
            {
                return &record;
            }
        }
        return nullptr;
    }

    const LifecycleRecord *find(ComponentId id) const
    {
        for (const auto &record : records_)
        {
            if (record.component.id == id)
            {
                return &record;
            }
        }
        return nullptr;
    }

    kernel::Mutex mutex_{};
    SystemState system_state_ = SystemState::Dormant;
    Records records_{};
};

} // namespace solar::lifecycle
