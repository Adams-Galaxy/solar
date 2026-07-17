# System And Blueprint

`solar::Blueprint<...>` declares the application. `solar::System<Blueprint>` is
the user-facing static System type derived from that declaration.

A Blueprint contains typed sections:

- `Devices<T...>` for application hardware behavior;
- `Facilities<T...>` for passive capabilities and state owners;
- `Services<T...>` for one-per-System active components;
- `Executors<T...>` for owned execution targets;
- `Execution<T...>` for root work registrations;
- subsystem catalogs such as `Parameters<T...>`, `Events<T...>`, and
  `Metrics<T...>`;
- subsystem configuration sections.

Solar normalizes sections, discovers contributions, inserts enabled built-ins,
validates catalogs and dependencies, then derives all static storage. The
System is not instantiated. `SOLAR_BIND_SYSTEM` connects global frontends such
as `solar::boot()` to one System type.

Multiple application tags can be bound with `SOLAR_BIND_SYSTEM_FOR` for tests
or deliberately separated firmware domains, but normal firmware has one
default binding.
