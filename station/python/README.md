# Solar Station

`solar-station` is the persistent, project-neutral host service for Solar
Remote. One `StationHost` owns the robot connection, firmware manifest, global
stream configuration, optional console capture, SQLite persistence, and local
client fan-out. Any number of `StationClient` instances can issue private
requests and selectively consume shared sources.

It deliberately contains no terminal UI and has no Argon dependency.

## Install

During Solar development:

```bash
python -m pip install -e sdk/python
python -m pip install -e "station/python[test]"
```

Run a host against automatic USB/simulator discovery:

```bash
solar-stationd
```

Or select the simulator or explicit transports:

```bash
solar-stationd --sim
solar-stationd \
  --remote serial:///dev/cu.usbmodemREMOTE \
  --console serial:///dev/cu.usbmodemCONSOLE
```

The launcher only configures and runs the service. Robot operations belong to
client applications.

## Python API

```python
from solar_station import StationClient

async with StationClient() as station:
    gain = await station.get("drive.gain")
    await station.set("drive.gain", 1.25)
    await station.call("imu.calibrate")

    await station.configure_stream("imu.euler", frequency=100)
    async with await station.watch("imu.euler") as samples:
        async for event in samples:
            print(event["value"])
```

`configure_stream()` controls the single robot-side source. `watch()` only
controls consumption by that client. Query and action responses are routed
only to their requesting client.

The initial listener is canonical CBOR over a length-prefixed Unix stream.
The request/event model is transport-neutral so a binary-CBOR WebSocket
listener can expose the same service contract later.

## Process API

Applications may own and run `StationHost` directly with a
`StationConfig`. The `solar-stationd` entry point is merely the default process
launcher around the same public host API.

The default environment variables are `SOLAR_STATION_SOCKET` and
`SOLAR_STATION_DATABASE`. Legacy `STATION_SOCKET` and `STATION_DATABASE`
overrides remain accepted during migration. If the generic database does not
yet exist, an existing `robocup-station` database is copied once with SQLite's
backup API so recordings and logs are preserved under the new generic path.
