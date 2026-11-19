/**
 * One broker of config.json as the emulator sees it, whatever the mode: the
 * transport handed to the Emulator. It holds the in-page MockBroker (always:
 * offline it is the broker; in the live modes it still takes injected
 * messages, as local deliveries) and, in a live mode, an MQTT connection to
 * the real broker over WebSocket (mqtt-live.ts, MQTT.js).
 *
 * The mode can change while a script runs, and a link outlives an Emulator
 * (Run again makes a new one): the connection stays, the subscriptions of
 * the script that stopped go.
 *
 * Nothing is decided here about what may be published: every publish is put
 * to `hooks.decide` (publish-policy.ts decidePublish with the session's
 * policy) and what came of it is reported to `hooks.record`. Nor are the
 * firmware's limits applied here: the wasm applies them before a subscribe
 * or publish gets here, and to every message this hands in.
 */
import { MockBroker, type ConnectOptions, type MockBrokerOptions, type MqttMessage, type MqttTransport, type PublishAbout, type TransportState } from './mqtt';
import { MqttLiveClient, type LinkError, type LinkState, type SocketFactory, type WireMessage } from './mqtt-live';
import type { PublishDecision, PublishMode, PublishRequest } from './publish-policy';

/** What happened to a message, for the log. */
export interface LinkRecord {
  /**
   * sent: published to the real broker; mock: to the in-page broker
   * (offline); would-publish: read-only, not sent; blocked: not sent, see
   * `reason`; not-connected: allowed but there was no connection;
   * received: from the real broker; local: delivered from the in-page broker
   * (an injected message, a retained one, loopback).
   */
  kind: 'sent' | 'mock' | 'would-publish' | 'blocked' | 'not-connected' | 'received' | 'local';
  broker: number;
  /** The topic the script or firmware gave. */
  requestedTopic: string;
  topic: string;
  payload: string;
  qos: number;
  retain: boolean;
  /** For a publish: what the policy decided and who published. */
  decision?: PublishDecision;
  about?: PublishAbout;
}

export interface LinkHooks {
  decide(request: PublishRequest): PublishDecision;
  record(record: LinkRecord): void;
  /** The connection to the real broker changed. */
  link?(index: number, state: LinkState, error: LinkError | null): void;
}

export interface LiveOptions {
  url: string;
  username?: string;
  password?: string;
  keepAliveSec?: number;
  socket?: SocketFactory;
  securePage?: boolean;
  backoffMs?: number;
  /** The last will to connect with; absent: none. */
  will?: WireMessage;
  /** A change of this makes the connection again (what the firmware should announce anew). */
  session?: string;
}

export class BrokerLink implements MqttTransport {
  readonly mock: MockBroker;
  private current: PublishMode = 'offline';
  private client: MqttLiveClient | null = null;
  private clientId = '';
  private attached = false;
  private effective: TransportState = 'disconnected';
  private readonly subs = new Map<string, number>();
  private readonly messageListeners = new Set<(message: MqttMessage) => void>();
  private readonly stateListeners = new Set<(state: TransportState) => void>();
  private pendingAbout: (PublishAbout & { topic: string }) | null = null;
  private liveUrl = '';
  /** What the connection was made with besides the address: will, session, user name, password. */
  private liveWill = '';

  private matcher: MockBrokerOptions['matches'];

  /**
   * mode: the mode the link starts in. A live mode without a connection yet
   * (setMode makes it): the firmware finds no broker until that one is up,
   * and never hears "connected" from the in-page broker first.
   */
  constructor(readonly index: number, options: MockBrokerOptions, private readonly hooks: LinkHooks, mode: PublishMode = 'offline') {
    this.current = mode;
    this.matcher = options.matches;
    // The filter match is the running emulator's (the firmware's): see adopt().
    this.mock = new MockBroker({ ...options, matches: (filter, topic) => this.matcher(filter, topic) });
    this.mock.onMessage(message => this.handIn(message, 'local'));
    this.mock.onState(() => this.refresh());
  }

