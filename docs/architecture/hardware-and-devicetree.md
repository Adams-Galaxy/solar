# Hardware And Devicetree

```{graphviz}
digraph hardware_layers {
  rankdir=TB;
  "DTS, overlays, bindings, pinctrl" -> "Resolved EDT";
  "Resolved EDT" -> "Zephyr devices and drivers";
  "Resolved EDT" -> "Solar generated selectors";
  "Zephyr devices and drivers" -> "solar::hardware wrappers";
  "Solar generated selectors" -> "solar::hardware wrappers";
  "solar::hardware wrappers" -> "Project board aliases";
  "Project board aliases" -> "Application Devices";
  "Application Devices" -> "Solar System graph";
}
```

Generation runs after Zephyr resolves devicetree and reads `edt.pickle` through
Zephyr's Python model. Output contains compile-time facts only; it does not
copy EDT into runtime storage. Wrappers retain native handles and add typed
roles, bounded values, ownership vocabulary, and error mapping.

Hardware endpoints stop below the System graph. A Device is the place where
hardware acquires application meaning, lifecycle, health, and recovery policy.
