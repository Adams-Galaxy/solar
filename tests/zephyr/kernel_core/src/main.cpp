#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <type_traits>

#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>

#include <solar/kernel.hpp>

using namespace std::chrono_literals;

namespace kernel = solar::kernel;

template <typename T>
concept HasNativeHandle = requires(T& value) { value.native_handle(); };

static_assert(
    std::is_same_v<decltype(kernel::MessageQueue<std::uint32_t, 4>::capacity), const std::size_t>);
static_assert(kernel::MessageQueue<std::uint32_t, 4>::capacity == 4);
static_assert(!std::is_default_constructible_v<kernel::Priority>);
static_assert(kernel::Priority::preemptive<0>().native_handle() == K_PRIO_PREEMPT(0));
static_assert(kernel::Priority::native<K_HIGHEST_APPLICATION_THREAD_PRIO>().native_handle() ==
              K_HIGHEST_APPLICATION_THREAD_PRIO);
static_assert(kernel::Priority::native<K_LOWEST_APPLICATION_THREAD_PRIO>().native_handle() ==
              K_LOWEST_APPLICATION_THREAD_PRIO);
static_assert(kernel::Priority::preemptive<0>().category() == kernel::PriorityClass::Preemptive);
static_assert(kernel::Priority::preemptive<0>().level() == 0);
static_assert(!std::is_copy_constructible_v<kernel::Mutex>);
static_assert(!std::is_move_constructible_v<kernel::Mutex>);
static_assert(!std::is_copy_constructible_v<kernel::RecursiveMutex>);
static_assert(!std::is_move_constructible_v<kernel::RecursiveMutex>);
static_assert(!std::is_copy_constructible_v<kernel::Semaphore>);
static_assert(!std::is_move_constructible_v<kernel::Semaphore>);
static_assert(std::is_default_constructible_v<kernel::CountingSemaphore<4, 2>>);
static_assert(!std::is_copy_constructible_v<kernel::MessageQueue<std::uint32_t, 2>>);
static_assert(!std::is_move_constructible_v<kernel::MessageQueue<std::uint32_t, 2>>);
static_assert(!std::is_copy_constructible_v<kernel::EventFlags>);
static_assert(!std::is_move_constructible_v<kernel::EventFlags>);
static_assert(std::is_trivially_copyable_v<kernel::SemaphoreRef>);
static_assert(std::is_trivially_copyable_v<kernel::RecursiveMutexRef>);
static_assert(std::is_trivially_copyable_v<kernel::ConditionVariableRef>);
static_assert(std::is_trivially_copyable_v<kernel::MessageQueueRef<std::uint32_t>>);
static_assert(std::is_trivially_copyable_v<kernel::TimerRef>);
static_assert(std::is_trivially_copyable_v<kernel::PipeRef>);
static_assert(std::is_trivially_copyable_v<kernel::MemorySlabRef<16>>);
static_assert(std::is_trivially_copyable_v<kernel::SpinLockRef>);
static_assert(!HasNativeHandle<kernel::Mutex>);
static_assert(!HasNativeHandle<kernel::RecursiveMutex>);
static_assert(!HasNativeHandle<kernel::ConditionVariable>);
static_assert(!HasNativeHandle<kernel::Semaphore>);
static_assert(!HasNativeHandle<kernel::MessageQueue<std::uint32_t, 2>>);
static_assert(!HasNativeHandle<kernel::Timer>);
static_assert(!HasNativeHandle<kernel::Pipe<16>>);
static_assert(!HasNativeHandle<kernel::MemorySlab<16, 2>>);
static_assert(!HasNativeHandle<kernel::SpinLock>);
#if defined(CONFIG_EVENTS)
static_assert(std::is_trivially_copyable_v<kernel::EventFlagsRef>);
static_assert(!HasNativeHandle<kernel::EventFlags>);
#endif
#if defined(CONFIG_POLL)
static_assert(std::is_trivially_copyable_v<kernel::PollSignalRef>);
static_assert(!HasNativeHandle<kernel::PollSignal>);
#endif
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
static_assert(kernel::detail::poll_signal_outcome(-EAGAIN)->delivery ==
              kernel::PollSignalDelivery::LatchedAfterTimeout);
static_assert(kernel::detail::poll_signal_outcome(-EAGAIN)->native == -EAGAIN);
static_assert(sizeof(kernel::Timer) >= sizeof(k_timer) + 2 * sizeof(kernel::Timer::Callback));
#if CONFIG_NUM_COOP_PRIORITIES > 0
static_assert(kernel::Priority::cooperative<0>().native_handle() == K_PRIO_COOP(0));
static_assert(kernel::Priority::cooperative<0>().category() == kernel::PriorityClass::Cooperative);
static_assert(kernel::Priority::cooperative<0>().level() == 0);
static_assert(kernel::Priority::cooperative<0>().higher_than(kernel::Priority::preemptive<0>()));
#endif
#if CONFIG_NUM_METAIRQ_PRIORITIES > 0
static_assert(kernel::Priority::meta_irq<0>().is_meta_irq());
#endif

