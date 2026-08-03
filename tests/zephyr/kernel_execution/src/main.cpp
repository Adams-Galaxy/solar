#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <zephyr/ztest.h>

#include <solar/kernel.hpp>

using namespace std::chrono_literals;

namespace kernel = solar::kernel;

#if defined(CONFIG_POLL)
static_assert(kernel::detail::triggered_work_error(-EINVAL).status == solar::Status::Busy);
static_assert(kernel::detail::triggered_work_error(-EINVAL).reason ==
              kernel::WorkErrorReason::Busy);
static_assert(kernel::detail::triggered_work_error(-EINVAL).native_error == -EINVAL);
#endif

static_assert(!std::is_copy_constructible_v<kernel::Thread<1024>>);
static_assert(!std::is_move_constructible_v<kernel::Thread<1024>>);
static_assert(!std::is_copy_constructible_v<kernel::Work>);
static_assert(!std::is_move_constructible_v<kernel::Work>);
static_assert(!std::is_copy_constructible_v<kernel::DelayableWork>);
static_assert(!std::is_move_constructible_v<kernel::DelayableWork>);
static_assert(!std::is_copy_constructible_v<kernel::WorkQueue<1024>>);
static_assert(!std::is_move_constructible_v<kernel::WorkQueue<1024>>);
static_assert(std::is_trivially_copyable_v<kernel::ThreadRef>);
static_assert(std::is_trivially_copyable_v<kernel::WorkQueueTarget>);
static_assert(!std::is_copy_constructible_v<kernel::MemorySlab<16, 2>>);
static_assert(!std::is_move_constructible_v<kernel::MemorySlab<16, 2>>);
static_assert(sizeof(kernel::MemorySlab<16, 2>) >= sizeof(k_mem_slab) + 32);
static_assert(kernel::Thread<1024>::stack_size() >= 1024);
static_assert(kernel::WorkQueue<1024>::stack_size() >= 1024);
static_assert(kernel::stack_diagnostics_available);
static_assert(kernel::runtime_diagnostics_available);
static_assert(kernel::thread_enumeration_available);
static_assert(kernel::runtime_stack_safety_available);
static_assert(kernel::triggered_work_available);

namespace
{

template <solar::ResultType R> solar::Status result_status(const R& result)
{
    return result ? solar::Status::Ok : solar::status_of(result.error());
}

struct ThreadContext
{
    kernel::Semaphore entered;
    kernel::Semaphore release;
    std::atomic_uint32_t calls{};
};

void controlled_thread(void* argument) noexcept
{
    auto& context = *static_cast<ThreadContext*>(argument);
    context.calls.fetch_add(1, std::memory_order_relaxed);
    context.entered.give();
    (void)context.release.take();
}

void record_priority(void* argument) noexcept
{
    static_cast<std::atomic_int*>(argument)->store(kernel::this_thread::priority()->native_handle(),
                                                   std::memory_order_release);
}

struct SelfSuspendContext
{
    kernel::Semaphore entered;
    kernel::Semaphore resumed;
};

void self_suspending_thread(void* argument) noexcept
{
    auto& context = *static_cast<SelfSuspendContext*>(argument);
    context.entered.give();
    (void)kernel::this_thread::suspend();
    context.resumed.give();
}

void self_aborting_thread(void* argument) noexcept
{
    static_cast<kernel::Semaphore*>(argument)->give();
    (void)kernel::this_thread::abort();
}

void sleeping_thread(void* argument) noexcept
{
    static_cast<kernel::Semaphore*>(argument)->give();
    (void)kernel::this_thread::sleep_for(50ms);
}

struct StopContext
{
    explicit StopContext(solar::StopToken value) : token(value) {}

