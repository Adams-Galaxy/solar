# Concurrency And Context

Every operation has a context contract. Ordinary operations may block or take a
mutex. `try_` operations are bounded and report contention. `_isr` operations
are explicitly designed for interrupt context and use fixed ingress storage or
Zephyr ISR-safe primitives.

Do not infer ISR safety from a function being static or allocation-free. Use
only the documented ISR surface.

State ownership remains static, but synchronization is still required when
threads, workqueues, services, callbacks, and interrupts access the same data.
Solar facilities own their synchronization; application-owned data passed to
Remote or tasks must provide an atomic copy, lock-protected copy, snapshot
function, or push path appropriate to its producer.
