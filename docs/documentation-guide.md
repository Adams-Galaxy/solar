# Documentation Guide

Solar uses two layers of documentation:

- Doxygen-style comments in public headers.
- Markdown concept documents under `solar/docs`.

## Header Comments

Add Doxygen comments when an API is:

- public;
- templated or concept-heavy;
- part of the system graph;
- part of an observability catalog;
- a lifecycle, threading, or Remote boundary.

Avoid comments that merely restate the function name. Prefer comments that explain ownership, lifetime, threading, static-vs-runtime behavior, and why the abstraction exists.

Good:

```cpp
/**
 * @brief Active service thread policy.
 *
 * Services own their run loop. Solar starts the RTOS thread and provides a
 * cooperative stop token; it does not poll services.
 */
```

Less useful:

```cpp
/// Starts the thing.
```

## Markdown Docs

Create or update a markdown doc when a concept crosses multiple headers or needs examples. Keep docs focused:

- one concept per file;
- examples should compile in spirit, even when abbreviated;
- state current limitations clearly;
- mention where future work should plug in.

When a subsystem changes shape, update:

1. the relevant subsystem doc;
2. `architecture.md` if the design philosophy changed;
3. this index if a new doc file was added.
