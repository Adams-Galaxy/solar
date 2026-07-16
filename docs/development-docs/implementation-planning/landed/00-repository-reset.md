# Stage 00: Repository Reset And Build Foundation

Status: landed

Landed date: 2026-07-15

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Baseline commit: `bed432f pre-solar-implementation`

## 1. Objective

Reduce Solar to a truthful, buildable C++23 Zephyr module foundation. Remove
the superseded architecture rather than repairing its transitional failures,
and establish the host, compile-fail, and native Zephyr test machinery used by
later stages.

## 2. Specification And Planning Coverage

Implemented the repository-reset contract from:

- `00-design-conventions.md` language, Zephyr, ownership, and naming baseline;
- `00a-modern-cpp-result-and-status.md` C++23 build prerequisite only;
- `14-integrated-architecture.md` hard-migration and Zephyr-native decisions;
- `01-repository-inventory.md` removal and reshape dispositions;
- Stage 00 of `04-implementation-roadmap.md`.

No final Result, System, lifecycle, execution, or subsystem API is claimed by
this stage.

## 3. Surface Landed

- Zephyr module registration through `zephyr/module.yml` and root CMake.
- C++23 requirement for standalone Solar consumers and Zephyr smoke tests.
- Foundation Kconfig:
  - `CONFIG_SOLAR`;
  - `CONFIG_SOLAR_STRICT_CATALOG_BINDING`, default off;
  - `CONFIG_SOLAR_DESCRIPTOR_STRINGS`;
  - `CONFIG_SOLAR_DIAGNOSTIC_DETAIL`;
  - `CONFIG_SOLAR_ALLOW_DYNAMIC_ALLOCATION`, default off.
- Minimal `<solar/solar.hpp>` exposing only `<solar/version.hpp>`.
- `solar::Version` and `solar::version` for foundation smoke verification.
- Host CTest scaffold.
- Generic expected-compile-failure harness requiring non-zero completion and a
  focused diagnostic token.
- Zephyr Ztest smoke matrix for relaxed, strict, and Solar-disabled Kconfig.

## 4. Removed Architecture

Removed without compatibility aliases:

- positional `System` and system-object orchestration;
- entry Profile, simulated entry, and Zephyr entry wrappers;
- old component Device tag helper and service aliases;
- Channels;
- old Tasks and executors;
- old lifecycle prototype and service execution storage;
- old contribution collector;
- old Events, Metrics, Logging, and Inspection implementations;
- old Remote codec, protocol, schema, service, and generated manifests;
- old Remote YAML schema;
- tests targeting the removed graph, lifecycle, shutdown, and task APIs;
- firmware `lib/solar_old` fallback symlink.

Git history remains the only archive.

## 5. Runtime Ownership

Stage 00 introduces no Solar runtime owner, thread, stack, timer, workqueue,
heap use, registration, or subsystem storage. The Zephyr smoke test uses only
the normal Ztest runtime selected by the test configuration.

## 6. Compile-Time And Kconfig Behavior

- Standalone CMake requests `cxx_std_23` on the Solar interface target.
- Zephyr smoke applications select `CONFIG_STD_CPP23` and full libcpp.
- Strict binding is a build-wide Kconfig choice but has no frontend
  implementation until Stage 03.
- Solar-disabled configuration remains buildable and does not expose dependent
  foundation symbols.
- No fallback configuration header is present.

## 7. Local Decisions

### 7.1 Compile-fail harness

Problem: the old Twister metadata used a `build_error` property rejected by
Zephyr 4.4, so the complete suite could not load.

Constraints: later stages need focused compile-fail diagnostics and must reject
both unexpected success and unrelated compiler failure.

Options considered:

- preserve unsupported Twister metadata;
- use `try_compile` for host-only cases;
- add a small command-agnostic harness that expects failure plus a token.

Decision: add `tests/compile_fail/expect_failure.py` and self-test it from CTest.

Why: it supports ordinary compiler commands now and can wrap complete Zephyr
build commands later without relying on unsupported Twister schema.

Physical implementation:

- `tests/compile_fail/expect_failure.py`;
- `tests/compile_fail/fixtures/expected_failure.cpp`;
- `tests/compile_fail/fixtures/unexpected_success.cpp`;
- `tests/host/CMakeLists.txt`.

Tests/evidence: one CTest requires the expected token; another uses CTest
`WILL_FAIL` to prove that an unexpected successful compile is rejected.

Reversal path: replace the command runner while preserving the token and
unexpected-success contracts; existing fixtures remain applicable.

### 7.2 Zephyr smoke verdict

Problem: a plain Zephyr `main()` built and returned zero, but Twister reported
an unknown test result because no recognized harness verdict was emitted.

Options considered: console-output matching, build-only tests, or Ztest.

Decision: use a minimal Ztest suite.

Why: later native runtime stages require Ztest, and this proves execution rather
than only compilation.

Physical implementation:

- `tests/zephyr/smoke/prj.conf` selects `CONFIG_ZTEST`;
- `tests/zephyr/smoke/src/main.cpp` defines one foundation Ztest;
- `tests/zephyr/smoke/testcase.yaml` owns the three Kconfig variants.

Reversal path: another Zephyr-supported harness may replace Ztest if it retains
runtime verdicts and the same configuration matrix.

## 8. Documentation State

The root Solar README now describes only the active C++23 module foundation and
test commands. `docs/README.md` marks the old documentation as pre-reform
reference. Full public documentation remains deferred.

## 9. Tests And Evidence

| Command | Result | Evidence |
| --- | --- | --- |
| `cmake -S . -B build/host -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` | pass | standalone C++23 configuration and Python harness dependency |
| `cmake --build build/host -j2` | pass | host foundation compiles and links |
| `ctest --test-dir build/host --output-on-failure` | 3/3 pass | host C++23 smoke and both compile-fail harness directions |
| `west twister -T tests/zephyr/smoke -p native_sim/native/64 --inline-logs --outdir build/twister-foundation-ztest` | 3/3 configurations and cases pass | relaxed, strict, and disabled Zephyr module runtime smoke |
| `git diff --check` in Solar | pass | patch formatting sanity |

The pre-reset firmware build failure was reproduced and attributed to the old
Remote service lacking the transitional `run(StopToken)` contract. It was not
repaired because both implementations were removed by policy.

## 10. Firmware Impact

No firmware build is required at the Stage 00 gate. The obsolete `solar_old`
fallback link was removed. Existing firmware remains intentionally incompatible
until the Stage 06 foundation integration gate.

## 11. Known Limits And Deferred Work

- The retained old core utility and kernel headers are not exposed by the
  umbrella as completed architecture.
- Stage 01 replaces Result/Status and core utilities.
- Stages 04-05 audit and reshape every retained kernel wrapper.
- No System or subsystem API exists yet.

## 12. Closure Statement

Stage 00 is complete: Solar is a minimal C++23 Zephyr module, all stage-owned
tests pass, unsupported old architecture and tests are gone, no compatibility
layer remains, and Stage 01 is unblocked.