  get mode(): PublishMode {
    return this.current;
  }

  /** The WebSocket address of the live connection ('' offline). */
  get url(): string {
    return this.current === 'offline' ? '' : this.liveUrl;
  }

  /** The connection to the real broker (disconnected while offline). */
  get linkState(): LinkState {
    return this.client?.state ?? 'disconnected';
  }

  get linkError(): LinkError | null {
    return this.client?.lastError ?? null;
  }

  /** What the firmware sees: the in-page broker offline, the real one in a live mode. */
  get state(): TransportState {
    return this.effective;
  }

  /**
   * Change the mode. A live mode needs `live` (the address) unless the link
   * has a connection already; between the two live modes the connection
   * stays, unless the address, the user name, the password, the will or
   * `session` changed.
   * identity: the MQTT client id, the emulator's virtual identity.
   */
  setMode(mode: PublishMode, identity: string, live?: LiveOptions): void {
    if (mode === 'offline') {
      const was = this.current;
      this.current = mode;
      void this.closeClient();
      // Another broker now (the in-page one): the firmware hears the old one go and this one come, and subscribes here.
      if (was !== 'offline') this.dropped();
      this.refresh();
      return;
    }
    // Everything the connection is made with: a corrected password connects again.
    const will = live ? JSON.stringify([live.will ?? null, live.session ?? '', live.username ?? '', live.password ?? '']) : '';
    const reuse = this.client && (!live || (live.url === this.liveUrl && will === this.liveWill)) && this.clientId === identity;
    this.current = mode;
    this.clientId = identity;
    if (!reuse) {
      if (!live) throw new Error('a live mode needs the broker\'s WebSocket address');
      // The connection before this one ends first (cleanly): the same client id again while it
      // is still there would be a take-over, and the broker would publish its last will.
      const replaced = this.client !== null;
      const ended = this.closeClient();
      // The broker the firmware talked to is gone (the in-page one, or the connection before this):
      // it hears that, and "connected" when this connection is up. It then makes its subscriptions there.
      this.dropped();
      this.liveUrl = live.url;
      this.liveWill = will;
      const { session: _session, ...options } = live;
      const client = new MqttLiveClient({ ...options, clientId: identity });
      this.client = client;
      // No subscription is made here: the firmware hears "connected" and makes its own again.
      client.onMessage(message => { if (this.client === client) this.handIn(message, 'received'); });
      client.onState((state, error) => {
        if (this.client !== client) return;
        this.hooks.link?.(this.index, state, error);
        this.refresh();
      });
      if (replaced) void ended.then(() => { if (this.client === client) client.connect(); });
      else client.connect();
    }
    this.refresh();
  }

  /** A new emulator takes this link over: its filter match is the one to use. */
  adopt(matches: MockBrokerOptions['matches']): void {
    this.matcher = matches;
  }

  /** The emulator of this script is gone: its subscriptions and listeners go, the connection stays for the next one. */
  detach(): void {
    this.attached = false;
    // The next emulator finds it disconnected and connects: it then hears the state it has.
    this.effective = 'disconnected';
    this.messageListeners.clear();
    this.stateListeners.clear();
    // The script that stopped leaves no subscription behind, here or on the real broker.
    for (const topic of [...this.subs.keys()]) {
      this.mock.unsubscribe(topic);
      this.client?.unsubscribe(topic);
    }
    this.subs.clear();
    this.pendingAbout = null;
  }

  /** End for good: the connection is closed. */
  close(): void {
    this.detach();
    this.current = 'offline';
    void this.closeClient();
    this.mock.disconnect();
    this.effective = 'disconnected';
  }

  /** A message handed to the script from here, as if the broker sent it. True: a subscription matches. */
  inject(topic: string, payload: string, options: { retain?: boolean } = {}): boolean {
    return this.mock.inject(topic, payload, options);
  }

