#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/intrusive_queue.hpp"
#include "solar/kernel/message_queue.hpp"
#include "solar/kernel/pipe.hpp"
#include "solar/kernel/semaphore.hpp"

namespace solar::kernel
{

class TriggeredWork;

inline constexpr bool poll_available = IS_ENABLED(CONFIG_POLL);

#if defined(CONFIG_POLL)

enum class PollSignalDelivery : std::uint8_t
{
    Delivered,
    LatchedAfterTimeout,
};

struct PollSignalOutcome
{
    PollSignalDelivery delivery{PollSignalDelivery::Delivered};
    int native{};

    [[nodiscard]] constexpr bool waiter_notified() const
    {
        return delivery == PollSignalDelivery::Delivered;
    }
};

namespace detail
{

[[nodiscard]] constexpr Result<PollSignalOutcome> poll_signal_outcome(int result)
{
    if (result == 0) {
        return PollSignalOutcome{.delivery = PollSignalDelivery::Delivered, .native = 0};
    }
    if (result == -EAGAIN) {
        return PollSignalOutcome{.delivery = PollSignalDelivery::LatchedAfterTimeout,
                                 .native = result};
    }
    return fail<Error>(error_from_errno(result));
}

} // namespace detail

/** Non-owning access to an initialized Zephyr poll signal. */
class PollSignalRef
{
  public:
    explicit constexpr PollSignalRef(k_poll_signal& signal) : signal_(&signal) {}

    [[nodiscard]] Result<PollSignalOutcome> raise(int value = 0) const
    {
        return detail::poll_signal_outcome(k_poll_signal_raise(signal_, value));
    }

    void reset() const
    {
        k_poll_signal_reset(signal_);
    }

    [[nodiscard]] std::optional<int> value() const
    {
        unsigned int signaled{};
        int result{};
        k_poll_signal_check(signal_, &signaled, &result);
        if (signaled == 0) {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] constexpr k_poll_signal* native_handle() const
    {
        return signal_;
    }

  private:
    k_poll_signal* signal_;
};

class PollSignal
{
  public:
    PollSignal()
    {
        k_poll_signal_init(&signal_);
    }

    PollSignal(const PollSignal&) = delete;
    PollSignal& operator=(const PollSignal&) = delete;
    PollSignal(PollSignal&&) = delete;
    PollSignal& operator=(PollSignal&&) = delete;

    [[nodiscard]] Result<PollSignalOutcome> raise(int value = 0)
    {
        return ref().raise(value);
    }
    void reset()
    {
        ref().reset();
    }
    [[nodiscard]] std::optional<int> value() const
    {
        return PollSignalRef{const_cast<k_poll_signal&>(signal_)}.value();
    }
    [[nodiscard]] PollSignalRef ref()
    {
        return PollSignalRef{signal_};
    }

