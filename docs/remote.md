# Remote

Remote is Solar's binary host/device control and introspection surface.

The first implementation is an active service:

```cpp
solar::services::Remote<Transport>
```

The service runs on its own Solar thread and reads/writes frames through a transport concept:

```cpp
Transport::write(bytes, len);
Transport::read();
Transport::available();
Transport::flush();
```

## Protocol

Remote frames are:

- little-endian;
- COBS-delimited;
- CRC16 protected;
- bounded by configured frame/payload sizes;
- identified by stable 32-bit method/topic/observable/type IDs.

There is no JSON control path. Human tooling should speak Remote through the SDK/CLI.

## Schema

Solar's built-in schema is generated from `.solar.yaml` files. Project schema can live under `firmware/remote/...` and be included by the same generator.

Components can contribute Remote vocabulary:

```cpp
using RemoteMethods = solar::remote::Methods<
    solar::remote::Method<
        solar::Name<"control.reset">,
        RequestType,
        ResponseType>>;
```

`Remote<Transport>` uses `System::Remote*Catalog` by default, so application and service vocabulary appears without a second manual registry.

## Current Core Methods

The current core surface includes:

- hello/capabilities;
- Remote summary;
- descriptor list methods;
- graph component listing;
- boot report.

Topics, observables, streaming, and app handlers are still being expanded.
