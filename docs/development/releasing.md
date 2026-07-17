# Releases And Versioning

Solar uses semantic versions from `CMakeLists.txt` and `VERSION`. Before a
release:

1. Run host, complete Zephyr integration, generator, firmware target, Remote
   host-path, documentation, and link gates.
2. Record physical tests and target-only omissions honestly.
3. Regenerate configuration and protocol artifacts.
4. Audit deferred design notes so they are not presented as shipped features.
5. Update compatibility, migration notes, and version values together.
6. Tag `vMAJOR.MINOR.PATCH`; publication retains that version alongside
   `latest`.

Before 1.0, hard API migration is allowed but must be intentional and
documented. Remote protocol and persisted schema changes require their own
compatibility decision regardless of the C++ source version.
