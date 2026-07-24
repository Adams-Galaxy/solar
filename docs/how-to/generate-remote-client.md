# Generate A Remote Client

1. Give every external Schema and endpoint stable IDs. Name every field with
   `Field<id, "name", &Value::member>`.
2. Declare every externally used application enum with `Schema<Enum>`,
   `EnumValues`, and stable numeric/name mappings, then add it through
   `ContributeSchemas`. Solar adapter enums are built in and appear
   automatically when referenced.
3. Contribute Data, Actions, Topics, Streams, and Links to the System.
4. Enable `CONFIG_SOLAR_REMOTE` and
   `CONFIG_SOLAR_REMOTE_GENERATE_MANIFEST`.
5. Install `tools/remote/requirements.txt` in the build environment.
6. Build the final Zephyr image. Generation runs after `zephyr.elf` links.
7. Distribute the firmware image, manifest digest, and generated host artifacts
   as one compatibility bundle.

If generation reports an ID or capability collision, fix the declarations; do
not patch generated JSON. If a host digest differs, perform a manifest-aware
compatibility check or deploy the matching client before opening a control
session.
