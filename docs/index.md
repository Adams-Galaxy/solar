# Solar

Solar is a modular C++23 framework over Zephyr for typed hardware, bounded
facilities, generated interfaces, and optional static application composition.

Solar works with Zephyr's Kconfig, devicetree, kernel, drivers, workqueues, and
build system. Each facility works alone; `solar::system` can compose those same
modules without a runtime context object or global binding.

::::{grid} 1 2 2 2
:gutter: 2

:::{grid-item-card} Build your first application
:link: getting-started/index
:link-type: doc
Install Solar, own modules, compose an application, and boot on `native_sim`.
:::

:::{grid-item-card} Find a subsystem
:link: subsystems/index
:link-type: doc
Go directly to Kernel, execution, hardware, data facilities, Remote, or safety.
:::

:::{grid-item-card} Look up an API
:link: reference/index
:link-type: doc
Browse C++ declarations, Kconfig, devicetree, and generated artifacts.
:::

:::{grid-item-card} Understand the architecture
:link: architecture/index
:link-type: doc
Trace compile-time construction, runtime ownership, and Zephyr boundaries.
:::

::::

```{toctree}
:maxdepth: 2
:hidden:

getting-started/index
tutorials/index
how-to/index
concepts/index
subsystems/index
reference/index
architecture/index
development/index
examples/index
```
