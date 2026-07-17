# Solar

Solar is a C++23 firmware framework and Zephyr module for statically composed,
bounded, observable embedded systems.

It works with Zephyr's Kconfig, devicetree, kernel, drivers, workqueues, and
build system. Solar adds typed application composition, lifecycle, execution,
data facilities, Remote host integration, health, and supervision.

Current version: `0.1.0`

## First Build

Solar applications require:

```text
CONFIG_CPP=y
CONFIG_STD_CPP23=y
CONFIG_REQUIRES_FULL_LIBCPP=y
CONFIG_SOLAR=y
```

Build the canonical first application in an initialized Zephyr workspace:

```sh
west build -b native_sim/native/64 examples/first-application
west build -t run
```

## Documentation

Install and build the warning-fatal documentation site:

```sh
python3 -m pip install -r docs/requirements.txt
cmake -S docs -B build/docs
cmake --build build/docs --target docs-html
```

Open `build/docs/html/index.html`. Documentation source lives under `docs/`;
accepted design and implementation records remain under
`docs/development-docs/` and are not part of the public site.

## Tests

```sh
cmake -S . -B build/host -DBUILD_TESTING=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure

west twister -T tests/zephyr -p native_sim/native/64
west twister -T examples -p native_sim/native/64
```
