# Testing

Choose evidence by the behavior changed.

| Change | Minimum evidence |
| --- | --- |
| Host-safe core/catalog/protocol | Host CMake build and CTest |
| Public Zephyr header | Header self-containment checker |
| Compile-time diagnostic | Focused compile-fail checker with stable token |
| Kernel/subsystem runtime | Focused native Twister application |
| Devicetree/generator | Controlled EDT fixture and deterministic output check |
| Hardware driver integration | Native emulator plus real target compile/link |
| Remote wire/client | C++ and Python vector/host tests |
| Documentation | Warning-fatal HTML, linkcheck, and affected examples |

Prefer focused Twister roots while developing. Run the complete integration
matrix before a release or broad architecture merge. A compile-only target gate
does not replace a physical hardware test.

Canonical documentation examples are tests, not illustrative copies. Keep
their named source regions stable and update prose only after the source passes.
