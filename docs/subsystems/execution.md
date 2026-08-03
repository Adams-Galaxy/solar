# Execution

`TaskQueue<Capacity>` is an explicitly pumped bounded queue. `ServiceRunner`
owns one Zephyr thread, stop source, join deadline, and static service
lifecycle. Neither requires System.

Remote accepts a compile-time scheduler policy. Its default executes bounded
work inline; projects can supply a named adapter backed by their chosen queue.
