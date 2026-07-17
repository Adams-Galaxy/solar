# Supervised Device Example

This native sample injects an IMU connection fault, records Health evidence,
attempts component recovery, enters an application safe state after recovery
failure, and verifies that a healthy supervision cycle fed the watchdog
provider.

```sh
west twister -T examples/supervised-device -p native_sim/native/64 --inline-logs
```
