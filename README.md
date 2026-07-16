# Solar

Solar is a C++23 firmware framework and Zephyr module under active architectural
reform.

The former positional `System`, entry Profile, Channel, task, observability, and
Remote APIs have been removed. New subsystem APIs are landing progressively
against the accepted static-system design. Until the implementation closes,
the repository may intentionally expose only the capabilities completed by the
current reform stage.

Current version: `0.1.0`

## Zephyr Module

Register Solar as a Zephyr module before `find_package(Zephyr ...)`:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES /path/to/solar)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

Applications using Solar require:

```text
CONFIG_CPP=y
CONFIG_STD_CPP23=y
CONFIG_REQUIRES_FULL_LIBCPP=y
CONFIG_SOLAR=y
```

The current foundation smoke suite runs with:

```sh
west twister \
  -T tests/zephyr/smoke \
  -p native_sim/native/64
```

Host foundation tests run with:

```sh
cmake -S . -B build/host -DBUILD_TESTING=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

## Development Architecture

The accepted design specifications and implementation plan live in the
companion firmware workspace under `development-docs/`. The repository `docs/`
directory describes the removed pre-reform implementation and is retained only
as historical development context until the final documentation pass.
