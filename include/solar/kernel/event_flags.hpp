#pragma once

#include <cstdint>
#include <limits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"

namespace solar::kernel
{

using EventBits = std::uint32_t;
inline constexpr bool event_flags_available = IS_ENABLED(CONFIG_EVENTS);

enum class ResetBeforeWait : bool
{
    No = false,
    Yes = true,
};

#if defined(CONFIG_EVENTS)

/** Non-owning access to initialized Zephyr event flags. */
class EventFlagsRef
{
  public:
    explicit constexpr EventFlagsRef(k_event& event) noexcept : event_(&event) {}

    [[nodiscard]] EventBits post(EventBits bits) const noexcept
    {
        return k_event_post(event_, bits);
    }

    [[nodiscard]] EventBits post_isr(EventBits bits) const noexcept
    {
        return post(bits);
    }

    [[nodiscard]] EventBits set(EventBits bits) const noexcept
    {
        return k_event_set(event_, bits);
    }

    [[nodiscard]] EventBits set_isr(EventBits bits) const noexcept
    {
        return set(bits);
    }

    [[nodiscard]] EventBits clear(EventBits bits) const noexcept
    {
        return k_event_clear(event_, bits);
    }

    [[nodiscard]] EventBits clear_isr(EventBits bits) const noexcept
    {
        return clear(bits);
    }

    [[nodiscard]] EventBits
    test(EventBits mask = std::numeric_limits<EventBits>::max()) const noexcept
    {
        return k_event_test(event_, mask);
    }

    [[nodiscard]] Result<EventBits>
    wait_any(EventBits mask, Timeout timeout = Timeout::forever(),
             ResetBeforeWait reset = ResetBeforeWait::No) const noexcept
    {
        return wait(mask, timeout, reset, false, false);
    }

    [[nodiscard]] Result<EventBits>
    wait_all(EventBits mask, Timeout timeout = Timeout::forever(),
             ResetBeforeWait reset = ResetBeforeWait::No) const noexcept
    {
        return wait(mask, timeout, reset, true, false);
    }

    [[nodiscard]] Result<EventBits>
    take_any(EventBits mask, Timeout timeout = Timeout::forever(),
             ResetBeforeWait reset = ResetBeforeWait::No) const noexcept
    {
        return wait(mask, timeout, reset, false, true);
    }

    [[nodiscard]] Result<EventBits>
    take_all(EventBits mask, Timeout timeout = Timeout::forever(),
             ResetBeforeWait reset = ResetBeforeWait::No) const noexcept
    {
        return wait(mask, timeout, reset, true, true);
    }

    [[nodiscard]] Result<EventBits> try_wait_any_isr(EventBits mask) const noexcept
    {
        return wait_any(mask, Timeout::no_wait());
    }

    [[nodiscard]] Result<EventBits> try_take_any_isr(EventBits mask) const noexcept
    {
        return take_any(mask, Timeout::no_wait());
    }

    [[nodiscard]] constexpr k_event* native_handle() const noexcept
    {
        return event_;
    }

  private:
    [[nodiscard]] Result<EventBits> wait(EventBits mask, Timeout timeout, ResetBeforeWait reset,
                                         bool all, bool consume) const noexcept
    {
        if (mask == 0) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }

        const bool reset_before = reset == ResetBeforeWait::Yes;
        EventBits received{};
        if (consume) {
            received =
                all ? k_event_wait_all_safe(event_, mask, reset_before, timeout.native_handle())
                    : k_event_wait_safe(event_, mask, reset_before, timeout.native_handle());
        } else {
            received = all ? k_event_wait_all(event_, mask, reset_before, timeout.native_handle())
                           : k_event_wait(event_, mask, reset_before, timeout.native_handle());
        }

        if (received == 0) {
            return fail<Error>(
                {.status = timeout.is_no_wait() ? Status::WouldBlock : Status::Timeout});
        }
        return received;
    }

    k_event* event_;
};

class EventFlags
{
  public:
    EventFlags() noexcept
    {
        k_event_init(&event_);
    }

    EventFlags(const EventFlags&) = delete;
    EventFlags& operator=(const EventFlags&) = delete;
    EventFlags(EventFlags&&) = delete;
    EventFlags& operator=(EventFlags&&) = delete;

    [[nodiscard]] EventFlagsRef ref() noexcept
    {
        return EventFlagsRef{event_};
    }

    [[nodiscard]] EventBits post(EventBits bits) noexcept
    {
        return ref().post(bits);
    }
    [[nodiscard]] EventBits post_isr(EventBits bits) noexcept
    {
        return ref().post_isr(bits);
    }
    [[nodiscard]] EventBits set(EventBits bits) noexcept
    {
        return ref().set(bits);
    }
    [[nodiscard]] EventBits set_isr(EventBits bits) noexcept
    {
        return ref().set_isr(bits);
    }
    [[nodiscard]] EventBits clear(EventBits bits) noexcept
    {
        return ref().clear(bits);
    }
    [[nodiscard]] EventBits clear_isr(EventBits bits) noexcept
    {
        return ref().clear_isr(bits);
    }
    [[nodiscard]] EventBits test(EventBits mask = std::numeric_limits<EventBits>::max()) noexcept
    {
        return ref().test(mask);
    }
    [[nodiscard]] Result<EventBits> wait_any(EventBits mask, Timeout timeout = Timeout::forever(),
                                             ResetBeforeWait reset = ResetBeforeWait::No) noexcept
    {
        return ref().wait_any(mask, timeout, reset);
    }
    [[nodiscard]] Result<EventBits> wait_all(EventBits mask, Timeout timeout = Timeout::forever(),
                                             ResetBeforeWait reset = ResetBeforeWait::No) noexcept
    {
        return ref().wait_all(mask, timeout, reset);
    }
    [[nodiscard]] Result<EventBits> take_any(EventBits mask, Timeout timeout = Timeout::forever(),
                                             ResetBeforeWait reset = ResetBeforeWait::No) noexcept
    {
        return ref().take_any(mask, timeout, reset);
    }
    [[nodiscard]] Result<EventBits> take_all(EventBits mask, Timeout timeout = Timeout::forever(),
                                             ResetBeforeWait reset = ResetBeforeWait::No) noexcept
    {
        return ref().take_all(mask, timeout, reset);
    }
    [[nodiscard]] Result<EventBits> try_wait_any_isr(EventBits mask) noexcept
    {
        return ref().try_wait_any_isr(mask);
    }
    [[nodiscard]] Result<EventBits> try_take_any_isr(EventBits mask) noexcept
    {
        return ref().try_take_any_isr(mask);
    }

  private:
    k_event event_{};
};

#else

template <typename> inline constexpr bool event_flags_dependent_false = false;

class EventFlags
{
  public:
    template <typename Disabled = void> EventFlags()
    {
        static_assert(event_flags_dependent_false<Disabled>,
                      "SOLAR_DIAGNOSTIC_EVENTS_DISABLED: enable CONFIG_EVENTS before using Solar "
                      "event flags");
    }
};

#endif

} // namespace solar::kernel
