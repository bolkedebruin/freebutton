/**
 * MQTT of the virtual device for one IDE session: the brokers of
 * config.json as BrokerLinks, the mode (offline / live read-only / live
 * publish), what the user allowed, and the log.
 *
 * Safety, as implemented here and in publish-policy.ts:
 *
 *   offline         (the mode at every start) nothing leaves the page.
 *   live read-only  connected to the real broker as the virtual identity;
 *                   the script's subscriptions are made there; no publish
 *                   is sent, each is logged as "would publish".
 *   live publish    publishes are sent, except:
 *                   - retain is cleared, unless "send retained" is on;
 *                   - announcements (Home Assistant discovery and
 *                     availability) are not sent, unless "announce to Home
 *                     Assistant" is on. Announcement topics that were sent
 *                     are remembered: removeAnnouncement() publishes an
 *                     empty retained message to each, which removes the
 *                     entities from Home Assistant and from the broker;
 *                   - a topic that names a real device ("btn_" + 6 hex,
 *                     not the virtual identity) is held: the user is asked
 *                     once per topic (Once / For this session / No);
 *                   - the firmware's own periodic reports (its sensors'
 *                     states, about eight a second) are not sent, unless
 *                     "publish the virtual device's own states" is on.
 *                   A key's default action (kind 'press') and what a script
 *                   publishes are what is being tested: they are sent,
 *                   under the rules above.
 *
 * The last will: the connection has one only while "announce" is on (the
 * firmware's, retained only with "send retained"). Without it a virtual
 * device that vanishes leaves no "offline" behind for a device nobody
 * announced. Switching "announce" makes the connection again, so that the
 * firmware sends its birth message and discovery under the new rule.
 *
 * The client id is always the virtual identity; a client id in config.json
 * is not used. The mode and everything allowed live in memory only: a
 * reload, a Stop, or a config with other brokers is offline again. Run
 * again with the same brokers keeps the connection and the mode. Only the
 * broker's WebSocket address is kept (localStorage, per broker host).
 */
import { BrokerLink, type LinkRecord, type LiveOptions } from './broker-link';
import type { MockSession, MqttTransport } from './mqtt';
import type { LinkError, LinkState, SocketFactory } from './mqtt-live';
import { decidePublish, type PublishMode, type TopicMatcher } from './publish-policy';
import type { Clock } from './clock';

export interface MqttLogEntry {
  /**
   * injected: handed to the script from here (local); delivered: the broker
   * (the real one, or the in-page one) sent it to the script; published: to
   * the in-page broker (offline); sent: to the real broker; would-publish:
   * read-only, not sent; blocked: not sent, the note says why.
   */
  kind: 'injected' | 'delivered' | 'published' | 'sent' | 'would-publish' | 'blocked';
  broker: number;
  topic: string;
  payload: string;
  retain: boolean;
  /** Clock time, ms (of the latest, for a collapsed row). */
  time: number;
  /** Where it went or why it did not. */
  note: string;
  /** Who published it; absent for what came in. */
  origin?: 'script' | 'firmware';
  /** What a firmware publish is: press, discovery, availability, sensor, state. */
  what?: string;
  /**
   * How many messages this row stands for. The firmware's periodic and bulk
   * publishes (sensor, state, discovery) are one row per topic and outcome:
   * the row keeps the latest payload and counts.
   */
  count: number;
  /** The payloads before the latest one (for discovery: the topics), newest first (a collapsed row; at most HISTORY_MAX). */
  history?: Array<{ time: number; payload: string }>;
}

/** Kinds of firmware publishes that are one row per topic. */
const COLLAPSED = new Set(['sensor', 'state', 'discovery']);
export const HISTORY_MAX = 8;

export interface BrokerView {
  index: number;
  name: string;
  url: string;
  subscriptions: Array<{ topic: string; qos: number }>;
}

export interface BrokerStatus {
  index: number;
  name: string;
  /** Host of config.json's broker url. */
  host: string;
  /** The WebSocket address used for the live modes (editable; kept per browser and host). */
  wsUrl: string;
  state: LinkState;
  error: LinkError | null;
  /** config.json gives this broker a client id: it is not used here. */
  configClientId: string;
  /** config.json has a user name for it (sent with the password in the live modes). */
  hasCredentials: boolean;
}

