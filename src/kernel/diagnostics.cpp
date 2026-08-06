#include <cerrno>
#include <cstddef>

#include <zephyr/kernel.h>
#include <zephyr/version.h>

#include <solar/kernel/diagnostics.hpp>

#if defined(CONFIG_THREAD_RUNTIME_STACK_SAFETY) && defined(CONFIG_INIT_STACKS) &&                  \
    defined(CONFIG_THREAD_STACK_INFO)

namespace solar::kernel::detail
{

#if ZEPHYR_VERSION_CODE == ZEPHYR_VERSION(4, 4, 0)

// Zephyr 4.4 declares k_thread_runtime_stack_unused_threshold_*(), but its
// implementation accidentally retained "stack_safety" in the symbol names.
// Keep that release-specific mismatch out of Solar's public headers.
extern "C" int z_impl_k_thread_runtime_stack_safety_unused_threshold_set(k_thread* thread,
                                                                         std::size_t threshold);
extern "C" std::size_t z_impl_k_thread_runtime_stack_safety_unused_threshold_get(k_thread* thread);

Result<void> set_stack_warning_margin_native(ThreadId thread, std::size_t margin)
{
    const int result = z_impl_k_thread_runtime_stack_safety_unused_threshold_set(thread, margin);
    return result == 0 ? Result<void>{} : Result<void>{fail<Error>(error_from_errno(result))};
}

std::size_t stack_warning_margin_native(ThreadId thread)
{
    return z_impl_k_thread_runtime_stack_safety_unused_threshold_get(thread);
}

#else

Result<void> set_stack_warning_margin_native(ThreadId thread, std::size_t margin)
{
    const int result = k_thread_runtime_stack_unused_threshold_set(thread, margin);
    return result == 0 ? Result<void>{} : Result<void>{fail<Error>(error_from_errno(result))};
}

std::size_t stack_warning_margin_native(ThreadId thread)
{
    return k_thread_runtime_stack_unused_threshold_get(thread);
}

#endif

} // namespace solar::kernel::detail

#endif
