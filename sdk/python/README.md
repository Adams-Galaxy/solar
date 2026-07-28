# Solar Remote Python SDK

`solar-remote` is the transport-independent async protocol runtime for Solar
Remote. It negotiates an already-open ordered byte channel, resolves the exact
firmware manifest, and exposes indexed descriptors, dynamic Python models, typed
endpoint operations, streams, inbound controls, and protocol ping.

```python
from solar_remote import AsyncSession

async with AsyncSession(open_channel) as remote:
    robot = remote.robot()
    sample = await robot.data["imu.euler"].get()

    async with await robot.data["imu.euler"].subscribe(
        frequency=10
    ) as frames:
        async for frame in frames:
            print(frame.value)

    async with robot.data[
        "drive.control.differential"
    ].open_input(
        frequency=50,
    ) as control:
        await control.send({"throttle": 0.4, "differential": -0.1})
```

`InboundStream` resolves its type from the runtime manifest, opens explicitly,
waits asynchronously for token-specific credit, and wakes blocked senders when
it is closed or replaced. It exposes `token`, `effective`, `credit_window`,
`closed`, `closure_reason`, and `wait_closed()`. A disconnected stream is not
reopened automatically.

The channel has only `receive(maximum)` and `send(data)` operations. Physical
serial/TCP opening, device discovery, reconnect policy, and channel closure are
owned by Solar Station. Consequently this package does not depend on pySerial.

Firmware-specific `models.py` and `client.py` files are emitted by
`tools/remote/generate_manifest.py`. Generic clients can use `AsyncSession`
directly with a fetched or cached manifest.

Host applications normally use the sibling `solar-station` package under
`station/python`. It owns the physical connection, reconnect supervision,
multi-process fan-out, inputs, console logs, and persistence while reusing this
package for protocol and manifest interpretation.
