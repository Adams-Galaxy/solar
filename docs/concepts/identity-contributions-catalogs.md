# Identity, Contributions, And Catalogs

Subsystem declarations expose a static `descriptor`. Descriptors provide names,
optional stable identity, versioning, and subsystem metadata.

Components contribute declarations with compact aliases such as `Tasks`,
`Parameters`, `Events`, and `Metrics`. Solar collects direct Blueprint entries
and component contributions into typed catalogs. It rejects duplicate types,
invalid descriptors, identity collisions, missing dependencies, and cycles at
compile time.

A stable ID is suitable for persistence and wire contracts. A local ID is a
compact index assigned inside one effective System. Code must not persist or
transmit a local ID as though it were stable identity.