namespace
{

template <typename T> solar::Status result_status(const solar::Result<T>& result)
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
    timer_signal.give();
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
    kernel::MemorySlab<16, 1>* slab;
    kernel::Pipe<16>* pipe;
    kernel::PollSet<1>* poll;
    kernel::Timer* timer;
    kernel::Thread<512>* thread;
    kernel::ConditionVariable* condition;
    bool observed_isr{};
    solar::Status semaphore_thread_status{solar::Status::Error};
    solar::Status semaphore_isr_status{solar::Status::Error};
    solar::Status queue_thread_status{solar::Status::Error};
    solar::Status queue_isr_status{solar::Status::Error};
    solar::Status event_thread_status{solar::Status::Error};
    solar::Status event_isr_status{solar::Status::Error};
    solar::Status slab_thread_status{solar::Status::Error};
    solar::Status slab_isr_status{solar::Status::Error};
    solar::Status pipe_status{solar::Status::Error};
    solar::Status poll_status{solar::Status::Error};
    solar::Status timer_start_status{solar::Status::Error};
    solar::Status sleep_status{solar::Status::Error};
    solar::Status yield_status{solar::Status::Error};
    solar::Status priority_status{solar::Status::Error};
    solar::Status thread_create_status{solar::Status::Error};
    solar::Status condition_notify_status{solar::Status::Error};
    solar::Status mutex_status{solar::Status::Error};
};

void unused_thread_entry(void*) noexcept {}

void exercise_isr(const void* argument)
{
    auto& context = *static_cast<IsrContext*>(const_cast<void*>(argument));
    context.observed_isr = kernel::in_isr();
    context.semaphore->give();
    context.semaphore_thread_status = result_status(context.semaphore->try_take());
    context.semaphore_isr_status = result_status(context.semaphore->try_take_isr());
    context.queue_thread_status = result_status(context.queue->try_send(41));
    context.queue_isr_status = result_status(context.queue->try_send_isr(42));
    (void)context.events->post(0x1);
    context.event_thread_status =
        result_status(context.events->wait_any(0x1, kernel::Timeout::no_wait()));
    context.event_isr_status = result_status(context.events->try_take_any_isr(0x1));
    context.slab_thread_status = result_status(context.slab->try_allocate().error());
    const auto block = context.slab->try_allocate_isr();
    context.slab_isr_status = block ? solar::Status::Ok : result_status(block.error());
    const std::array<std::byte, 1> byte{};
    context.pipe_status = result_status(context.pipe->try_write(byte).error());
    context.poll_status = result_status(context.poll->try_wait().error());
    context.timer_start_status = result_status(context.timer->start(kernel::Timeout::no_wait()));
    context.sleep_status =
        result_status(kernel::this_thread::sleep_for(kernel::Timeout::no_wait()));
    context.yield_status = result_status(kernel::this_thread::yield());
    context.priority_status = result_status(kernel::this_thread::priority());
    context.thread_create_status = result_status(context.thread->prepare(
        unused_thread_entry,
        {.priority = kernel::Priority::preemptive<0>(), .name = nullptr, .options = 0}));
    context.condition_notify_status = result_status(context.condition->notify_one());
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
    zassert_equal(preemptive->level(), 0);
    zassert_false(kernel::Priority::try_preemptive(CONFIG_NUM_PREEMPT_PRIORITIES).has_value());

#if CONFIG_NUM_COOP_PRIORITIES > 0
    const auto cooperative = kernel::Priority::try_cooperative(0);
    zassert_true(cooperative.has_value());
    zassert_true(cooperative->is_cooperative());
    zassert_equal(cooperative->native_handle(), K_PRIO_COOP(0));
#endif
    zassert_true(kernel::Priority::from_native(K_HIGHEST_APPLICATION_THREAD_PRIO).has_value());
    zassert_true(kernel::Priority::from_native(K_LOWEST_APPLICATION_THREAD_PRIO).has_value());
    zassert_false(kernel::Priority::from_native(K_HIGHEST_APPLICATION_THREAD_PRIO - 1).has_value());
    zassert_false(kernel::Priority::from_native(K_LOWEST_APPLICATION_THREAD_PRIO + 1).has_value());

    const auto original = kernel::this_thread::priority();
    zassert_true(original.has_value());
    zassert_equal(result_status(kernel::this_thread::set_priority(*preemptive)), solar::Status::Ok);
    zassert_equal(kernel::this_thread::priority()->native_handle(), preemptive->native_handle());
    zassert_equal(result_status(kernel::this_thread::set_priority(*original)), solar::Status::Ok);

    zassert_not_null(kernel::this_thread::id());
    zassert_equal(kernel::this_thread::ref()->id(), kernel::this_thread::id());
    zassert_equal(result_status(kernel::this_thread::yield()), solar::Status::Ok);
    zassert_equal(result_status(kernel::reschedule()), solar::Status::Ok);
    zassert_equal(kernel::current_is_preemptible(), k_is_preempt_thread() != 0);
    zassert_equal(result_status(kernel::this_thread::busy_wait_for(10us)), solar::Status::Ok);

    const auto current_ref = kernel::this_thread::ref();
    zassert_true(current_ref.has_value());
    zassert_equal(current_ref->priority()->native_handle(), original->native_handle());
#if defined(CONFIG_SYS_CLOCK_EXISTS)
    zassert_true(current_ref->wake_remaining().count() >= 0);
#endif

    auto scheduler_lock = kernel::SchedulerLock::acquire();
    zassert_true(scheduler_lock.has_value());
}

