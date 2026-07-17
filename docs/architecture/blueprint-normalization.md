# Blueprint Normalization

Normalization converts user sections into one effective application shape. It
validates section uniqueness, gathers component contributions, inserts
Kconfig-enabled built-in candidates when demanded, applies configuration policy
precedence, and derives subsystem catalogs.

Policy precedence is declaration policy, then Blueprint configuration, then
Kconfig default. Kconfig controls feature inclusion and project-wide bounds;
typed policy controls local behavior without mixing configuration objects into
catalog contents.

The normalized type is available through `System::Effective` for advanced
compile-time inspection, but ordinary code should use public frontends and
catalog queries.
