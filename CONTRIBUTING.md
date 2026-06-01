# Contributing

Solar is in active development and the architecture is still settling. Contributions should keep the static-first design intact.

## Development Principles

- Prefer type graph declarations over runtime registries.
- Keep facilities passive and services active/threaded.
- Avoid heap allocation in normal runtime paths.
- Use fixed-capacity buffers and queues.
- Put platform/hardware bindings behind project-owned `low_level` facades.
- Document public template APIs with Doxygen-style comments.
- Update the relevant markdown docs when architecture changes.

## Checks

Run host tests:

```sh
cmake -S solar -B solar/build -DSOLAR_BUILD_TESTS=ON
cmake --build solar/build
ctest --test-dir solar/build --output-on-failure
```

In this project, also run the firmware and simulator checks before larger changes.

## Documentation

See [docs/documentation-guide.md](docs/documentation-guide.md).