ZTEST(solar_kernel_core, test_borrowed_native_objects)
{
    k_sem native_semaphore;
    zassert_equal(k_sem_init(&native_semaphore, 0, 2), 0);
    kernel::SemaphoreRef semaphore{native_semaphore};
    semaphore.give();
    zassert_equal(semaphore.count(), 1);
    zassert_equal(result_status(semaphore.try_take()), solar::Status::Ok);

    k_mutex native_mutex;
    zassert_equal(k_mutex_init(&native_mutex), 0);
    kernel::RecursiveMutexRef mutex{native_mutex};
    zassert_equal(result_status(mutex.lock()), solar::Status::Ok);
    zassert_equal(result_status(mutex.lock()), solar::Status::Ok);
    zassert_equal(result_status(mutex.unlock()), solar::Status::Ok);

    k_condvar native_condition;
    zassert_equal(k_condvar_init(&native_condition), 0);
    kernel::ConditionVariableRef condition{native_condition};
    kernel::Mutex condition_mutex;
    auto condition_lock = kernel::UniqueLock<kernel::Mutex>::acquire(condition_mutex);
    zassert_true(condition_lock.has_value());
    zassert_equal(result_status(condition.wait(*condition_lock, kernel::Timeout::no_wait())),
                  solar::Status::WouldBlock);
    zassert_false(condition_lock->owns_lock());
    zassert_equal(result_status(condition_lock->lock()), solar::Status::Ok);
    zassert_equal(result_status(condition_lock->unlock()), solar::Status::Ok);
    zassert_equal(result_status(mutex.unlock()), solar::Status::Ok);

    alignas(std::uint32_t) std::array<std::byte, sizeof(std::uint32_t) * 2> storage{};
    k_msgq native_queue;
    k_msgq_init(&native_queue, reinterpret_cast<char*>(storage.data()), sizeof(std::uint32_t), 2);
    const auto borrowed_queue = kernel::MessageQueueRef<std::uint32_t>::borrow(native_queue);
    zassert_true(borrowed_queue.has_value());
    const auto queue = *borrowed_queue;
    zassert_equal(result_status(queue.try_send(42)), solar::Status::Ok);
    zassert_equal(queue.size(), 1);
    zassert_equal(*queue.try_receive(), 42);
    const auto wrong_queue = kernel::MessageQueueRef<std::uint16_t>::borrow(native_queue);
    zassert_false(wrong_queue.has_value());
    zassert_equal(result_status(wrong_queue.error()), solar::Status::Invalid);

    k_timer native_timer;
    k_timer_init(&native_timer, nullptr, nullptr);
    kernel::TimerRef timer{native_timer};
    zassert_equal(result_status(timer.start(kernel::Timeout::after(10ms))), solar::Status::Ok);
    zassert_true(timer.running());
    timer.stop();
    zassert_false(timer.running());

    std::array<std::uint8_t, 8> pipe_storage{};
    k_pipe native_pipe;
    k_pipe_init(&native_pipe, pipe_storage.data(), pipe_storage.size());
    kernel::PipeRef pipe{native_pipe};
    const std::array<std::byte, 3> outgoing{std::byte{1}, std::byte{2}, std::byte{3}};
    std::array<std::byte, 3> incoming{};
    zassert_equal(*pipe.try_write(outgoing), outgoing.size());
    zassert_equal(*pipe.try_read(incoming), incoming.size());
    zassert_mem_equal(incoming.data(), outgoing.data(), outgoing.size());

    alignas(void*) std::array<std::byte, 32> slab_storage{};
    k_mem_slab native_slab;
    zassert_equal(k_mem_slab_init(&native_slab, slab_storage.data(), 16, 2), 0);
    kernel::MemorySlabRef<16> slab{native_slab};
    {
        auto block = slab.try_allocate();
        zassert_true(block.has_value());
        zassert_equal(slab.used(), 1);
        block->bytes()[0] = std::byte{0x5a};
    }
    zassert_equal(slab.used(), 0);

    k_spinlock native_spinlock{};
    kernel::SpinLockRef spinlock{native_spinlock};
    {
        auto guard = spinlock.acquire();
        (void)guard;
    }

#if defined(CONFIG_EVENTS)
    k_event native_events;
    k_event_init(&native_events);
    kernel::EventFlagsRef events{native_events};
    (void)events.post(0x4);
    zassert_equal(*events.take_any(0x4, kernel::Timeout::no_wait()), 0x4);
#endif

#if defined(CONFIG_POLL)
    k_poll_signal native_signal;
    k_poll_signal_init(&native_signal);
    kernel::PollSignalRef signal{native_signal};
    kernel::PollSet<1> poll;
    zassert_equal(result_status(poll.add(signal)), solar::Status::Ok);
    const auto raised = signal.raise(7);
    zassert_equal(result_status(raised), solar::Status::Ok);
    zassert_true(raised->waiter_notified());
    const auto waited = poll.try_wait();
    zassert_true(waited.has_value());
    zassert_equal(waited->ready, 1);
    zassert_equal(*signal.value(), 7);
#endif
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
    kernel::CountingSemaphore<2> semaphore;
    const auto unavailable = semaphore.try_take();
    zassert_equal(result_status(unavailable), solar::Status::WouldBlock);
    zassert_equal(unavailable.error().native, -EBUSY);
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
    const auto full = queue.try_send(3);
    zassert_equal(result_status(full), solar::Status::Full);
    zassert_equal(full.error().native, -ENOMSG);
    zassert_equal(*queue.peek(), 1);
    zassert_equal(*queue.peek_at(1), 2);
    zassert_equal(*queue.try_receive(), 1);
    zassert_equal(result_status(queue.try_send_front(9)), solar::Status::Ok);
    zassert_equal(*queue.try_receive(), 9);

    zassert_equal(*queue.try_receive(), 2);
    const auto timed_out = queue.receive(kernel::Timeout::after(2ms));
    zassert_equal(result_status(timed_out.error()), solar::Status::Timeout);
    zassert_equal(timed_out.error().native, -EAGAIN);
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

    const auto raised = signal.raise(77);
    zassert_equal(result_status(raised), solar::Status::Ok);
    zassert_equal(raised->delivery, kernel::PollSignalDelivery::Delivered);
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
    kernel::MemorySlab<16, 1> slab;
    kernel::Pipe<16> pipe;
    kernel::PollSet<1> poll;
    kernel::Timer timer;
    kernel::Thread<512> thread;
    kernel::ConditionVariable condition;
    zassert_equal(result_status(poll.add(semaphore)), solar::Status::Ok);
    IsrContext context{
        .semaphore = &semaphore,
        .queue = &queue,
        .events = &events,
        .mutex = &mutex,
        .slab = &slab,
        .pipe = &pipe,
        .poll = &poll,
        .timer = &timer,
        .thread = &thread,
        .condition = &condition,
    };

    irq_offload(exercise_isr, &context);

    zassert_true(context.observed_isr);
    zassert_equal(result_status(context.semaphore_thread_status), solar::Status::Invalid);
    zassert_equal(result_status(context.semaphore_isr_status), solar::Status::Ok);
    zassert_equal(result_status(context.queue_thread_status), solar::Status::Invalid);
    zassert_equal(result_status(context.queue_isr_status), solar::Status::Ok);
    zassert_equal(result_status(context.event_thread_status), solar::Status::Invalid);
    zassert_equal(result_status(context.event_isr_status), solar::Status::Ok);
    zassert_equal(result_status(context.slab_thread_status), solar::Status::Invalid);
    zassert_equal(result_status(context.slab_isr_status), solar::Status::Ok);
    zassert_equal(result_status(context.pipe_status), solar::Status::Invalid);
    zassert_equal(result_status(context.poll_status), solar::Status::Invalid);
    zassert_equal(result_status(context.timer_start_status), solar::Status::Invalid);
    zassert_equal(result_status(context.sleep_status), solar::Status::Invalid);
    zassert_equal(result_status(context.yield_status), solar::Status::Invalid);
    zassert_equal(result_status(context.priority_status), solar::Status::Invalid);
    zassert_equal(result_status(context.thread_create_status), solar::Status::Invalid);
    zassert_equal(result_status(context.condition_notify_status), solar::Status::Invalid);
    zassert_equal(result_status(context.mutex_status), solar::Status::Invalid);
    zassert_equal(*queue.try_receive(), 42);
    zassert_equal(result_status(events.try_wait_any_isr(0x1).error()), solar::Status::WouldBlock);
}

ZTEST_SUITE(solar_kernel_core, nullptr, nullptr, nullptr, nullptr, nullptr);
