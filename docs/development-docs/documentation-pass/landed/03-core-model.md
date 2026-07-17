# Stage 3: Core Model

Status: complete

## Landed

- Added the runnable `examples/system-composition` sample in strict and relaxed variants.
- Documented Blueprint/System binding, components, catalogs, lifecycle, results,
  concurrency, capacity, and feature availability.
- Added the first curated core, System, and lifecycle API pages.
- Added static-System, normalization, catalog/binding, and lifecycle architecture pages.

## Evidence

- `native_sim/native/64`: 2 configurations and 2 test cases passed.
- Warning-fatal Sphinx HTML and link-check builds passed.

## Decisions

Breathe extracts stable concrete declarations. Heavily templated composition
signatures are authored from the public headers because broad Doxygen namespace
rendering currently exposes normalization internals and creates invalid links.
