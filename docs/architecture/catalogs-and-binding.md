# Catalogs And Binding

Catalogs validate descriptors, provenance, stable identity, ownership, and
duplicates at compile time. Local IDs provide bounded runtime indexing into
static storage.

Relaxed frontends bind generated operation tables during lifecycle. This adds a
small fixed indirection and permits runtime availability errors. Strict mode
generates direct compile-time dispatch and rejects unregistered uses during
compilation.

Both modes resolve to the same canonical subsystem storage. Binding changes
dispatch and diagnostics, not ownership or semantics.
