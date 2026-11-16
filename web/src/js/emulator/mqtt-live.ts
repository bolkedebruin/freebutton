/**
 * The live connection of the emulator to a real broker: MQTT over WebSocket
 * with MQTT.js (npm `mqtt`), which is what esp-mqtt is on the device. This
 * file is the part that is the emulator's own:
 *
 *   how it connects   MQTT 3.1.1, client id = the virtual identity, a clean
 *                     session, the firmware's keepalive, the broker entry's
 *                     user name and password, a last will only when the
 *                     policy gives one
 *   subscriptions     MQTT.js does not make them again after a reconnect
 *                     (`resubscribe: false`): the firmware does, when it
 *                     hears "connected", as on the device. One owner, so
 *                     each subscription is made once.
 *   reconnecting      MQTT.js's own timer is off (it has one fixed period);
 *                     the pause here doubles from 1 s to 30 s
 *   errors            what a failure means for the user (LinkError)
 *
 * Payloads arrive as bytes (MQTT.js decodes nothing) and are UTF-8 decoded
 * here, as the script's API has strings; outgoing strings are UTF-8 encoded
 * by MQTT.js.
 *
 * Not here, on purpose: what may be published (publish-policy.ts decides
 * before anything reaches this client) and the firmware's limits (the wasm
 * applies them before a subscribe or a publish gets here, and to every
 * message handed in).
 */
import type { IClientOptions, MqttClient } from 'mqtt';

/**
 * MQTT.js is about 100 KB compressed: it is fetched when a live mode is
 * first chosen, not with the emulator (offline needs none of it).
 */
let library: Promise<typeof import('mqtt')> | null = null;
function loadMqtt(): Promise<typeof import('mqtt')> {
  return library ??= import('mqtt');
}

export type LinkState = 'disconnected' | 'connecting' | 'connected';

/**
 * Why there is no connection.
 *   url            not a ws:// or wss:// address
 *   mixed-content  the page is https and the address is ws://: the browser blocks it
 *   unreachable    no MQTT broker answered (refused, wrong port, no websockets listener, host down)
 *   not-mqtt       a WebSocket answered, MQTT did not
 *   auth           the broker refused the user name or password
 *   client-id      the broker refused the client id
 *   refused        the broker refused for another reason (its return code)
 *   taken-over     the broker keeps closing the connection right after it is made:
 *                  another client uses the same client id
 *   closed         the connection ended; it is being made again
 */
export interface LinkError {
  kind: 'url' | 'mixed-content' | 'unreachable' | 'not-mqtt' | 'auth' | 'client-id' | 'refused' | 'taken-over' | 'closed';
  message: string;
}

export interface WireMessage {
  topic: string;
  payload: string;
  qos: number;
  retain: boolean;
}

/** MQTT.js's hook for the socket (tests pass node's `ws`). */
export type SocketFactory = NonNullable<IClientOptions['createWebsocket']>;

export interface MqttLiveOptions {
  url: string;
  clientId: string;
  username?: string;
  password?: string;
  /** Seconds (default 30). */
  keepAliveSec?: number;
  /** The last will the broker publishes when this client vanishes; absent: none. */
  will?: WireMessage;
  /** Default: the browser's WebSocket. */
  socket?: SocketFactory;
  /** The page was loaded over https (default: read from location). */
  securePage?: boolean;
  /** First pause before a new attempt, ms (default 1000; doubles up to maxBackoffMs). */
  backoffMs?: number;
  maxBackoffMs?: number;
  /** How long to wait for the broker's CONNACK, ms (default 8000). */
  connectTimeoutMs?: number;
}

/** What a CONNACK return code other than 0 means. */
export function connackError(code: number): LinkError {
  if (code === 4 || code === 5) return { kind: 'auth', message: 'The broker refused the user name or password (config.json "brokers": username, password)' };
  if (code === 2) return { kind: 'client-id', message: 'The broker refused the client id' };
  return { kind: 'refused', message: `The broker refused the connection (CONNACK return code ${code})` };
}

