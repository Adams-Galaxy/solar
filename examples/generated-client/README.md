# Generated Python Client

Generate an application package from the production fixture, then use it with
any `solar_remote` session:

```sh
python tools/generate_solar.py \
  --project tests/application_fixture/solar.project.yaml \
  --output build/generated-example \
  --lock build/generated-example/solar.interface.lock \
  --update-lock
PYTHONPATH=build/generated-example/python:sdk/python/src \
  python examples/generated-client/client.py
```

The generated package owns application names and types. `solar_remote` owns
transport, framing, binding policy, reconnect, and session behavior.
