# `@solar/station`

Browser-first TypeScript client for Solar Station's binary-CBOR WebSocket API.

```ts
import { StationClient } from "@solar/station";

const station = new StationClient("ws://robot-host:47002/station");
await station.connect();

console.log(await station.status());
const unsubscribe = await station.subscribe("imu.euler", (event) => {
  console.log(event.value);
});
```

The first release intentionally doesn't reconnect automatically. A closed
connection rejects pending requests and discards subscriptions; callers decide
when to reconnect and resubscribe.