    solar::StopToken token;
    kernel::Semaphore entered;
    std::atomic<solar::Status> result{solar::Status::Error};
};

void stop_waiter(void* argument) noexcept
{
    auto& context = *static_cast<StopContext*>(argument);
    context.entered.give();
    context.result.store(result_status(context.token.wait(100ms)), std::memory_order_release);
}

struct ConditionContext
{
    kernel::Mutex mutex;
    kernel::ConditionVariable condition;
    kernel::Semaphore entered;
    bool ready{};
    solar::Status result{solar::Status::Error};
};

struct QueuePollContext
{
    kernel::Queue<std::uint32_t>* queue{};
    kernel::Semaphore entered;
    std::atomic<solar::Status> status{solar::Status::Error};
    std::atomic_bool interrupted{false};
    std::atomic_bool cancelled{false};
};

void queue_poll_waiter(void* argument) noexcept
{
    auto& context = *static_cast<QueuePollContext*>(argument);
    kernel::PollSet<1> poll;
    if (!poll.add(*context.queue)) {
        return;
    }
    context.entered.give();
    const auto result = poll.wait(kernel::Timeout::after(100ms));
    context.status.store(result ? solar::Status::Ok : solar::status_of(result.error()),
                         std::memory_order_release);
    if (result) {
        context.interrupted.store(result->interrupted, std::memory_order_release);
        const auto event = poll.event(0);
        context.cancelled.store(event &&
                                    kernel::has_state(event->state, kernel::PollState::Cancelled),
                                std::memory_order_release);
    }
}

struct BlockingResult
{
    kernel::Semaphore entered;
    std::atomic<solar::Status> status{solar::Status::Error};
    std::atomic_int native{};
};

struct SemaphoreWaitContext : BlockingResult
{
    kernel::Semaphore* semaphore{};
};

void semaphore_waiter(void* argument) noexcept
{
    auto& context = *static_cast<SemaphoreWaitContext*>(argument);
    context.entered.give();
    const auto result = context.semaphore->take(kernel::Timeout::after(100ms));
    context.status.store(result ? solar::Status::Ok : result.error().status,
                         std::memory_order_release);
    context.native.store(result ? 0 : result.error().native, std::memory_order_release);
}

struct QueueSendContext : BlockingResult
{
    kernel::MessageQueue<std::uint32_t, 1>* queue{};
};

void queue_sender(void* argument) noexcept
{
    auto& context = *static_cast<QueueSendContext*>(argument);
    context.entered.give();
    const auto result = context.queue->send(2, kernel::Timeout::after(100ms));
    context.status.store(result ? solar::Status::Ok : result.error().status,
                         std::memory_order_release);
    context.native.store(result ? 0 : result.error().native, std::memory_order_release);
}

struct PipeReadContext : BlockingResult
{
    kernel::Pipe<8>* pipe{};
};

void pipe_reader(void* argument) noexcept
{
    auto& context = *static_cast<PipeReadContext*>(argument);
    std::array<std::byte, 1> destination{};
    context.entered.give();
    const auto result = context.pipe->read(destination, kernel::Timeout::after(100ms));
    context.status.store(result ? solar::Status::Ok : result.error().status,
                         std::memory_order_release);
    context.native.store(result ? 0 : result.error().native, std::memory_order_release);
}

void condition_waiter(void* argument) noexcept
{
    auto& context = *static_cast<ConditionContext*>(argument);
    auto lock = kernel::UniqueLock<kernel::Mutex>::acquire(context.mutex);
    if (!lock) {
        context.result = solar::status_of(lock.error());
        return;
    }
    context.entered.give();
    context.result =
        result_status(context.condition.wait(*lock, [&context] { return context.ready; }, 100ms));
}

kernel::Semaphore work_entered;
kernel::Semaphore work_release;
std::atomic_uint32_t work_calls{};
std::atomic<solar::Status> self_flush_status{solar::Status::Error};

void blocking_work(kernel::Work& work) noexcept
{
    work_calls.fetch_add(1, std::memory_order_relaxed);
    const auto self_flush = work.flush();
    self_flush_status.store(self_flush ? solar::Status::Ok : self_flush.error().status,
                            std::memory_order_release);
    work_entered.give();
    (void)work_release.take();
}

kernel::Semaphore simple_work_done;
std::atomic_uint32_t simple_work_calls{};

void simple_work(kernel::Work&) noexcept
{
    simple_work_calls.fetch_add(1, std::memory_order_relaxed);
    simple_work_done.give();
}

kernel::Semaphore delayed_done;
std::atomic_uint32_t delayed_calls{};

void delayed_work(kernel::DelayableWork&) noexcept
{
    delayed_calls.fetch_add(1, std::memory_order_relaxed);
    delayed_done.give();
}

kernel::Semaphore triggered_done;

void triggered_work(kernel::TriggeredWork&) noexcept
{
    triggered_done.give();
}

std::atomic_uint32_t enumerated_threads{};
std::atomic_bool found_thread{};
k_tid_t expected_thread{};

void count_thread(k_tid_t thread, void*) noexcept
{
    enumerated_threads.fetch_add(1, std::memory_order_relaxed);
    if (thread == expected_thread) {
        found_thread.store(true, std::memory_order_relaxed);
    }
}

} // namespace

