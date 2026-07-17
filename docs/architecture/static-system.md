# Static System Architecture

Solar has one integrated firmware System without a runtime System object. The
user names `solar::System<Blueprint>` as a type. Its effective catalogs,
component graph, facilities, services, and state slots are static type-owned
storage.

```{graphviz}
digraph static_system {
  rankdir=LR;
  node [shape=box];
  "Blueprint" -> "Normalization" -> "System";
  "System" -> "Catalogs";
  "System" -> "Component graph";
  "System" -> "Static state owners";
  "Binding" -> "Global frontends";
  "System" -> "Binding";
}
```

This shape avoids passing a context or object reference through every module.
Application modules include the project types they depend on directly. The
composition root includes all components once, declares the Blueprint, and
binds the resulting System.

Static access is not permissionless shared mutation. Each subsystem still has
one canonical state owner and synchronization contract.
