/**
 * A small MQTT 3.1.1 broker over WebSocket, in the test process: what a
 * Mosquitto websockets listener is to the emulator's live modes. It records
 * what clients did and can misbehave on request (refuse, go silent, split
 * frames), which is why it is this and not a full broker.
 *
 * The wire format is `mqtt-packet`'s (the codec MQTT.js uses); the socket is
 * node's `ws`. Both come with dependencies the project already has; tests
 * only.
 */
import { WebSocketServer, WebSocket, type WebSocket as ServerSocket } from 'ws';
import mqttPacket, { type Packet } from 'mqtt-packet';
import type { SocketFactory } from '../emulator/mqtt-live';

export interface SeenConnect {
  clientId: string;
  username?: string;
  password?: string;
  keepAlive: number;
  cleanSession: boolean;
  will?: { topic: string; payload: string; qos: number; retain: boolean };
  protocol: string;
  level: number;
  subprotocol: string;
}

export interface SeenPublish {
  clientId: string;
  topic: string;
  payload: string;
  qos: number;
  retain: boolean;
}

export interface TestBrokerOptions {
  /** user -> password; absent: anyone may connect. */
  users?: Record<string, string>;
  /** Answer every CONNECT with this return code. */
  connack?: number;
  /** Every packet the broker sends goes out in pieces of this many bytes (one WebSocket frame each). */
  chunk?: number;
  /** Do not answer PINGREQ. */
  ignorePing?: boolean;
  /** Accept the WebSocket but never answer MQTT. */
  mute?: boolean;
}

interface Client {
  socket: ServerSocket;
  id: string;
  subs: Map<string, number>;
  will?: SeenConnect['will'];
  /** It said DISCONNECT: its will is not published. */
  clean: boolean;
}

/** MQTT topic filter match ('+', '#'). */
export function topicMatches(filter: string, topic: string): boolean {
  const f = filter.split('/');
  const t = topic.split('/');
  for (let i = 0; i < f.length; i++) {
    if (f[i] === '#') return true;
    if (i >= t.length) return false;
    if (f[i] !== '+' && f[i] !== t[i]) return false;
  }
  return f.length === t.length;
}

export class TestBroker {
  readonly connects: SeenConnect[] = [];
  readonly published: SeenPublish[] = [];
  readonly subscribed: Array<{ clientId: string; topic: string; qos: number }> = [];
  readonly unsubscribed: Array<{ clientId: string; topic: string }> = [];
  /** Packet ids of PUBACKs clients sent for what the broker delivered. */
  readonly acked: number[] = [];
  /** Last wills the broker published (a client vanished without DISCONNECT). */
  readonly wills: SeenPublish[] = [];
  pings = 0;
  disconnects = 0;
  readonly retained = new Map<string, { payload: string; qos: number }>();
  options: TestBrokerOptions;

  private server!: WebSocketServer;
  private clients = new Set<Client>();
  private nextId = 100;

  private constructor(options: TestBrokerOptions) {
    this.options = options;
  }

  static async start(options: TestBrokerOptions = {}): Promise<TestBroker> {
    const broker = new TestBroker(options);
    await new Promise<void>((resolve) => {
      broker.server = new WebSocketServer({ port: 0, host: '127.0.0.1', handleProtocols: protocols => (protocols.has('mqtt') ? 'mqtt' : false) }, resolve);
    });
    broker.server.on('connection', socket => broker.accept(socket));
    return broker;
  }

  get port(): number {
    return (this.server.address() as { port: number }).port;
  }

  get url(): string {
    return `ws://127.0.0.1:${this.port}`;
  }

  /** Client ids connected now. */
  get clientIds(): string[] {
    return [...this.clients].filter(c => c.id).map(c => c.id);
  }

  subscriptionsOf(clientId: string): string[] {
    return [...this.clients].filter(c => c.id === clientId).flatMap(c => [...c.subs.keys()]);
  }

  /** A publish from "another client" of this broker. */
  publish(topic: string, payload: string, options: { qos?: number; retain?: boolean } = {}): void {
    this.route(topic, payload, options.qos ?? 0, options.retain ?? false);
  }

  /** Close every connection without a word, as a broker that dies does. No wills (it is the broker that went). */
  dropAll(): void {
    for (const c of [...this.clients]) { c.clean = true; c.socket.terminate(); }
    this.clients.clear();
  }

  /** Cut one client's connection as a network failure does: the broker publishes its will. */
  cut(clientId: string): void {
    for (const c of [...this.clients]) if (c.id === clientId) c.socket.terminate();
  }

  async stop(): Promise<void> {
    this.dropAll();
    await new Promise<void>(resolve => this.server.close(() => resolve()));
  }

  private accept(socket: ServerSocket): void {
    const client: Client = { socket, id: '', subs: new Map(), clean: false };
    this.clients.add(client);
    const parser = mqttPacket.parser({ protocolVersion: 4 });
    parser.on('packet', (p: Packet) => this.receive(client, p, socket.protocol));
    parser.on('error', () => socket.terminate());
    socket.on('message', (data: Buffer) => { parser.parse(data); });
    socket.on('close', () => {
      if (!this.clients.delete(client)) return;
      if (client.will && !client.clean) {
        this.wills.push({ clientId: client.id, ...client.will });
        this.route(client.will.topic, client.will.payload, client.will.qos, client.will.retain);
      }
    });
  }

