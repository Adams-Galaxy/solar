# Use Solar From An ISR

Call only operations explicitly suffixed `_isr` or documented as ISR-safe.
Prefer `try_..._isr` frontends that copy into bounded ingress storage and return
immediately. Handle `WouldBlock`, full, or dropped outcomes according to the
owning subsystem's policy.

Examples of ordinary ISR-safe operations include semaphore give, event post,
intrusive-queue insertion, stack push, timer stop, work submission, and
poll-signal raise. The explicit no-wait family includes
`MessageQueue::try_send_isr()`, `Stack::try_pop_isr()`,
`MemorySlab::try_allocate_isr()`, and `Heap::try_allocate_isr()`.

An ordinary `try_` method is still thread-only when it is merely a no-wait form
of a potentially blocking Zephyr call. Solar uses the `_isr` suffix to make
that native context distinction visible in application code.

Do not call lifecycle, blocking query, mutex-backed parameter, Remote query, or
ordinary executor operations from interrupt context merely because their API is
static.
Mailbox operations, heap free, poll, thread lifecycle, and synchronous work
cancellation are always thread-only.
