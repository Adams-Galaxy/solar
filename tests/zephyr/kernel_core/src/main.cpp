#include <atomic>
#include <chrono>
#include <cstdint>
#include <type_traits>

#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>

#include <solar/kernel.hpp>

using namespace std::chrono_literals;

namespace kernel = solar::kernel;

static_assert(
    std::is_same_v<decltype(kernel::MessageQueue<std::uint32_t, 4>::capacity), const std::size_t>);
static_assert(kernel::MessageQueue<std::uint32_t, 4>::capacity == 4);
static_assert(kernel::Priority::preemptive<0>().native_handle() == K_PRIO_PREEMPT(0));
static_assert(!std::is_copy_constructible_v<kernel::Mutex>);
static_assert(!std::is_move_constructible_v<kernel::Mutex>);
static_assert(!std::is_copy_constructible_v<kernel::RecursiveMutex>);
static_assert(!std::is_move_constructible_v<kernel::RecursiveMutex>);
static_assert(!std::is_copy_constructible_v<kernel::Semaphore>);
static_assert(!std::is_move_constructible_v<kernel::Semaphore>);
static_assert(!std::is_copy_constructible_v<kernel::MessageQueue<std::uint32_t, 2>>);
static_assert(!std::is_move_constructible_v<kernel::MessageQueue<std::uint32_t, 2>>);
static_assert(!std::is_copy_constructible_v<kernel::EventFlags>);
static_assert(!std::is_move_constructible_v<kernel::EventFlags>);
static_assert(!std::is_copy_constructible_v<kernel::PollSet<3>>);
static_assert(!std::is_move_constructible_v<kernel::PollSet<3>>);
static_assert(!std::is_copy_constructible_v<kernel::Timer>);
static_assert(!std::is_move_constructible_v<kernel::Timer>);
static_assert(sizeof(kernel::RecursiveMutex) == sizeof(k_mutex));
static_assert(sizeof(kernel::Semaphore) == sizeof(k_sem));
static_assert(sizeof(kernel::EventFlags) == sizeof(k_event));
static_assert(sizeof(kernel::Mutex) >= sizeof(k_mutex) + sizeof(std::atomic<k_tid_t>));
static_assert(sizeof(kernel::MessageQueue<std::uint32_t, 2>) >=
              sizeof(k_msgq) + 2 * sizeof(std::uint32_t));
static_assert(sizeof(kernel::PollSet<3>) >= 3 * sizeof(k_poll_event));
static_assert(sizeof(kernel::Timer) >= sizeof(k_timer) + 2 * sizeof(kernel::Timer::Callback));
#if CONFIG_NUM_COOP_PRIORITIES > 0
static_assert(kernel::Priority::cooperative<0>().native_handle() == K_PRIO_COOP(0));
#endif

namespace
{

solar::Status result_status(const solar::Result<void>& result)
{
    return result ? solar::Status::Ok : solar::status_of(result.error());
}

solar::Status result_status(const solar::Error& error)
{
    return solar::status_of(error);
}

solar::Status result_status(solar::Status status)
{
    return status;
}

kernel::Mutex held_mutex;
kernel::Semaphore holder_ready;
kernel::Semaphore holder_release;
K_THREAD_STACK_DEFINE(holder_stack, 1024);
k_thread holder_thread;

void hold_mutex(void*, void*, void*)
{
    zassert_equal(result_status(held_mutex.lock()), solar::Status::Ok);
    holder_ready.give();
    zassert_equal(result_status(holder_release.take()), solar::Status::Ok);
    zassert_equal(result_status(held_mutex.unlock()), solar::Status::Ok);
}

std::atomic_uint32_t timer_expiries{};
std::atomic_bool timer_callback_was_isr{};
std::atomic_bool timer_stop_was_thread{};
kernel::Semaphore timer_signal;

void timer_expired(kernel::Timer&) noexcept
{
    timer_callback_was_isr.store(kernel::in_isr(), std::memory_order_relaxed);
    timer_expiries.fetch_add(1, std::memory_order_relaxed);
    timer_signal.give_isr();
}

void timer_stopped(kernel::Timer&) noexcept
{
    timer_stop_was_thread.store(!kernel::in_isr(), std::memory_order_relaxed);
}

struct IsrContext
{
    kernel::Semaphore* semaphore;
    kernel::MessageQueue<std::uint32_t, 2>* queue;
    kernel::EventFlags* events;
    kernel::Mutex* mutex;
    bool observed_isr{};
    solar::Status queue_status{solar::Status::Error};
    solar::Status mutex_status{solar::Status::Error};
};

void exercise_isr(const void* argument)
{
    auto& context = *static_cast<IsrContext*>(const_cast<void*>(argument));
    context.observed_isr = kernel::in_isr();
    context.semaphore->give_isr();
    context.queue_status = result_status(context.queue->try_send_isr(42));
    (void)context.events->post_isr(0x1);
    context.mutex_status = result_status(context.mutex->try_lock());
}

} // namespace

