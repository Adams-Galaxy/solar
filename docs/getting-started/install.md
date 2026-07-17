# Install Solar

Solar participates in a Zephyr build as an extra module. Register its repository
before `find_package(Zephyr ...)` in the application `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES /absolute/path/to/solar)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_solar_application LANGUAGES CXX)

target_sources(app PRIVATE src/main.cpp)
```

For an example stored inside the Solar repository, use a relative path instead:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../..)
```

Solar's `zephyr/module.yml` directs Zephyr to the module CMake and Kconfig
files. Once registered, application code can include `<solar/solar.hpp>` and
`menuconfig` exposes the `SOLAR_*` options.

## Verify The Module

From an initialized Zephyr workspace:

```sh
west build -b native_sim/native/64 examples/first-application
```

If CMake cannot find Solar, check that `ZEPHYR_EXTRA_MODULES` is set before
`find_package(Zephyr ...)` and points to the directory containing Solar's
`zephyr/module.yml`.
