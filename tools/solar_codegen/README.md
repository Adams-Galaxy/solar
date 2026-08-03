# Solar interface compiler

Run the production compiler with `python -m solar_codegen` (with `tools` on
`PYTHONPATH`) or through `tools/generate_solar.py`:

```sh
tools/generate_solar.py \
  --project solar.project.yaml \
  --output build/generated \
  --lock solar.interface.lock \
  --update-lock
```

The command returns `0` on success and `2` for authored, lock, or filesystem
errors. It writes normalized IR, compatibility data, an effective-manifest
expectation, C++ headers, Python sources, and a depfile below `--output`.
Generated files never need to be written into the source tree. Omit
`--update-lock` in verification builds so an existing identity lock remains
authoritative. Use `--version` to inspect the compiler version.
