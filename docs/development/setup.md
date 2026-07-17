# Contributor Setup

Use a Zephyr 4.4 west workspace with Solar present as a module. Install the
Zephyr SDK/toolchain, Python environment, CMake, Ninja, Doxygen, and Graphviz.

```sh
python -m pip install -r docs/requirements.txt
python -m pip install -r tools/remote/requirements.txt
cmake -S . -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Configure one native Zephyr fixture before editing target-dependent code:

```sh
west twister -T tests/zephyr/kernel_core -p native_sim/native/64 --inline-logs
```

Keep C++23, exceptions-off, RTTI-off, and warning-as-error settings aligned
with repository fixtures. Do not introduce a fallback `config.hpp`; Zephyr
Kconfig is Solar's firmware configuration source.
