# Documentation Stage 1: Toolchain And Site Skeleton

Status: landed

Landed date: 2026-07-17

## Outcome

Documentation is now a first-class, warning-fatal CMake build product. Sphinx
and MyST own authored pages, Doxygen produces XML, Breathe renders C++
declarations, Furo provides responsive HTML, and Graphviz is available for
architecture diagrams.

## Public Surface

- top-level navigation for getting started, tutorials, how-to, concepts,
  subsystems, reference, architecture, development, and examples;
- initial C++ API extraction for `solar::Version` and `solar::version`;
- a public home page and one build-visible page in every major section;
- development records excluded from the public site.

## Build Surface

- `docs/requirements.txt`
- `docs/CMakeLists.txt`
- `docs/Doxyfile.in`
- `docs/conf.py`
- `docs/_static/solar.css`
- `.github/workflows/documentation.yml`

## Verification

| Command | Result |
| --- | --- |
| `python3 -m pip install -r docs/requirements.txt` | pass |
| `cmake -S docs -B build/docs` | pass |
| `cmake --build build/docs --target docs-html` | pass, zero warnings |
| `cmake --build build/docs --target docs-linkcheck` | pass |

The only nitpick exception is the `solar` namespace qualifier emitted by
Breathe for `solar::version`; Doxygen does not create a standalone target for
that namespace. The exception is exact and documented in `conf.py`.

## Gate

A clean dependency environment can generate API XML, render the complete site
skeleton, fail on warnings, validate links, and produce a CI upload artifact.
