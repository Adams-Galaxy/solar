#include <string>

#include <solar/application/priority.hpp>
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
#elif SOLAR_FAIL_CASE == 12
constexpr auto invalid_priority =
    solar::kernel::Priority::cooperative<CONFIG_NUM_COOP_PRIORITIES>();
#elif SOLAR_FAIL_CASE == 13
constexpr auto invalid_priority =
    solar::kernel::Priority::native<K_HIGHEST_APPLICATION_THREAD_PRIO - 1>();
#elif SOLAR_FAIL_CASE == 14
constexpr auto invalid_priority =
    solar::kernel::Priority::native<K_LOWEST_APPLICATION_THREAD_PRIO + 1>();
#elif SOLAR_FAIL_CASE == 15 && CONFIG_NUM_METAIRQ_PRIORITIES > 0
constexpr auto invalid_priority =
    solar::kernel::Priority::meta_irq<CONFIG_NUM_METAIRQ_PRIORITIES>();
#elif SOLAR_FAIL_CASE == 16
constexpr auto invalid_priority =
    solar::PreemptivePriority<CONFIG_NUM_PREEMPT_PRIORITIES>::resolve();
#elif SOLAR_FAIL_CASE == 17
constexpr auto invalid_priority = solar::CooperativePriority<CONFIG_NUM_COOP_PRIORITIES>::resolve();
#elif SOLAR_FAIL_CASE == 18
void invalid_native_access(solar::kernel::Mutex& mutex)
{
    (void)mutex.native_handle();
}
#elif SOLAR_FAIL_CASE == 19
void invalid_native_access(solar::kernel::Thread<1024>& thread)
{
    (void)thread.native_handle();
}
#elif SOLAR_FAIL_CASE == 20
void invalid_native_access(solar::kernel::WorkQueue<1024>& queue)
{
    (void)queue.native_handle();
}
#elif SOLAR_FAIL_CASE == 21
void invalid_native_access(solar::kernel::Timer& timer)
{
    (void)timer.native_handle();
}
#elif SOLAR_FAIL_CASE == 22
void invalid_native_access(solar::kernel::ConditionVariable& condition)
{
    (void)condition.native_handle();
}
#elif SOLAR_FAIL_CASE == 23
void invalid_native_access(solar::kernel::Pipe<16>& pipe)
{
    (void)pipe.native_handle();
}
#elif SOLAR_FAIL_CASE == 24
void invalid_native_access(solar::kernel::MemorySlab<16, 2>& slab)
{
    (void)slab.native_handle();
}
#elif SOLAR_FAIL_CASE == 25
void invalid_native_access(solar::kernel::SpinLock& lock)
{
    (void)lock.native_handle();
}
#elif SOLAR_FAIL_CASE == 26
solar::kernel::ThreadConfiguration invalid_raw_options{
    .priority = solar::kernel::Priority::preemptive<0>(), .options = K_USER};
#elif SOLAR_FAIL_CASE == 27
solar::kernel::ThreadOptions invalid_user_mode{K_USER};
#elif SOLAR_FAIL_CASE == 28
solar::kernel::Stack<std::int32_t, 2> invalid_stack_value;
#elif SOLAR_FAIL_CASE == 29
solar::kernel::Stack<std::uint32_t, 0> invalid_stack_capacity;
#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_KERNEL_FAILURE_CASE
#endif

int main()
{
    return 0;
}
