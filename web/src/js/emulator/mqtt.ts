/**
 * MQTT for the emulator: the transport interface and the in-page MockBroker.
 *
 * The script's side of MQTT (subscription table and its limits, topic
 * matching, the event ring between "MQTT task" and loop, payload handling) is
 * the firmware's code inside the wasm. A transport is what esp-mqtt is on the
 * device: it takes SUBSCRIBE / UNSUBSCRIBE / PUBLISH and hands in the
 * messages the broker sends.
 *
 * Stage 1 has one transport, MockBroker. The WebSocket transport of stage 3
 * implements the same interface; what it needs is in docs/EMULATOR.md (its
 * own client id = the virtual identity, clean session, no last will).
 */
import type { Clock } from './clock';
import type { TopicMatcher } from './publish-policy';

export type TransportState = 'disconnected' | 'connecting' | 'connected';

export interface MqttMessage {
  topic: string;
  payload: string;
  qos: number;
  retain: boolean;
}

/** Who made a publish and what it is (publish-policy.ts PublishRequest origin / kind). */
export interface PublishAbout {
  origin?: 'script' | 'firmware';
  kind?: string;
}

export interface ConnectOptions {
  /** The MQTT client id: the emulator's virtual identity, never a device's. */
  clientId: string;
}

export interface MqttTransport {
  readonly state: TransportState;
  /** Starts connecting; the state listeners hear when it is connected. */
  connect(options: ConnectOptions): void;
  disconnect(): void;
  subscribe(topic: string, qos: number): void;
  unsubscribe(topic: string): void;
  /**
   * `about`: what the sender knows of the publish (who made it, what kind);
   * a transport that decides what leaves the page (BrokerLink) uses it.
   */
  publish(message: MqttMessage, about?: PublishAbout): void;
  /** A message from the broker for one of the subscriptions. Returns the unsubscribe function. */
  onMessage(listener: (message: MqttMessage) => void): () => void;
  onState(listener: (state: TransportState) => void): () => void;
}

/** A publish the script (or the firmware side) made, as the mock recorded it. */
export interface PublishRecord extends MqttMessage {
  /** Clock time, ms since the epoch. */
  time: number;
}

export interface MockSubscription {
  topic: string;
  qos: number;
}

/** One event of a recorded session. dir 'in': broker to device; 'out': a publish of the device. */
export interface SessionEvent extends MqttMessage {
  /** Milliseconds since the session started. */
  t: number;
  dir: 'in' | 'out';
}

/** A MockBroker session as JSON, for record / replay. */
export interface MockSession {
  version: 1;
  /** Retained messages the broker held at export. */
  retained: MqttMessage[];
  events: SessionEvent[];
}

export interface MockBrokerOptions {
  /** MQTT filter match; the emulator passes the firmware's. */
  matches: TopicMatcher;
  clock: Clock;
  /**
   * Deliver a publish to the matching subscriptions of this same client, as
   * a real broker does. Default false: a script then only hears what is
   * injected.
   */
  loopback?: boolean;
  /** Connect at once on connect() (default true). false: call setConnected(). */
  autoConnect?: boolean;
}

/**
 * A broker in the page. Nothing leaves it.
 *
 * - `subscriptions`: what the script subscribed to.
 * - `published`: every publish, with topic, payload, qos, retain and time.
 * - inject(): a message from "another client"; delivered when a
 *   subscription matches, remembered when retained.
 * - Retained messages (injected, or published with retain) are delivered to
 *   a new subscription, flagged retain, as a real broker does.
 * - exportSession() / importSession() / replay(): record and replay.
 */
export class MockBroker implements MqttTransport {
  loopback: boolean;
  /** Every publish, oldest first. */
  readonly published: PublishRecord[] = [];

  private current: TransportState = 'disconnected';
  private clientId = '';
  private readonly subs = new Map<string, number>();
  private readonly retained = new Map<string, MqttMessage>();
  private readonly events: SessionEvent[] = [];
  private readonly messageListeners = new Set<(message: MqttMessage) => void>();
  private readonly stateListeners = new Set<(state: TransportState) => void>();
  private readonly matches: TopicMatcher;
  private readonly clock: Clock;
  private readonly autoConnect: boolean;
  private started: number;

  constructor(options: MockBrokerOptions) {
    this.matches = options.matches;
    this.clock = options.clock;
    this.loopback = options.loopback ?? false;
    this.autoConnect = options.autoConnect ?? true;
    this.started = this.clock.now();
  }

  get state(): TransportState {
    return this.current;
  }

  /** The client id the emulator connected with. */
  get connectedAs(): string {
    return this.clientId;
  }

  /** The script's subscriptions, in the order they were made. */
  get subscriptions(): MockSubscription[] {
    return [...this.subs].map(([topic, qos]) => ({ topic, qos }));
  }

