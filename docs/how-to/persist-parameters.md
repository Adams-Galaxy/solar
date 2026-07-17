# Persist Parameters

1. Give every persisted Parameter a stable ID and schema version.
2. Select `Manual<Store>`, `Immediate<Store>`, `Deferred<Store, Delay>`, or
   `Transactional<Group>` as its `Persistence` policy.
3. Implement the bounded Store adapter, or enable the Zephyr Settings adapter.
4. Register transactional groups in `parameters::Configuration`.
5. Provide a migration function before changing an encoded schema.

Treat a successful RAM commit and failed persistence as distinct facts. The
returned update/error and focused persistence record identify both. Deferred
persistence coalesces writes and runs on its Execution registration; call the
documented flush operation before shutdown or a controlled reset when durable
commit is required.
