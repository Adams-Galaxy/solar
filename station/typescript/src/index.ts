import { decode, encode } from "cborg";

export const STATION_PROTOCOL_VERSION = 1;
export const DEFAULT_MAXIMUM_MESSAGE = 1 << 20;

export type StationConnectionState =
  | "disconnected"
  | "connecting"
  | "connected"
  | "closing";

export interface StationHello {
  live_sequence: number;
  manifest_digest: number | null;
  robot_state: string;
  server_id: string;
  type: "hello_response";
  version: number;
}

export interface StationEvent<T = unknown> {
  endpoint_id?: number | null;
  endpoint_name?: string | null;
  live_sequence?: number;
  manifest_digest?: Uint8Array | null;
  monotonic_ns?: number;
  schema_id?: number | null;
  server_id?: string;
  source: string;
  source_kind: string;
  source_loss_count?: number;
  stored_event_id?: number | null;
  value: T;
  wall_ns?: number;
}

export interface StationErrorBody {
  code: string;
  details?: unknown;
  message: string;
}

export interface StationClientOptions {
  clientName?: string;
  connectTimeoutMs?: number;
  maximumMessage?: number;
  requestTimeoutMs?: number;
  webSocketFactory?: (url: string) => WebSocketLike;
}

export interface WebSocketLike {
  binaryType: BinaryType;
  close(code?: number, reason?: string): void;
  onclose: ((event: CloseEvent) => void) | null;
  onerror: ((event: Event) => void) | null;
  onmessage: ((event: MessageEvent) => void) | null;
  onopen: ((event: Event) => void) | null;
  readyState: number;
  send(data: ArrayBufferLike | ArrayBufferView | Blob | string): void;
}

type EventListener<T> = (event: StationEvent<T>) => void;
type StateListener = (state: StationConnectionState) => void;

interface PendingRequest {
  reject: (error: Error) => void;
  resolve: (value: unknown) => void;
  timeout: ReturnType<typeof setTimeout>;
}

interface SourceSubscription {
  listeners: Set<EventListener<unknown>>;
  ready: Promise<void>;
}

export class StationProtocolError extends Error {
  readonly code: string;
  readonly details?: unknown;

  constructor(body: StationErrorBody) {
    super(body.message);
    this.name = "StationProtocolError";
    this.code = body.code;
    if (body.details !== undefined) this.details = body.details;
  }
}

export class StationDisconnectedError extends Error {
  constructor(message = "Station connection closed") {
    super(message);
    this.name = "StationDisconnectedError";
  }
}

export class StationClient {
  readonly url: string;
  readonly clientName: string;
  readonly connectTimeoutMs: number;
  readonly maximumMessage: number;
  readonly requestTimeoutMs: number;

  private readonly webSocketFactory: (url: string) => WebSocketLike;
  private readonly pending = new Map<number, PendingRequest>();
  private readonly sources = new Map<string, SourceSubscription>();
  private readonly stateListeners = new Set<StateListener>();
  private socket: WebSocketLike | undefined;
  private nextRequestId = 1;
  private helloResolve: ((hello: StationHello) => void) | undefined;
  private helloReject: ((error: Error) => void) | undefined;

  state: StationConnectionState = "disconnected";
  hello: StationHello | undefined;

  constructor(url: string, options: StationClientOptions = {}) {
    this.url = url;
    this.clientName = options.clientName ?? "solar-station-typescript";
    this.connectTimeoutMs = options.connectTimeoutMs ?? 5_000;
    this.maximumMessage = options.maximumMessage ?? DEFAULT_MAXIMUM_MESSAGE;
    this.requestTimeoutMs = options.requestTimeoutMs ?? 5_000;
    this.webSocketFactory =
      options.webSocketFactory ??
      ((target) => {
        if (typeof WebSocket === "undefined") {
          throw new Error(
            "No global WebSocket implementation; provide webSocketFactory",
          );
        }
        return new WebSocket(target);
      });
  }