export interface MqttStatus {
  mode: PublishMode;
  /** live publish: keep the retain flag. */
  sendRetained: boolean;
  /** live publish: let discovery and availability through. */
  announce: boolean;
  /** live publish: let the firmware's own sensor and state reports through. */
  ownStates: boolean;
  /** Announcement topics sent in this session and not removed. */
  announced: number;
  /** The page is https: a ws:// address will be blocked by the browser. */
  securePage: boolean;
  brokers: BrokerStatus[];
}

/** A recorded session of every broker, as the export file holds it. */
export interface SessionFile {
  format: 'freebutton-mqtt-session';
  version: 1;
  identity: string;
  brokers: Array<{ name: string; session: MockSession }>;
}

export interface AskRequest {
  title: string;
  message?: string;
  choices: Array<{ id: string; label: string; primary?: boolean; danger?: boolean }>;
  cancel?: string;
}

export interface MqttSessionDeps {
  identity: () => string;
  /** The log or the subscriptions changed (often: the firmware reports every second). */
  changed: () => void;
  /** The mode, what is allowed, or a connection changed (seldom). */
  statusChanged: () => void;
  say: (text: string, type: 'info' | 'error') => void;
  /** The IDE's question dialog: the chosen id, or null. */
  ask: (request: AskRequest) => Promise<string | null>;
  /** The broker's WebSocket address per host: a convenience kept by the browser. */
  storage?: Pick<Storage, 'getItem' | 'setItem'> | null;
  /** Tests: the socket to use, and whether the page counts as https. */
  socket?: SocketFactory;
  securePage?: boolean;
  backoffMs?: number;
  keepAliveSec?: number;
  /** The last will the firmware's client asked for (Emulator.lastWill). */
  will?: (index: number) => { topic: string; payload: string; qos: number; retain: boolean } | undefined;
}

interface BrokerConfig {
  name: string;
  url: string;
  host: string;
  username: string;
  password: string;
  clientId: string;
}

export const MQTT_LOG_MAX = 200;
/** The websockets listener of a broker is usually here (Mosquitto's examples use it). */
/** 1884: the port Home Assistant's Mosquitto add-on uses for websockets. */
export const DEFAULT_WS_PORT = 1884;
const URL_KEY = 'fb-mqtt-ws:';

type Obj = Record<string, unknown>;

function text(v: unknown): string {
  return typeof v === 'string' ? v : typeof v === 'number' ? String(v) : '';
}

