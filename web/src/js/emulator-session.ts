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
 * in-page broker at every start ("offline"); the live modes and what they
 * may publish are emulator/live-session.ts (loaded with the engine).
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
import type { Emulator, ScriptError, SensorState } from './emulator';
import type { AskRequest, BrokerView, MqttLogEntry, MqttSession, MqttStatus, SessionFile } from './emulator/live-session';
import type { SocketFactory } from './emulator/mqtt-live';

type EmulatorModule = typeof import('./emulator/session-runtime');
type PublishMode = EmulatorSnapshot['mqtt'];

/** What Run starts with. */
export interface RunSource {
  script: string;
  config: string;
  /** Other text files of the device, for load(): path -> content. */
  files?: Record<string, string>;
}

export type { BrokerStatus, BrokerView, MqttLogEntry, MqttStatus, SessionFile } from './emulator/live-session';

const ID_KEY = 'fb-virtual-device-id';
const ID_PATTERN = /^virt_btn_[0-9a-f]{6}$/;
/** The MQTT log holds this many entries (emulator/live-session.ts). */
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
  /** Where MQTT goes, and the state of the live connection ('none' offline). */
  mqtt: PublishMode;
  mqttLink: EmulatorSnapshot['mqttLink'];
  /** config.json "brokers", by name. */
  brokers: string[] | null;
  sensors: number | null;
  /** The firmware the engine was built from, and the model it reports (DeviceState). */
  firmware: string | null;
  model: string | null;
  /** The POSIX TZ string the device's local time follows (config.json "core.timezone", else the firmware's default). */
  timezone: string | null;
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

  /** For tests: the question dialog, the socket of the live connection, whether the page counts as https. */
  ask: ((request: AskRequest) => Promise<string | null>) | null = null;
  socket: SocketFactory | undefined;
  securePage: boolean | undefined;
  liveTiming: { backoffMs?: number; keepAliveSec?: number } = {};

  private emu: Emulator | null = null;
  private mqtt: MqttSession | null = null;
  private api: EmulatorModule | null = null;
  private status: EmulatorSnapshot['status'] = 'idle';
  private error = '';
  private errors = 0;
  private stale: EmulatorSnapshot['stale'] = null;
  private startedAt = 0;
  /** What the firmware said of itself at the last Run (DeviceState): kept to show after Stop. */
  private built: { version: string; model: string; timezone: string } | null = null;
  private used = false;
  private basePages: PageConfig[] = [];
  private stateQueued = false;
  private runToken = 0;
  private offs: Array<() => void> = [];
  private unlisten: Array<() => void> | null = null;
  private pressedAt = new Map<number, number>();
  private releases = new Map<number, ReturnType<typeof setTimeout>>();

  snapshot(): EmulatorSnapshot {
    return {
      status: this.status, identity: virtualDeviceId(), mqtt: this.mqtt?.currentMode ?? 'offline',
      mqttLink: this.mqtt?.linkState() ?? 'none', error: this.error, errors: this.errors,
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

  /** Seconds east of UTC of the running device's local time (its time zone rule); 0 when nothing runs. */
  utcOffset(): number {
    try { return this.running ? this.emu!.utcOffsetSeconds(Date.now()) : 0; } catch { return 0; }
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
      // The first Run fetches the engine (about 170 KB compressed): a device serves that slowly.
      if (!this.api) this.say('Loading the virtual device (the first Run only)…', 'info');
      const api = this.api ??= await import('./emulator/session-runtime');
      if (token !== this.runToken) return;
      const mqtt = this.mqtt ??= new api.MqttSession({
        identity: virtualDeviceId,
        changed: () => emit('emulator-mqtt'),
        statusChanged: () => this.publish(),
        say: (text, type) => this.say(text, type),
        ask: request => (this.ask ?? askInApp)(request),
        storage: safeStorage(),
        will: index => this.emu?.lastWill(index),
        ...(this.socket ? { socket: this.socket } : {}),
        ...(this.securePage !== undefined ? { securePage: this.securePage } : {}),
        ...this.liveTiming,
      });
      // Other brokers than the run before: offline again. The same: the connection and the mode stay.
      mqtt.configure(source.config);
      const connectors = connectorKinds(getConnectors());
      this.emu = await this.api.Emulator.create({
        config: source.config.trim() === '' ? {} : source.config,
        files: source.files,
        identity: virtualDeviceId(),
        // Every publish reaches the broker link, which decides with the session's policy
        // (emulator/live-session.ts): the mode can change while the script runs.
        publishMode: 'live-publish', allowDiscovery: true, allowRetain: true,
        transport: context => mqtt.transport(context),
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
    this.built = { version: emu.state.firmware, model: emu.state.model, timezone: emu.state.timezone };
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
    // Nothing runs: no connection to a real broker stays open, and the next start is offline.
    this.mqtt?.stop();
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
    const base = {
      identity: virtualDeviceId(), status: this.status, error: this.status === 'error' ? this.error : '',
      mqtt: this.mqtt?.currentMode ?? 'offline', mqttLink: this.mqtt?.linkState() ?? 'none',
    };
    if (!emu || this.status !== 'running') {
      return {
        ...base, uptimeSeconds: null, heap: null, bytecode: null, timers: null, subscriptions: null, mqttDropped: null,
        brokers: null, sensors: null, firmware: this.built?.version || null, model: this.built?.model || null,
        timezone: this.built?.timezone || null, connectors: null,
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
      firmware: emu.state.firmware || null,
      model: emu.state.model || null,
      timezone: emu.state.timezone || null,
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

  /** The sensors whose reading can be set here: the hardware ones (the firmware computes its memory sensors itself). */
  sensors(): SensorState[] {
    return this.emu ? this.emu.state.sensors.filter(s => s.input >= 0).map(s => ({ ...s })) : [];
  }

  setSensor(id: number, value: number): boolean {
    return this.running && Number.isFinite(value) ? this.emu!.setSensor(id, value) : false;
  }

  // ---- MQTT (emulator/live-session.ts) ------------------------------------------

  brokers(): BrokerView[] {
    return this.running ? this.mqtt?.brokers() ?? [] : [];
  }

  /** Newest first. */
  mqttLog(): readonly MqttLogEntry[] {
    return this.mqtt?.entries() ?? [];
  }

  /** The mode, the connections and what is allowed; null before the first Run. */
  mqttStatus(): MqttStatus | null {
    return this.mqtt?.status() ?? null;
  }

  /** offline / live read-only / live publish, while a script runs. '' when done, else why not. */
  setMqttMode(mode: PublishMode): string {
    if (!this.running || !this.mqtt) return 'Run the virtual device first';
    return this.mqtt.setMode(mode);
  }

  /** The broker's WebSocket address (kept by the browser per broker host). */
  setBrokerUrl(index: number, url: string): void {
    this.mqtt?.setWsUrl(index, url);
  }

  /** live publish: keep the retain flag of what is published. */
  setSendRetained(on: boolean): void {
    this.mqtt?.setSendRetained(on);
  }

  /** live publish: let Home Assistant discovery and availability through. */
  setAnnounce(on: boolean): void {
    this.mqtt?.setAnnounce(on);
  }

  /** live publish: let the firmware's own sensor and state reports through. */
  setOwnStates(on: boolean): void {
    this.mqtt?.setOwnStates(on);
  }

  removeAnnouncement(): string {
    return this.mqtt?.removeAnnouncement() ?? 'Nothing was announced in this session';
  }

  get loopback(): boolean {
    return this.running && !!this.mqtt?.loopback;
  }

  /** A publish of the script comes back to its own matching subscriptions, as from a real broker (offline). */
  setLoopback(on: boolean): void {
    if (this.running) this.mqtt?.setLoopback(on);
  }

  /** A message handed to the script here (local, in every mode). False when nothing runs or there is no such broker. */
  inject(broker: number, topic: string, payload: string, retain = false): boolean {
    return this.running && !!this.mqtt?.inject(broker, topic, payload, retain);
  }

  clearLog(): void {
    this.mqtt?.clearLog();
  }

  /** Every broker's recorded session (retained messages, what came in, what went out); null when nothing runs. */
  exportSession(): SessionFile | null {
    return this.running ? this.mqtt?.exportSession() ?? null : null;
  }

  /** Replay a session file. Returns what is wrong with the file, or ''. */
  importSession(file: unknown): string {
    if (!this.running || !this.mqtt) return 'Run the virtual device first';
    return this.mqtt.importSession(file);
  }

  // ---- internals ---------------------------------------------------------------

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
    this.mqtt = null;
    this.publish();
  }

  private follow(emu: Emulator): void {
    this.offs.push(
      emu.on('console', line => this.say(line, 'log')),
      emu.on('error', error => this.onError(error)),
      emu.on('state', () => this.queueState()),
      // What the next publish is (who made it, what kind): the broker link decides with it.
      emu.on('publish', (p) => {
        const about = p as typeof p & { origin?: 'script' | 'firmware'; kind?: string };
        this.mqtt?.expect(p.broker, { topic: p.requestedTopic, origin: about.origin, kind: about.kind });
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

  private fail(message: string, report = true): void {
    this.status = 'error';
    this.error = message;
    this.mqtt?.stop();
    if (report) {
      this.errors++;
      this.say(`Virtual device did not start: ${message}`, 'error');
    }
    this.publish();
    emit('emulator-state');
    emit('emulator-mqtt');
  }

  private disposeEmulator(): void {
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

/** The browser's storage, looked up at each use (it can be absent or blocked). */
function safeStorage(): Pick<Storage, 'getItem' | 'setItem'> {
  return {
    getItem: (key) => { try { return localStorage.getItem(key); } catch { return null; } },
    setItem: (key, value) => { try { localStorage.setItem(key, value); } catch { /* not kept */ } },
  };
}

/** The IDE's in-app question dialog. */
async function askInApp(request: AskRequest): Promise<string | null> {
  const { askChoice } = await import('../components/fb-dialog.js');
  return askChoice(request);
}

export const emulatorSession = new EmulatorSession();
