# Health And Supervision Flow

```{graphviz}
digraph supervision {
  rankdir=TB;
  "Zephyr and subsystem facts" -> "Health monitors";
  "Component reports and checks" -> "Health monitors";
  "Health monitors" -> "Assessments and transition history";
  "Assessments and transition history" -> "Supervisor policy";
  "Supervisor policy" -> "Recovery and safe-state actions";
  "Supervisor policy" -> "Watchdog gate";
  "Watchdog gate" -> "Board watchdog provider";
}
```

Health is a passive Facility and remains queryable without Supervisor.
Supervisor is a Service with its own stack, lifecycle, progress, and bounded
cycle cadence. It reads Health; it does not overwrite source facts.

Recovery runs outside Health storage locks. Its result becomes new evidence and
a Supervisor response record. An unsuccessful cycle cannot feed the watchdog.
Fatal Zephyr paths use the separate panic-safe Kernel/Logging bridge because
ordinary Health and Supervisor locks or threads may no longer be usable.
