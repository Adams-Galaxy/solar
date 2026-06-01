# Events

Events are typed facts about meaningful runtime occurrences. They are not log lines. Logs are diagnostic text; events are structured state transitions or noteworthy system facts.

Components contribute event descriptors:

```cpp
using Events = solar::events::List<
    solar::events::Event<solar::Name<"motor.fault">, void, solar::events::Severity::Error>>;
```

The event facility owns a fixed ring history and optional direct sinks:

```cpp
using Events = solar::events::Facility<
    solar::Name<"events">,
    64,
    solar::events::Sinks<UsbEventSink>>;
```

The default graph spelling is:

```cpp
solar::facilities::Events
```

## Emitting

```cpp
Events::emit<MotorFault, solar::Name<"left_motor">>(code, detail);
auto status = Events::try_emit<MotorFault, solar::Name<"left_motor">>(code, detail);
```

The current compact runtime record contains:

- timestamp
- sequence
- event ID
- severity
- name
- source
- integer value
- integer detail

The descriptor keeps a `Payload` type hook so richer typed payload encoding can be added without changing catalog ownership.

## Sinks

Event sinks mirror logging sinks:

```cpp
solar::events::Sink<Name, Writer, Format, Filter>
```

Use sinks for serial text, memory buffers, persistent storage, or future Remote event streams. The event facility itself is passive; it has no worker thread.
