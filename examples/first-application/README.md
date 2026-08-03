# Solar First Application

This is the smallest composed application. Its module remains independently
usable; the optional static System only owns lifecycle ordering.

```sh
west build -b native_sim/native/64 examples/first-application
west build -t run
```

The `printk` line is only the sample harness completion signal.
