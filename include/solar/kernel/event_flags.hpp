#pragma once

#include <cstdint>
#include <limits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/interrupt.hpp"

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
    explicit constexpr EventFlagsRef(k_event& event) : event_(&event) {}

    [[nodiscard]] EventBits post(EventBits bits) const
    {
        return k_event_post(event_, bits);
    }

    [[nodiscard]] EventBits set(EventBits bits) const
    {
        return k_event_set(event_, bits);
    }

    [[nodiscard]] EventBits set_masked(EventBits bits, EventBits mask) const
    {
        return k_event_set_masked(event_, bits, mask);
    }

    [[nodiscard]] EventBits clear(EventBits bits) const
    {
        return k_event_clear(event_, bits);
    }

    [[nodiscard]] EventBits
    test(EventBits mask = std::numeric_limits<EventBits>::max()) const
    {
        return k_event_test(event_, mask);
    }

    [[nodiscard]] Result<EventBits>
    wait_any(EventBits mask, Timeout timeout = Timeout::forever(),
             ResetBeforeWait reset = ResetBeforeWait::No) const
    {
        return wait(mask, timeout, reset, false, false);
    }

    [[nodiscard]] Result<EventBits>
    wait_all(EventBits mask, Timeout timeout = Timeout::forever(),
             ResetBeforeWait reset = ResetBeforeWait::No) const
    {
        return wait(mask, timeout, reset, true, false);
    }

    [[nodiscard]] Result<EventBits>
    take_any(EventBits mask, Timeout timeout = Timeout::forever(),
             ResetBeforeWait reset = ResetBeforeWait::No) const
    {
        return wait(mask, timeout, reset, false, true);
    }

    [[nodiscard]] Result<EventBits>
    take_all(EventBits mask, Timeout timeout = Timeout::forever(),
             ResetBeforeWait reset = ResetBeforeWait::No) const
    {
        return wait(mask, timeout, reset, true, true);
    }

    [[nodiscard]] Result<EventBits> try_wait_any_isr(EventBits mask) const
    {
        return wait_native(mask, Timeout::no_wait(), ResetBeforeWait::No, false, false);
    }

    [[nodiscard]] Result<EventBits> try_take_any_isr(EventBits mask) const
    {
        return wait_native(mask, Timeout::no_wait(), ResetBeforeWait::No, false, true);
    }

    [[nodiscard]] constexpr k_event* native_handle() const
    {
        return event_;
    }

  private:
    [[nodiscard]] Result<EventBits> wait(EventBits mask, Timeout timeout, ResetBeforeWait reset,
                                         bool all, bool consume) const
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return wait_native(mask, timeout, reset, all, consume);
    }

    [[nodiscard]] Result<EventBits> wait_native(EventBits mask, Timeout timeout,
                                                ResetBeforeWait reset, bool all,
                                                bool consume) const
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
    EventFlags()
    {
        k_event_init(&event_);
    }

    EventFlags(const EventFlags&) = delete;
    EventFlags& operator=(const EventFlags&) = delete;
    EventFlags(EventFlags&&) = delete;
    EventFlags& operator=(EventFlags&&) = delete;

    [[nodiscard]] EventFlagsRef ref()
    {
        return EventFlagsRef{event_};
    }

    [[nodiscard]] EventBits post(EventBits bits)
    {
        return ref().post(bits);
    }
    [[nodiscard]] EventBits set(EventBits bits)
    {
        return ref().set(bits);
    }
    [[nodiscard]] EventBits set_masked(EventBits bits, EventBits mask)
    {
        return ref().set_masked(bits, mask);
    }
    [[nodiscard]] EventBits clear(EventBits bits)
    {
        return ref().clear(bits);
    }
    [[nodiscard]] EventBits test(EventBits mask = std::numeric_limits<EventBits>::max())
    {
        return ref().test(mask);
    }
    [[nodiscard]] Result<EventBits> wait_any(EventBits mask, Timeout timeout = Timeout::forever(),
                                             ResetBeforeWait reset = ResetBeforeWait::No)
    {
        return ref().wait_any(mask, timeout, reset);
    }
    [[nodiscard]] Result<EventBits> wait_all(EventBits mask, Timeout timeout = Timeout::forever(),
                                             ResetBeforeWait reset = ResetBeforeWait::No)
    {
        return ref().wait_all(mask, timeout, reset);
    }
    [[nodiscard]] Result<EventBits> take_any(EventBits mask, Timeout timeout = Timeout::forever(),
                                             ResetBeforeWait reset = ResetBeforeWait::No)
    {
        return ref().take_any(mask, timeout, reset);
    }
    [[nodiscard]] Result<EventBits> take_all(EventBits mask, Timeout timeout = Timeout::forever(),
                                             ResetBeforeWait reset = ResetBeforeWait::No)
    {
        return ref().take_all(mask, timeout, reset);
    }
    [[nodiscard]] Result<EventBits> try_wait_any_isr(EventBits mask)
    {
        return ref().try_wait_any_isr(mask);
    }
    [[nodiscard]] Result<EventBits> try_take_any_isr(EventBits mask)
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
