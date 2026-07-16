# Stage 01: Modern Core Result And Utilities

Status: landed

Landed date: 2026-07-15

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Baseline commit: `bed432f pre-solar-implementation`, with Stage 00 changes in
the active worktree

## 1. Objective

Land the small, allocation-free vocabulary that every later Solar subsystem can
share: a C++23 language contract, standard `std::expected` result transport, a
broad status boundary, compile-time strings, chrono vocabulary, and generic
type-list algorithms with no assumptions about the future component graph.

## 2. Specification Coverage

| Specification | Coverage | Notes |
| --- | --- | --- |
| `00a-modern-cpp-result-and-status.md` | language baseline, `Status`, `Result`, typed errors, explicit mapping, monadic composition, tests | complete for the shared core; subsystem errors land with their owners |
| `00-design-conventions.md` | modern C++, bounded storage, explicit errors, compile-time identity vocabulary | complete for Stage 01 |
| Stage 01 of `04-implementation-roadmap.md` | all declared scope and test categories | complete |

Assertion and fatal-policy implementations remain with the kernel fatal
boundary in Stage 05. Stage 01 introduces no competing host-oriented assertion
runtime.

## 3. Public Surface Landed

The active `<solar/solar.hpp>` aggregate now exposes `<solar/core.hpp>` and the
version header. Core provides:

- `solar::Status`, with positive errno-backed values;
- `solar::ok`, `solar::to_errno`, `solar::to_native_errno`, and
  `solar::status_from_errno`;
- `solar::Result<T, E = Status>` as a direct alias of
  `std::expected<T, E>`;
- `solar::Failure<E>` as a direct alias of `std::unexpected<E>`;
- forwarding `solar::fail(error)`;
- `solar::FixedString` and `solar::Name`;
- chrono duration aliases;
- generic `TypeList` concepts and size, count, containment, concatenation,
  uniqueness, filtering, transformation, indexed access, and iteration.

Representative use:

```cpp
enum class ReadError { Offline };

solar::Result<int, ReadError> read(bool ready)
{
    return ready ? solar::Result<int, ReadError>{42}
                 : solar::fail(ReadError::Offline);
}

auto doubled = read(true).transform([](int value) { return value * 2; });
```

There is no Solar Result wrapper and no compatibility `.status()` observer.

## 4. Runtime Ownership

Stage 01 introduces no runtime owner, mutable global state, thread, stack,
workqueue, timer, poll object, registration, or static initialization.

`Result`, `Status`, compile-time strings, chrono aliases, and type lists are
value/type vocabulary. Their operations do not require dynamic allocation.
Dynamic allocation in a result payload remains the payload owner's explicit
choice; it is not performed by `Result` itself.

## 5. Compile-Time Behavior

- Solar rejects pre-C++23 language modes with
  `SOLAR_DIAGNOSTIC_REQUIRES_CPP23`.
- Solar separately verifies `<expected>` and monadic expected support through
  `__cpp_lib_expected`.
- The language-mode check uses the value emitted by the supported GCC 14
  toolchain (`202100L`) and relies on the feature-test macro for the exact
  required library semantics.
- `Result<int>` cannot be implicitly constructed from `Status`; failures must
  use `solar::fail` or `std::unexpected`.
- Type-list algorithms accept the generic `TypeList` vocabulary only. Graph,
  category, catalog, and dependency semantics are deliberately absent.
- Fixed strings and result composition are usable in constant evaluation.

## 6. Error Behavior

`Status` remains a broad framework classification. It is not a universal domain
error enum. Subsystem errors remain typed until an explicit `status_of(error)`
mapping at a generic boundary.

Status values use positive errno values; native Zephyr-style return values are
obtained with `to_native_errno`. `status_from_errno` accepts either sign.
Unknown errno values map to `Status::Error`.

Specialized status errno fallbacks can share a platform errno. Reverse mapping
therefore gives canonical broad errors precedence. In particular, `EIO` always
maps to `Status::Error`, even on a platform where a specialized fallback also
uses `EIO`.

## 7. Zephyr Integration

Core uses no Zephyr API. This is deliberate: chrono-facing vocabulary remains
independent from kernel tick conversion and waits, which land in Stage 04.

The Zephyr test configuration selects:

- `CONFIG_CPP=y`;
- `CONFIG_STD_CPP23=y`;
- `CONFIG_REQUIRES_FULL_LIBCPP=y`;
- `CONFIG_CPP_EXCEPTIONS=n`;
- `CONFIG_CPP_RTTI=n`.

The same test application compiles for `native_sim/native/64` and Teensy 4.0.

## 8. Files Changed

### Added

- `include/solar/core/language.hpp`
- `tests/host/core.cpp`
- `tests/compile_fail/fixtures/requires_cpp23.cpp`
- `tests/compile_fail/fixtures/result_rejects_implicit_status.cpp`
- `tests/zephyr/core/CMakeLists.txt`
- `tests/zephyr/core/prj.conf`
- `tests/zephyr/core/src/main.cpp`
- `tests/zephyr/core/testcase.yaml`

