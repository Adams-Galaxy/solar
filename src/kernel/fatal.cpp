#include <atomic>
#include <limits>

#include <solar/kernel/fatal.hpp>

namespace solar::kernel
{

namespace
{

constexpr unsigned int no_fatal_reason = std::numeric_limits<unsigned int>::max();

std::atomic<unsigned int> latched_native_reason{no_fatal_reason};
std::atomic<int> requested_status{to_errno(Status::Error)};
std::atomic<FatalObserver> fatal_observer{nullptr};

static_assert(std::atomic<unsigned int>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<FatalObserver>::is_always_lock_free,
              "SOLAR_DIAGNOSTIC_FATAL_OBSERVER_NOT_LOCK_FREE");

} // namespace

namespace detail
{
void latch_fatal(unsigned int native_reason) noexcept
{
    latched_native_reason.store(native_reason, std::memory_order_release);
    if (const auto observer = fatal_observer.load(std::memory_order_acquire); observer != nullptr) {
        const FatalError error{
            .reason = normalize_fatal_reason(native_reason),
            .native_reason = native_reason,
            .trigger_status = static_cast<Status>(requested_status.load(std::memory_order_acquire)),
        };
        observer(error);
    }
}

} // namespace detail

Result<void> install_fatal_observer(FatalObserver observer) noexcept
{
    if (observer == nullptr) {
        return fail<Error>({.status = Status::Invalid});
    }
    FatalObserver expected = nullptr;
    if (fatal_observer.compare_exchange_strong(expected, observer, std::memory_order_acq_rel)) {
        return {};
    }
    return fail<Error>({.status = expected == observer ? Status::Already : Status::Busy});
}

Result<FatalError> fatal_reason() noexcept
{
    const auto native = latched_native_reason.load(std::memory_order_acquire);
    if (native == no_fatal_reason) {
        return fail<solar::Error>({.status = solar::Status::NotReady});
    }
    return FatalError{
        .reason = normalize_fatal_reason(native),
        .native_reason = native,
        .trigger_status = static_cast<Status>(requested_status.load(std::memory_order_acquire)),
    };
}

namespace detail
{

void latch_requested_panic(Status status) noexcept
{
    requested_status.store(to_errno(status), std::memory_order_release);
}

} // namespace detail

} // namespace solar::kernel

extern "C" void k_sys_fatal_error_handler(unsigned int reason, const arch_esf*)
{
    solar::kernel::detail::latch_fatal(reason);
    k_fatal_halt(reason);
}
