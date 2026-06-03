#pragma once

#include <cstddef>
#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/config.hpp"
#include "solar/kernel/thread.hpp"

namespace solar::kernel
{

struct ThreadSnapshot
{
    const char *name = nullptr;
    ThreadId id = nullptr;
    bool running = false;
    std::size_t stack_size = 0;
    std::size_t stack_unused = 0;
    std::size_t stack_used = 0;
    bool stack_available = false;
    std::uint64_t runtime_cycles = 0;
    bool runtime_available = false;
};

inline Status fill_stack_usage(ThreadSnapshot &snapshot, const Thread &thread, std::size_t stack_size)
{
    snapshot.stack_size = stack_size;

    std::size_t unused = 0;
    const int result = k_thread_stack_space_get(thread.native_thread(), &unused);
    const Status status = status_from_native(result);
    if (status != Status::Ok)
    {
        snapshot.stack_available = false;
        return status;
    }

    snapshot.stack_unused = unused;
    snapshot.stack_used = stack_size >= unused ? stack_size - unused : 0;
    snapshot.stack_available = true;
    return Status::Ok;
}

inline ThreadSnapshot snapshot_thread(const char *name, const Thread &thread, std::size_t stack_size = 0)
{
    ThreadSnapshot snapshot{};
    snapshot.name = name;
    snapshot.id = thread.native_handle();
    snapshot.running = thread.running();
    if (stack_size > 0)
    {
        (void)fill_stack_usage(snapshot, thread, stack_size);
    }
    return snapshot;
}

inline ThreadId current_thread()
{
    return k_current_get();
}

inline bool in_isr()
{
    return k_is_in_isr();
}

} // namespace solar::kernel
