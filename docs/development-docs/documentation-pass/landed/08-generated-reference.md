# Stage 8: Generated And Configuration Reference

Status: complete

## Landed

- Added a Kconfiglib generator and complete reference for all 173 Solar symbols.
- Added devicetree input/generated-output and target-constraint references.
- Completed Remote protocol and generated artifact references.
- Replaced provisional compatibility prose with verified target, language,
  disabled-feature, and deferred-capability matrices.

## Evidence

- Kconfig generation is deterministic and a dependency of documentation builds.
- Structural audit compares every current Solar symbol with its generated heading.

## Decisions

The hardware reference describes supported EDT input classes and uses existing
controlled native/Teensy generator fixtures as evidence. It does not publish one
board's generated alias list as a portable API.
