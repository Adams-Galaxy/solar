# Solar First Application

This is the canonical source for Solar's first-application guide. Build it with:

```sh
west build -b native_sim/native/64 examples/first-application
west build -t run
```

The `printk` line exists only to give the Zephyr sample harness a completion
signal. Solar does not provide a serial-text console or CLI; application
observability normally uses Logging and Remote.
