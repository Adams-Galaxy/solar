# Remote Control Example

The firmware sample uses a deterministic in-memory link and proves that the
Remote service opens a link, starts a session handshake, and accepts typed
stream publication. Its build emits the manifest and generated Python client.

```sh
west build -b native_sim/native/64 examples/remote-control
python examples/remote-control/host_demo.py --generated build/solar/remote
```

A real project replaces the in-memory link with its UART, USB CDC, TCP, or
other asynchronous Link while retaining the endpoint and host APIs.