const FATAL: ReadonlySet<LinkError['kind']> = new Set(['url', 'mixed-content', 'auth', 'client-id', 'refused']);
/** A connection that ends this soon after it was made, twice in a row: another client has the id. */
const TAKEOVER_MS = 5000;
const decoder = new TextDecoder('utf-8');

export class MqttLiveClient {
  private client: MqttClient | null = null;
  private current: LinkState = 'disconnected';
  private wanted = false;
  private error: LinkError | null = null;
  private pending: LinkError | null = null;
  private readonly stateListeners = new Set<(state: LinkState, error: LinkError | null) => void>();
  private readonly messageListeners = new Set<(message: WireMessage) => void>();
  private retryTimer: ReturnType<typeof setTimeout> | null = null;
  private backoff: number;
  private connectedAt = 0;
  private shortLived = 0;
  private everConnected = false;

  constructor(private readonly options: MqttLiveOptions) {
    this.backoff = options.backoffMs ?? 1000;
  }

  get state(): LinkState {
    return this.current;
  }

  /** Why there is no connection (null while connected or never tried). */
  get lastError(): LinkError | null {
    return this.error;
  }

  onState(listener: (state: LinkState, error: LinkError | null) => void): () => void {
    this.stateListeners.add(listener);
    return () => { this.stateListeners.delete(listener); };
  }

  onMessage(listener: (message: WireMessage) => void): () => void {
    this.messageListeners.add(listener);
    return () => { this.messageListeners.delete(listener); };
  }

  /** Connect, and keep connected until close(). */
  connect(): void {
    this.wanted = true;
    this.shortLived = 0;
    this.backoff = this.options.backoffMs ?? 1000;
    const url = this.options.url.trim();
    if (!/^wss?:\/\/[^/\s]+/i.test(url)) {
      this.fail({ kind: 'url', message: `"${url}" is not a WebSocket address (ws://host:port)` });
      return;
    }
    const secure = this.options.securePage ?? (typeof location !== 'undefined' && location.protocol === 'https:');
    if (secure && /^ws:\/\//i.test(url)) {
      this.fail({
        kind: 'mixed-content',
        message: 'This page was loaded over https and the broker address is ws://: the browser blocks that. '
          + 'Open the IDE over http (from the device), or give the broker a wss:// listener.',
      });
      return;
    }
    this.setState('connecting', null);
    void loadMqtt().then(
      (lib) => { if (this.wanted && !this.client) this.open(lib, url); },
      () => this.fail({ kind: 'unreachable', message: 'The MQTT client could not be loaded from the device that serves the IDE' }),
    );
  }

