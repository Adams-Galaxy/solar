#pragma once

#include <cstddef>

#include "solar/core/lifecycle.hpp"
#include "solar/core/status.hpp"

namespace solar
{

struct StopFailure
{
    LifecycleOperation operation = LifecycleOperation::None;
    Status status = Status::Ok;
    ComponentDescriptor component{};

    constexpr bool present() const
    {
        return status != Status::Ok;
    }
};

struct StopReport
{
    Status status = Status::Ok;
    StopFailure first_failure{};
    std::size_t failure_count = 0;
    std::size_t completed_operations = 0;

    constexpr bool ok() const
    {
        return status == Status::Ok;
    }

    constexpr void record_failure(ComponentDescriptor component,
                                  LifecycleOperation operation,
                                  Status failure_status)
    {
        if (failure_status == Status::Ok)
        {
            return;
        }

        if (failure_count == 0)
        {
            status = failure_status;
            first_failure = {operation, failure_status, component};
        }
        ++failure_count;
    }

    constexpr void record_success()
    {
        ++completed_operations;
    }
};

} // namespace solar
