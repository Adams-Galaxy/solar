#pragma once

#include <cstdint>

#include "solar/kernel/time.hpp"

namespace solar::kernel
{

enum class DeadlineStatus : std::uint8_t
{
    Met,
    Late,
    Missed,
};

/**
 * @brief Absolute tick deadline with optional grace period.
 *
 * Deadlines are used by blocking wrappers so code can pass one budget through
 * multiple waits instead of recalculating independent timeouts.
 */
class Deadline
{
public:
    Deadline() = default;
    explicit Deadline(Tick deadline_ticks, Tick grace_ticks = 0)
        : deadline_ticks_(deadline_ticks), grace_ticks_(grace_ticks) {}

    template <class Rep, class Period>
    static Deadline after(std::chrono::duration<Rep, Period> duration, Tick grace_ticks = 0)
    {
        return Deadline(now_ticks() + to_ticks(duration), grace_ticks);
    }

    static Deadline at_ticks(Tick deadline_ticks, Tick grace_ticks = 0)
    {
        return Deadline(deadline_ticks, grace_ticks);
    }

    Tick ticks() const
    {
        return deadline_ticks_;
    }

    Tick grace_ticks() const
    {
        return grace_ticks_;
    }

    void set_grace_ticks(Tick grace_ticks)
    {
        grace_ticks_ = grace_ticks;
    }

    Tick remaining_ticks() const
    {
        const Tick current = now_ticks();
        return deadline_ticks_ <= current ? 0 : deadline_ticks_ - current;
    }

    bool expired() const
    {
        return remaining_ticks() == 0;
    }

    Timeout remaining_timeout() const
    {
        return Timeout::after_ticks(remaining_ticks());
    }

    DeadlineStatus status() const
    {
        return status_at(now_ticks());
    }

    DeadlineStatus status_at(Tick current) const
    {
        if (current <= deadline_ticks_)
        {
            return DeadlineStatus::Met;
        }
        if (current <= deadline_ticks_ + grace_ticks_)
        {
            return DeadlineStatus::Late;
        }
        return DeadlineStatus::Missed;
    }

private:
    Tick deadline_ticks_ = 0;
    Tick grace_ticks_ = 0;
};

} // namespace solar::kernel
