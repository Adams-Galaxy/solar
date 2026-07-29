import { decode, encode } from "cborg";
import { describe, expect, it } from "vitest";

import {
  StationClient,
  StationDisconnectedError,
  StationProtocolError,
  type WebSocketLike,
} from "./index.js";

class FakeWebSocket implements WebSocketLike {
  binaryType: BinaryType = "blob";
  onclose: ((event: CloseEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  onmessage: ((event: MessageEvent) => void) | null = null;
  onopen: ((event: Event) => void) | null = null;
  readyState = 0;
  sent: Record<string, unknown>[] = [];

  open(): void {
    this.readyState = 1;
    this.onopen?.(new Event("open"));
  }

  close(): void {
    if (this.readyState === 3) return;
    this.readyState = 3;
    this.onclose?.(new CloseEvent("close"));
  }

  send(data: ArrayBufferLike | ArrayBufferView | Blob | string): void {
    if (typeof data === "string" || data instanceof Blob) {
      throw new Error("expected binary CBOR");
    }
    const bytes =
      ArrayBuffer.isView(data)
        ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
        : new Uint8Array(data);
    this.sent.push(decode(bytes) as Record<string, unknown>);
  }

  receive(message: Record<string, unknown>): void {
    const bytes = encode(message);
    this.onmessage?.(
      new MessageEvent("message", {
        data: bytes.buffer.slice(
          bytes.byteOffset,
          bytes.byteOffset + bytes.byteLength,
        ),
      }),
    );
  }
}

function fixture() {
  const socket = new FakeWebSocket();
  const client = new StationClient("ws://station:47002/station", {
    webSocketFactory: () => socket,
    requestTimeoutMs: 100,
  });
  const connecting = client.connect();
  socket.open();
  expect(socket.sent[0]).toMatchObject({
    type: "hello",
    version: 1,
  });
  socket.receive({
    type: "hello_response",
    version: 1,
    server_id: "test-server",
    robot_state: "online",
    manifest_digest: 42,
    live_sequence: 4,
  });
  return { client, connecting, socket };
}

describe("StationClient", () => {
  it("handshakes and correlates requests and errors", async () => {
    const { client, connecting, socket } = fixture();
    await expect(connecting).resolves.toMatchObject({ server_id: "test-server" });

    const status = client.status<{ ready: boolean }>();
    const request = socket.sent.at(-1);
    expect(request).toMatchObject({ type: "request", operation: "status" });
    socket.receive({
      type: "response",
      id: request?.id,
      result: { ready: true },
    });
    await expect(status).resolves.toEqual({ ready: true });

    const missing = client.get("missing");
    const missingRequest = socket.sent.at(-1);
    socket.receive({
      type: "error",
      id: missingRequest?.id,
      error: { code: "unknown_endpoint", message: "not found" },
    });
    await expect(missing).rejects.toMatchObject<StationProtocolError>({
      code: "unknown_endpoint",
      message: "not found",
    });
  });

  it("reference-counts explicit subscriptions", async () => {
    const { client, connecting, socket } = fixture();
    await connecting;
    const values: unknown[] = [];

    const firstPromise = client.subscribe("imu.euler", (event) =>
      values.push(event.value),
    );
    const subscribe = socket.sent.at(-1);
    socket.receive({
      type: "response",
      id: subscribe?.id,
      result: { source: "imu.euler", subscribed: true },
    });
    const first = await firstPromise;
    const second = await client.subscribe("imu.euler", () => undefined);
    expect(socket.sent.filter((message) => message.type === "subscribe")).toHaveLength(
      1,
    );

    socket.receive({
      type: "event",
      event: {
        wall_ns: 1,
        live_sequence: 1,
        source: "imu.euler",
        source_kind: "stream",
        value: [1, 2, 3],
      },
    });
    await new Promise<void>((resolve) => queueMicrotask(resolve));
    expect(values).toEqual([[1, 2, 3]]);

    await first();
    expect(
      socket.sent.filter((message) => message.type === "unsubscribe"),
    ).toHaveLength(0);
    const closing = second();
    const unsubscribe = socket.sent.at(-1);
    socket.receive({
      type: "response",
      id: unsubscribe?.id,
      result: { source: "imu.euler", subscribed: false },
    });
    await closing;
  });

  it("rejects pending work when the connection closes", async () => {
    const { client, connecting, socket } = fixture();
    await connecting;
    const request = client.ping();
    socket.close();
    await expect(request).rejects.toBeInstanceOf(StationDisconnectedError);
    expect(client.state).toBe("disconnected");
  });
});
