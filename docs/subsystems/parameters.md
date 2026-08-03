# Parameters

`parameters::Store<Schema<...>>` is explicitly owned typed storage. It works
without Remote or System, validates bounds before committing, serializes reads
and writes, and supports atomic multi-value transactions. The static facade
`StaticStore<Application, Schema>` gives one application-scoped instance.

Authored IDL normally generates declarations and the application schema. A
separate `PersistenceAdapter` connects a store to versioned byte storage; the
store itself has no persistence or transport dependency.