ZTEST(solar_kernel_core, test_time_timeout_and_deadline)
{
    zassert_true(K_TIMEOUT_EQ(kernel::Timeout::no_wait().native_handle(), K_NO_WAIT));
    zassert_true(K_TIMEOUT_EQ(kernel::Timeout::forever().native_handle(), K_FOREVER));
    zassert_true(kernel::to_ticks_ceil(1ms) > 0);
    zassert_true(kernel::to_ticks_ceil(1us) > 0);

    const auto before = kernel::now();
    auto deadline = kernel::Deadline::after(10ms);
    zassert_false(deadline.expired());
    zassert_false(deadline.remaining().is_no_wait());
    (void)kernel::this_thread::sleep_for(20ms);
    zassert_true(deadline.expired());
    zassert_true(deadline.remaining().is_no_wait());
    zassert_true(kernel::now() > before);

    const auto forever = kernel::Deadline::forever();
    zassert_false(forever.expired());
    zassert_true(forever.remaining().is_forever());
}

ZTEST(solar_kernel_core, test_priority_scheduler_and_current_thread)
{
    const auto preemptive = kernel::Priority::try_preemptive(0);
    zassert_true(preemptive.has_value());
    zassert_true(preemptive->is_preemptive());
    zassert_false(kernel::Priority::try_preemptive(CONFIG_NUM_PREEMPT_PRIORITIES).has_value());

    const auto original = kernel::this_thread::priority();
    kernel::this_thread::set_priority(*preemptive);
    zassert_equal(kernel::this_thread::priority().native_handle(), preemptive->native_handle());
    kernel::this_thread::set_priority(original);

    zassert_not_null(kernel::this_thread::id());
    zassert_equal(result_status(kernel::this_thread::yield()), solar::Status::Ok);
    zassert_equal(result_status(kernel::this_thread::busy_wait_for(10us)), solar::Status::Ok);

    auto scheduler_lock = kernel::SchedulerLock::acquire();
    zassert_true(scheduler_lock.has_value());
}

