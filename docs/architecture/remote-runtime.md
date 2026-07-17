# Remote Runtime

```{graphviz}
digraph remote_runtime {
  rankdir=LR;
  "Bound catalogs" -> "Remote Facility";
  "Remote Facility" -> "Remote Service";
  "Remote Service" -> "Link A";
  "Remote Service" -> "Link B";
  "Link events" -> "Remote Service";
  "Remote Service" -> "Execution targets";
  "Remote Facility" -> "Source subsystem APIs";
  "Post-link manifest" -> "Generated host client";
}
```

Link callbacks admit small connection/RX/TX/fault events into a bounded queue.
The Remote Service thread owns frame parsing, sessions, request correlation,
output scheduling, and transport-independent timeouts. It dispatches typed
application work inline only when declared, otherwise through visible
Execution registrations.

The Facility translates endpoint IDs to compile-time dispatch entries and
acquires owned values from source APIs. Encoding and transmission happen after
source synchronization is released. Per-session lanes prevent bulk streams
from silently consuming all control-response capacity.
