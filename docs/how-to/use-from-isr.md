# Use Solar From An ISR

Call only operations explicitly suffixed `_isr` or documented as ISR-safe.
Prefer `try_..._isr` frontends that copy into bounded ingress storage and return
immediately. Handle `WouldBlock`, full, or dropped outcomes according to the
owning subsystem's policy.

Do not call lifecycle, blocking query, mutex-backed parameter, Remote query, or
ordinary executor operations from interrupt context merely because their API is
static.