ZTEST(solar_kernel_core, test_mutex_lock_ownership_and_timeout)
{
    kernel::Mutex mutex;
    zassert_equal(result_status(mutex.lock()), solar::Status::Ok);
    zassert_equal(result_status(mutex.try_lock()), solar::Status::Deadlock);
    zassert_equal(result_status(mutex.unlock()), solar::Status::Ok);
    zassert_equal(result_status(mutex.unlock()), solar::Status::PermissionDenied);

    {
        auto guard = kernel::lock_guard(mutex);
        zassert_true(guard.has_value());
        zassert_equal(result_status(mutex.try_lock()), solar::Status::Deadlock);
    }
    zassert_equal(result_status(mutex.try_lock()), solar::Status::Ok);
    zassert_equal(result_status(mutex.unlock()), solar::Status::Ok);

    auto unique = kernel::UniqueLock<kernel::Mutex>::acquire(mutex);
    zassert_true(unique.has_value());
    zassert_true(unique->owns_lock());
    zassert_equal(result_status(unique->unlock()), solar::Status::Ok);
    zassert_false(unique->owns_lock());
    zassert_equal(result_status(unique->try_lock()), solar::Status::Ok);
    zassert_true(unique->owns_lock());
    zassert_equal(result_status(unique->unlock()), solar::Status::Ok);

    kernel::RecursiveMutex recursive;
    zassert_equal(result_status(recursive.lock()), solar::Status::Ok);
    zassert_equal(result_status(recursive.lock()), solar::Status::Ok);
    zassert_equal(result_status(recursive.unlock()), solar::Status::Ok);
    zassert_equal(result_status(recursive.unlock()), solar::Status::Ok);

    k_thread_create(&holder_thread, holder_stack, K_THREAD_STACK_SIZEOF(holder_stack), hold_mutex,
                    nullptr, nullptr, nullptr, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
    zassert_equal(result_status(holder_ready.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(result_status(held_mutex.lock(kernel::Timeout::after(10ms))),
                  solar::Status::Timeout);
    holder_release.give();
    zassert_ok(k_thread_join(&holder_thread, K_MSEC(100)));
}

ZTEST(solar_kernel_core, test_semaphore_message_queue_and_events)
{
    kernel::Semaphore semaphore{0, 2};
    zassert_equal(result_status(semaphore.try_take()), solar::Status::WouldBlock);
    semaphore.give();
    zassert_equal(semaphore.count(), 1);
    zassert_equal(result_status(semaphore.take()), solar::Status::Ok);
    zassert_equal(result_status(semaphore.take(kernel::Timeout::after(2ms))),
                  solar::Status::Timeout);

    kernel::MessageQueue<std::uint32_t, 2> queue;
    const auto empty = queue.try_receive();
    zassert_false(empty.has_value());
    zassert_equal(result_status(empty.error()), solar::Status::Empty);
    zassert_equal(result_status(queue.try_send(1)), solar::Status::Ok);
    zassert_equal(result_status(queue.try_send(2)), solar::Status::Ok);
    zassert_true(queue.full());
    zassert_equal(result_status(queue.try_send(3)), solar::Status::Full);
    zassert_equal(*queue.peek(), 1);
    zassert_equal(*queue.peek_at(1), 2);
    zassert_equal(*queue.try_receive(), 1);
    zassert_equal(result_status(queue.try_send_front(9)), solar::Status::Ok);
    zassert_equal(*queue.try_receive(), 9);
    zassert_equal(*queue.try_receive(), 2);
    zassert_equal(result_status(queue.receive(kernel::Timeout::after(2ms)).error()),
                  solar::Status::Timeout);
    queue.purge();
    zassert_true(queue.empty());

    kernel::EventFlags events;
    zassert_equal(result_status(events.try_wait_any_isr(0x1).error()), solar::Status::WouldBlock);
    zassert_equal(events.post(0x1), 0);
    zassert_equal(*events.wait_any(0x1), 0x1);
    zassert_equal(*events.take_any(0x1), 0x1);
    zassert_equal(result_status(events.try_wait_any_isr(0x1).error()), solar::Status::WouldBlock);
    (void)events.post(0x3);
    zassert_equal(*events.take_all(0x3), 0x3);
    zassert_equal(result_status(events.wait_any(0x1, kernel::Timeout::after(2ms)).error()),
                  solar::Status::Timeout);
    zassert_equal(result_status(events.wait_any(0, kernel::Timeout::no_wait()).error()),
                  solar::Status::Invalid);
}

ZTEST(solar_kernel_core, test_poll_signal_semaphore_and_message_queue)
{
    kernel::PollSignal signal;
    kernel::Semaphore semaphore;
    kernel::MessageQueue<std::uint32_t, 1> queue;
    kernel::PollSet<3> poll;

    zassert_equal(result_status(poll.add(signal, 1)), solar::Status::Ok);
    zassert_equal(result_status(poll.add(semaphore, 2)), solar::Status::Ok);
    zassert_equal(result_status(poll.add(queue, 3)), solar::Status::Ok);
    zassert_equal(result_status(poll.add(signal, 4)), solar::Status::Full);
    zassert_equal(result_status(poll.try_wait().error()), solar::Status::WouldBlock);

    semaphore.give();
    const auto semaphore_ready = poll.wait(kernel::Timeout::after(20ms));
    zassert_true(semaphore_ready.has_value());
    zassert_equal(semaphore_ready->ready, 1);
    zassert_equal(poll.event(1)->state, kernel::PollState::SemaphoreAvailable);
    zassert_equal(result_status(semaphore.take()), solar::Status::Ok);

    zassert_equal(result_status(signal.raise(77)), solar::Status::Ok);
    const auto signal_ready = poll.wait(kernel::Timeout::after(20ms));
    zassert_true(signal_ready.has_value());
    zassert_equal(poll.event(0)->state, kernel::PollState::Signaled);
    zassert_equal(*signal.value(), 77);
    signal.reset();

    zassert_equal(result_status(queue.try_send(5)), solar::Status::Ok);
    const auto queue_ready = poll.wait(kernel::Timeout::after(20ms));
    zassert_true(queue_ready.has_value());
    zassert_equal(poll.event(2)->state, kernel::PollState::MessageAvailable);
    zassert_equal(*queue.try_receive(), 5);

    zassert_equal(result_status(poll.wait(kernel::Timeout::after(2ms)).error()),
                  solar::Status::Timeout);
    zassert_equal(result_status(poll.event(9).error()), solar::Status::NotFound);
}

ZTEST(solar_kernel_core, test_timer_callback_context_and_sync)
{
    timer_expiries.store(0, std::memory_order_relaxed);
    timer_callback_was_isr.store(false, std::memory_order_relaxed);
    timer_stop_was_thread.store(false, std::memory_order_relaxed);
    timer_signal.reset();

    kernel::Timer timer{timer_expired, timer_stopped};
    zassert_equal(result_status(timer.start_after(5ms)), solar::Status::Ok);
    zassert_true(timer.running());
    zassert_equal(result_status(timer_signal.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_true(timer_callback_was_isr.load(std::memory_order_relaxed));
    zassert_equal(timer_expiries.load(std::memory_order_relaxed), 1);
    zassert_true(timer.expirations() >= 1);

    zassert_equal(result_status(timer.start_periodic(2ms, 2ms)), solar::Status::Ok);
    zassert_equal(result_status(timer_signal.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(result_status(timer_signal.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    timer.stop();
    zassert_true(timer_stop_was_thread.load(std::memory_order_relaxed));
    zassert_false(timer.running());

    kernel::Timer synchronized;
    zassert_equal(result_status(synchronized.start_after(2ms)), solar::Status::Ok);
    const auto count = synchronized.sync();
    zassert_true(count.has_value());
    zassert_true(*count >= 1);
    zassert_equal(result_status(synchronized.start(kernel::Timeout::forever())),
                  solar::Status::Invalid);
}

ZTEST(solar_kernel_core, test_isr_specific_operations)
{
    kernel::Semaphore semaphore;
    kernel::MessageQueue<std::uint32_t, 2> queue;
    kernel::EventFlags events;
    kernel::Mutex mutex;
    IsrContext context{
        .semaphore = &semaphore, .queue = &queue, .events = &events, .mutex = &mutex};

    irq_offload(exercise_isr, &context);

    zassert_true(context.observed_isr);
    zassert_equal(result_status(context.queue_status), solar::Status::Ok);
    zassert_equal(result_status(context.mutex_status), solar::Status::Invalid);
    zassert_equal(result_status(semaphore.try_take()), solar::Status::Ok);
    zassert_equal(*queue.try_receive(), 42);
    zassert_equal(*events.take_any(0x1), 0x1);
}

ZTEST_SUITE(solar_kernel_core, nullptr, nullptr, nullptr, nullptr, nullptr);