  async connect(): Promise<StationHello> {
    if (this.state === "connected" && this.hello) return this.hello;
    if (this.state !== "disconnected") {
      throw new Error(`Cannot connect while Station client is ${this.state}`);
    }

    this.setState("connecting");
    let socket: WebSocketLike;
    try {
      socket = this.webSocketFactory(this.url);
    } catch (error) {
      this.setState("disconnected");
      throw normalizeError(error);
    }
    this.socket = socket;
    socket.binaryType = "arraybuffer";
    socket.onmessage = (event) => this.handleMessage(event);
    socket.onclose = () => this.handleClose();
    socket.onerror = () => {
      if (this.state === "connecting") {
        this.helloReject?.(new Error(`Could not connect to ${this.url}`));
      }
    };

    const hello = new Promise<StationHello>((resolve, reject) => {
      this.helloResolve = resolve;
      this.helloReject = reject;
    });
    const timeout = setTimeout(() => {
      this.helloReject?.(new Error("Timed out waiting for Station handshake"));
      socket.close();
    }, this.connectTimeoutMs);

    socket.onopen = () => {
      this.sendMessage({
        type: "hello",
        version: STATION_PROTOCOL_VERSION,
        client: this.clientName,
      });
    };

    try {
      const result = await hello;
      this.hello = result;
      this.setState("connected");
      return result;
    } catch (error) {
      socket.close();
      this.handleClose(normalizeError(error));
      throw normalizeError(error);
    } finally {
      clearTimeout(timeout);
      this.helloResolve = undefined;
      this.helloReject = undefined;
    }
  }

  async close(): Promise<void> {
    if (this.state === "disconnected") return;
    this.setState("closing");
    this.socket?.close(1000, "client closed");
    this.handleClose();
  }

  onStateChange(listener: StateListener): () => void {
    this.stateListeners.add(listener);
    listener(this.state);
    return () => this.stateListeners.delete(listener);
  }

