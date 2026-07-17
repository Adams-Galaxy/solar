# Facility Storage And Flow

```{graphviz}
digraph facility_flow {
  rankdir=LR;
  "Typed declarations" -> "Effective catalogs";
  "Effective catalogs" -> "Facility state slots";
  "Producer frontend" -> "Admission and policy";
  "Admission and policy" -> "Facility state slots";
  "Facility state slots" -> "Focused records";
  "Admission and policy" -> "Execution registrations";
  "Execution registrations" -> "Processors, hooks, and sinks";
}
```

Each facility specializes static storage from the bound System's effective
catalog. Storage is emitted once per declaration and only includes state
required by its selected policies. Relaxed frontends add an application
binding indirection; they do not duplicate canonical facility state.

Cross-context paths copy bounded values into facility-owned ingress or route
storage. No subsystem retains a caller's temporary reference. Execution owns
deferred work, while each facility remains responsible for admission,
backpressure, records, and shutdown ordering.

Events may feed Metrics and Logging through explicit adapters. These adapters
do not transfer canonical ownership: Metrics still owns measurements and
Logging still owns log ingress/history.
