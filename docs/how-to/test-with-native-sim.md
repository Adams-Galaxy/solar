# Test With Native Sim

Give a Zephyr application a `sample.yaml` with
`native_sim/native/64` in `platform_allow`, then run:

```sh
west twister -T path/to/application -p native_sim/native/64 --inline-logs
```

Use a console harness for complete application examples or Ztest for focused
state and concurrency assertions. Add strict, disabled-subsystem, and policy
variants as separate test entries sharing the same source where possible.
