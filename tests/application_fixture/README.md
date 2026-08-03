# Solar Generated Application Fixture

This fixture exercises Solar's production ownership, composition, IDL
generation, and generated-client pipeline.

Generate the fixture manually for IDE refresh with:

```sh
python tools/generate_solar.py \
  --project tests/application_fixture/solar.project.yaml \
  --output build/application-generated \
  --lock build/application-generated/interface.lock.json \
  --update-lock
```

Normal CMake and Zephyr builds run generation before compiling code that
includes the generated application headers.
