# Data Pipeline Example

This runnable Zephyr sample combines Bus, Parameters, Events, Metrics, and
Logging. Build it through Twister:

```sh
west twister -T examples/data-pipeline -p native_sim/native/64 --inline-logs
```

The `printk` line is only the Zephyr test-harness completion marker. Application
records use Solar Logging.
