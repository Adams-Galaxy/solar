# Cross-Subsystem Dependencies

Dependencies point from integration adapters to canonical owners:

```{graphviz}
digraph subsystem_dependencies {
  rankdir=LR;
  "Execution" -> "Kernel";
  "Bus" -> "Execution";
  "Parameters" -> "Execution";
  "Events" -> "Execution";
  "Events adapters" -> "Metrics";
  "Events adapters" -> "Logging";
  "Remote" -> "Execution";
  "Remote adapters" -> "Owning subsystem APIs";
  "Inspection" -> "Owning subsystem APIs";
  "Health adapters" -> "Owning subsystem records";
  "Supervisor" -> "Health";
  "Devices" -> "Hardware";
}
```

Kconfig removes unavailable subsystems at compile time. Builtin normalization
adds required facilities/services once and validates dependencies. Adapters do
not reverse ownership: Event-to-Metric logic updates Metrics through its API;
Remote parameter updates delegate to Parameters; Inspection pages records
without storing a second copy.

Component headers include Solar capability headers and their direct project
dependencies, never the composition root. The root includes components, forms
the Blueprint/System, and binds it once. This prevents include cycles while
keeping component implementations non-templated where no System-specific type
is required.
