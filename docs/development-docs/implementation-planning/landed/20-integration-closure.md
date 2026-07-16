# Stage 20: Full Integration And Closure

Status: landed

Landed date: 2026-07-16

Implementation repositories/branches:

- `/workspaces/solar`, `static_reform`;
- `/workspaces/ENMT301-RoboCup`, `dev`.

Relevant commits or change identifiers: complete working-tree reform; no commit
was requested.

## 1. Objective

Close the hard Solar architecture reform as one coherent Zephyr-native system,
verify the representative application and host boundary, remove remaining
obsolete architecture, inventory resource ownership, and leave a direct input
map for the separate public-documentation pass.

## 2. Integrated System

The firmware composition root now demonstrates the intended complete shape:

- one `System<Blueprint<...>>` and one `SOLAR_BIND_SYSTEM`;
- generated devicetree Hardware aliases and an application Device boundary;
- component-owned Parameters, Events, Metrics, Tasks, Remote Data, and Link
  contributions;
- Kconfig-selected built-in facilities and generated services;
- global `solar::boot()` and subsystem APIs;
- C++23 `Result` handling throughout;
- no application context, runtime System object, entry profile, Channel, or
  duplicated registry.

The strict native firmware compiles and links the same source spelling as the
default relaxed image.

## 3. Legacy Removal

The final source audit found no old positional System, entry Profile, Channel,
System context, old lifecycle include, or old task umbrella use. The only
remaining `snapshot()` API is Parameters' typed coherent multi-parameter read,
which is intentionally narrow rather than a system snapshot.

The application repository's obsolete `solar-remote-sdk` and
`solar-remote-cli` were removed. They implemented the abandoned CRC16/socket
protocol and could not interoperate with current firmware. Canonical host
protocol code now lives under Solar `tools/remote/solar_remote`, while each
final firmware ELF emits its exact manifest, constants, digest, and generated
`FirmwareClient`.

Spec 10 explicitly defers complete convenience SDK and manifest-rendered CLI
generation. This is an extension, not a stale compatibility tool.

## 4. Verification Evidence

| Gate | Result |
| --- | --- |
| last complete pre-Health Solar matrix | 83/83 configurations selected; 81 executed plus 2 target build-only; 259/259 cases; no warnings |
| Health closure | relaxed and strict rich suites pass 10/10 cases, availability and disabled pass; 59 scenarios of the broad matrix executed with zero failures before its previously-green tail was skipped under the progression rule |
| Supervisor closure | 5/5 configurations and 5/5 cases; 5/5 expected diagnostics; 5/5 headers; no warnings |
| host boundary | previous 57/57 CTest pass retained; all host C++23 targets rebuilt after final Blueprint, Health, and Supervisor changes |
| strict native firmware | clean configure, compile, link, Hardware generation, and Remote manifest generation pass |
| relaxed native firmware | passed at Stage 17; the repeated final build was deliberately stopped rather than waiting on a previously-green target |
| Teensy 4.0 firmware | compile/link passed at Stage 17 and was accepted without a redundant final rebuild |
| live Remote interoperability | generated strict-image client handshakes over native PTY, queries `DriveGain`, decodes `1.25`, and acknowledges the response |
| source/format audit | `git diff --check` passes in both repositories; legacy and root-include searches are clean |
| aggregate include check | full configured `solar/solar.hpp` syntax check passes in 2.215 seconds |

The user explicitly authorized assuming completion of remaining builds that had
already passed and skipping their repeated wait. This summary distinguishes
fresh execution from prior-green accepted evidence rather than claiming a final
monolithic rerun.

No physical board was exposed to this workspace during closure. Physical GPIO,
UART, bus, timing, and watchdog smoke execution therefore remains unevaluated;
native driver behavior, generated devicetree facts, and Teensy compile/link are
the available target evidence.

## 5. Representative Resources

### Strict native firmware

- text: 198,070 bytes;
- data: 27,976 bytes;
- BSS: 40,424 bytes;
- main stack: 4096 bytes;
- Zephyr system-workqueue stack: 4096 bytes;
- Remote service stack: 4096 bytes;
- generated Remote manifest: 895-byte canonical CBOR and 1,949-byte JSON.

### Teensy firmware

Stage 17 records 126,014 bytes text, 25,528 bytes data, and 36,589 bytes BSS,
corresponding to 151,544 bytes flash and 62,144 bytes RAM in Zephyr's report.

### Supervisor acceptance system

- native image: 115,413 bytes text, 8,982 bytes data, 29,516 bytes BSS;
- Supervisor policy/record state: 680 bytes for three rules and eight retained
  responses;
- generated service execution state: 4424 bytes including its 4096-byte stack;
- no heap, hidden workqueue, timer, or default watchdog provider.

Subsystem-specific capacities and ownership are recorded in Stages 01-19.
Kconfig owns global inclusion and ceilings; typed declarations own local
semantics and static shape.

## 6. Ownership Audit

- Kernel owns typed Zephyr primitives and explicit wrapper state.
- Lifecycle owns graph ordering and lifecycle records.
- Execution owns service threads, work state, admission, and containment.
- each facility owns one canonical bounded state derived from effective
  catalogs.
- Remote owns one generated service only when effective links require it.
- Health is passive and owns assessment truth; Supervisor is active and owns
  response and watchdog-policy state.
- Hardware wraps devicetree and drivers without becoming a System component;
  application Devices own semantic lifecycle boundaries.
- disabled subsystem frontends retain no service stack or runtime storage.

No hidden general executor, System object, heap-backed registry, universal
snapshot, or cross-subsystem duplicate truth was found.

## 7. Kconfig And Include Audit

Solar remains a Zephyr module with C++23 and Kconfig as its capability source.
Optional facilities and services have disabled fixtures or compile-time
availability diagnostics. Global capacities are Kconfig values; Blueprint
policy selects semantic behavior with accepted precedence.

Ordinary application component and board headers include Solar and public
Hardware headers, never the composition root. `app/robot.hpp` is the deliberate
root. Public subsystem scripts prove standalone inclusion; the final aggregate
check proves the complete configured umbrella.

## 8. Specification Mapping And Deferrals

Accepted design specifications 00 through 14 map to landed summaries 00 through
20 in `public-documentation-inputs.md`. Explicit accepted extensions include:

- controlled in-process reboot and generic component restart;
- complete convenience host SDK and CLI generation;
- additional transports and hardware families;
- advanced SoC-specific DMA helpers outside normal Zephyr drivers;
- broader RTIO use where it provides measured value;
- retained crash history across reboot;
- C++26 reflection after Zephyr and toolchain maturity.

No compatibility placeholder was retained for these extensions.

## 9. Documentation Handoff

`implementation-planning/public-documentation-inputs.md` links each public
topic to accepted design, landed behavior, executable examples, generated
references, and measurements. Public documentation can now be written without
reverse-engineering the reform history.

## 10. Closure

Stages 00 through 20 are complete. The static System architecture, Zephyr
integration, facilities, services, Remote, Hardware, Health, and Supervisor are
implemented and coherently integrated. The implementation pass ends here,
before the separate public-documentation pass.
