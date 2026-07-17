# Lifecycle

Lifecycle gives every component an optional opportunity to participate in four
ordered phases:

1. `init()` acquires or initializes resources;
2. `start()` makes initialized behavior active;
3. `stop()` quiesces active behavior;
4. `deinit()` releases initialized resources.

Hooks return `solar::Result<void, E>`. A missing hook is recorded as not
present and treated as successful participation. Boot follows dependency order;
stop follows reverse dependency order.

An init or start failure stops forward progress and triggers bounded rollback.
The boot error preserves the primary failure classification while the retained
boot report records cleanup failures. Stop continues best-effort cleanup and
retains a stop report even when one hook fails.

The initial boot policy rejects another boot while running and does not promise
in-process reboot after stop. Query state with `solar::lifecycle::state()`,
component records with `record<T>()`, and retained reports with
`boot_report()` or `stop_report()`.