  request<T = unknown>(
    operation: string,
    arguments_: Record<string, unknown> = {},
    timeoutMs = this.requestTimeoutMs,
  ): Promise<T> {
    this.requireConnected();
    const id = this.allocateRequestId();
    return new Promise<T>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Station request ${operation} timed out`));
      }, timeoutMs);
      this.pending.set(id, {
        reject,
        resolve: (value) => resolve(value as T),
        timeout,
      });
      try {
        this.sendMessage({
          type: "request",
          id,
          operation,
          arguments: arguments_,
        });
      } catch (error) {
        clearTimeout(timeout);
        this.pending.delete(id);
        reject(normalizeError(error));
      }
    });
  }

  async subscribe<T>(
    source: string,
    listener: EventListener<T>,
  ): Promise<() => Promise<void>> {
    this.requireConnected();
    let subscription = this.sources.get(source);
    if (!subscription) {
      const listeners = new Set<EventListener<unknown>>();
      const ready = this.subscriptionRequest("subscribe", source).then(
        () => undefined,
      );
      subscription = { listeners, ready };
      this.sources.set(source, subscription);
      ready.catch(() => {
        if (this.sources.get(source) === subscription) this.sources.delete(source);
      });
    }
    subscription.listeners.add(listener as EventListener<unknown>);
    await subscription.ready;

    let active = true;
    return async () => {
      if (!active) return;
      active = false;
      const current = this.sources.get(source);
      current?.listeners.delete(listener as EventListener<unknown>);
      if (current && current.listeners.size === 0) {
        this.sources.delete(source);
        if (this.state === "connected") {
          await this.subscriptionRequest("unsubscribe", source);
        }
      }
    };
  }

  status<T = unknown>(): Promise<T> {
    return this.request<T>("status");
  }
  manifest<T = unknown>(): Promise<T> {
    return this.request<T>("manifest");
  }
  get<T = unknown>(endpoint: string | number): Promise<T> {
    return this.request<T>("get", { endpoint });
  }
  set(endpoint: string | number, value: unknown): Promise<unknown> {
    return this.request("set", { endpoint, value });
  }
  call<T = unknown>(
    endpoint: string | number,
    value?: unknown,
  ): Promise<T> {
    return this.request<T>(
      "call",
      value === undefined ? { endpoint } : { endpoint, value },
    );
  }
  ping(): Promise<unknown> {
    return this.request("ping");
  }
  streams<T = unknown>(): Promise<T> {
    return this.request<T>("streams");
  }
  stream(
    endpoint: string | number,
    options: { batch?: number; frequency?: number; stop?: boolean } = {},
  ): Promise<unknown> {
    return this.request("stream", { endpoint, ...options });
  }

  private subscriptionRequest(
    type: "subscribe" | "unsubscribe",
    source: string,
  ): Promise<unknown> {
    this.requireConnected();
    const id = this.allocateRequestId();
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Station ${type} for ${source} timed out`));
      }, this.requestTimeoutMs);
      this.pending.set(id, { reject, resolve, timeout });
      this.sendMessage({ type, id, source });
    });
  }

  private handleMessage(event: MessageEvent): void {
    void this.decodeFrame(event.data)
      .then((message) => this.dispatchMessage(message))
      .catch((error) => {
        this.handleClose(normalizeError(error));
        this.socket?.close(1003, "invalid Station message");
      });
  }

  private async decodeFrame(data: unknown): Promise<Record<string, unknown>> {
    let bytes: Uint8Array;
    if (data instanceof ArrayBuffer) bytes = new Uint8Array(data);
    else if (ArrayBuffer.isView(data)) {
      bytes = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    } else if (typeof Blob !== "undefined" && data instanceof Blob) {
      bytes = new Uint8Array(await data.arrayBuffer());
    } else {
      throw new Error("Station sent a non-binary WebSocket message");
    }
    if (bytes.byteLength === 0 || bytes.byteLength > this.maximumMessage) {
      throw new Error(`Station message size ${bytes.byteLength} is invalid`);
    }
    const value = decode(bytes);
    if (!isRecord(value)) throw new Error("Station message is not a CBOR map");
    return value;
  }

  private dispatchMessage(message: Record<string, unknown>): void {
    if (message.type === "hello_response") {
      if (message.version !== STATION_PROTOCOL_VERSION) {
        this.helloReject?.(
          new Error(`Unsupported Station protocol version ${String(message.version)}`),
        );
        return;
      }
      this.helloResolve?.(message as unknown as StationHello);
      return;
    }
    if (message.type === "response" || message.type === "error") {
      const id = message.id;
      if (typeof id !== "number") {
        if (message.type === "error") {
          this.helloReject?.(protocolError(message.error));
        }
        return;
      }
      const pending = this.pending.get(id);
      if (!pending) return;
      clearTimeout(pending.timeout);
      this.pending.delete(id);
      if (message.type === "error") pending.reject(protocolError(message.error));
      else pending.resolve(message.result);
      return;
    }
    if (message.type === "event" && isRecord(message.event)) {
      const event = message.event as unknown as StationEvent;
      this.sources
        .get(event.source)
        ?.listeners.forEach((listener) => listener(event));
      return;
    }
    if (
      message.type === "subscription_state" &&
      typeof message.source === "string"
    ) {
      const event: StationEvent = {
        source: message.source,
        source_kind: "server",
        value: message,
      };
      this.sources
        .get(event.source)
        ?.listeners.forEach((listener) => listener(event));
    }
  }

  private sendMessage(message: Record<string, unknown>): void {
    const socket = this.socket;
    if (!socket || socket.readyState !== 1) {
      throw new StationDisconnectedError();
    }
    const body = encode(message);
    if (body.byteLength === 0 || body.byteLength > this.maximumMessage) {
      throw new Error(`Station message size ${body.byteLength} is invalid`);
    }
    socket.send(body);
  }

  private requireConnected(): void {
    if (this.state !== "connected") throw new StationDisconnectedError();
  }

  private allocateRequestId(): number {
    for (;;) {
      const id = this.nextRequestId;
      this.nextRequestId =
        id >= Number.MAX_SAFE_INTEGER ? 1 : this.nextRequestId + 1;
      if (!this.pending.has(id)) return id;
    }
  }

  private handleClose(reason = new StationDisconnectedError()): void {
    if (this.state === "disconnected" && !this.socket) return;
    this.socket = undefined;
    this.hello = undefined;
    this.helloReject?.(reason);
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timeout);
      pending.reject(reason);
    }
    this.pending.clear();
    this.sources.clear();
    this.setState("disconnected");
  }

  private setState(state: StationConnectionState): void {
    if (this.state === state) return;
    this.state = state;
    this.stateListeners.forEach((listener) => listener(state));
  }
}

function protocolError(value: unknown): StationProtocolError {
  if (
    isRecord(value) &&
    typeof value.code === "string" &&
    typeof value.message === "string"
  ) {
    return new StationProtocolError(value as unknown as StationErrorBody);
  }
  return new StationProtocolError({
    code: "protocol_error",
    message: "Station returned a malformed error",
  });
}

function normalizeError(value: unknown): Error {
  return value instanceof Error ? value : new Error(String(value));
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}
