/**
 * The emulator behind the virtual device: one at a time, started with Run.
 *
 * Run takes scripts/main.js and config.json as the user sees them (the
 * editor's unsaved text for the file that is open, else the stored file),
 * starts the firmware's script engine in the page (emulator/, loaded on the
 * first Run: nothing of it is in the IDE's first load) and follows it:
 *
 *   emulator-change   status, identity, errors, heap (EmulatorSnapshot)
 *   emulator-state    the script changed the device: draw pages() again
 *   emulator-mqtt     the MQTT log or the subscriptions changed
 *   console-line      print() output, errors and evaluated lines, all to the
 *                     'virtual' console: nothing of it reaches the device's
 *
 * The identity is "virt_btn_" + 6 hex digits, made once per browser and
 * kept in localStorage: it is never taken from the real device, so topics a
 * script builds from system.id() are the virtual device's own. MQTT is the
 * in-page broker only (stage 1: "offline").
 *
 * What the script changes is runtime state for drawing. It is never written
 * to config.json: edits of config.json go through config-edit.ts, from the
 * config model, and the virtual device does not offer them while this runs.
 */
import { emit, on, type ConnectorConfig, type EmulatorSnapshot, type PageConfig } from './app-state';
import { parseConfigText } from './config-model';
import { getConnectors } from './device-config';
import { getCode, getEditorView, showRuntimeError } from './editor';
import { CONFIG_KEY, MAIN_SCRIPT_KEY } from './fs-rules';
import { workspace } from './workspace';
import type { Emulator, MockSession, ScriptError, SensorState } from './emulator';

type EmulatorModule = typeof import('./emulator');

/** What Run starts with. */
export interface RunSource {
  script: string;
  config: string;
  /** Other text files of the device, for load(): path -> content. */
  files?: Record<string, string>;
}

export interface MqttLogEntry {
  /** injected: sent to the broker here; delivered: the broker sent it to the script; published: the script published it. */
  kind: 'injected' | 'delivered' | 'published';
  broker: number;
  topic: string;
  payload: string;
  retain: boolean;
  /** Clock time, ms. */
  time: number;
  /** What happened to it ("not sent: offline", "no subscription matches"). */
  note: string;
}

export interface BrokerView {
  index: number;
  name: string;
  url: string;
  subscriptions: Array<{ topic: string; qos: number }>;
}

/** A recorded session of every broker, as the export file holds it. */
export interface SessionFile {
  format: 'freebutton-mqtt-session';
  version: 1;
  identity: string;
  brokers: Array<{ name: string; session: MockSession }>;
}

const ID_KEY = 'fb-virtual-device-id';
const ID_PATTERN = /^virt_btn_[0-9a-f]{6}$/;
export const MQTT_LOG_MAX = 200;

/**
 * The virtual device as its info side shows it. Everything is the
 * emulator's: nothing here is asked of the real device. Null = not known
 * while nothing runs.
 */
export interface VirtualInfo {
  identity: string;
  status: EmulatorSnapshot['status'];
  /** Why it stopped ('' = no error). */
  error: string;
  /** Seconds since Run. */
  uptimeSeconds: number | null;
  /** The script's JS heap after a garbage collection, as /api/device reports it on a device. */
  heap: { free: number; used: number; size: number } | null;
  /** The running script's bytecode and the most the device stores. */
  bytecode: { bytes: number; limit: number } | null;
  /** Pending setTimeout timers and the firmware's slots. */
  timers: { used: number; limit: number } | null;
  /** The script's MQTT subscriptions and the firmware's room for them (per broker x brokers that can have any). */
  subscriptions: { used: number; limit: number } | null;
  /** MQTT events lost because the script did not keep up. */
  mqttDropped: number | null;
  /** Stage 1: always the broker in the page. */
  mqtt: 'offline';
  /** config.json "brokers", by name. */
  brokers: string[] | null;
  sensors: number | null;
  /** system.version() / system.model() of the firmware the engine was built from. */
  firmware: string | null;
  model: string | null;
  /** Module per connector 0..3: 0 none, 1 bar (two keys), 2 main display. */
  connectors: Array<0 | 1 | 2> | null;
}
/** The shortest press given to the firmware's key scan (three scans of 20 ms and a margin). */
export const MIN_PRESS_MS = 70;

