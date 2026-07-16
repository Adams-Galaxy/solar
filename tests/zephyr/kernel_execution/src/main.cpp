#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <zephyr/ztest.h>

#include <solar/kernel.hpp>

using namespace std::chrono_literals;

namespace kernel = solar::kernel;

static_assert(!std::is_copy_constructible_v<kernel::Thread<1024>>);
static_assert(!std::is_move_constructible_v<kernel::Thread<1024>>);
static_assert(!std::is_copy_constructible_v<kernel::Work>);
static_assert(!std::is_move_constructible_v<kernel::Work>);
static_assert(!std::is_copy_constructible_v<kernel::DelayableWork>);
static_assert(!std::is_move_constructible_v<kernel::DelayableWork>);
static_assert(!std::is_copy_constructible_v<kernel::WorkQueue<1024>>);
static_assert(!std::is_move_constructible_v<kernel::WorkQueue<1024>>);
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
    context.result.store(context.token.wait(100ms), std::memory_order_release);
}

struct ConditionContext
{
    kernel::Mutex mutex;
    kernel::ConditionVariable condition;
    kernel::Semaphore entered;
    bool ready{};
    solar::Status result{solar::Status::Error};
};

void condition_waiter(void* argument) noexcept
{
    auto& context = *static_cast<ConditionContext*>(argument);
    auto lock = kernel::UniqueLock<kernel::Mutex>::acquire(context.mutex);
    if (!lock) {
        context.result = lock.error();
        return;
    }
    context.entered.give();
    context.result = context.condition.wait(*lock, [&context] { return context.ready; }, 100ms);
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
    const kernel::ThreadConfiguration configuration{
        .priority = kernel::Priority::preemptive<1>(), .name = "solar-prepared"};

    zassert_equal(thread.prepare(&controlled_thread, &context, configuration), solar::Status::Ok);
    zassert_equal(thread.state(), kernel::ThreadExecutionState::Prepared);
    (void)kernel::this_thread::sleep_for(2ms);
    zassert_equal(context.calls.load(std::memory_order_relaxed), 0);
    zassert_equal(thread.start(), solar::Status::Ok);
    zassert_equal(context.entered.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_equal(context.calls.load(std::memory_order_relaxed), 1);
    zassert_equal(thread.start(), solar::Status::Already);
    context.release.give();
    zassert_equal(thread.join(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_true(*thread.exited());

    ThreadContext delayed_context;
    kernel::Thread<2048> delayed;
    zassert_equal(delayed.launch(&controlled_thread, &delayed_context,
                                 {.priority = kernel::Priority::preemptive<1>(),
                                  .name = "solar-delayed"},
                                 kernel::Timeout::after(10ms)),
                  solar::Status::Ok);
    (void)kernel::this_thread::sleep_for(2ms);
    zassert_equal(delayed_context.calls.load(std::memory_order_relaxed), 0);
    zassert_equal(delayed_context.entered.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    delayed_context.release.give();
    zassert_equal(delayed.join(kernel::Timeout::after(100ms)), solar::Status::Ok);
}

ZTEST(solar_kernel_execution, test_thread_suspend_resume_and_abort)
{
    ThreadContext context;
    kernel::Thread<2048> thread;
    zassert_equal(thread.launch(&controlled_thread, &context,
                                {.priority = kernel::Priority::preemptive<1>()}),
                  solar::Status::Ok);
    zassert_equal(context.entered.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_equal(thread.suspend(), solar::Status::Ok);
    zassert_equal(thread.state(), kernel::ThreadExecutionState::Suspended);
    context.release.give();
    (void)kernel::this_thread::sleep_for(2ms);
    zassert_false(*thread.exited());
    zassert_equal(thread.resume(), solar::Status::Ok);
    zassert_equal(thread.join(kernel::Timeout::after(100ms)), solar::Status::Ok);

    ThreadContext aborted_context;
    kernel::Thread<2048> aborted;
    zassert_equal(aborted.launch(&controlled_thread, &aborted_context,
                                 {.priority = kernel::Priority::preemptive<1>()}),
                  solar::Status::Ok);
    zassert_equal(aborted_context.entered.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_equal(aborted.abort(), solar::Status::Ok);
    zassert_equal(aborted.state(), kernel::ThreadExecutionState::Aborted);
    zassert_true(*aborted.exited());
}

ZTEST(solar_kernel_execution, test_stop_token_and_condition_variable)
{
    kernel::StopSource source;
    StopContext stop_context{source.token()};
    kernel::Thread<2048> waiter;
    zassert_equal(waiter.launch(&stop_waiter, &stop_context,
                                {.priority = kernel::Priority::preemptive<1>()}),
                  solar::Status::Ok);
    zassert_equal(stop_context.entered.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    const auto first = source.request_stop();
    zassert_true(first.has_value());
    zassert_true(*first);
    zassert_equal(waiter.join(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_equal(stop_context.result.load(std::memory_order_acquire), solar::Status::Ok);
    zassert_true(stop_context.token.stop_requested());
    zassert_false(*source.request_stop());

    ConditionContext condition_context;
    kernel::Thread<2048> condition_thread;
    zassert_equal(condition_thread.launch(&condition_waiter, &condition_context,
                                          {.priority = kernel::Priority::preemptive<1>()}),
                  solar::Status::Ok);
    zassert_equal(condition_context.entered.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    auto lock = kernel::UniqueLock<kernel::Mutex>::acquire(condition_context.mutex);
    zassert_true(lock.has_value());
    condition_context.ready = true;
    zassert_equal(condition_context.condition.notify_one(), solar::Status::Ok);
    zassert_equal(lock->unlock(), solar::Status::Ok);
    zassert_equal(condition_thread.join(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_equal(condition_context.result, solar::Status::Ok);
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
    zassert_equal(work_entered.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    const auto second = work.submit(kernel::system_work_queue);
    zassert_true(second.has_value());
    zassert_equal(*second, kernel::WorkSubmission::RequeuedAfterCurrent);
    zassert_equal(self_flush_status.load(std::memory_order_acquire), solar::Status::Deadlock);
    work_release.give();
    zassert_equal(work_entered.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
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
    zassert_equal(delayed_done.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
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

    zassert_equal(queue.start({.priority = kernel::Priority::preemptive<1>(),
                               .name = "solar-work-q"}),
                  solar::Status::Ok);
    zassert_true(queue.started());
    zassert_equal(*work.submit(queue), kernel::WorkSubmission::Queued);
    zassert_equal(simple_work_done.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_true(work.flush().has_value());

    const auto drained = queue.drain(true);
    zassert_true(drained.has_value());
    const auto plugged = work.submit(queue);
    zassert_false(plugged.has_value());
    zassert_equal(plugged.error().status, solar::Status::Busy);
    zassert_equal(queue.unplug(), solar::Status::Ok);
    zassert_equal(*work.submit(queue), kernel::WorkSubmission::Queued);
    zassert_equal(simple_work_done.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_true(work.flush().has_value());
    zassert_true(queue.drain(true).has_value());
    zassert_equal(queue.stop(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_false(queue.started());
}

ZTEST(solar_kernel_execution, test_triggered_work_and_poll_lifetime)
{
    triggered_done.reset();
    kernel::PollSignal signal;
    kernel::PollSet<1> events;
    kernel::TriggeredWork work{&triggered_work};
    zassert_equal(events.add(signal), solar::Status::Ok);
    zassert_true(work.submit(events).has_value());
    zassert_true(work.pending());
    zassert_true(kernel::has_state(work.state(), kernel::WorkState::Triggered));
    const auto duplicate = work.submit(events);
    zassert_false(duplicate.has_value());
    zassert_equal(duplicate.error().status, solar::Status::Busy);
    const auto armed_flush = work.flush();
    zassert_false(armed_flush.has_value());
    zassert_equal(armed_flush.error().status, solar::Status::Busy);
    zassert_equal(signal.raise(7), solar::Status::Ok);
    zassert_equal(triggered_done.take(kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_true(work.flush().has_value());

    signal.reset();
    kernel::TriggeredWork cancelled{&triggered_work};
    zassert_true(cancelled.submit(events).has_value());
    zassert_true(cancelled.pending());
    zassert_equal(cancelled.cancel_trigger(), solar::Status::Ok);
    zassert_false(cancelled.pending());
    zassert_true(cancelled.cancel_sync().has_value());

    kernel::PollSet<1> empty;
    kernel::TriggeredWork invalid{&triggered_work};
    const auto empty_submission = invalid.submit(empty);
    zassert_false(empty_submission.has_value());
    zassert_equal(empty_submission.error().reason, kernel::WorkErrorReason::InvalidEvents);
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
    zassert_equal(exhausted.error(), solar::Status::NoMemory);
    first->bytes()[0] = std::byte{0x2A};
    first->reset();
    zassert_equal(slab.available(), 1);
    zassert_true(slab.try_allocate().has_value());

    kernel::Pipe<8> pipe;
    const std::array input{std::byte{1}, std::byte{2}, std::byte{3}};
    std::array<std::byte, 3> output{};
    zassert_equal(*pipe.try_write(input), input.size());
    zassert_equal(*pipe.try_read(output), output.size());
    zassert_mem_equal(input.data(), output.data(), input.size());
    zassert_equal(pipe.try_read(output).error(), solar::Status::WouldBlock);

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
    zassert_equal(thread.launch(&controlled_thread, &context,
                                {.priority = kernel::Priority::preemptive<1>(),
                                 .name = "solar-diag"}),
                  solar::Status::Ok);
    zassert_equal(context.entered.take(kernel::Timeout::after(100ms)), solar::Status::Ok);

    const auto diagnostics = kernel::thread_diagnostics(thread);
    zassert_true(diagnostics.has_value());
    zassert_true(diagnostics->name.has_value());
    zassert_equal(*diagnostics->name, "solar-diag");
    zassert_true(diagnostics->stack_size.has_value());
    zassert_true(diagnostics->stack_unused.has_value());
    zassert_true(diagnostics->runtime.has_value());

    zassert_equal(kernel::set_stack_warning_margin(thread.native_handle(), 16), solar::Status::Ok);
    const auto safety = kernel::check_stack_safety(thread.native_handle(), true);
    zassert_true(safety.has_value());
    zassert_true(safety->unused > 0);

    expected_thread = thread.native_handle();
    enumerated_threads.store(0, std::memory_order_relaxed);
    found_thread.store(false, std::memory_order_relaxed);
    zassert_equal(kernel::for_each_thread_locked(&count_thread), solar::Status::Ok);
    zassert_true(enumerated_threads.load(std::memory_order_relaxed) > 0);
    zassert_true(found_thread.load(std::memory_order_relaxed));

    context.release.give();
    zassert_equal(thread.join(kernel::Timeout::after(100ms)), solar::Status::Ok);
}

ZTEST(solar_kernel_execution, test_fatal_vocabulary_without_bridge)
{
    static_assert(!kernel::fatal_bridge_available);
    zassert_equal(kernel::normalize_fatal_reason(K_ERR_CPU_EXCEPTION),
                  kernel::FatalReason::CpuException);
    zassert_equal(kernel::normalize_fatal_reason(K_ERR_STACK_CHK_FAIL),
                  kernel::FatalReason::StackCheckFailure);
    zassert_equal(kernel::install_fatal_observer(nullptr), solar::Status::NotSupported);
    zassert_equal(kernel::fatal_reason().error(), solar::Status::NotSupported);
}

ZTEST_SUITE(solar_kernel_execution, nullptr, nullptr, nullptr, nullptr, nullptr);
