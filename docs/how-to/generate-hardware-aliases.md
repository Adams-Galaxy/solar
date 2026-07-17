# Generate Hardware Aliases

1. Describe and enable hardware in the board DTS and application overlay.
2. Add semantic aliases, chosen entries, or node labels where appropriate.
3. Enable `CONFIG_SOLAR_HARDWARE` and the required family options.
4. Configure the Zephyr application normally. Solar's module hook reads the
   resolved EDT and writes generated selectors into the build directory.
5. Include `<solar/hardware.hpp>` and define project board aliases.

```cpp
using StatusLed = solar::hardware::gpio::Output<
    solar::hardware::dt::alias<"status-led">>;
```

Do not edit generated headers. Change DTS, overlays, bindings, or Kconfig and
reconfigure. A missing selector means the resolved image does not contain a
supported enabled endpoint with that identity; inspect Zephyr's generated DTS
and the Solar generator diagnostic.

Use explicit structural descriptors when testing without a generated board
identity or when a valid Zephyr endpoint has no semantic alias.