  private:
    k_poll_signal signal_{};
};

enum class PollState : std::uint32_t
{
    NotReady = K_POLL_STATE_NOT_READY,
    Signaled = K_POLL_STATE_SIGNALED,
    SemaphoreAvailable = K_POLL_STATE_SEM_AVAILABLE,
    DataAvailable = K_POLL_STATE_DATA_AVAILABLE,
    MessageAvailable = K_POLL_STATE_MSGQ_DATA_AVAILABLE,
    PipeDataAvailable = K_POLL_STATE_PIPE_DATA_AVAILABLE,
    Cancelled = K_POLL_STATE_CANCELLED,
};

[[nodiscard]] constexpr PollState operator|(PollState left, PollState right)
{
    return static_cast<PollState>(static_cast<std::uint32_t>(left) |
                                  static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool has_state(PollState value, PollState flag)
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct PollEvent
{
    std::uint8_t tag{};
    PollState state{PollState::NotReady};
};

struct PollResult
{
    std::size_t ready{};
    bool interrupted{};
};

template <std::size_t Capacity> class PollSet
{
    static_assert(Capacity > 0,
                  "SOLAR_DIAGNOSTIC_POLL_ZERO_CAPACITY: poll capacity must be non-zero");

  public:
    static constexpr std::size_t capacity = Capacity;

    PollSet() = default;

    PollSet(const PollSet&) = delete;
    PollSet& operator=(const PollSet&) = delete;
    PollSet(PollSet&&) = delete;
    PollSet& operator=(PollSet&&) = delete;

    [[nodiscard]] Result<void> add(PollSignal& signal, std::uint8_t tag = 0)
    {
        return add(signal.ref(), tag);
    }

    [[nodiscard]] Result<void> add(PollSignalRef signal, std::uint8_t tag = 0)
    {
        return add_native(K_POLL_TYPE_SIGNAL, signal.native_handle(), tag);
    }

    [[nodiscard]] Result<void> add(Semaphore& semaphore, std::uint8_t tag = 0)
    {
        return add(semaphore.ref(), tag);
    }

    [[nodiscard]] Result<void> add(SemaphoreRef semaphore, std::uint8_t tag = 0)
    {
        return add_native(K_POLL_TYPE_SEM_AVAILABLE, semaphore.native_handle(), tag);
    }

    template <typename Message, std::size_t Depth>
    [[nodiscard]] Result<void> add(MessageQueue<Message, Depth>& queue,
                                   std::uint8_t tag = 0)
    {
        return add(queue.ref(), tag);
    }

    template <typename Message>
    [[nodiscard]] Result<void> add(MessageQueueRef<Message> queue, std::uint8_t tag = 0)
    {
        return add_native(K_POLL_TYPE_MSGQ_DATA_AVAILABLE, queue.native_queue(), tag);
    }

    template <typename Value>
    [[nodiscard]] Result<void> add(Queue<Value>& queue, std::uint8_t tag = 0)
    {
        return add(queue.ref(), tag);
    }

    template <typename Value>
    [[nodiscard]] Result<void> add(Fifo<Value>& fifo, std::uint8_t tag = 0)
    {
        return add(fifo.ref(), tag);
    }

    template <typename Value>
    [[nodiscard]] Result<void> add(Lifo<Value>& lifo, std::uint8_t tag = 0)
    {
        return add(lifo.ref(), tag);
    }

    template <typename Value>
    [[nodiscard]] Result<void> add(QueueRef<Value> queue, std::uint8_t tag = 0)
    {
        return add_native(K_POLL_TYPE_DATA_AVAILABLE, queue.native_queue(), tag);
    }

    template <std::size_t Bytes>
    [[nodiscard]] Result<void> add(Pipe<Bytes>& pipe, std::uint8_t tag = 0)
    {
        return add(pipe.ref(), tag);
    }

    [[nodiscard]] Result<void> add(PipeRef pipe, std::uint8_t tag = 0)
    {
        return add_native(K_POLL_TYPE_PIPE_DATA_AVAILABLE, pipe.native_pipe(), tag);
    }

    [[nodiscard]] Result<PollResult> wait(Timeout timeout = Timeout::forever())
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (claimed()) {
            return fail<Error>({.status = Status::Busy});
        }
        if (count_ == 0) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }

        reset_states();
        const int result =
            k_poll(events_.data(), static_cast<int>(count_), timeout.native_handle());
        if (result == 0 || result == -EINTR) {
            return PollResult{.ready = ready_count(), .interrupted = result == -EINTR};
        }
        auto waited = detail::map_wait(result, timeout, Status::WouldBlock);
        return fail<Error>(waited.error());
    }

    [[nodiscard]] Result<PollResult> wait(const Deadline& deadline)
    {
        return wait(deadline.remaining());
    }

    [[nodiscard]] Result<PollResult> try_wait()
    {
        return wait(Timeout::no_wait());
    }

    [[nodiscard]] Result<PollEvent> event(std::size_t index) const
    {
        if (index >= count_) {
            return fail<solar::Error>({.status = solar::Status::NotFound});
        }
        return PollEvent{.tag = static_cast<std::uint8_t>(events_[index].tag),
                         .state = state_of(events_[index].state)};
    }

    [[nodiscard]] std::size_t size() const
    {
        return count_;
    }

    [[nodiscard]] Result<void> clear()
    {
        if (claimed()) {
            return fail<Error>({.status = Status::Busy});
        }
        count_ = 0;
        return {};
    }

  private:
    [[nodiscard]] k_poll_event* native_events()
    {
        return events_.data();
    }

    friend class TriggeredWork;

    [[nodiscard]] Result<void> add_native(std::uint32_t type, void* object,
                                          std::uint8_t tag)
    {
        if (claimed()) {
            return fail<Error>({.status = Status::Busy});
        }
        if (object == nullptr) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (count_ == Capacity) {
            return fail<Error>({.status = Status::Full});
        }

        k_poll_event_init(&events_[count_], type, K_POLL_MODE_NOTIFY_ONLY, object);
        events_[count_].tag = tag;
        ++count_;
        return {};
    }

    void reset_states()
    {
        for (std::size_t index = 0; index < count_; ++index) {
            events_[index].state = K_POLL_STATE_NOT_READY;
        }
    }

    [[nodiscard]] std::size_t ready_count() const
    {
        std::size_t ready{};
        for (std::size_t index = 0; index < count_; ++index) {
            ready += events_[index].state != K_POLL_STATE_NOT_READY ? 1U : 0U;
        }
        return ready;
    }

    [[nodiscard]] static constexpr PollState state_of(std::uint32_t state)
    {
        return static_cast<PollState>(state);
    }

    [[nodiscard]] bool claim(const void* owner)
    {
        const void* expected = nullptr;
        return claimant_.compare_exchange_strong(expected, owner, std::memory_order_acq_rel);
    }

    void release(const void* owner)
    {
        const void* expected = owner;
        (void)claimant_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }

    [[nodiscard]] bool claimed_by(const void* owner) const
    {
        return claimant_.load(std::memory_order_acquire) == owner;
    }

    [[nodiscard]] bool claimed() const
    {
        return claimant_.load(std::memory_order_acquire) != nullptr;
    }

    std::array<k_poll_event, Capacity> events_{};
    std::size_t count_{};
    std::atomic<const void*> claimant_{nullptr};
};

#else

template <typename> inline constexpr bool poll_dependent_false = false;

class PollSignal
{
  public:
    template <typename Disabled = void> PollSignal()
    {
        static_assert(poll_dependent_false<Disabled>,
                      "SOLAR_DIAGNOSTIC_POLL_DISABLED: enable CONFIG_POLL before using Solar poll "
                      "primitives");
    }
};

template <std::size_t Capacity> class PollSet
{
    static_assert(Capacity == 0 && Capacity != 0,
                  "SOLAR_DIAGNOSTIC_POLL_DISABLED: enable CONFIG_POLL before using Solar poll "
                  "primitives");
};

#endif

} // namespace solar::kernel
