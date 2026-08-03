# Kernel And Execution Boundary

```{graphviz}
digraph execution_boundary {
  rankdir=TB;
  "Zephyr kernel" -> "solar::kernel";
  "solar::kernel" -> "Direct application mechanisms";
  "solar::kernel" -> "solar::execution";
  "Generated roles and adapters" -> "solar::execution";
  "solar::execution" -> "System lifecycle and records";
}
```

Kernel wrappers own native storage and preserve Zephyr state-machine semantics.
Execution provides bounded task queues and service runners. Registration
metadata and target selection belong to the module or adapter using them. A
direct Kernel work item remains the responsibility of its application owner;
Solar does not discover it.

The system workqueue is Zephyr-owned. Application workqueue executors and
service threads are explicitly owned modules. This distinction controls what an
application may stop, drain, join, or abort during shutdown.
