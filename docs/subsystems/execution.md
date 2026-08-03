# Execution

`TaskQueue<Capacity>` is an explicitly pumped bounded queue. `ServiceRunner`
owns one Zephyr thread, stop source, join deadline, and static service
lifecycle. Neither requires System.

Application `Run` declarations select `PreemptivePriority<N>`,
`CooperativePriority<N>`, or an explicitly raw `Priority<N>`. A synthesized
`ServiceRunner` exposes `scheduled_priority()` and `scheduling()` so expansion
tests and build tooling can inspect the exact Zephyr class, zero-based level,
signed native value, and requested stack size.

Remote accepts a compile-time scheduler policy. Its default executes bounded
work inline; projects can supply a named adapter backed by their chosen queue.
