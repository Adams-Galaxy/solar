# Solar Remote Python SDK

`solar-remote` is the reusable async host runtime for Solar Remote. It supports
the simulator TCP stream and a dedicated USB CDC serial stream, resolves the
connected firmware's exact manifest digest, and uses either generated models or
the same manifest dynamically.

```python
from solar_remote import connect

async with connect("tcp://127.0.0.1:47000") as remote:
    sample = await remote.query("imu.euler")

    subscription = await remote.stream("imu.euler", frequency=10)
    async for sample in subscription:
        ...
```

Serial uses pySerial through an owned worker thread; public APIs remain
asyncio-native. Baud 134 is rejected because it is reserved for the explicit
Teensy reboot mechanism.

Firmware-specific `models.py` and `client.py` files are emitted by
`tools/remote/generate_manifest.py`. Generic clients can use `AsyncSession`
directly with a fetched or cached manifest.

Applications that need one persistent connection shared between multiple
processes should use the sibling `solar-station` package under
`station/python`. `solar-remote` remains the direct transport/session SDK;
Solar Station builds connection supervision, fan-out, logs, and persistence
on top of it.
