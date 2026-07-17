# Kernel And Execution Boundary

```{graphviz}
digraph execution_boundary {
  rankdir=TB;
  "Zephyr kernel" -> "solar::kernel";
  "solar::kernel" -> "Direct application mechanisms";
  "solar::kernel" -> "solar::execution";
  "Blueprint registrations" -> "solar::execution";
  "solar::execution" -> "System lifecycle and records";
}
```

Kernel wrappers own native storage and preserve Zephyr state-machine semantics.
Execution owns registration metadata, target resolution, System state slots,
activation, quiescence, and containment. A direct Kernel work item remains the
responsibility of its application owner; Solar does not discover it.

The system workqueue is Zephyr-owned. Application workqueue executors and
service threads are Solar-owned components. This distinction controls what
Solar may stop, drain, join, or abort during shutdown.
