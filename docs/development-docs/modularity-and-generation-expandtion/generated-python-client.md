# Generated Python Application Clients

Date: 2026-08-03

Status: accepted package boundary; initial API validated and locked

## 1. Objective

An application-specific Solar client should make a remote embedded interface
feel like a native asynchronous Python API:

```python
await robot.parameters.drive.kp.set(1.25)

report = await robot.actions.lidar.self_test()

async with robot.streams.imu.euler.subscribe(frequency=20) as samples:
    async for frame in samples:
        print(frame.value.roll)
```

This API is generated from the same application contract used by the firmware.

## 2. Package Boundary

There are two Python packages with different responsibilities.

### 2.1 Standard `solar_remote` SDK

The standard SDK owns application-independent Remote behaviour:

- framing, envelopes, correlation, fragmentation, and cancellation;
- protocol handshake and negotiation;
- sessions and session epochs;
- manifest retrieval, parsing, and validation;
- dynamic schemas and endpoint access;
- encode and decode;
- subscriptions, input credit, and backpressure;
- compatibility checks; and
- common protocol and schema exceptions.

In accordance with the accepted Python host-stack redesign, `solar_remote`
operates on an already-open asynchronous byte channel. Physical USB and TCP
selection, discovery, and reconnect supervision remain Station responsibilities.

The SDK remains fully useful with previously unknown firmware through dynamic
manifest access.

### 2.2 Generated application package

Each application can generate a separate package, for example:

```text
distribution: robot-solar-client
import:       robot_solar
dependency:   solar-remote >=0.4,<0.5
```

It owns:

- application dataclasses and enumerations;
- typed endpoint namespaces;
- application-specific exceptions;
- expected protocol and interface metadata;
- required capability declarations;
- optional exact shipment identity; and
- typed wrappers over a standard SDK session.

It does not implement framing, transport, reconnect, manifest parsing, or
codec infrastructure.

## 3. Binding Flow

Station or another host supplies an open channel. The standard SDK starts and
validates the protocol session. The generated client binds to that session:

```python
channel = await station.open_robot_channel()
session = await solar_remote.open(channel)

robot = await robot_solar.Robot.bind(session)
```

Station may expose the already-active session directly:

```python
robot = await robot_solar.Robot.bind(station.robot.session)
```

The generated package may provide convenience helpers accepting an injected
channel or session factory, but it must delegate physical connection ownership
to the host stack rather than introduce another transport implementation.

## 4. Generated API Shape

Dotted Solar names produce nested Python namespaces:

```text
drive.kp          -> robot.parameters.drive.kp
imu.euler         -> robot.streams.imu.euler
lidar.self-test   -> robot.actions.lidar.self_test
navigation.pose   -> robot.data.navigation.pose
```

The generator must detect source names that would produce ambiguous or invalid
Python paths.

### 4.1 Parameters and data

```python
kp: float = await robot.parameters.drive.kp.get()
update = await robot.parameters.drive.kp.set(1.25)
```

Generated endpoint wrappers expose only declared operations. Update responses
should preserve useful protocol facts such as effective value, revision, or
clamping when those are part of the contract.

### 4.2 Actions

Actions are callable and strongly typed:

```python
report: DiagnosticReport = await robot.actions.lidar.self_test()

result = await robot.actions.navigation.plan(
    PlanRequest(destination=Pose(x=1.0, y=2.0))
)
```

Declared application errors map to typed Python exceptions or a generated
typed error model. The prototype will choose the final Python convention.

### 4.3 Output streams

Continuous consumption uses an async context manager and iterator:

```python
async with robot.streams.imu.euler.subscribe(frequency=50) as samples:
    async for frame in samples:
        print(frame.value)
```

The returned frame retains receipt time, sequence/session identity, endpoint
metadata, and loss information supplied by the standard SDK.

### 4.4 Input streams

Input streams use a symmetrical scoped producer:

```python
async with robot.streams.drive.command.open(frequency=50) as commands:
    await commands.send(
        DriveCommand(throttle=0.5, differential=-0.1)
    )
```

The standard SDK remains responsible for credit and protocol flow control.

### 4.5 Models

Initial generated models use ordinary typed Python constructs:

```python
@dataclass(frozen=True, slots=True)
class DriveCommand:
    throttle: float
    differential: float
```

Enums use `IntEnum` or Solar's open-enum representation. Generated source is
fully annotated for IDEs, Pyright, and mypy. A heavyweight validation library
is not required by default.

Every endpoint object also retains descriptor access for tooling:

```python
robot.parameters.drive.kp.id
robot.parameters.drive.kp.descriptor
robot.parameters.drive.kp.supported
```

## 5. Dynamic And Generated Access Coexist

The generated client is an ergonomic typed view over the standard SDK session.
It does not remove dynamic access.

Unknown additive firmware capabilities remain reachable through the standard
SDK manifest facade. This supports generic Station commands, inspection,
forward compatibility, and debugging.

Generated and dynamic calls must share one session, subscription router, codec
registry, and reconnect/session epoch. They must not open parallel logical
connections to the robot.

## 6. Validation And Compatibility

Three identities are distinct:

1. protocol identity determines whether communication is possible;
2. interface identity describes schemas and capabilities; and
3. build identity identifies one firmware build or shipment.

Binding validates them in that order.

The generated client will support policies equivalent to:

- exact interface: effective manifest digest must match;
- compatible interface: all required generated contracts remain compatible;
- dynamic extension: bind the known interface while retaining access to
  additional compatible capabilities;
- any build: permit any build carrying the accepted interface; and
- exact build: require the declared firmware shipment identity.

The final names and default policy are prototype decisions. The leading default
for a client generated from a final firmware artifact is exact interface with
any build carrying that interface. Deployment tools may additionally require
the exact build.

Binding failures must report semantic differences rather than only two hashes:

```text
Generated client cannot bind to firmware:

  expected interface: 84c012...
  received interface: 1b76a3...

  breaking differences:
    - drive.command.throttle changed from float32 to float64
    - action lidar.self-test is absent

  additive differences:
    - stream navigation.velocity was added
```

## 7. Generation Source

IDL generation creates common Python models and namespace structure. The final
firmware manifest determines the exact endpoints and capabilities included in a
shipment package.

The generated package embeds at least:

- supported Remote protocol version;
- exact effective interface digest;
- required schema and capability records;
- generated-package version; and
- optional build/shipment identity.

It must verify that its generated assumptions match the manifest parsed by the
standard SDK before exposing an operational `Robot` facade.

## 8. Distribution

The firmware shipment may contain a ready-to-install wheel. Generated packages
declare a compatible standard SDK version range and contain no platform-specific
transport dependencies.

The same package should be usable by:

- Station;
- standalone diagnostic scripts with an injected channel;
- notebooks;
- test harnesses and simulators; and
- future UI or NATS integration processes operating through Station.

## 9. Non-Goals

The generated package will not:

- duplicate `solar_remote` protocol code;
- own USB or TCP discovery and reconnection;
- require firmware to match a build ID when its interface is accepted, unless
  explicitly requested;
- hide descriptor or dynamic access needed by tooling; or
- generate synchronous wrappers around an asynchronous protocol core.
