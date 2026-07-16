#pragma once

#include <zephyr/kernel.h>

namespace solar::kernel
{

using NativeThread = k_tid_t;
using NativeTimeout = k_timeout_t;
using NativeTimepoint = k_timepoint_t;
using NativeMutex = k_mutex;
using NativeSemaphore = k_sem;
using NativeMessageQueue = k_msgq;
using NativeEventFlags = k_event;
using NativeTimer = k_timer;
using NativePollEvent = k_poll_event;
using NativePollSignal = k_poll_signal;
using NativeConditionVariable = k_condvar;
using NativeSpinLock = k_spinlock;
using NativePipe = k_pipe;
using NativeMemorySlab = k_mem_slab;
using NativeWork = k_work;
using NativeDelayableWork = k_work_delayable;
using NativeWorkQueue = k_work_q;
#if defined(CONFIG_POLL)
using NativeTriggeredWork = k_work_poll;
#endif

} // namespace solar::kernel