ZTEST(solar_kernel_execution, test_thread_prepare_release_join_and_delayed_launch)
{
    ThreadContext context;
    kernel::Thread<2048> thread;
    const kernel::ThreadConfiguration configuration{.priority = kernel::Priority::preemptive<1>(),
                                                    .name = "solar-prepared"};

    zassert_equal(result_status(thread.prepare(&controlled_thread, &context, configuration)),
                  solar::Status::Ok);
    zassert_equal(thread.lifecycle(), kernel::ThreadLifecycleState::Prepared);
    (void)kernel::this_thread::sleep_for(2ms);
    zassert_equal(context.calls.load(std::memory_order_relaxed), 0);
    zassert_equal(result_status(thread.start()), solar::Status::Ok);
    zassert_equal(result_status(context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(context.calls.load(std::memory_order_relaxed), 1);
    zassert_equal(result_status(thread.start()), solar::Status::Already);
    context.release.give();
    zassert_equal(result_status(thread.join(kernel::Timeout::after(100ms))), solar::Status::Ok);
    zassert_true(*thread.exited());

    ThreadContext reused_context;
    zassert_equal(result_status(thread.launch(&controlled_thread, &reused_context, configuration)),
                  solar::Status::Ok);
    zassert_equal(result_status(reused_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    reused_context.release.give();
    zassert_equal(result_status(thread.join(kernel::Timeout::after(100ms))), solar::Status::Ok);

    ThreadContext delayed_context;
    kernel::Thread<2048> delayed;
    zassert_equal(result_status(delayed.launch(
                      &controlled_thread, &delayed_context,
                      {.priority = kernel::Priority::preemptive<1>(), .name = "solar-delayed"},
                      kernel::Timeout::after(10ms))),
                  solar::Status::Ok);
    (void)kernel::this_thread::sleep_for(2ms);
    zassert_equal(delayed_context.calls.load(std::memory_order_relaxed), 0);
    const auto delayed_ref = delayed.ref();
    zassert_true(delayed_ref.has_value());
    delayed_ref->wakeup();
    zassert_equal(result_status(delayed_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    delayed_context.release.give();
    zassert_equal(result_status(delayed.join(kernel::Timeout::after(100ms))), solar::Status::Ok);
}

#if CONFIG_NUM_COOP_PRIORITIES > 0
ZTEST(solar_kernel_execution, test_cooperative_priority_reaches_native_thread)
{
    std::atomic_int observed{K_IDLE_PRIO};
    kernel::Thread<2048> thread;
    constexpr auto priority = kernel::Priority::cooperative<0>();

    zassert_equal(result_status(thread.launch(&record_priority, &observed, {.priority = priority})),
                  solar::Status::Ok);
    zassert_equal(result_status(thread.join(kernel::Timeout::after(100ms))), solar::Status::Ok);
    zassert_equal(observed.load(std::memory_order_acquire), K_PRIO_COOP(0));
}
#endif

ZTEST(solar_kernel_execution, test_thread_suspend_resume_and_abort)
{
    ThreadContext context;
    kernel::Thread<2048> thread;
    zassert_equal(result_status(thread.launch(&controlled_thread, &context,
                                              {.priority = kernel::Priority::preemptive<1>()})),
                  solar::Status::Ok);
    zassert_equal(result_status(context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(result_status(thread.suspend()), solar::Status::Ok);
    zassert_equal(thread.lifecycle(), kernel::ThreadLifecycleState::Started);
    context.release.give();
    (void)kernel::this_thread::sleep_for(2ms);
    zassert_false(*thread.exited());
    zassert_equal(result_status(thread.resume()), solar::Status::Ok);
    zassert_equal(result_status(thread.join(kernel::Timeout::after(100ms))), solar::Status::Ok);

    ThreadContext aborted_context;
    kernel::Thread<2048> aborted;
    zassert_equal(result_status(aborted.launch(&controlled_thread, &aborted_context,
                                               {.priority = kernel::Priority::preemptive<1>()})),
                  solar::Status::Ok);
    zassert_equal(result_status(aborted_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(result_status(aborted.abort()), solar::Status::Ok);
    zassert_equal(aborted.lifecycle(), kernel::ThreadLifecycleState::Aborted);
    zassert_true(*aborted.exited());
}

ZTEST(solar_kernel_execution, test_self_suspend_and_abort)
{
    const kernel::ThreadConfiguration configuration{.priority = kernel::Priority::preemptive<1>()};

    SelfSuspendContext context;
    kernel::Thread<2048> suspended;
    zassert_equal(result_status(suspended.launch(self_suspending_thread, &context, configuration)),
                  solar::Status::Ok);
    zassert_equal(result_status(context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    (void)kernel::this_thread::sleep_for(2ms);
    const auto suspended_ref = suspended.ref();
    zassert_true(suspended_ref.has_value());
    suspended_ref->resume();
    zassert_equal(result_status(context.resumed.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(result_status(suspended.join(kernel::Timeout::after(100ms))), solar::Status::Ok);

    kernel::Semaphore abort_entered;
    kernel::Thread<2048> aborted;
    zassert_equal(
        result_status(aborted.launch(self_aborting_thread, &abort_entered, configuration)),
        solar::Status::Ok);
    zassert_equal(result_status(abort_entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(result_status(aborted.join(kernel::Timeout::after(100ms))), solar::Status::Ok);
    zassert_equal(aborted.lifecycle(), kernel::ThreadLifecycleState::Finished);
}

ZTEST(solar_kernel_execution, test_thread_wake_timing_and_wakeup)
{
    kernel::Semaphore entered;
    kernel::Thread<2048> sleeper;
    zassert_equal(result_status(sleeper.launch(sleeping_thread, &entered,
                                               {.priority = kernel::Priority::preemptive<1>()})),
                  solar::Status::Ok);
    zassert_equal(result_status(entered.take(kernel::Timeout::after(100ms))), solar::Status::Ok);
    (void)kernel::this_thread::sleep_for(2ms);
    const auto reference = sleeper.ref();
    zassert_true(reference.has_value());
#if defined(CONFIG_SYS_CLOCK_EXISTS)
    zassert_true(reference->wake_remaining().count() > 0);
    zassert_true(reference->wake_deadline() >= kernel::now());
#endif
    reference->wakeup();
    zassert_equal(result_status(sleeper.join(kernel::Timeout::after(100ms))), solar::Status::Ok);
}

ZTEST(solar_kernel_execution, test_stop_token_and_condition_variable)
{
    kernel::StopSource source;
    StopContext stop_context{source.token()};
    kernel::Thread<2048> waiter;
    zassert_equal(result_status(waiter.launch(&stop_waiter, &stop_context,
                                              {.priority = kernel::Priority::preemptive<1>()})),
                  solar::Status::Ok);
    zassert_equal(result_status(stop_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    const auto first = source.request_stop();
    zassert_true(first.has_value());
    zassert_true(*first);
    zassert_equal(result_status(waiter.join(kernel::Timeout::after(100ms))), solar::Status::Ok);
    zassert_equal(stop_context.result.load(std::memory_order_acquire), solar::Status::Ok);
    zassert_true(stop_context.token.stop_requested());
    zassert_false(*source.request_stop());
    const auto stopped_generation = source.token();
    zassert_equal(result_status(source.reset()), solar::Status::Ok);
    const auto fresh_generation = source.token();
    zassert_true(stopped_generation.stop_requested());
    zassert_false(fresh_generation.stop_requested());

    kernel::StopSource reset_source;
    StopContext reset_context{reset_source.token()};
    kernel::Thread<2048> reset_waiter;
    zassert_equal(
        result_status(reset_waiter.launch(&stop_waiter, &reset_context,
                                          {.priority = kernel::Priority::preemptive<1>()})),
        solar::Status::Ok);
    zassert_equal(result_status(reset_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(result_status(reset_source.reset()), solar::Status::Ok);
    zassert_equal(result_status(reset_waiter.join(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(reset_context.result.load(std::memory_order_acquire), solar::Status::Ok);
    zassert_true(reset_context.token.stop_requested());
    zassert_false(reset_source.token().stop_requested());

    ConditionContext condition_context;
    kernel::Thread<2048> condition_thread;
    zassert_equal(
        result_status(condition_thread.launch(&condition_waiter, &condition_context,
                                              {.priority = kernel::Priority::preemptive<1>()})),
        solar::Status::Ok);
    zassert_equal(result_status(condition_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    auto lock = kernel::UniqueLock<kernel::Mutex>::acquire(condition_context.mutex);
    zassert_true(lock.has_value());
    condition_context.ready = true;
    zassert_equal(result_status(condition_context.condition.notify_one()), solar::Status::Ok);
    zassert_equal(result_status(lock->unlock()), solar::Status::Ok);
    zassert_equal(result_status(condition_thread.join(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(condition_context.result, solar::Status::Ok);
}

ZTEST(solar_kernel_execution, test_native_reset_purge_and_close_outcomes)
{
    const kernel::ThreadConfiguration configuration{.priority = kernel::Priority::preemptive<1>()};

    kernel::Semaphore semaphore;
    SemaphoreWaitContext semaphore_context;
    semaphore_context.semaphore = &semaphore;
    kernel::Thread<2048> semaphore_thread;
    zassert_equal(
        result_status(semaphore_thread.launch(semaphore_waiter, &semaphore_context, configuration)),
        solar::Status::Ok);
    zassert_equal(result_status(semaphore_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    (void)kernel::this_thread::sleep_for(2ms);
    semaphore.reset();
    zassert_equal(result_status(semaphore_thread.join(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(semaphore_context.status.load(std::memory_order_acquire), solar::Status::Timeout);
    zassert_equal(semaphore_context.native.load(std::memory_order_acquire), -EAGAIN);

    kernel::MessageQueue<std::uint32_t, 1> queue;
    zassert_true(queue.try_send(1).has_value());
    QueueSendContext queue_context;
    queue_context.queue = &queue;
    kernel::Thread<2048> queue_thread;
    zassert_equal(result_status(queue_thread.launch(queue_sender, &queue_context, configuration)),
                  solar::Status::Ok);
    zassert_equal(result_status(queue_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    (void)kernel::this_thread::sleep_for(2ms);
    queue.purge();
    zassert_equal(result_status(queue_thread.join(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(queue_context.status.load(std::memory_order_acquire), solar::Status::Cancelled);
    zassert_equal(queue_context.native.load(std::memory_order_acquire), -ENOMSG);

    kernel::Pipe<8> reset_pipe;
    PipeReadContext reset_context;
    reset_context.pipe = &reset_pipe;
    kernel::Thread<2048> reset_thread;
    zassert_equal(result_status(reset_thread.launch(pipe_reader, &reset_context, configuration)),
                  solar::Status::Ok);
    zassert_equal(result_status(reset_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    (void)kernel::this_thread::sleep_for(2ms);
    reset_pipe.reset();
    zassert_equal(result_status(reset_thread.join(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(reset_context.status.load(std::memory_order_acquire), solar::Status::Cancelled);
    zassert_equal(reset_context.native.load(std::memory_order_acquire), -ECANCELED);

    kernel::Pipe<8> closed_pipe;
    PipeReadContext close_context;
    close_context.pipe = &closed_pipe;
    kernel::Thread<2048> close_thread;
    zassert_equal(result_status(close_thread.launch(pipe_reader, &close_context, configuration)),
                  solar::Status::Ok);
    zassert_equal(result_status(close_context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    (void)kernel::this_thread::sleep_for(2ms);
    closed_pipe.close();
    zassert_equal(result_status(close_thread.join(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_equal(close_context.status.load(std::memory_order_acquire),
                  solar::Status::UnexpectedExit);
    zassert_equal(close_context.native.load(std::memory_order_acquire), -EPIPE);
}

ZTEST(solar_kernel_execution, test_intrusive_queue_poll_cancellation_preserves_event_state)
{
    kernel::Queue<std::uint32_t> queue;
    QueuePollContext context{.queue = &queue};
    kernel::Thread<2048> waiter;
    zassert_equal(result_status(waiter.launch(&queue_poll_waiter, &context,
                                              {.priority = kernel::Priority::preemptive<1>()})),
                  solar::Status::Ok);
    zassert_equal(result_status(context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    (void)kernel::this_thread::sleep_for(2ms);
    queue.cancel_wait();
    zassert_equal(result_status(waiter.join(kernel::Timeout::after(100ms))), solar::Status::Ok);
    zassert_equal(context.status.load(std::memory_order_acquire), solar::Status::Ok);
    zassert_true(context.interrupted.load(std::memory_order_acquire));
    zassert_true(context.cancelled.load(std::memory_order_acquire));
}

ZTEST(solar_kernel_execution, test_system_work_submission_and_self_deadlock_detection)
{
    work_calls.store(0, std::memory_order_relaxed);
    self_flush_status.store(solar::Status::Error, std::memory_order_relaxed);
    work_entered.reset();
    work_release.reset();
    kernel::Work work{&blocking_work};

    const auto first = work.submit(kernel::system_work_queue);
    zassert_true(first.has_value());
    zassert_equal(*first, kernel::WorkSubmission::Queued);
    zassert_equal(result_status(work_entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    const auto second = work.submit(kernel::system_work_queue);
    zassert_true(second.has_value());
    zassert_equal(*second, kernel::WorkSubmission::RequeuedAfterCurrent);
    zassert_equal(self_flush_status.load(std::memory_order_acquire), solar::Status::Deadlock);
    work_release.give();
    zassert_equal(result_status(work_entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    work_release.give();
    const auto flushed = work.flush();
    zassert_true(flushed.has_value());
    zassert_equal(work_calls.load(std::memory_order_relaxed), 2);
    zassert_false(work.pending());
}

ZTEST(solar_kernel_execution, test_delayable_work_schedule_reschedule_flush_and_cancel)
{
    delayed_calls.store(0, std::memory_order_relaxed);
    delayed_done.reset();
    kernel::DelayableWork work{&delayed_work};

    zassert_equal(*work.schedule(30ms), kernel::WorkSubmission::Queued);
    zassert_equal(*work.schedule(30ms), kernel::WorkSubmission::AlreadyQueued);
    zassert_true(work.remaining() > kernel::TickDuration::zero());
    zassert_equal(*work.reschedule(2ms), kernel::WorkSubmission::Queued);
    zassert_equal(result_status(delayed_done.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_true(work.flush().has_value());
    zassert_equal(delayed_calls.load(std::memory_order_relaxed), 1);

    zassert_equal(*work.schedule(1s), kernel::WorkSubmission::Queued);
    const auto cancelled = work.cancel_sync();
    zassert_true(cancelled.has_value());
    zassert_true(*cancelled);
    zassert_false(work.pending());
}

ZTEST(solar_kernel_execution, test_owned_workqueue_drain_plug_unplug_and_stop)
{
    simple_work_calls.store(0, std::memory_order_relaxed);
    simple_work_done.reset();
    kernel::WorkQueue<2048> queue;
    kernel::Work work{&simple_work};

    zassert_equal(result_status(queue.start(
                      {.priority = kernel::Priority::preemptive<1>(), .name = "solar-work-q"})),
                  solar::Status::Ok);
    zassert_true(queue.started());
    zassert_equal(*work.submit(queue.target()), kernel::WorkSubmission::Queued);
    zassert_equal(result_status(simple_work_done.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_true(work.flush().has_value());

    const auto drained = queue.drain(true);
    zassert_true(drained.has_value());
    const auto plugged = work.submit(queue.target());
    zassert_false(plugged.has_value());
    zassert_equal(plugged.error().status, solar::Status::Busy);
    zassert_equal(result_status(queue.unplug()), solar::Status::Ok);
    zassert_equal(*work.submit(queue.target()), kernel::WorkSubmission::Queued);
    zassert_equal(result_status(simple_work_done.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_true(work.flush().has_value());
    zassert_true(queue.drain(true).has_value());
    zassert_equal(result_status(queue.stop(kernel::Timeout::after(100ms))), solar::Status::Ok);
    zassert_false(queue.started());
    zassert_equal(queue.lifecycle(), kernel::WorkQueueLifecycle::Stopped);
    zassert_equal(result_status(queue.start(
                      {.priority = kernel::Priority::preemptive<1>(), .name = "solar-work-q"})),
                  solar::Status::Ok);
    zassert_true(queue.drain(true).has_value());
    zassert_equal(result_status(queue.stop(kernel::Timeout::after(100ms))), solar::Status::Ok);
}

ZTEST(solar_kernel_execution, test_triggered_work_and_poll_lifetime)
{
    triggered_done.reset();
    kernel::PollSignal signal;
    kernel::PollSet<1> events;
    kernel::TriggeredWork work{&triggered_work};
    zassert_equal(result_status(events.add(signal)), solar::Status::Ok);
    zassert_true(work.arm(events).has_value());
    zassert_true(work.pending());
    zassert_true(kernel::has_state(work.state(), kernel::WorkState::Triggered));
    const auto duplicate = work.arm(events);
    zassert_false(duplicate.has_value());
    zassert_equal(duplicate.error().status, solar::Status::Busy);
    const auto armed_flush = work.flush();
    zassert_false(armed_flush.has_value());
    zassert_equal(armed_flush.error().status, solar::Status::Busy);
    zassert_equal(result_status(signal.raise(7)), solar::Status::Ok);
    zassert_equal(result_status(triggered_done.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_true(work.flush().has_value());

    kernel::PollSignal replacement_signal;
    kernel::PollSet<1> replacement_events;
    zassert_equal(result_status(replacement_events.add(replacement_signal)), solar::Status::Ok);
    signal.reset();
    zassert_true(work.arm(events).has_value());
    zassert_equal(solar::status_of(events.clear().error()), solar::Status::Busy);
    zassert_equal(solar::status_of(events.try_wait().error()), solar::Status::Busy);
    zassert_true(work.replace(replacement_events).has_value());
    zassert_equal(result_status(events.clear()), solar::Status::Ok);
    zassert_equal(solar::status_of(replacement_events.clear().error()), solar::Status::Busy);
    zassert_equal(result_status(replacement_signal.raise()), solar::Status::Ok);
    zassert_equal(result_status(triggered_done.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_true(work.flush().has_value());
    zassert_equal(result_status(replacement_events.clear()), solar::Status::Ok);

    zassert_equal(result_status(events.add(signal)), solar::Status::Ok);
    kernel::TriggeredWork cancelled{&triggered_work};
    zassert_true(cancelled.arm(events).has_value());
    zassert_true(cancelled.pending());
    zassert_equal(result_status(cancelled.cancel_trigger()), solar::Status::Ok);
    zassert_false(cancelled.pending());
    zassert_true(cancelled.cancel_sync().has_value());

    triggered_done.reset();
    kernel::PollSignal timeout_signal;
    kernel::PollSet<1> timeout_events;
    zassert_equal(result_status(timeout_events.add(timeout_signal)), solar::Status::Ok);
    kernel::TriggeredWork timed{&triggered_work};
    zassert_true(timed.arm(timeout_events, kernel::Timeout::after(2ms)).has_value());
    zassert_equal(result_status(triggered_done.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_true(timed.flush().has_value());
    zassert_equal(result_status(timeout_events.clear()), solar::Status::Ok);

    kernel::PollSet<1> empty;
    kernel::TriggeredWork invalid{&triggered_work};
    const auto empty_submission = invalid.arm(empty);
    zassert_false(empty_submission.has_value());
    zassert_equal(empty_submission.error().reason, kernel::WorkErrorReason::InvalidEvents);
}

ZTEST(solar_kernel_execution, test_triggered_work_rejects_cross_queue_replacement)
{
    kernel::WorkQueue<2048> first_queue;
    kernel::WorkQueue<2048> second_queue;
    const kernel::WorkQueueConfiguration configuration{.priority =
                                                           kernel::Priority::preemptive<1>()};
    zassert_equal(result_status(first_queue.start(configuration)), solar::Status::Ok);
    zassert_equal(result_status(second_queue.start(configuration)), solar::Status::Ok);

    kernel::PollSignal signal;
    kernel::PollSet<1> events;
    kernel::TriggeredWork work{&triggered_work};
    zassert_equal(result_status(events.add(signal)), solar::Status::Ok);
    zassert_true(work.arm(events, first_queue.target()).has_value());
    const auto replacement = work.replace(events, second_queue.target());
    zassert_false(replacement.has_value());
    zassert_equal(replacement.error().reason, kernel::WorkErrorReason::DifferentQueue);
    zassert_equal(replacement.error().native_error, -EADDRINUSE);
    zassert_true(work.cancel_sync().has_value());

    zassert_true(first_queue.drain(true).has_value());
    zassert_equal(result_status(first_queue.stop(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
    zassert_true(second_queue.drain(true).has_value());
    zassert_equal(result_status(second_queue.stop(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);
}

ZTEST(solar_kernel_execution, test_memory_slab_pipe_and_spinlock)
{
    kernel::MemorySlab<16, 2> slab;
    auto first = slab.try_allocate();
    auto second = slab.try_allocate();
    zassert_true(first.has_value());
    zassert_true(second.has_value());
    zassert_equal(slab.used(), 2);
    const auto exhausted = slab.try_allocate();
    zassert_false(exhausted.has_value());
    zassert_equal(solar::status_of(exhausted.error()), solar::Status::NoMemory);
    first->bytes()[0] = std::byte{0x2A};
    first->reset();
    zassert_equal(slab.available(), 1);
    zassert_true(slab.try_allocate().has_value());
    const auto statistics = slab.statistics();
    zassert_true(statistics.has_value());
    zassert_equal(statistics->allocated_bytes, 16);
    zassert_equal(statistics->free_bytes, 16);
#if defined(CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION)
    zassert_equal(statistics->maximum_allocated_bytes, 32);
    zassert_equal(result_status(slab.reset_maximum_usage()), solar::Status::Ok);
    zassert_equal(slab.statistics()->maximum_allocated_bytes, 16);
#else
    zassert_equal(statistics->maximum_allocated_bytes, 0);
    zassert_equal(result_status(slab.reset_maximum_usage()), solar::Status::NotSupported);
#endif

    kernel::Pipe<8> pipe;
    const std::array input{std::byte{1}, std::byte{2}, std::byte{3}};
    std::array<std::byte, 3> output{};
    zassert_equal(*pipe.try_write(input), input.size());
    zassert_equal(*pipe.try_read(output), output.size());
    zassert_mem_equal(input.data(), output.data(), input.size());
    zassert_equal(solar::status_of(pipe.try_read(output).error()), solar::Status::WouldBlock);

    kernel::SpinLock spin;
    std::uint32_t protected_value{};
    {
        auto guard = spin.acquire();
        protected_value = 42;
    }
    zassert_equal(protected_value, 42);
    zassert_true(spin.try_acquire().has_value());
}

ZTEST(solar_kernel_execution, test_thread_diagnostics_and_enumeration)
{
    ThreadContext context;
    kernel::Thread<2048> thread;
    zassert_equal(result_status(thread.launch(
                      &controlled_thread, &context,
                      {.priority = kernel::Priority::preemptive<1>(), .name = "solar-diag"})),
                  solar::Status::Ok);
    zassert_equal(result_status(context.entered.take(kernel::Timeout::after(100ms))),
                  solar::Status::Ok);

    const auto diagnostics = kernel::thread_diagnostics(thread);
    zassert_true(diagnostics.has_value());
    zassert_true(diagnostics->name.has_value());
    zassert_equal(*diagnostics->name, "solar-diag");
    zassert_true(diagnostics->stack_size.has_value());
    zassert_true(diagnostics->stack_unused.has_value());
    zassert_true(diagnostics->runtime.has_value());

    const auto thread_ref = thread.ref();
    zassert_true(thread_ref.has_value());
    zassert_equal(result_status(kernel::set_stack_warning_margin(thread_ref->id(), 16)),
                  solar::Status::Ok);
    zassert_equal(kernel::thread_diagnostics(thread)->stack_warning_margin.value_or(0), 16);
    const auto safety = kernel::check_stack_safety(thread_ref->id(), true);
    zassert_true(safety.has_value());
    zassert_true(safety->unused > 0);

    expected_thread = thread_ref->id();
    enumerated_threads.store(0, std::memory_order_relaxed);
    found_thread.store(false, std::memory_order_relaxed);
    zassert_equal(result_status(kernel::for_each_thread_locked(&count_thread)), solar::Status::Ok);
    zassert_true(enumerated_threads.load(std::memory_order_relaxed) > 0);
    zassert_true(found_thread.load(std::memory_order_relaxed));

    context.release.give();
    zassert_equal(result_status(thread.join(kernel::Timeout::after(100ms))), solar::Status::Ok);
}

ZTEST(solar_kernel_execution, test_fatal_vocabulary_without_bridge)
{
    static_assert(!kernel::fatal_bridge_available);
    zassert_equal(kernel::normalize_fatal_reason(K_ERR_CPU_EXCEPTION),
                  kernel::FatalReason::CpuException);
    zassert_equal(kernel::normalize_fatal_reason(K_ERR_STACK_CHK_FAIL),
                  kernel::FatalReason::StackCheckFailure);
    zassert_equal(result_status(kernel::install_fatal_observer(nullptr)),
                  solar::Status::NotSupported);
    zassert_equal(solar::status_of(kernel::fatal_reason().error()), solar::Status::NotSupported);
}

ZTEST_SUITE(solar_kernel_execution, nullptr, nullptr, nullptr, nullptr, nullptr);
