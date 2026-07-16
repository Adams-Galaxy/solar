#include <string>

#include <solar/kernel.hpp>

#if SOLAR_FAIL_CASE == 1
solar::kernel::MessageQueue<std::string, 2> invalid_queue;
#elif SOLAR_FAIL_CASE == 2
solar::kernel::MessageQueue<std::uint32_t, 0> invalid_queue;
#elif SOLAR_FAIL_CASE == 3
constexpr auto invalid_priority =
    solar::kernel::Priority::preemptive<CONFIG_NUM_PREEMPT_PRIORITIES>();
#elif SOLAR_FAIL_CASE == 4
solar::kernel::PollSet<1> unavailable_poll;
#elif SOLAR_FAIL_CASE == 5
solar::kernel::EventFlags unavailable_events;
#elif SOLAR_FAIL_CASE == 6
solar::kernel::Thread<0> invalid_thread;
#elif SOLAR_FAIL_CASE == 7
solar::kernel::WorkQueue<0> invalid_work_queue;
#elif SOLAR_FAIL_CASE == 8
solar::kernel::MemorySlab<0, 1> invalid_slab;
#elif SOLAR_FAIL_CASE == 9
solar::kernel::MemorySlab<16, 0> invalid_slab;
#elif SOLAR_FAIL_CASE == 10
solar::kernel::MemorySlab<16, 1, 3> invalid_slab;
#elif SOLAR_FAIL_CASE == 11
solar::kernel::Pipe<0> invalid_pipe;
#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_KERNEL_FAILURE_CASE
#endif

int main()
{
    return 0;
}
