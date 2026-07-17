# Availability And Disabled Features

Kconfig can remove complete built-in subsystems. A disabled subsystem owns no
runtime storage or thread and is not silently enabled because another subsystem
could use it.

Availability failures distinguish:

- **disabled**: excluded by configuration;
- **not ready**: present but lifecycle/binding is incomplete;
- **not registered**: the requested type is absent from the effective catalog;
- **not supported**: the selected target or backend cannot provide the operation.

Relaxed frontends can report these states at runtime. Strict binding moves
catalog-membership mistakes to compilation. Tests should cover disabled and
enabled builds when an application conditionally uses a subsystem.
