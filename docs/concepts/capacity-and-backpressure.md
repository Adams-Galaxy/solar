# Capacity And Backpressure

Solar uses bounded queues, buffers, histories, catalogs, and in-flight windows.
Kconfig establishes repository-wide maxima and declaration policy selects
behavior within those bounds.

When capacity is unavailable, an operation may reject, report `WouldBlock`,
drop according to explicit policy, coalesce, overwrite an oldest entry, or wait
for a bounded timeout. The owning subsystem documents the exact response.

Capacity is part of correctness, not only tuning. Size ingress for credible
bursts, reserve elevated paths where supported, inspect accounting, and test
the selected overflow behavior on `native_sim` before deploying.
