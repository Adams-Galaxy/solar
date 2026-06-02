# Contributing

Solar is in active development and the architecture is still settling. Contributions should keep the static-first design intact.

## Development Principles

- Prefer type graph declarations over runtime registries.
- Keep facilities passive and services active/threaded.
- Avoid heap allocation in normal runtime paths.
- Use fixed-capacity buffers and queues.
- Use Zephyr as the kernel, board, driver, and simulation substrate.
- Document public template APIs with Doxygen-style comments.
- Update the relevant markdown docs when architecture changes.

## Checks

Build the owning Zephyr firmware application. On Linux, use native sim:

```sh
west build -p auto -b native_sim/native/64 firmware
west build -t run
```

On macOS, use QEMU for the local smoke build:

```sh
west build -p auto -b qemu_x86_64 firmware
west build -t run
```

Add Zephyr-native tests as runtime code grows.

## Documentation

See [docs/documentation-guide.md](docs/documentation-guide.md).