/** The virtual device's id of this browser; made on first use. */
export function virtualDeviceId(): string {
  try {
    const stored = localStorage.getItem(ID_KEY);
    if (stored && ID_PATTERN.test(stored)) return stored;
  } catch { /* no storage: a new id per page load */ }
  const bytes = new Uint8Array(3);
  crypto.getRandomValues(bytes);
  const id = 'virt_btn_' + Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
  try { localStorage.setItem(ID_KEY, id); } catch { /* see above */ }
  return id;
}

/** How a publish of the script ended, in words. */
function publishNote(action: string, reason: string, log?: string): string {
  if (action === 'mock') return 'not sent: offline (in-page broker only)';
  if (action === 'block') return `blocked: ${reason}${log ? ' (' + log + ')' : ''}`;
  return `sent (${reason})`;
}

function connectorKinds(connectors: ConnectorConfig[]): Array<0 | 1 | 2> | undefined {
  if (connectors.length === 0) return undefined;
  const out: Array<0 | 1 | 2> = [0, 0, 0, 0];
  for (const c of connectors) if (c.id < out.length) out[c.id] = c.type === 'bar' ? 1 : c.type === 'display' ? 2 : 0;
  return out;
}

export class EmulatorSession {
  /** For tests: the wasm (a compiled module or bytes) instead of the fetch. */
  wasm: WebAssembly.Module | BufferSource | undefined;
  /** For tests: what Run reads, instead of the editor and the workspace. */
  readSource: (() => Promise<RunSource | null>) | null = null;

  private emu: Emulator | null = null;
  private api: EmulatorModule | null = null;
  private status: EmulatorSnapshot['status'] = 'idle';
  private error = '';
  private errors = 0;
  private stale: EmulatorSnapshot['stale'] = null;
  private startedAt = 0;
  private built: { version: string; model: string } | null = null;
  private used = false;
  private basePages: PageConfig[] = [];
  private log: MqttLogEntry[] = [];
  private stateQueued = false;
  private runToken = 0;
  private offs: Array<() => void> = [];
  private unlisten: Array<() => void> | null = null;
  private cancelReplay: Array<() => void> = [];
  private pressedAt = new Map<number, number>();
  private releases = new Map<number, ReturnType<typeof setTimeout>>();

  snapshot(): EmulatorSnapshot {
    return {
      status: this.status, identity: virtualDeviceId(), mqtt: 'offline', error: this.error, errors: this.errors,
      stale: this.stale, used: this.used,
    };
  }

  /** A script context exists: keys, MQTT and the console reach it. */
  get running(): boolean {
    return this.status === 'running' && !!this.emu;
  }

  /**
   * The pages as the running device shows them: the config Run started with,
   * with what the script changed laid over it. Null when nothing runs.
   */
  pages(): PageConfig[] | null {
    return this.emu && this.api && this.status === 'running' ? this.api.overlayPages(this.basePages, this.emu.state) : null;
  }

  /** The page the running device shows. */
  shownPage(): number | null {
    return this.running ? this.emu!.state.shownPage : null;
  }

  /** Key positions of the main display's left and right half; null without a main display. */
  mainKeys(): [number, number] | null {
    const main = this.running ? this.emu!.state.connectors.indexOf(2) : -1;
    return main < 0 ? null : [main * 2 + 1, main * 2 + 2];
  }

  /**
   * Start (or start again) with the current script and config. Resolves
   * when the script's top level ran or the start failed; the outcome is in
   * the snapshot and in the console.
   */
  async run(): Promise<void> {
    this.listen();
    const token = ++this.runToken;
    this.disposeEmulator();
    // The virtual console keeps what the run before left: a line marks where this one starts.
    if (this.used) this.say(`──── Run again · ${new Date().toLocaleTimeString()} ────`, 'info');
    this.used = true;
    this.status = 'starting';
    this.error = '';
    this.errors = 0;
    this.stale = null;
    this.log = [];
    this.publish();
    emit('emulator-mqtt');

    let source: RunSource | null;
    try {
      source = await (this.readSource ?? readFromWorkspace)();
      if (token !== this.runToken) return;
      if (!source) throw new Error(`/${MAIN_SCRIPT_KEY} could not be read`);
      if (source.config.trim() !== '' && parseConfigText(source.config) === null) {
        throw new Error('config.json is not valid JSON: correct it, then Run');
      }
      this.api ??= await import('./emulator');
      if (token !== this.runToken) return;
      const connectors = connectorKinds(getConnectors());
      this.emu = await this.api.Emulator.create({
        config: source.config.trim() === '' ? {} : source.config,
        files: source.files,
        identity: virtualDeviceId(),
        ...(this.wasm ? { wasm: this.wasm } : {}),
        ...(connectors ? { connectors } : {}),
      });
    } catch (err) {
      if (token !== this.runToken) return;
      this.fail(err instanceof Error ? err.message : String(err));
      return;
    }
    if (token !== this.runToken) {
      this.emu.dispose();
      this.emu = null;
      return;
    }
    const emu = this.emu;
    this.basePages = parseConfigText(source.config)?.pages ?? [];
    this.follow(emu);
    this.say(`Virtual device ${emu.identity} started (MQTT offline: in-page broker)`, 'info');
    const result = emu.run(source.script);
    if (!result.ok && !emu.running) {
      // Nothing runs (compile or load error; the error event said why).
      const message = this.error || result.error.message;
      this.disposeEmulator();
      this.fail(message, false);
      return;
    }
    this.status = 'running';
    this.startedAt = Date.now();
    this.built = emu.firmware();
    this.publish();
    emit('emulator-state');
    emit('emulator-mqtt');
  }