  /** The retained messages the broker holds. */
  get retainedMessages(): MqttMessage[] {
    return [...this.retained.values()];
  }

  connect(options: ConnectOptions): void {
    this.clientId = options.clientId;
    if (this.autoConnect) this.setConnected(true);
    else this.setState('connecting');
  }

  disconnect(): void {
    this.setConnected(false);
  }

  /**
   * The connection comes up or goes away (a broker restart, a network drop).
   * Coming up, nothing is delivered by itself: the session is clean, the
   * firmware makes its subscriptions again (resubscribeAll) and each of them
   * gets the retained messages, as from a real broker.
   */
  setConnected(connected: boolean): void {
    this.setState(connected ? 'connected' : 'disconnected');
  }

  subscribe(topic: string, qos: number): void {
    this.subs.set(topic, qos);
    for (const message of this.retained.values()) {
      if (this.matches(topic, message.topic)) this.deliver({ ...message, retain: true });
    }
  }

  unsubscribe(topic: string): void {
    this.subs.delete(topic);
  }

  publish(message: MqttMessage): void {
    const record: PublishRecord = { ...message, time: this.clock.now() };
    this.published.push(record);
    this.events.push({ ...message, t: record.time - this.started, dir: 'out' });
    this.remember(message);
    if (this.loopback) this.route({ ...message, retain: false });
  }

  onMessage(listener: (message: MqttMessage) => void): () => void {
    this.messageListeners.add(listener);
    return () => { this.messageListeners.delete(listener); };
  }

  onState(listener: (state: TransportState) => void): () => void {
    this.stateListeners.add(listener);
    return () => { this.stateListeners.delete(listener); };
  }

  /**
   * A message from another client of this broker. Returns whether a
   * subscription of the script matched (a real broker sends it only then).
   */
  inject(topic: string, payload: string, options: { qos?: number; retain?: boolean } = {}): boolean {
    const message: MqttMessage = { topic, payload, qos: options.qos ?? 0, retain: options.retain ?? false };
    this.events.push({ ...message, t: this.clock.now() - this.started, dir: 'in' });
    this.remember(message);
    return this.route({ ...message, retain: false });
  }

  /** Forgets the publish log and the recorded events (not the retained messages or subscriptions). */
  clearLog(): void {
    this.published.length = 0;
    this.events.length = 0;
    this.started = this.clock.now();
  }

  exportSession(): MockSession {
    return {
      version: 1,
      retained: this.retainedMessages.map(m => ({ ...m })),
      events: this.events.map(e => ({ ...e })),
    };
  }

  /** Takes over a session's retained messages (delivered to matching subscriptions now) and starts a new log. */
  importSession(session: MockSession): void {
    if (session.version !== 1) throw new Error(`unknown mock session version ${String(session.version)}`);
    this.retained.clear();
    this.clearLog();
    for (const message of session.retained) {
      this.retained.set(message.topic, { ...message, retain: true });
      for (const topic of this.subs.keys()) {
        if (this.matches(topic, message.topic)) {
          this.deliver({ ...message, retain: true });
          break;
        }
      }
    }
  }

  /**
   * Injects the session's incoming messages again, each at its recorded
   * time from now (one clock timer chain). Returns a function that cancels
   * what is left.
   */
  replay(session: MockSession): () => void {
    const incoming = session.events.filter(e => e.dir === 'in').sort((a, b) => a.t - b.t);
    const start = this.clock.now();
    let at = 0;
    let timer: number | null = null;
    const step = (): void => {
      timer = null;
      const elapsed = this.clock.now() - start;
      while (at < incoming.length && incoming[at]!.t <= elapsed) {
        const e = incoming[at++]!;
        this.inject(e.topic, e.payload, { qos: e.qos, retain: e.retain });
      }
      if (at < incoming.length) timer = this.clock.setTimer(step, incoming[at]!.t - elapsed);
    };
    step();
    return () => {
      if (timer !== null) this.clock.clearTimer(timer);
      timer = null;
      at = incoming.length;
    };
  }

  private remember(message: MqttMessage): void {
    if (!message.retain) return;
    // MQTT: a retained message with an empty payload deletes the retained one.
    if (message.payload === '') this.retained.delete(message.topic);
    else this.retained.set(message.topic, { ...message, retain: true });
  }

  /** To the client when one of its subscriptions matches. */
  private route(message: MqttMessage): boolean {
    for (const topic of this.subs.keys()) {
      if (this.matches(topic, message.topic)) {
        this.deliver(message);
        return true;
      }
    }
    return false;
  }

  private deliver(message: MqttMessage): void {
    if (this.current !== 'connected') return;
    for (const listener of [...this.messageListeners]) listener(message);
  }

  private setState(state: TransportState): void {
    if (this.current === state) return;
    this.current = state;
    for (const listener of [...this.stateListeners]) listener(state);
  }
}
