# Documentation Stage 0: Contract Inventory And Cleanup

Status: landed

Landed date: 2026-07-17

## Outcome

The public surface, authoritative inputs, terminology, generated references,
known stale claims, and coverage obligations are inventoried. Public versus
advanced/internal header roles are explicitly classified before API extraction.

## Artifacts

- `documentation-pass/inventory.md`
- `documentation-pass/terminology.md`
- `documentation-pass/coverage.md`
- `documentation-planning.md`

## Evidence

- 193 Solar headers and 19 aggregate headers enumerated from `include/solar/`.
- 173 Solar Kconfig symbols enumerated from `zephyr/Kconfig`.
- all accepted design specs and 22 landed implementation records mapped.
- executable evidence mapped across host and focused Zephyr suites.

## Decisions

- relaxed binding is the common tutorial path;
- API reference is curated by public role rather than emitted as a flat parse;
- runtime/storage/detail implementation remains architecture-only;
- generated Kconfig, devicetree, Remote, and C++ references remain mechanical
  build products;
- the stale README is replaced in Stage 2 after the first application exists.

## Gate

Every required topic now has an authoritative source, target public location,
example obligation, architecture obligation, and executable evidence row.
