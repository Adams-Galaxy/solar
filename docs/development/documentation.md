# Writing Documentation

## Build

```sh
cmake -S docs -B build/docs
cmake --build build/docs --target docs-html
cmake --build build/docs --target docs-linkcheck
```

Both targets are warning-fatal and nitpicky. Fix broken references at their
source; do not add broad warning suppression.

## Page contract

Subsystem pages lead with the common path, then declaration/composition,
ownership, context, capacity/backpressure, errors, configuration, integration,
testing, and limits. Architecture pages explain canonical mechanisms without
making them prerequisites for ordinary use.

Use MyST `{doc}` links for internal navigation. Include executable code from
canonical examples with `literalinclude` and paired named region comments.
Avoid snippets that silently omit error handling or context requirements.

Stable concrete declarations may use Breathe. Curate heavily templated APIs as
authored signatures until Doxygen groups can render them without internal
normalization details or invalid cross-references.

Kconfig reference is generated. Edit `zephyr/Kconfig` help, not the generated
page. Development planning and landed summaries stay excluded from the public
site.