  /** Stop the script and release the engine. The device shows config.json again. */
  stop(): void {
    this.runToken++;
    const was = this.status;
    this.disposeEmulator();
    this.status = 'idle';
    this.error = '';
    this.stale = null;
    if (was === 'running') this.say('Virtual device stopped', 'info');
    this.publish();
    emit('emulator-state');
    emit('emulator-mqtt');
  }

  /**
   * The virtual device's own numbers, measured now (the info side asks at
   * most once a second, and only while it is shown).
   */
  info(): VirtualInfo {
    const emu = this.emu;
    const base = { identity: virtualDeviceId(), status: this.status, error: this.status === 'error' ? this.error : '', mqtt: 'offline' as const };
    if (!emu || this.status !== 'running') {
      return {
        ...base, uptimeSeconds: null, heap: null, bytecode: null, timers: null, subscriptions: null, mqttDropped: null,
        brokers: null, sensors: null, firmware: this.built?.version ?? null, model: this.built?.model ?? null, connectors: null,
      };
    }
    const stats = emu.stats();
    const brokers = emu.state.brokers;
    return {
      ...base,
      uptimeSeconds: Math.floor((Date.now() - this.startedAt) / 1000),
      heap: { free: stats.jsHeapFree, used: stats.jsHeapUsed, size: stats.jsHeapSize },
      bytecode: { bytes: emu.bytecodeSize, limit: emu.limits.bytecodeBytes },
      timers: { used: stats.timers, limit: stats.timerSlots },
      subscriptions: {
        used: stats.subscriptions,
        limit: emu.limits.subscriptionsPerBroker * Math.min(brokers.length, emu.limits.brokersWithSubscriptions),
      },
      mqttDropped: stats.mqttDropped,
      brokers: brokers.map(b => b.name || `broker ${b.index}`),
      sensors: emu.state.sensors.length,
      firmware: this.built?.version ?? null,
      model: this.built?.model ?? null,
      connectors: [...emu.state.connectors],
    };
  }

  // ---- keys, sensors --------------------------------------------------------

  /** A key goes down (a pointer or a keyboard key holds it). False: nothing runs, or the device has no such key. */
  press(position: number): boolean {
    if (!this.running) return false;
    const pending = this.releases.get(position);
    if (pending) {
      // Pressed again before the short press before it ended: one press.
      clearTimeout(pending);
      this.releases.delete(position);
    }
    this.pressedAt.set(position, Date.now());
    return this.emu!.press(position);
  }

  /**
   * The key goes up. The firmware reads the keys every 20 ms and a tap with
   * a mouse or a touch pad can be shorter than that: a press is held for at
   * least MIN_PRESS_MS, so that every tap is a click, as a finger on the
   * real key is.
   */
  release(position: number): boolean {
    const emu = this.emu;
    if (!emu || !this.running) return false;
    const held = Date.now() - (this.pressedAt.get(position) ?? 0);
    if (held >= MIN_PRESS_MS) return emu.release(position);
    if (!this.releases.has(position)) {
      this.releases.set(position, setTimeout(() => {
        this.releases.delete(position);
        if (this.emu === emu) emu.release(position);
      }, MIN_PRESS_MS - held));
    }
    return true;
  }

  sensors(): SensorState[] {
    return this.emu ? this.emu.state.sensors.map(s => ({ ...s })) : [];
  }

  setSensor(id: number, value: number): boolean {
    return this.running && Number.isFinite(value) ? this.emu!.setSensor(id, value) : false;
  }

  // ---- MQTT (the in-page brokers) --------------------------------------------

