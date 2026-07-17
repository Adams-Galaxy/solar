# Runtime Ownership

Solar has no instantiated root runtime object. The bound `System<Blueprint>`
type names canonical static state slots specialized by System, owner, key, and
state type.

| Resource | Canonical owner |
| --- | --- |
| Component lifecycle records and reports | Lifecycle Engine state |
| Service threads and stop state | Execution service state slot |
| Application workqueues | Executor component state slot |
| Bus route buffers | Bus Facility route state |
| Parameter values/revisions | Parameters Facility declaration state |
| Event ingress/history | Events Facility |
| Metric instruments | Metrics Facility declaration state |
| Log ingress/history/sink state | Logging Facility |
| Remote sessions/lanes/requests | Remote Service |
| Remote schemas/dispatch/frontends | Remote Facility |
| Health evidence/history | Health Facility |
| Supervisor cycles/responses | Supervisor Service |
| Native devices and driver DMA | Zephyr drivers |

Global frontends resolve the application binding and operate on these owners.
Relaxed binding adds a small per-operation application indirection; strict
binding resolves membership at compile time. Neither mode duplicates state.

Storage is policy-shaped: declarations emit only synchronization, history,
queue, reducer, or persistence state they select. Capacity is a type parameter
or Kconfig ceiling, not runtime growth.