  private send(client: Client, p: Packet): void {
    if (client.socket.readyState !== 1) return;
    const bytes = mqttPacket.generate(p, { protocolVersion: 4 });
    const size = this.options.chunk;
    if (!size) { client.socket.send(bytes); return; }
    for (let at = 0; at < bytes.length; at += size) client.socket.send(bytes.subarray(at, at + size));
  }

  private receive(client: Client, p: Packet, subprotocol: string): void {
    if (this.options.mute) return;
    switch (p.cmd) {
      case 'connect': {
        const will = p.will ? { topic: p.will.topic, payload: p.will.payload.toString(), qos: p.will.qos ?? 0, retain: !!p.will.retain } : undefined;
        const password = p.password === undefined ? undefined : p.password.toString();
        this.connects.push({
          clientId: p.clientId, username: p.username, password, keepAlive: p.keepalive ?? 0, cleanSession: p.clean !== false,
          ...(will ? { will } : {}), protocol: p.protocolId ?? '', level: p.protocolVersion ?? 0, subprotocol,
        });
        let code = this.options.connack ?? 0;
        if (!code && this.options.users) {
          if (p.username === undefined || this.options.users[p.username] !== password) code = p.username === undefined ? 5 : 4;
        }
        if (code) {
          this.send(client, { cmd: 'connack', returnCode: code, sessionPresent: false });
          client.clean = true;
          client.socket.close();
          return;
        }
        // The same client id again: the broker ends the older connection, and publishes its will (MQTT 3.1.1, 3.1.4-2).
        for (const other of [...this.clients]) {
          if (other !== client && other.id === p.clientId) other.socket.terminate();
        }
        client.id = p.clientId;
        client.will = will;
        this.send(client, { cmd: 'connack', returnCode: 0, sessionPresent: false });
        return;
      }
      case 'subscribe': {
        for (const sub of p.subscriptions) {
          client.subs.set(sub.topic, sub.qos);
          this.subscribed.push({ clientId: client.id, topic: sub.topic, qos: sub.qos });
        }
        this.send(client, { cmd: 'suback', messageId: p.messageId ?? 0, granted: p.subscriptions.map(s => s.qos) });
        for (const sub of p.subscriptions) {
          for (const [topic, m] of this.retained) {
            if (topicMatches(sub.topic, topic)) this.deliver(client, topic, m.payload, Math.min(sub.qos, m.qos), true);
          }
        }
        return;
      }
      case 'unsubscribe':
        for (const topic of p.unsubscriptions) {
          client.subs.delete(topic);
          this.unsubscribed.push({ clientId: client.id, topic });
        }
        this.send(client, { cmd: 'unsuback', messageId: p.messageId ?? 0 } as Packet);
        return;
      case 'publish': {
        const payload = p.payload.toString();
        this.published.push({ clientId: client.id, topic: p.topic, payload, qos: p.qos, retain: p.retain });
        if (p.qos > 0) this.send(client, { cmd: 'puback', messageId: p.messageId ?? 0 });
        this.route(p.topic, payload, p.qos, p.retain);
        return;
      }
      case 'puback':
        this.acked.push(p.messageId ?? 0);
        return;
      case 'pingreq':
        this.pings++;
        if (!this.options.ignorePing) this.send(client, { cmd: 'pingresp' });
        return;
      case 'disconnect':
        this.disconnects++;
        client.clean = true;
        client.socket.close();
        return;
    }
  }

  private route(topic: string, payload: string, qos: number, retain: boolean): void {
    if (retain) {
      if (payload === '') this.retained.delete(topic);
      else this.retained.set(topic, { payload, qos });
    }
    for (const client of this.clients) {
      let granted = -1;
      for (const [filter, subQos] of client.subs) if (topicMatches(filter, topic)) granted = Math.max(granted, subQos);
      if (granted >= 0) this.deliver(client, topic, payload, Math.min(qos, granted), false);
    }
  }

  private deliver(client: Client, topic: string, payload: string, qos: number, retain: boolean): void {
    this.send(client, {
      cmd: 'publish', topic, payload, qos: qos as 0 | 1, retain, dup: false, ...(qos > 0 ? { messageId: this.nextId++ } : {}),
    } as Packet);
  }
}

/** The socket for MQTT.js in tests: node's WebSocket, whatever the test environment thinks a browser has. */
export const nodeSocket: SocketFactory = (url, protocols) => new WebSocket(url, protocols) as never;

/** Wait until `check` holds (polling), or fail after `ms`. */
export async function until(check: () => boolean, ms = 2000, what = 'condition'): Promise<void> {
  const end = Date.now() + ms;
  while (!check()) {
    if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
    await new Promise(r => setTimeout(r, 5));
  }
}