### Reshaped

- `include/solar/core/status.hpp`
- `include/solar/core/fixed_string.hpp`
- `include/solar/core/type_list.hpp`
- `include/solar/core.hpp`
- `include/solar/solar.hpp`
- `tests/host/CMakeLists.txt`

### Removed

- The custom `Result<T>` and `Result<void>` classes.
- Positional component-list and graph-validation semantics from
  `core/type_list.hpp`.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `cmake -S . -B build/host -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` | host GCC, C++23 | pass | standalone core configuration |
| `cmake --build build/host -j2` | host GCC, C++23 | pass | public aggregate and core compile/link |
| `ctest --test-dir build/host --output-on-failure` | host | 6/6 pass | constexpr use, all four monadic operations, void and typed errors, move-only values/errors, errno mapping, type-list algorithms, no allocation during core result chains, language and conversion diagnostics |
| `west twister -T tests/zephyr -p native_sim/native/64 --inline-logs --outdir build/twister-core` | native Zephyr | 4/4 configurations and 6/6 cases pass | Stage 00 regression matrix plus C++23 core runtime under no exceptions/no RTTI |
| `west build -p always -b teensy40 tests/zephyr/core -d build/core-teensy40` | Teensy 4.0 / ARM GCC 14.3 | pass | target-toolchain compile and link |
| Teensy build size report | Ztest image | FLASH 44,876 B; RAM 5,760 B | recorded build image, not attributed solely to Solar core |
| `git diff --check` | Solar worktree | pass | patch whitespace sanity |

## 10. Local Decisions

### 10.1 C++23 diagnostic threshold

Problem: the supported GCC 14 toolchains report `__cplusplus == 202100L` in
C++23 mode rather than the final publication value `202302L`.

Constraints: Solar must reject older modes while requiring the complete
monadic `std::expected` contract actually used by the framework.

Options considered: require `202302L`; test only `__cplusplus`; or accept the
compiler's C++23 draft marker and separately require the exact library feature.

Decision: require `__cplusplus >= 202100L` and
`__cpp_lib_expected >= 202211L`.

Why: this accepts the workspace-supported Zephyr SDK without weakening the
required `std::expected` behavior.

Physical implementation: `include/solar/core/language.hpp` and the C++20
compile-fail fixture.

Tests/evidence: host C++20 fails with the stable Solar token; host, native, and
ARM C++23 builds pass all monadic operations.

Reversal path: raise the language threshold when the supported toolchain emits
the final standard value; retain the independent feature check.

### 10.2 Errno reverse-mapping precedence

Problem: not every errno symbol exists on every supported libc, so specialized
statuses need fallback values that can collide with broad errno values.

Constraints: reverse conversion must be deterministic and must not classify a
normal `EIO` as an unrelated specialized failure.

Options considered: abandon errno-backed values; add private invented values;
or preserve errno values and define canonical reverse precedence.

Decision: preserve errno-backed storage and map ordinary broad errors before
checking specialized fallback values.

Why: native conversion stays trivial while ambiguous reverse conversion remains
predictable.

Physical implementation: `include/solar/core/status.hpp`.

Tests/evidence: host constexpr and Zephyr runtime tests assert that both signs
of common errno values map correctly and `EIO` remains `Status::Error`.

Reversal path: a future stable non-errno status encoding can replace the enum
values while retaining the explicit conversion functions as the compatibility
boundary.

## 11. Specification Refinements

None. The result specification explicitly left the final status/errno
relationship and exact feature thresholds to implementation review.

## 12. Firmware And Host Impact

No firmware build is required at this roadmap gate. The target core test was
built for Teensy as direct toolchain evidence, but firmware migration remains
deferred to Stage 06.

Retained kernel headers are still pre-reform internals and are not exposed by
the active aggregate. `kernel/this_thread.hpp` contains an old `.status()` call;
it is deliberately assigned to the complete kernel reshape in Stages 04-05.

## 13. Known Limits And Deferred Work

- Subsystem typed errors and `status_of` mappings land with their owning
  subsystems.
- Kernel clocks, time points, deadlines, waits, assertions, and fatal behavior
  land in Stages 04-05.
- Descriptor identity and catalog semantics land in Stage 02.
- Type-list algorithms are an internal compile-time vocabulary, not a promised
  reflection framework.

## 14. Documentation Handoff

Public documentation should explain direct standard expected semantics,
failure construction, typed errors, explicit broad-status mapping, monadic
composition, errno sign conventions, and the distinction between allocation by
`Result` and allocation chosen by a payload type. `tests/host/core.cpp` is the
primary executable example source.

## 15. Closure Statement

Stage 01 is complete. The active core is C++23-native, allocation-free by
default, independent of Zephyr runtime ownership, verified on host, native
simulation, and the Teensy target toolchain, and contains no positional graph
assumptions. Stages 02 and 04 are now unblocked; serial implementation proceeds
to Stage 02.
