# Style And Public Contracts

- Follow `.clang-format`; use ASCII unless existing content requires otherwise.
- Prefer static typed ownership, bounded storage, explicit context, and
  `Result<T, E>` over hidden allocation or exceptions.
- Preserve native Zephyr semantics and expose native handles where useful.
- Use subsystem error domains with a `Status` projection; return failures with
  `fail<ErrorType>({...})`.
- Keep ordinary component headers independent from the composition root.
- Add abstractions only when they contribute typing, ownership, validation,
  bounded policy, or meaningful ergonomics.
- Public aggregate headers must be self-contained under valid Kconfig.
- Intentional invalid use needs a stable `SOLAR_DIAGNOSTIC_*` compile token.

Public names and behavior are contracts even before 1.0 documentation. This
project permits hard migration, but a change must update examples, tests,
generated reference, design/landed record, and compatibility claims together.