  brokers(): BrokerView[] {
    const emu = this.emu;
    if (!emu) return [];
    return emu.state.brokers.map((b, index) => ({
      index, name: b.name, url: b.url, subscriptions: emu.broker(index).subscriptions,
    }));
  }

  /** Newest first. */
  mqttLog(): readonly MqttLogEntry[] {
    return this.log;
  }

  get loopback(): boolean {
    return !!this.emu && this.emu.state.brokers.length > 0 && this.emu.broker(0).loopback;
  }

  /** A publish of the script comes back to its own matching subscriptions, as from a real broker. */
  setLoopback(on: boolean): void {
    const emu = this.emu;
    if (!emu) return;
    emu.state.brokers.forEach((_, i) => { emu.broker(i).loopback = on; });
    emit('emulator-mqtt');
  }

  /** A message from "another client" of broker `broker`. False when nothing runs or there is no such broker. */
  inject(broker: number, topic: string, payload: string, retain = false): boolean {
    const emu = this.emu;
    if (!emu || !this.running || !emu.state.brokers[broker] || topic === '') return false;
    // The entry first: the delivery it causes is logged after it (shown above it).
    const entry = this.addLog({ kind: 'injected', broker, topic, payload, retain, time: Date.now(), note: '' });
    const matched = emu.broker(broker).inject(topic, payload, { retain });
    entry.note = matched ? 'a subscription matches' : retain ? 'no subscription matches: kept as retained' : 'no subscription matches: not delivered';
    emit('emulator-mqtt');
    return true;
  }

  clearLog(): void {
    this.log = [];
    const emu = this.emu;
    emu?.state.brokers.forEach((_, i) => emu.broker(i).clearLog());
    emit('emulator-mqtt');
  }

  /** Every broker's recorded session (retained messages, what came in, what went out); null when nothing runs. */
  exportSession(): SessionFile | null {
    const emu = this.emu;
    if (!emu) return null;
    return {
      format: 'freebutton-mqtt-session', version: 1, identity: emu.identity,
      brokers: emu.state.brokers.map((b, i) => ({ name: b.name, session: emu.broker(i).exportSession() })),
    };
  }

  /**
   * Replay a session file: its retained messages are set and its incoming
   * messages are injected again at their recorded times (brokers by name,
   * else by position). Returns what is wrong with the file, or ''.
   */
  importSession(file: unknown): string {
    const emu = this.emu;
    if (!emu || !this.running) return 'Run the virtual device first';
    const f = file as Partial<SessionFile> | null;
    if (!f || f.format !== 'freebutton-mqtt-session' || f.version !== 1 || !Array.isArray(f.brokers)) {
      return 'Not a FreeButton MQTT session file';
    }
    let events = 0;
    for (const [position, recorded] of f.brokers.entries()) {
      const byName = emu.state.brokers.findIndex(b => b.name === recorded.name);
      const index = byName >= 0 ? byName : position;
      if (!emu.state.brokers[index] || !recorded.session || !Array.isArray(recorded.session.events)) continue;
      try {
        const broker = emu.broker(index);
        broker.importSession(recorded.session);
        events += recorded.session.events.filter(ev => ev.dir === 'in').length;
        this.cancelReplay.push(this.replayLogged(index, recorded.session));
      } catch (err) {
        return err instanceof Error ? err.message : String(err);
      }
    }
    this.say(`MQTT session: replaying ${events} message(s)`, 'info');
    emit('emulator-mqtt');
    return '';
  }

  // ---- internals ---------------------------------------------------------------

  /** The session's incoming messages again, through inject() so each is logged. */
  private replayLogged(broker: number, session: MockSession): () => void {
    const incoming = session.events.filter(e => e.dir === 'in').sort((a, b) => a.t - b.t);
    const timers = incoming.map(e => setTimeout(() => { this.inject(broker, e.topic, e.payload, e.retain); }, Math.max(0, e.t)));
    return () => timers.forEach(clearTimeout);
  }

  private listen(): void {
    if (this.unlisten) return;
    this.unlisten = [on('console-eval', (e) => {
      if (e.detail.target !== 'virtual') return;
      const emu = this.emu;
      if (!emu || !this.running) {
        this.say('The virtual device is not running', 'error');
        return;
      }
      const result = emu.eval(e.detail.code);
      // An error was reported by the error event already.
      if (result.ok && result.value && result.value !== 'undefined') this.say(result.value, 'result');
    }),
    // What runs is the text Run took: say when the files moved on.
    on('editor-doc-change', () => {
      if (this.status !== 'running' || this.stale === 'config') return;
      const key = workspace.snapshot().currentKey;
      const next = key === CONFIG_KEY ? 'config' : key === MAIN_SCRIPT_KEY ? 'script' : null;
      if (!next || next === this.stale) return;
      this.stale = next;
      this.publish();
    })];
  }

