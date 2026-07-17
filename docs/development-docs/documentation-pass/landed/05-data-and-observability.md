# Stage 5: Data And Observability

Status: complete

## Landed

- Added the runnable `examples/data-pipeline` sample.
- Added Bus, Parameters, Events, Metrics, and Logging subsystem/API pages.
- Added Parameter persistence and log-sink how-to guides.
- Added the facility storage/data-flow architecture page.

## Evidence

- `native_sim/native/64`: data pipeline passed 1 configuration and 1 case.
- Warning-fatal documentation HTML passed.

## Decisions

The canonical sample deliberately projects each subsystem's rich typed error
into `solar::Error` at its component boundary. This demonstrates that error
domains compose explicitly rather than through an implicit lossy conversion.
