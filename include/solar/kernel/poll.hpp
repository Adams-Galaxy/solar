#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/message_queue.hpp"
#include "solar/kernel/semaphore.hpp"

namespace solar::kernel
{

inline constexpr bool poll_available = IS_ENABLED(CONFIG_POLL);

#if defined(CONFIG_POLL)

class PollSignal
{
  public:
    PollSignal() noexcept
    {
        k_poll_signal_init(&signal_);
    }

    PollSignal(const PollSignal&) = delete;
    PollSignal& operator=(const PollSignal&) = delete;
    PollSignal(PollSignal&&) = delete;
    PollSignal& operator=(PollSignal&&) = delete;

    [[nodiscard]] Status raise(int value = 0) noexcept
    {
        return detail::map_native(k_poll_signal_raise(&signal_, value));
    }

    void reset() noexcept
    {
        k_poll_signal_reset(&signal_);
    }

    [[nodiscard]] std::optional<int> value() const noexcept
    {
        unsigned int signaled{};
        int result{};
        k_poll_signal_check(const_cast<k_poll_signal*>(&signal_), &signaled, &result);
        if (signaled == 0) {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] k_poll_signal* native_handle() noexcept
    {
        return &signal_;
    }

    [[nodiscard]] const k_poll_signal* native_handle() const noexcept
    {
        return &signal_;
    }

  private:
    k_poll_signal signal_{};
};

enum class PollState : std::uint8_t
{
    NotReady,
    Signaled,
    SemaphoreAvailable,
    MessageAvailable,
    Cancelled,
    Unknown,
};

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

    [[nodiscard]] Status add(PollSignal& signal, std::uint8_t tag = 0) noexcept
    {
        return add_native(K_POLL_TYPE_SIGNAL, signal.native_handle(), tag);
    }

    [[nodiscard]] Status add(Semaphore& semaphore, std::uint8_t tag = 0) noexcept
    {
        return add_native(K_POLL_TYPE_SEM_AVAILABLE, semaphore.native_handle(), tag);
    }

    template <typename Message, std::size_t Depth>
    [[nodiscard]] Status add(MessageQueue<Message, Depth>& queue, std::uint8_t tag = 0) noexcept
    {
        return add_native(K_POLL_TYPE_MSGQ_DATA_AVAILABLE, queue.native_handle(), tag);
    }

    [[nodiscard]] Result<PollResult> wait(Timeout timeout = Timeout::forever()) noexcept
    {
        if (count_ == 0) {
            return fail(Status::Invalid);
        }

        reset_states();
        const int result =
            k_poll(events_.data(), static_cast<int>(count_), timeout.native_handle());
        if (result == 0 || result == -EINTR) {
            return PollResult{.ready = ready_count(), .interrupted = result == -EINTR};
        }
        return fail(detail::map_wait(result, timeout, Status::WouldBlock));
    }

    [[nodiscard]] Result<PollResult> wait(const Deadline& deadline) noexcept
    {
        return wait(deadline.remaining());
    }

    [[nodiscard]] Result<PollResult> try_wait() noexcept
    {
        return wait(Timeout::no_wait());
    }

    [[nodiscard]] Result<PollEvent> event(std::size_t index) const noexcept
    {
        if (index >= count_) {
            return fail(Status::NotFound);
        }
        return PollEvent{.tag = static_cast<std::uint8_t>(events_[index].tag),
                         .state = state_of(events_[index].state)};
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return count_;
    }

    void clear() noexcept
    {
        count_ = 0;
    }

    [[nodiscard]] k_poll_event* native_events() noexcept
    {
        return events_.data();
    }

  private:
    [[nodiscard]] Status add_native(std::uint32_t type, void* object, std::uint8_t tag) noexcept
    {
        if (object == nullptr) {
            return Status::Invalid;
        }
        if (count_ == Capacity) {
            return Status::Full;
        }

        k_poll_event_init(&events_[count_], type, K_POLL_MODE_NOTIFY_ONLY, object);
        events_[count_].tag = tag;
        ++count_;
        return Status::Ok;
    }

    void reset_states() noexcept
    {
        for (std::size_t index = 0; index < count_; ++index) {
            events_[index].state = K_POLL_STATE_NOT_READY;
        }
    }

    [[nodiscard]] std::size_t ready_count() const noexcept
    {
        std::size_t ready{};
        for (std::size_t index = 0; index < count_; ++index) {
            ready += events_[index].state != K_POLL_STATE_NOT_READY ? 1U : 0U;
        }
        return ready;
    }

    [[nodiscard]] static PollState state_of(std::uint32_t state) noexcept
    {
        if ((state & K_POLL_STATE_CANCELLED) != 0) {
            return PollState::Cancelled;
        }
        if ((state & K_POLL_STATE_SIGNALED) != 0) {
            return PollState::Signaled;
        }
        if ((state & K_POLL_STATE_SEM_AVAILABLE) != 0) {
            return PollState::SemaphoreAvailable;
        }
        if ((state & K_POLL_STATE_MSGQ_DATA_AVAILABLE) != 0) {
            return PollState::MessageAvailable;
        }
        if (state == K_POLL_STATE_NOT_READY) {
            return PollState::NotReady;
        }
        return PollState::Unknown;
    }

    std::array<k_poll_event, Capacity> events_{};
    std::size_t count_{};
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
