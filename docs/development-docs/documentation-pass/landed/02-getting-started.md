# Stage 2: Getting Started

Status: complete

## Landed

- Added the runnable `examples/first-application` Zephyr sample.
- Replaced the repository README with current build, test, and documentation entry points.
- Added requirements, installation, first-application, and next-step guides.
- Established named source regions and `literalinclude` as the canonical snippet path.

## Evidence

- `native_sim/native/64`: 1 configuration and 1 test case passed.
- Warning-fatal Sphinx HTML and link-check builds passed.

## Decisions

The example uses Zephyr `printk` only to report sample completion. Solar does
not claim an in-firmware CLI or serial-text logging path; application logging
is documented separately through Solar Logging and Remote.