  /** Stop, and stop listening to the page (a session that is thrown away: tests). */
  destroy(): void {
    this.stop();
    this.unlisten?.forEach(off => off());
    this.unlisten = null;
    this.used = false;
    this.publish();
  }

  private follow(emu: Emulator): void {
    this.offs.push(
      emu.on('console', line => this.say(line, 'log')),
      emu.on('error', error => this.onError(error)),
      emu.on('state', () => this.queueState()),
      emu.on('publish', (p) => {
        this.addLog({
          kind: 'published', broker: p.broker, topic: p.topic, payload: p.payload, retain: p.retain, time: Date.now(),
          note: publishNote(p.action, p.reason, p.log),
        });
        emit('emulator-mqtt');
      }),
      emu.on('subscribe', () => emit('emulator-mqtt')),
      emu.on('unsubscribe', () => emit('emulator-mqtt')),
      emu.on('exit', ({ reason }) => {
        if (reason === 'interrupted' || reason === 'panic') {
          // The error event gave the reason; nothing runs any more.
          queueMicrotask(() => {
            if (this.emu !== emu) return;
            const message = this.error;
            this.disposeEmulator();
            this.fail(message || `the script stopped (${reason})`, false);
          });
        }
      }),
    );
    emu.state.brokers.forEach((_, index) => {
      this.offs.push(emu.broker(index).onMessage((m) => {
        this.addLog({
          kind: 'delivered', broker: index, topic: m.topic, payload: m.payload, retain: m.retain, time: Date.now(),
          note: m.retain ? 'retained message for a new subscription' : 'to the script',
        });
        emit('emulator-mqtt');
      }));
    });
  }

  private onError(error: ScriptError): void {
    this.errors++;
    const at = error.line ? ` (line ${error.line}${error.column ? ':' + error.column : ''})` : '';
    this.error = `${error.message}${at}`;
    this.say(`${error.where}: ${error.message}${at}${error.stack ? '\n' + error.stack.trimEnd() : ''}`, 'error');
    // A marker in the editor when the script that failed is the open file.
    if (error.line && error.where !== 'eval' && workspace.snapshot().currentKey === MAIN_SCRIPT_KEY) {
      showRuntimeError({ line: error.line, column: error.column, message: `Virtual device: ${error.message}` });
    }
    this.publish();
  }

  /** One redraw per frame, however many changes a handler made. */
  private queueState(): void {
    if (this.stateQueued) return;
    this.stateQueued = true;
    const fire = () => {
      this.stateQueued = false;
      emit('emulator-state');
    };
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(fire);
    else setTimeout(fire, 16);
  }

  private addLog(entry: MqttLogEntry): MqttLogEntry {
    this.log.unshift(entry);
    if (this.log.length > MQTT_LOG_MAX) this.log.length = MQTT_LOG_MAX;
    return entry;
  }

  private fail(message: string, report = true): void {
    this.status = 'error';
    this.error = message;
    if (report) {
      this.errors++;
      this.say(`Virtual device did not start: ${message}`, 'error');
    }
    this.publish();
    emit('emulator-state');
    emit('emulator-mqtt');
  }

  private disposeEmulator(): void {
    this.cancelReplay.forEach(cancel => cancel());
    this.cancelReplay = [];
    this.releases.forEach(timer => clearTimeout(timer));
    this.releases.clear();
    this.pressedAt.clear();
    this.offs.forEach(off => off());
    this.offs = [];
    this.emu?.dispose();
    this.emu = null;
    showRuntimeError(null);
  }

  private say(text: string, type: 'log' | 'info' | 'error' | 'result'): void {
    emit('console-line', { text, type, to: 'virtual' });
  }

  private publish(): void {
    emit('emulator-change', this.snapshot());
  }
}

/** The file open in the editor as the user sees it, else the stored one. */
async function readFromWorkspace(): Promise<RunSource | null> {
  const open = workspace.snapshot().currentKey;
  const text = async (key: string): Promise<string | null> =>
    (open === key && getEditorView() ? getCode() : await workspace.readText(key));
  const script = await text(MAIN_SCRIPT_KEY);
  if (script === null) return null;
  return { script, config: (await text(CONFIG_KEY)) ?? '' };
}

export const emulatorSession = new EmulatorSession();
