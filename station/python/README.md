# Solar Station

`solar-station` is the persistent, project-neutral host service for Solar
Remote. One `StationHost` owns the robot connection, firmware manifest, global
stream configuration, optional console capture, SQLite persistence, and local
client fan-out. Any number of `StationClient` instances can issue private
requests and selectively consume shared sources.

It deliberately contains no terminal UI and has no Argon dependency.

## Station modules

Station modules are installed Python entry points for shared, long-lived
project services:

```toml
[project.entry-points."solar_station.modules"]
"project.controls" = "project.controls:ControlsModule"
```

Modules implement `StationModule`, receive a `StationModuleContext`, and expose
request methods, a status snapshot, and events. Generic IPC operations list,
enable, disable, and call installed modules. Clients subscribe to
`module.NAME` exactly like other Station sources:

```python
modules = await station.list_modules()
await station.enable_module("project.controls")
status = await station.module_request("project.controls", "status")

async with await station.subscribe("module.project.controls") as events:
    async for event in events:
        print(event["value"])
```

Station itself remains project-neutral. The module implementation and its
dependencies live in the application package that registered the entry point.
Modules are loaded into the daemon, so installing or updating module code
requires restarting Station.

## Install

During Solar development:

```bash
python -m pip install -e sdk/python
python -m pip install -e "station/python[test]"
```

Run a host with automatic transport discovery:

```bash
solar-stationd
```

Automatic selection prefers a directly attached USB device, then an active
Solar Bridge at `bridge.local`, then the local simulator. Remote and console
always come from the same selected source. Override or disable bridge discovery
with:

```bash
solar-stationd --bridge robot-bridge.local
solar-stationd --bridge none
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

`status()` includes a `discovery` object with the selected source and latest
bridge observation. A reachable bridge reports its mode, device state,
generation, serial number, and availability separately, so clients can
distinguish an unreachable Pi from a Pi whose Teensy is absent or in
maintenance.

## Python API

```python
from solar_station import StationClient

async with StationClient() as station:
    gain = station.robot.data["drive.gain"]
    current = await gain.get()
    await gain.set(type(current)(value=1.25))
    await station.robot.actions["imu.calibrate"]()

    imu = station.sources["imu.euler"]
    await imu.configure(frequency=100)
    async with await imu.subscribe() as frames:
        async for frame in frames:
            print(frame.value.yaw)

    async with station.inputs["drive.control.differential"].open(
        frequency=50
    ) as drive:
        await drive.send(
            DifferentialCommand(throttle=0.4, differential=-0.1),
            timeout=0.5,
        )

    latency = await station.connection.ping()
    print(latency.round_trip_ms)
```

`configure_stream()` controls the single robot-side source. `watch()` only
controls consumption by that client. Query and action responses are routed
only to their requesting client.

Station owns inbound producers on behalf of the local client that opened each
one. Numeric handles remain private IPC details behind `InputProducer`.
Exclusive firmware replacement closes the producer and emits an `input_closed`
server event. Client disappearance, robot disconnect, or reflash closes its
affected producers; Station never replays control values or reopens them after
reconnect.

The raw request methods remain temporarily available to project tooling while
it migrates. New application code should use `robot`, `sources`, `inputs`,
`logs`, `recordings`, and `connection`.

Live fan-out does not wait for SQLite. Persistence enters a bounded queue and a
background writer commits batches by size or deadline. Log reads and recording
boundaries flush admitted events; `status()` reports `persistence_dropped` if
the writer cannot keep up.

The initial listener is canonical CBOR over a length-prefixed Unix stream.
The request/event model is transport-neutral so a binary-CBOR WebSocket
listener can expose the same service contract later.

## Process API

Applications may own and run `StationHost` directly with a `StationConfig`.
`TransportDiscovery`, `BridgeStatus`, `DiscoverySnapshot`, and `probe_bridge`
are also public for applications that want to inspect discovery without
starting a host. The `solar-stationd` entry point is merely the default process
launcher around the same public host API.

The default environment variables are `SOLAR_STATION_SOCKET` and
`SOLAR_STATION_DATABASE`. Legacy `STATION_SOCKET` and `STATION_DATABASE`
overrides remain accepted during migration. If the generic database does not
yet exist, an existing `robocup-station` database is copied once with SQLite's
backup API so recordings and logs are preserved under the new generic path.