  private open(lib: typeof import('mqtt'), url: string): void {
    const o = this.options;
    const connect = lib.connect ?? lib.default.connect;
    const client = connect(url, {
      protocolVersion: 4,              // MQTT 3.1.1, as the firmware's client
      clientId: o.clientId,
      clean: true,
      keepalive: o.keepAliveSec ?? 30,
      reconnectPeriod: 0,              // the pause between attempts is this file's (it grows)
      resubscribe: false,              // the firmware subscribes again when it hears "connected"
      queueQoSZero: false,
      connectTimeout: o.connectTimeoutMs ?? 8000,
      ...(o.username ? { username: o.username, ...(o.password ? { password: o.password } : {}) } : {}),
      ...(o.will ? { will: { topic: o.will.topic, payload: o.will.payload, qos: Math.min(1, o.will.qos) as 0 | 1, retain: o.will.retain } } : {}),
      // A given socket is a test's: its environment has no Web Worker for MQTT.js's keepalive timer.
      ...(o.socket ? { createWebsocket: o.socket, timerVariant: 'native' as const } : {}),
    });
    this.client = client;
    client.on('connect', () => {
      if (this.client !== client) return;
      this.connectedAt = Date.now();
      this.everConnected = true;
      this.pending = null;
      this.backoff = this.options.backoffMs ?? 1000;
      this.setState('connected', null);
    });
    client.on('message', (topic, payload, packet) => {
      if (this.client !== client) return;
      const message: WireMessage = { topic, payload: decoder.decode(payload), qos: packet.qos, retain: packet.retain === true };
      for (const listener of [...this.messageListeners]) listener(message);
    });
    client.on('error', (err: Error & { code?: number | string }) => {
      if (this.client !== client) return;
      // A refused CONNECT carries the broker's return code; no CONNACK at all is a WebSocket that is not MQTT.
      if (typeof err.code === 'number' && err.code > 0 && err.code <= 5) this.pending = connackError(err.code);
      else if (/connack timeout/i.test(err.message)) {
        this.pending = { kind: 'not-mqtt', message: 'A WebSocket answered at this address but MQTT did not: is it the broker\'s websockets listener?' };
      }
    });
    client.on('close', () => {
      if (this.client !== client) return;
      const was = this.current;
      if (!this.wanted) {
        this.setState('disconnected', null);
        return;
      }
      let error: LinkError;
      if (this.pending) {
        error = this.pending;
      } else if (was === 'connected') {
        this.shortLived = Date.now() - this.connectedAt < TAKEOVER_MS ? this.shortLived + 1 : 0;
        error = this.shortLived >= 2
          ? { kind: 'taken-over', message: `The broker keeps closing the connection: another client is connected as "${this.options.clientId}" (the virtual device running in another tab or window?)` }
          : { kind: 'closed', message: 'The connection to the broker ended; connecting again' };
      } else {
        error = { kind: 'unreachable', message: this.everConnected
          ? 'The broker cannot be reached any more; trying again'
          : 'No MQTT broker answers at this WebSocket address (refused or unreachable). Most likely the broker has no websockets listener there, or the port is another one.' };
      }
      this.pending = null;
      this.setState('disconnected', error);
      if (FATAL.has(error.kind)) {
        this.wanted = false;
        client.end(true);
        return;
      }
      if (this.retryTimer) clearTimeout(this.retryTimer);
      this.retryTimer = setTimeout(() => {
        this.retryTimer = null;
        if (!this.wanted || this.client !== client) return;
        this.setState('connecting', this.error);
        client.reconnect();
      }, this.backoff);
      this.backoff = Math.min(this.backoff * 2, this.options.maxBackoffMs ?? 30000);
    });
  }

  /**
   * A clean end: DISCONNECT, then the socket. No new attempt. Resolves when
   * the broker has it (or shortly after, when it does not answer): a new
   * connection with the same client id made before that would be a
   * take-over, and the broker would publish this one's last will.
   */
  close(): Promise<void> {
    this.wanted = false;
    if (this.retryTimer) clearTimeout(this.retryTimer);
    this.retryTimer = null;
    const client = this.client;
    const connected = this.current === 'connected';
    this.client = null;
    this.setState('disconnected', null);
    if (!client) return Promise.resolve();
    client.removeAllListeners();
    // An error after this is nobody's (a socket closed while it connects).
    client.on('error', () => {});
    return new Promise((resolve) => {
      const timer = setTimeout(resolve, 1500);
      client.end(!connected, {}, () => { clearTimeout(timer); resolve(); });
    });
  }

  /** While connected; otherwise nothing is kept (the firmware subscribes again at "connected"). */
  subscribe(topic: string, qos: number): void {
    if (this.current === 'connected') this.client?.subscribe(topic, { qos: Math.min(1, Math.max(0, qos)) as 0 | 1 });
  }

  unsubscribe(topic: string): void {
    if (this.current === 'connected') this.client?.unsubscribe(topic);
  }

  /** False when there is no connection: the message is not kept for later. */
  publish(message: WireMessage): boolean {
    if (this.current !== 'connected' || !this.client) return false;
    this.client.publish(message.topic, message.payload, { qos: Math.min(1, Math.max(0, message.qos)) as 0 | 1, retain: message.retain });
    return true;
  }

  private fail(error: LinkError): void {
    this.wanted = false;
    this.setState('disconnected', error);
  }

  private setState(state: LinkState, error: LinkError | null): void {
    this.error = error;
    if (state === this.current && state !== 'disconnected') return;
    this.current = state;
    for (const listener of [...this.stateListeners]) listener(state, error);
  }
}
