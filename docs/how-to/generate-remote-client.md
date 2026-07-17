# Generate A Remote Client

1. Give every external Schema and endpoint stable IDs.
2. Contribute Data, Actions, Topics, Streams, and Links to the System.
3. Enable `CONFIG_SOLAR_REMOTE` and
   `CONFIG_SOLAR_REMOTE_GENERATE_MANIFEST`.
4. Install `tools/remote/requirements.txt` in the build environment.
5. Build the final Zephyr image. Generation runs after `zephyr.elf` links.
6. Distribute the firmware image, manifest digest, and generated host artifacts
   as one compatibility bundle.

If generation reports an ID or capability collision, fix the declarations; do
not patch generated JSON. If a host digest differs, perform a manifest-aware
compatibility check or deploy the matching client before opening a control
session.
