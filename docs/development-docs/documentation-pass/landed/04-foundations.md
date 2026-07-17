# Stage 4: Kernel, Execution, And Hardware

Status: complete

## Landed

- Added practical Kernel, Execution, and Hardware subsystem guides.
- Added curated API indexes and Kernel/Execution and Hardware/devicetree architecture pages.
- Added the generated hardware alias how-to.
- Documented native handles, ISR boundaries, executor ownership, async buffer
  lifetime, RTIO, and driver-managed DMA explicitly.

## Evidence

- Six native fixture configurations passed with 45 test cases and no warnings.
- Warning-fatal documentation HTML passed.

## Decisions

Hardware compile evidence reuses the dedicated explicit and generated EDT
fixtures rather than adding a second synthetic board example. Hardware itself
remains outside the System; application Devices provide lifecycle integration.
