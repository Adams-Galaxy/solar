# Stage 21: Error And Result Refinement

Status: landed

Landed date: 2026-07-17

Implementation repository/branch: `solar` / `static_reform`

## 1. Objective

Unify Solar's fallible API around C++23 `std::expected`, make `solar::Error` the
default concrete error, retain rich subsystem errors, and demote `Status` to a
cross-domain classification rather than a success/failure return carrier.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `docs/development-docs/error-and-result-refinement.md` | 1-14 | Complete |

No required clauses were deferred.

## 3. Public Surface Landed

`solar/core/status.hpp` now provides:

- `solar::Error { Status status; int native; }`;
- `solar::ErrorType` with a non-throwing `status_of(error)` projection;
- `solar::Result<T, E = solar::Error>` as a transparent `std::expected` alias;
- `solar::ResultType` and `solar::VoidResult` concepts;
- explicit `solar::fail<ErrorType>(error)` failure construction;
- `solar::error_from_errno()` for the default native boundary.

The normal shape is:

```cpp
solar::Result<void> initialize()
{
    return solar::fail<solar::Error>({.status = solar::Status::NotReady});
}
```

All public Solar fallible operations, lifecycle hooks, execution callbacks,
kernel wrappers, subsystem protocols, and test links now use Result. Rich
subsystem errors remain the declared error type where domain detail belongs.

## 4. Runtime Ownership

No runtime owner, thread, timer, work item, queue, or heap allocation was added.
The default failure payload grew from `Status` to the bounded `solar::Error`
value. Existing subsystem runtime ownership is unchanged.

## 5. Compile-Time Behavior

`Status` cannot satisfy `ErrorType`, so `Result<T, Status>` is rejected. Error
types must provide a visible, exact, `noexcept` projection to `Status`; types
with a `Status status` member receive Solar's generic projection, while custom
enums and layouts use ADL.

Strict and relaxed system binding behavior is unchanged. Both modes compile
through the migrated contracts.

## 6. Error And Availability Behavior

Success is represented only by a populated Result. Generic infrastructure
records `status_of(error)` at deliberately lossy boundaries. Subsystem APIs
preserve their complete typed errors. Generic lifecycle, execution, and
infrastructure callback surfaces normalize to `solar::Error` where they do not
own a domain-specific error contract.

There is no compatibility `fail(Status)`, implicit subsystem conversion,
exception hierarchy, universal variant, automatic logging, or heap-backed
type erasure.

## 7. Zephyr Integration

Kernel and hardware wrappers convert Zephyr integer results once at the Solar
boundary and retain the native value in `solar::Error::native` where relevant.
Required Zephyr callback signatures remain unchanged. ISR and thread-context
rules are unchanged by this refinement.

## 8. Files Changed

### Added

- `docs/development-docs/error-and-result-refinement.md`
- `tests/compile_fail/fixtures/result_requires_error_projection.cpp`
- this landed summary

### Reshaped

- `include/solar/core/status.hpp`
- kernel, lifecycle, execution, bus, parameters, events, metrics, logging,
  Remote, inspection, hardware, health, Supervisor, catalog, and frontend APIs
- corresponding host and Zephyr fixtures
- Remote declarations, including the default `solar::Error` schema

### Removed

- status-only fallible Solar return conventions
- implicit failure-helper deduction at Solar call sites

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `cmake --build build/error-refinement` | host, C++23 | Pass | Host API and generation build |
| `ctest --test-dir build/error-refinement --output-on-failure` | 58 host and compile-fail tests | Pass | Core behavior, strict/relaxed policy, and negative contracts |
| `west twister -T tests/zephyr -p native_sim/native/64 --build-only ...` | 90 configurations | Pass after focused rebuild of five early failures | Repository-wide Zephyr instantiation |
| `west twister -T tests/zephyr -p native_sim/native/64 --test-only ...` | 90 configurations, 277 cases | 90/90 configurations pass; 276 cases pass and one generated hardware case reports no explicit status | Native runtime behavior |

Twister's sole warning is the existing
`solar.hardware.foundation.native.hardware_foundation.generated_board_aliases_compile`
case reporting no explicit status. It is not an error and no configuration
failed.

## 10. Specification Refinements

Observed contract: namespace-local `status_of` overloads can hide a parent
namespace overload at cross-subsystem adapter boundaries.

Evidence: the event-to-log adapter attempted to classify `solar::log::Error`
inside `solar::events`.

Accepted change: cross-domain projections use qualified
`solar::status_of(error)`; same-domain projections may use ADL naturally.

Specifications updated: implementation record only; public semantics are
unchanged.

Verification added: logging integration instantiates and executes the adapter.

## 11. Firmware And Host Impact

This is a hard migration with no source compatibility promise. Application
components must return Result from fallible hooks and inspect errors through
`.error()` and `status_of`. No firmware repository migration was included in
this Solar-only refinement.

## 12. Known Limits And Deferred Work

- Error text formatting and public error documentation belong to the later
  documentation pass.
- Remote serialization remains opt-in for custom errors; only `solar::Error`
  receives a built-in schema here.
- Error cause chains and coroutine-aware propagation remain deliberately out of
  scope.

## 13. Documentation Handoff

Public documentation should lead with `Result<void>`, typed subsystem Results,
explicit `fail<ErrorType>({...})`, monadic composition, and classification via
`status_of`. It must explain that returning an error does not automatically log
or publish it, and that lossy conversion belongs at explicit ownership
boundaries.

## 14. Closure Statement

Solar now has one repository-wide fallible-return convention backed directly by
C++23 `std::expected`. Core, all subsystem surfaces, strict and relaxed builds,
and native execution agree on the same model, unblocking public documentation.