/** The host of a broker url of config.json ("mqtt://10.0.0.5/", "10.0.0.5", "mqtts://broker.lan:8883"). */
export function brokerHost(url: string): string {
  const match = /^(?:[a-z][a-z0-9+.-]*:\/\/)?(?:[^@/]*@)?(\[[^\]]+\]|[^:/?#]+)/i.exec(url.trim());
  return match ? match[1]! : '';
}

/** config.json "brokers" as the live connection needs them. */
export function brokerConfigs(config: string): BrokerConfig[] {
  let parsed: unknown;
  try { parsed = JSON.parse(config); } catch { return []; }
  const list = (parsed as Obj | null)?.brokers;
  if (!Array.isArray(list)) return [];
  return list.filter((b): b is Obj => typeof b === 'object' && b !== null).map((b, index) => ({
    name: text(b.brokerid) || `broker${index}`,
    url: text(b.url),
    host: brokerHost(text(b.url)),
    username: text(b.username),
    password: text(b.password),
    clientId: text(b.clientid ?? b.clientId ?? b.client_id),
  }));
}

function retainNote(record: LinkRecord): string {
  return record.decision?.retainCleared ? ' · retain cleared' : record.retain ? ' · retained' : '';
}

export class MqttSession {
  private configs: BrokerConfig[] = [];
  private links: BrokerLink[] = [];
  private mode: PublishMode = 'offline';
  private sendRetained = false;
  private announce = false;
  private ownStates = false;
  private readonly realAllowed = new Set<string>();
  private readonly realDenied = new Set<string>();
  private readonly asking = new Set<string>();
  private readonly announced = new Map<string, { broker: number; topic: string }>();
  private log: MqttLogEntry[] = [];
  private matcher: TopicMatcher = (filter, topic) => filter === topic;
  private cancelReplay: Array<() => void> = [];
  private attached = false;

  constructor(private readonly deps: MqttSessionDeps) {}

  // ---- the emulator's side -----------------------------------------------------

  /**
   * The config a Run starts with. Other brokers than before (names or
   * addresses): the connections end and the mode is offline again. The
   * same: the links, the connection and the mode stay.
   */
  configure(config: string): void {
    const next = brokerConfigs(config);
    const same = next.length === this.configs.length
      && next.every((b, i) => b.name === this.configs[i]!.name && b.url === this.configs[i]!.url);
    this.log = [];
    this.asking.clear();
    if (!same) {
      this.end();
    }
    this.configs = next;
    this.attached = true;
  }

  /** The transport of broker `index` for a new Emulator. */
  transport(context: { index: number; matches: TopicMatcher; clock: Clock }): MqttTransport {
    this.matcher = context.matches;
    let link = this.links[context.index];
    if (link) {
      link.adopt(context.matches);
    } else {
      link = new BrokerLink(context.index, { matches: context.matches, clock: context.clock }, {
        decide: request => decidePublish(request, {
          mode: this.mode, virtualId: this.deps.identity(), allowDiscovery: this.announce, allowRetain: this.sendRetained,
          holdStates: true, allowStates: this.ownStates, askRealDevice: true, realTopics: this.realAllowed,
        }, this.matcher),
        record: record => this.record(record),
        link: (i, state, error) => this.onLink(i, state, error),
      });
      this.links[context.index] = link;
    }
    return link;
  }

  /** The emulator says what its next publish is (who made it, what kind) just before it publishes. */
  expect(broker: number, about: { topic: string; origin?: 'script' | 'firmware'; kind?: string }): void {
    this.links[broker]?.expect(about);
  }

  /** The script stopped for good (Stop, or it failed): the connections end, offline again. */
  stop(): void {
    this.cancelReplay.forEach(cancel => cancel());
    this.cancelReplay = [];
    this.attached = false;
    this.end();
  }

  private end(): void {
    for (const link of this.links) link?.close();
    this.links = [];
    this.mode = 'offline';
    this.sendRetained = false;
    this.announce = false;
    this.ownStates = false;
    this.realAllowed.clear();
    this.realDenied.clear();
    this.asking.clear();
    // What was announced stays known only while it can be removed: with the connection gone it cannot.
    this.announced.clear();
  }

  // ---- mode and what is allowed ---------------------------------------------------

  status(): MqttStatus {
    return {
      mode: this.mode, sendRetained: this.sendRetained, announce: this.announce, ownStates: this.ownStates, announced: this.announced.size,
      securePage: this.securePage(),
      brokers: this.configs.map((b, index) => ({
        index, name: b.name, host: b.host, wsUrl: this.wsUrl(index),
        state: this.links[index]?.linkState ?? 'disconnected', error: this.links[index]?.linkError ?? null,
        configClientId: b.clientId, hasCredentials: b.username !== '',
      })),
    };
  }

  /** The aggregate of the live connections: 'none' offline. */
  linkState(): 'none' | 'connecting' | 'connected' | 'error' {
    if (this.mode === 'offline' || this.links.length === 0) return 'none';
    const states = this.links.map(l => l.linkState);
    if (states.every(s => s === 'connected')) return 'connected';
    return this.links.some(l => l.linkState === 'disconnected' && l.linkError) ? 'error' : 'connecting';
  }

  get currentMode(): PublishMode {
    return this.mode;
  }

  /** '' when done, else why not. */
  setMode(mode: PublishMode): string {
    if (!this.attached) return 'Run the virtual device first';
    if (mode !== 'offline' && this.configs.length === 0) return 'config.json has no "brokers" to connect to';
    if (mode === this.mode) return '';
    const was = this.mode;
    this.mode = mode;
    if (mode !== 'live-publish') {
      // What was allowed for publishing does not outlive the mode.
      this.sendRetained = false;
      this.announce = false;
      this.ownStates = false;
      this.realAllowed.clear();
      this.realDenied.clear();
    }
    this.links.forEach((link, index) => {
      if (mode === 'offline') link.setMode('offline', this.deps.identity());
      else link.setMode(mode, this.deps.identity(), this.liveOptions(index));
    });
    if (mode === 'offline') this.announced.clear();
    const words: Record<PublishMode, string> = {
      offline: 'MQTT offline: the in-page broker, nothing leaves this page',
      'live-readonly': `MQTT live, read-only: connected as ${this.deps.identity()}; nothing is published`,
      'live-publish': `MQTT live: connected as ${this.deps.identity()}; the virtual device PUBLISHES to the real broker`,
    };
    if (was !== mode) this.deps.say(words[mode], 'info');
    this.deps.changed();
    this.deps.statusChanged();
    return '';
  }

  /** The WebSocket address of broker `index`: what the user set for its host, else ws://host:1884. */
  wsUrl(index: number): string {
    const b = this.configs[index];
    if (!b) return '';
    let stored: string | null = null;
    try { stored = this.deps.storage?.getItem(URL_KEY + b.host) ?? null; } catch { /* no storage */ }
    return stored || (b.host ? `ws://${b.host}:${DEFAULT_WS_PORT}` : '');
  }

  setWsUrl(index: number, url: string): void {
    const b = this.configs[index];
    if (!b) return;
    const value = url.trim();
    try { if (value) this.deps.storage?.setItem(URL_KEY + b.host, value); } catch { /* no storage */ }
    if (this.mode !== 'offline') this.links[index]?.setMode(this.mode, this.deps.identity(), this.liveOptions(index, value));
    this.deps.changed();
    this.deps.statusChanged();
  }

  setSendRetained(on: boolean): void {
    this.sendRetained = on && this.mode === 'live-publish';
    // The will's retain flag follows: the connection is made again when it has one.
    if (this.announce) this.relink();
    this.deps.changed();
    this.deps.statusChanged();
  }

  /**
   * Announcing on or off. The connection is made again either way: with the
   * firmware's last will or without one, and the firmware then sends its
   * birth message and discovery under the new rule.
   */
  setAnnounce(on: boolean): void {
    const next = on && this.mode === 'live-publish';
    if (next === this.announce) return;
    this.announce = next;
    this.relink();
    this.deps.changed();
    this.deps.statusChanged();
  }

  /** live publish: the firmware's own sensor and state reports. */
  setOwnStates(on: boolean): void {
    this.ownStates = on && this.mode === 'live-publish';
    this.deps.changed();
    this.deps.statusChanged();
  }

  private relink(): void {
    if (this.mode === 'offline') return;
    this.links.forEach((link, index) => link.setMode(this.mode, this.deps.identity(), this.liveOptions(index)));
  }

  /**
   * Take back what was announced: "announce" goes off, and an empty retained
   * message goes to every announcement topic sent in this session. '' when
   * done, else why not.
   */
  removeAnnouncement(): string {
    if (this.announced.size === 0) return 'Nothing was announced in this session';
    if (this.mode !== 'live-publish') return 'Only in live publish: the removal is a publish';
    // No new announcement from here on: the switch goes off first, or the firmware would announce again.
    const wasOn = this.announce;
    this.announce = false;
    let failed = 0;
    for (const [key, { broker, topic }] of [...this.announced]) {
      if (this.links[broker]?.sendDirect({ topic, payload: '', qos: 1, retain: true })) {
        this.announced.delete(key);
        this.add({ kind: 'sent', broker, topic, payload: '', retain: true, time: Date.now(), count: 1, note: 'announcement removed (empty retained message)' });
      } else {
        failed++;
      }
    }
    // The connection is made again without the last will (a clean DISCONNECT: the will is not published).
    if (wasOn) this.relink();
    this.deps.changed();
    this.deps.statusChanged();
    return failed ? `${failed} could not be sent: no connection to the broker` : '';
  }

  // ---- the panel -----------------------------------------------------------------------

  brokers(): BrokerView[] {
    return this.configs.flatMap((b, index) => (this.links[index]
      ? [{ index, name: b.name, url: b.url, subscriptions: this.links[index]!.mock.subscriptions }] : []));
  }

  /** Newest first. */
  entries(): readonly MqttLogEntry[] {
    return this.log;
  }

  get loopback(): boolean {
    return !!this.links[0]?.mock.loopback;
  }

  setLoopback(on: boolean): void {
    for (const link of this.links) link.mock.loopback = on;
    this.deps.changed();
  }

  /** A message handed to the script from here: local, also in the live modes (the real broker does not get it). */
  inject(broker: number, topic: string, payload: string, retain = false): boolean {
    const link = this.links[broker];
    if (!link || !this.attached || topic === '') return false;
    // The entry first: the delivery it causes is logged after it (shown above it).
    const entry = this.add({ kind: 'injected', broker, topic, payload, retain, time: Date.now(), count: 1, note: '' });
    const matched = link.inject(topic, payload, { retain });
    entry.note = `local${this.mode === 'offline' ? '' : ' (not sent to the broker)'}: ${matched ? 'a subscription matches'
      : retain ? 'no subscription matches: kept as retained' : 'no subscription matches: not delivered'}`;
    this.deps.changed();
    return true;
  }

  clearLog(): void {
    this.log = [];
    for (const link of this.links) link.mock.clearLog();
    this.deps.changed();
  }

  exportSession(): SessionFile | null {
    if (!this.attached) return null;
    return {
      format: 'freebutton-mqtt-session', version: 1, identity: this.deps.identity(),
      brokers: this.configs.map((b, i) => ({ name: b.name, session: this.links[i]!.mock.exportSession() })).filter((_, i) => this.links[i]),
    };
  }

  /** Replay a session file through inject(), each incoming message at its recorded time. '' when started, else what is wrong. */
  importSession(file: unknown): string {
    if (!this.attached) return 'Run the virtual device first';
    const f = file as Partial<SessionFile> | null;
    if (!f || f.format !== 'freebutton-mqtt-session' || f.version !== 1 || !Array.isArray(f.brokers)) {
      return 'Not a FreeButton MQTT session file';
    }
    let events = 0;
    for (const [position, recorded] of f.brokers.entries()) {
      const byName = this.configs.findIndex(b => b.name === recorded.name);
      const index = byName >= 0 ? byName : position;
      const link = this.links[index];
      if (!link || !recorded.session || !Array.isArray(recorded.session.events)) continue;
      try {
        link.mock.importSession(recorded.session);
      } catch (err) {
        return err instanceof Error ? err.message : String(err);
      }
      const incoming = recorded.session.events.filter(e => e.dir === 'in').sort((a, b) => a.t - b.t);
      events += incoming.length;
      const timers = incoming.map(e => setTimeout(() => { this.inject(index, e.topic, e.payload, e.retain); }, Math.max(0, e.t)));
      this.cancelReplay.push(() => timers.forEach(clearTimeout));
    }
    this.deps.say(`MQTT session: replaying ${events} message(s)`, 'info');
    this.deps.changed();
    return '';
  }

  // ---- internals ----------------------------------------------------------------------

  private securePage(): boolean {
    return this.deps.securePage ?? (typeof location !== 'undefined' && location.protocol === 'https:');
  }

  private liveOptions(index: number, url = this.wsUrl(index)): LiveOptions {
    const b = this.configs[index]!;
    // A last will only for a device that is announced; retained only when retained messages are sent.
    const will = this.announce && this.mode === 'live-publish' ? this.deps.will?.(index) : undefined;
    return {
      url, securePage: this.securePage(), session: `announce:${this.announce}`,
      ...(will ? { will: { ...will, retain: will.retain && this.sendRetained } } : {}),
      ...(b.username ? { username: b.username, password: b.password } : {}),
      ...(this.deps.socket ? { socket: this.deps.socket } : {}),
      ...(this.deps.backoffMs ? { backoffMs: this.deps.backoffMs } : {}),
      ...(this.deps.keepAliveSec ? { keepAliveSec: this.deps.keepAliveSec } : {}),
    };
  }

  private onLink(index: number, state: LinkState, error: LinkError | null): void {
    const name = this.configs[index]?.name ?? `broker ${index}`;
    if (state === 'connected') this.deps.say(`MQTT: connected to ${name} (${this.links[index]?.url}) as ${this.deps.identity()}`, 'info');
    else if (state === 'disconnected' && error) this.deps.say(`MQTT ${name}: ${error.message}`, 'error');
    this.deps.changed();
    this.deps.statusChanged();
  }

  private add(entry: MqttLogEntry): MqttLogEntry {
    if (entry.what && COLLAPSED.has(entry.what)) {
      // One row per topic and outcome, where it is: a row that counts does not move the list.
      // Sensor and state reports: per topic. Discovery (some fifty topics, once each): one row for all of it.
      const bulk = entry.what === 'discovery';
      const row = this.log.find(e => e.what === entry.what && e.kind === entry.kind && e.broker === entry.broker
        && (bulk || e.topic === entry.topic));
      if (row) {
        row.history = [{ time: row.time, payload: bulk ? row.topic : row.payload }, ...(row.history ?? [])].slice(0, HISTORY_MAX);
        row.topic = entry.topic;
        row.count++;
        row.payload = entry.payload;
        row.time = entry.time;
        row.retain = entry.retain;
        row.note = entry.note;
        return row;
      }
    }
    this.log.unshift(entry);
    if (this.log.length > MQTT_LOG_MAX) this.log.length = MQTT_LOG_MAX;
    return entry;
  }

  private record(r: LinkRecord): void {
    const out = r.decision ? { origin: r.about?.origin ?? 'script' as const, ...(r.about?.kind ? { what: r.about.kind } : {}) } : {};
    const base = { broker: r.broker, topic: r.topic, payload: r.payload, retain: r.retain, time: Date.now(), count: 1, ...out };
    switch (r.kind) {
      case 'received':
        this.add({ ...base, kind: 'delivered', note: `from the broker${r.retain ? ' (retained)' : ''}` });
        break;
      case 'local':
        this.add({ ...base, kind: 'delivered', note: this.mode === 'offline'
          ? (r.retain ? 'retained message for a new subscription' : 'to the script')
          : `local, from the in-page broker${r.retain ? ' (retained)' : ''}` });
        break;
      case 'mock':
        this.add({ ...base, kind: 'published', note: 'not sent: offline (in-page broker only)' });
        break;
      case 'would-publish':
        this.add({ ...base, kind: 'would-publish', note: 'not sent: read-only' });
        break;
      case 'not-connected':
        this.add({ ...base, kind: 'blocked', note: 'not sent: no connection to the broker' });
        break;
      case 'sent':
        if (r.decision?.announce) this.announced.set(`${r.broker}\n${r.topic}`, { broker: r.broker, topic: r.topic });
        this.add({
          ...base, kind: 'sent',
          note: `sent to the broker${r.decision?.announce ? ' · announcement' : ''}${r.decision?.realDeviceId ? ` · as the real device ${r.decision.realDeviceId}` : ''}${retainNote(r)}`,
        });
        break;
      case 'blocked':
        if (r.decision?.reason === 'real-device') this.askRealDevice(r);
        else if (r.decision?.reason === 'discovery') this.add({ ...base, kind: 'blocked', note: 'not sent: announcing to Home Assistant is off' });
        else if (r.decision?.reason === 'own-state') this.add({ ...base, kind: 'blocked', note: 'not sent: the virtual device\'s own states are off' });
        else this.add({ ...base, kind: 'blocked', note: `not sent: ${r.decision?.reason ?? 'blocked'}` });
        break;
    }
    this.deps.changed();
  }

  /**
   * A publish to a real device's topic in live publish. Asked once per topic
   * at a time; "No" holds for the session; while the question is open,
   * further publishes to the topic are not sent (and not queued).
   */
  private askRealDevice(r: LinkRecord): void {
    const id = r.decision!.realDeviceId!;
    const base = {
      broker: r.broker, topic: r.topic, payload: r.payload, retain: r.retain, time: Date.now(), count: 1,
      origin: r.about?.origin ?? 'script' as const, ...(r.about?.kind ? { what: r.about.kind } : {}),
    };
    if (this.realDenied.has(r.topic)) {
      this.add({ ...base, kind: 'blocked', note: `not sent: you said no to publishing as the real device ${id}` });
      return;
    }
    if (this.asking.has(r.topic)) {
      this.add({ ...base, kind: 'blocked', note: `not sent: waiting for your answer about the real device ${id}` });
      return;
    }
    this.asking.add(r.topic);
    const entry = this.add({ ...base, kind: 'blocked', note: `held: asking whether to publish as the real device ${id}` });
    const link = this.links[r.broker];
    void this.deps.ask({
      title: `This topic belongs to the real device ${id}. Publish as it?`,
      message: `${r.topic}\n= ${r.payload === '' ? '(empty)' : r.payload}\n\nPublished there, the virtual device acts as the real one: `
        + 'whatever listens to that device will react.',
      choices: [{ id: 'once', label: 'Once' }, { id: 'session', label: 'For this session', danger: true }],
      cancel: 'No',
    }).then((answer) => {
      this.asking.delete(r.topic);
      if (answer !== 'once' && answer !== 'session') {
        this.realDenied.add(r.topic);
        entry.note = `not sent: you said no to publishing as the real device ${id}`;
      } else if (this.mode !== 'live-publish' || this.links[r.broker] !== link || !link) {
        entry.note = 'not sent: no longer in live publish';
      } else {
        if (answer === 'session') this.realAllowed.add(r.topic);
        const retain = r.retain && this.sendRetained;
        const sent = link.sendDirect({ topic: r.topic, payload: r.payload, qos: r.qos, retain });
        entry.kind = sent ? 'sent' : 'blocked';
        entry.retain = retain;
        entry.note = sent
          ? `sent to the broker · as the real device ${id} (${answer === 'session' ? 'allowed for this session' : 'this once'})`
          : 'not sent: no connection to the broker';
      }
      this.deps.changed();
    });
  }
}
