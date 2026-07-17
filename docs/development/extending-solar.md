# Extending Solar

## Add a subsystem

1. Define its identity tag, descriptors, declaration concepts, and typed error.
2. Add conventional contribution aliases and `contribution_source`
   specializations.
3. Normalize direct and contributed entries into an effective catalog.
4. Give mutable state one typed System state-slot owner.
5. Define Kconfig inclusion, capacities, and disabled-use diagnostics.
6. Add lifecycle dependencies only for real ownership/order requirements.
7. Add focused typed and generic Inspection query surfaces.
8. Add host, header, compile-fail, native runtime, and integration tests.
9. Add subsystem/API/architecture docs and a canonical example path.

## Add a component category

Extend section traits, graph category validation, lifecycle protocol, descriptor
collection, and every generic component collection deliberately. Do not infer a
new category from naming or inheritance. Verify ordering, rollback, Health
subject generation, Inspection, and compile diagnostics.

## Add an adapter

Call the canonical owner's public API and retain owner/origin metadata. Do not
copy canonical state or introduce a reverse dependency from the source
declaration to its consumers.
