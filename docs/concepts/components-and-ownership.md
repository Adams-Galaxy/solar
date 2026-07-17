# Components And Ownership

Components are static types. They may own state in inline static members, expose
descriptors and dependencies, contribute declarations, and implement optional
lifecycle or health operations.

Solar defines four component categories:

- a **device** represents meaningful application hardware behavior;
- a **facility** is a passive capability or state owner;
- a **service** owns contained concurrent execution;
- an **executor** accepts work registrations on an owned target.

Categories describe lifecycle and runtime treatment. They do not require
inheritance. A task is a leaf execution registration, not a component.

Raw Zephyr devices and Solar hardware endpoints remain below application
devices. This separation keeps devicetree and driver mechanics out of the
System graph until an application type gives them lifecycle and domain meaning.