  /** What the next publish() to `topic` is (for a sender that cannot say it in the call). */
  expect(about: PublishAbout & { topic: string }): void {
    this.pendingAbout = about;
  }

  /** Publish outside the policy: what the user allowed after being asked, and the removal of announcements. */
  sendDirect(message: MqttMessage): boolean {
    return this.current === 'live-publish' && !!this.client && this.client.publish(message);
  }

  // ---- MqttTransport (the Emulator's side) ---------------------------------------

  connect(options: ConnectOptions): void {
    this.attached = true;
    this.mock.connect(options);
    this.refresh();
  }

  /** The Emulator's dispose(): see detach(). */
  disconnect(): void {
    this.detach();
  }

  subscribe(topic: string, qos: number): void {
    this.subs.set(topic, qos);
    this.mock.subscribe(topic, qos);
    this.client?.subscribe(topic, qos);
  }

  unsubscribe(topic: string): void {
    this.subs.delete(topic);
    this.mock.unsubscribe(topic);
    this.client?.unsubscribe(topic);
  }

  publish(message: MqttMessage, about?: PublishAbout): void {
    const hinted = this.pendingAbout && this.pendingAbout.topic === message.topic ? this.pendingAbout : null;
    this.pendingAbout = null;
    const who: PublishAbout = about ?? (hinted ? { origin: hinted.origin, kind: hinted.kind } : {});
    const decision = this.hooks.decide({
      broker: this.index, topic: message.topic, payload: message.payload, qos: message.qos, retain: message.retain,
      origin: who.origin ?? 'script', ...(who.kind !== undefined ? { kind: who.kind } : {}),
    });
    const out: MqttMessage = { topic: decision.topic, payload: message.payload, qos: message.qos, retain: decision.retain };
    let kind: LinkRecord['kind'];
    if (decision.action === 'mock') {
      this.mock.publish(out);
      kind = 'mock';
    } else if (decision.action === 'send') {
      kind = this.client?.publish(out) ? 'sent' : 'not-connected';
    } else {
      kind = decision.reason === 'read-only' ? 'would-publish' : 'blocked';
    }
    this.hooks.record({
      kind, broker: this.index, requestedTopic: message.topic, topic: out.topic, payload: out.payload, qos: out.qos, retain: out.retain,
      decision, about: who,
    });
  }

  onMessage(listener: (message: MqttMessage) => void): () => void {
    this.messageListeners.add(listener);
    return () => { this.messageListeners.delete(listener); };
  }

  onState(listener: (state: TransportState) => void): () => void {
    this.stateListeners.add(listener);
    return () => { this.stateListeners.delete(listener); };
  }

  // ---- internals -------------------------------------------------------------------

  private handIn(message: MqttMessage, kind: 'received' | 'local'): void {
    if (!this.attached) return;
    this.hooks.record({
      kind, broker: this.index, requestedTopic: message.topic, topic: message.topic, payload: message.payload,
      qos: message.qos, retain: message.retain,
    });
    for (const listener of [...this.messageListeners]) listener(message);
  }

  private closeClient(): Promise<void> {
    const client = this.client;
    this.client = null;
    if (!client) return Promise.resolve();
    const ended = client.close();
    this.hooks.link?.(this.index, 'disconnected', null);
    return ended;
  }

  /** The firmware's broker went away. */
  private dropped(): void {
    if (this.effective === 'disconnected') return;
    this.effective = 'disconnected';
    for (const listener of [...this.stateListeners]) listener('disconnected');
  }

  /** Tell the firmware when the broker it talks to came or went. */
  private refresh(): void {
    const next: TransportState = !this.attached ? 'disconnected'
      : this.current === 'offline' ? this.mock.state
        : this.client?.state ?? 'disconnected';
    if (next === this.effective) return;
    this.effective = next;
    for (const listener of [...this.stateListeners]) listener(next);
  }
}
