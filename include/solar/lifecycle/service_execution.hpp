#pragma once

#include <cstddef>
#include <cstdint>

#include "solar/core/lifecycle.hpp"
#include "solar/core/status.hpp"
#include "solar/kernel/thread.hpp"

namespace solar
{

struct ServiceExecutionRecord
{
    ComponentDescriptor service{};
    bool thread_created = false;
    bool running = false;
    bool stop_requested = false;
    bool exited = false;
    bool exit_after_stop_request = false;
    bool join_timed_out = false;
    bool abort_configured = false;
    bool abort_attempted = false;
    bool aborted = false;
    std::uint32_t stop_timeout_ms = 0;
    std::size_t configured_stack_bytes = 0;
    std::size_t stack_unused_bytes = 0;
    std::size_t stack_used_bytes = 0;
    bool stack_usage_available = false;
    kernel::ThreadId native_id = nullptr;
    Status run_status = Status::NotReady;
    Status abort_status = Status::NotReady;
};

} // namespace solar
