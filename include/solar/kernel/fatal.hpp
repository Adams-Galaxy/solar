#pragma once

#include <cstdint>

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>

#include "solar/core/status.hpp"

namespace solar::kernel
{

enum class FatalReason : std::uint8_t
{
    CpuException,
    SpuriousInterrupt,
    StackCheckFailure,
    KernelOops,
    KernelPanic,
    ArchitectureSpecific,
    Unknown,
};

struct FatalError
{
    FatalReason reason{FatalReason::Unknown};
    unsigned int native_reason{};
    Status trigger_status{Status::Error};
};

using FatalObserver = void (*)(const FatalError&);

inline constexpr bool fatal_bridge_available = IS_ENABLED(CONFIG_SOLAR_FATAL_BRIDGE);

[[nodiscard]] constexpr FatalReason normalize_fatal_reason(unsigned int reason)
{
    switch (reason) {
    case K_ERR_CPU_EXCEPTION:
        return FatalReason::CpuException;
    case K_ERR_SPURIOUS_IRQ:
        return FatalReason::SpuriousInterrupt;
    case K_ERR_STACK_CHK_FAIL:
        return FatalReason::StackCheckFailure;
    case K_ERR_KERNEL_OOPS:
        return FatalReason::KernelOops;
    case K_ERR_KERNEL_PANIC:
        return FatalReason::KernelPanic;
    default:
        return reason >= K_ERR_ARCH_START ? FatalReason::ArchitectureSpecific
                                          : FatalReason::Unknown;
    }
}

#if defined(CONFIG_SOLAR_FATAL_BRIDGE)

[[nodiscard]] Result<void> install_fatal_observer(FatalObserver observer);
[[nodiscard]] Result<FatalError> fatal_reason();

namespace detail
{

void latch_requested_panic(Status status);

} // namespace detail

#else

[[nodiscard]] inline Result<void> install_fatal_observer(FatalObserver)
{
    return fail<Error>({.status = Status::NotSupported});
}

[[nodiscard]] inline Result<FatalError> fatal_reason()
{
    return fail<solar::Error>({.status = solar::Status::NotSupported});
}

namespace detail
{

inline void latch_requested_panic(Status) {}

} // namespace detail

#endif

[[noreturn]] inline void panic(Status status = Status::Error)
{
    detail::latch_requested_panic(status);
    k_panic();
    CODE_UNREACHABLE;
}

[[noreturn]] inline void fatal_halt(FatalReason reason)
{
    unsigned int native = K_ERR_KERNEL_PANIC;
    switch (reason) {
    case FatalReason::CpuException:
        native = K_ERR_CPU_EXCEPTION;
        break;
    case FatalReason::SpuriousInterrupt:
        native = K_ERR_SPURIOUS_IRQ;
        break;
    case FatalReason::StackCheckFailure:
        native = K_ERR_STACK_CHK_FAIL;
        break;
    case FatalReason::KernelOops:
        native = K_ERR_KERNEL_OOPS;
        break;
    case FatalReason::KernelPanic:
    case FatalReason::ArchitectureSpecific:
    case FatalReason::Unknown:
        native = K_ERR_KERNEL_PANIC;
        break;
    }
    k_fatal_halt(native);
    CODE_UNREACHABLE;
}

} // namespace solar::kernel
