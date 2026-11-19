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
 * script builds from system.id() are the virtual device's own. MQTT: a Run
 * connects to the brokers of config.json (live publish, under the rules of
 * emulator/live-session.ts, loaded with the engine) unless the user chose
 * live read-only or offline, which the browser remembers; a config without
 * brokers is offline (the broker in the page). The mode, the three
 * switches and each broker's address, user name and password can be set
 * while nothing runs (the back of the virtual device): mqttStatus() lists
 * the brokers of config.json as the workspace has it then.
 *
 *   virtual-broker-change   what the browser keeps for a broker host changed
 *
 * The face. The idle device: showPreview() (the virtual device is on screen)
 * makes an emulator from config.json without a script, what the device is at
 * boot, and makes a new one when config.json changes. face() is the emulator
 * the virtual device takes its layout from: the running one, else that idle
 * one; null while there is none. While a script runs its displays are shown
 * (the firmware's pixels); of the idle device only the layout is used: the
 * virtual device draws an outline with the name per element, no pixels. The
 * engine and the media are fetched when the virtual device is on screen, not
 * when the IDE loads.
 *
 *   emulator-face     face() or faceInfo() changed
 *
 * What the script changes is runtime state for drawing. It is never written
 * to config.json: edits of config.json go through config-edit.ts, from the
 * config model, and the virtual device does not offer them while this runs.
 */
import { emit, on, type ConnectorConfig, type EmulatorSnapshot, type PageConfig } from './app-state';
import { parseConfigText } from './config-model';
import { getConnectors } from './device-config';
import { getCode, getEditorView, showRuntimeError } from './editor';
import { oneAtATime } from './device-queue';
import { CONFIG_KEY, MAIN_SCRIPT_KEY } from './fs-rules';
import { workspace } from './workspace';
import { virtualMedia } from './virtual-media';
import type { Emulator, MediaCache, MediaSource, ScriptError, SensorState } from './emulator';
import type { AskRequest, BrokerView, MqttLogEntry, MqttSession, MqttStatus, SessionFile } from './emulator/live-session';
import type { SocketFactory } from './emulator/mqtt-live';
import { FirmwareNotes, isProblem, logMessage, type FirmwareLogEntry } from './emulator/firmware-log';
import {
  brokerConfigs, brokerSummary, browserStorage, clearBrokerSettings, defaultStartMode, readBrokerSettings, readStoredMode,
  writeBrokerSettings, writeStoredMode, type BrokerSettings,
} from './virtual-broker';

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
export type { BrokerSettings } from './virtual-broker';

const ID_KEY = 'fb-virtual-device-id';
const ID_PATTERN = /^virt_btn_[0-9a-f]{6}$/;
/** The MQTT log holds this many entries (emulator/live-session.ts). */
export const MQTT_LOG_MAX = 200;
/** A Run writes at most this many warnings and errors of the firmware to the Virtual console. */
export const FIRMWARE_LINES_MAX = 40;

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
  /** Where MQTT goes (while nothing runs: where the next Run's will), and the state of the live connection ('none' offline, and while nothing runs). */
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
  /**
   * LVGL's memory pool as the shown device has it (the device's size): free
   * now, its size, the least that was free. Null: no display is drawn by the
   * firmware.
   */
  lvgl: { free: number; total: number; lowest: number } | null;
  /** Texts the firmware drew without their formats (collapsed) or not at all (skipped) because that pool was low. */
  textCollapsed: number;
  textSkipped: number;
}

/** What the face of the virtual device is made from. */
export interface FaceInfo {
  /**
   * none     no config.json to lay out (the page shows its sample)
   * loading  the engine or the media are on their way
   * engine   the firmware in the page: its layout, and its pixels while a script runs
   * failed   the engine could not start: `note` says why
   */
  state: 'none' | 'loading' | 'engine' | 'failed';
  note: string;
  /** Something to know that is not an error (no font files, a part of config.json or a font file the firmware does not use). */
  hint: string;
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
  /** For tests: config.json for the idle device, instead of the workspace's. */
  readConfig: (() => string | null) | null = null;
  /** For tests: where the media comes from, instead of the device or the workspace (virtual-media.ts). */
  mediaSource: MediaSource | null = null;

  private emu: Emulator | null = null;
  private mqtt: MqttSession | null = null;
  private api: EmulatorModule | null = null;
  private loading: Promise<EmulatorModule> | null = null;
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
  /** The device as config.json makes it, no script: laid out while nothing runs. */
  private idle: Emulator | null = null;
  private idleKey = '';
  private media: MediaCache | null = null;
  private previewOn = false;
  private previewQueued = false;
  private previewToken = 0;
  private faceState: FaceInfo['state'] = 'none';
  private faceNote = '';
  /** The page the idle device shows (the stepper's). */
  private page = 1;
  /** What the firmware of an emulator said that belongs under the virtual device (emulator/firmware-log.ts). */
  private notes = new WeakMap<Emulator, FirmwareNotes>();
  /** The firmware's warnings this Run wrote to the Virtual console: each once. */
  private firmwareSaid = new Set<string>();
  private pressedAt = new Map<number, number>();
  private releases = new Map<number, ReturnType<typeof setTimeout>>();
  /** Chosen before the first Run (the engine's MqttSession holds them after it): the mode (null: the stored one, else the default) and the switches. */
  private early: { mode: PublishMode | null; sendRetained: boolean; announce: boolean; ownStates: boolean } = {
    mode: null, sendRetained: false, announce: false, ownStates: false,
  };

  snapshot(): EmulatorSnapshot {
    return {
      status: this.status, identity: virtualDeviceId(), mqtt: this.mqttMode(),
      mqttLink: this.mqtt?.linkState() ?? 'none', error: this.error, errors: this.errors,
      stale: this.stale, used: this.used,
    };
  }

  /** A script context exists: keys, MQTT and the console reach it. */
  get running(): boolean {
    return this.status === 'running' && !!this.emu;
  }

  // ---- the face ---------------------------------------------------------------

  /** The emulator the virtual device is drawn from: the running one, else the idle one; null: none (the page lays out the config model itself). */
  face(): Emulator | null {
    if (this.status === 'running' && this.emu && canDraw()) return this.emu;
    return this.idle;
  }

  faceInfo(): FaceInfo {
    const face = this.face();
    const engine = face !== null;
    return {
      state: engine ? 'engine' : this.faceState === 'engine' ? 'none' : this.faceState,
      note: engine ? '' : this.faceNote,
      hint: face ? [virtualMedia.hint(), this.notesOf(face).hint()].filter(Boolean).join('. ') : '',
    };
  }

  /**
   * What the firmware of `emu` said about config.json and the font files:
   * from its start (Emulator.bootLog) and, from the first call on, from
   * every later line of its log.
   */
  private notesOf(emu: Emulator): FirmwareNotes {
    let notes = this.notes.get(emu);
    if (!notes) {
      const made = notes = new FirmwareNotes(emu.bootLog);
      this.notes.set(emu, made);
      emu.on('log', (entry) => {
        if (made.add(entry) && this.face() === emu) emit('emulator-face');
      });
    }
    return notes;
  }

  /** A warning or an error of the running firmware: to the Virtual console, each text once per Run. */
  private sayFirmware(entry: FirmwareLogEntry): void {
    if (!isProblem(entry) || this.firmwareSaid.size >= FIRMWARE_LINES_MAX) return;
    const message = logMessage(entry);
    if (this.firmwareSaid.has(message)) return;
    this.firmwareSaid.add(message);
    this.say(`Firmware ${entry.level === 'E' ? 'error' : 'warning'}: ${message}`, entry.level === 'E' ? 'error' : 'warn');
  }

  /** The virtual device is on screen: lay out config.json with the firmware, now and after every change of it. */
  showPreview(): void {
    if (this.previewOn) return;
    this.previewOn = true;
    this.listen();
    virtualMedia.onStale = () => this.queuePreview();
    this.queuePreview();
  }

  /** The virtual device is gone: nothing is laid out any more. */
  hidePreview(): void {
    this.previewOn = false;
    this.previewToken++;
    virtualMedia.onStale = null;
    this.setIdle(null, 'none', '');
  }

  /**
   * Show a page of config.json on the idle device (the stepper looks through
   * its pages while nothing runs). Never on the running device: its page is
   * the script's own (display.showPage()), and a script that keeps the page
   * in a variable of its own would draw nothing on a page it did not choose.
   */
  showPage(page: number): void {
    this.page = page;
    if (this.idle && this.status !== 'running' && this.status !== 'starting') showPageOn(this.idle, page);
  }

  /** The main display as the firmware draws it now, at the device's resolution; null: no display is drawn by the firmware. */
  screenshot(): { name: string; width: number; height: number; rgba: Uint8ClampedArray } | null {
    const emu = this.face();
    if (!emu || !emu.displays().some(d => d.id === 'main')) return null;
    const shot = emu.screenshot('main');
    return { name: 'main', width: shot.width, height: shot.height, rgba: shot.data };
  }

  private queuePreview(): void {
    if (!this.previewOn || this.previewQueued) return;
    this.previewQueued = true;
    queueMicrotask(() => {
      this.previewQueued = false;
      void this.refreshPreview();
    });
  }

  private setIdle(emu: Emulator | null, state: FaceInfo['state'], note: string, key = ''): void {
    const old = this.idle;
    if (old === emu && state === this.faceState && note === this.faceNote) return;
    this.idle = emu;
    this.idleKey = emu ? key : '';
    this.faceState = state;
    this.faceNote = note;
    if (old && old !== emu) old.dispose();
    emit('emulator-face');
  }

  /** Bring the idle device in line with config.json as the workspace has it now. */
  private async refreshPreview(): Promise<void> {
    // While a script runs its device is the one shown; Stop comes back here.
    if (!this.previewOn || this.status === 'running' || this.status === 'starting') return;
    const token = ++this.previewToken;
    const text = this.readConfig ? this.readConfig() : workspace.configText();
    if (text === null || text.trim() === '') {
      this.setIdle(null, 'none', '');
      return;
    }
    const why = cannotDraw();
    drawable = why === '';
    if (why) {
      this.setIdle(null, 'failed', why);
      return;
    }
    let config: unknown;
    try {
      config = JSON.parse(text);
    } catch {
      // Mid-edit: what was laid out stays.
      if (!this.idle) this.setIdle(null, 'failed', 'config.json is not valid JSON');
      return;
    }
    const connectors = connectorKinds(getConnectors());
    const key = `${virtualMedia.generation}|${connectors?.join('') ?? ''}|${text}`;
    if (this.idle && key === this.idleKey) return;
    if (!this.idle) this.setIdle(null, 'loading', '');
    try {
      const api = await this.loadApi();
      if (token !== this.previewToken) return;
      virtualMedia.useConfig(config, path => workspace.readQuietly(path));
      const emu = await api.Emulator.create({
        config: text,
        identity: virtualDeviceId(),
        media: this.mediaCache(api),
        // Nothing runs: no sensor reports, no broker but the one in the page.
        sampleSensors: false,
        ...(this.wasm ? { wasm: this.wasm } : {}),
        ...(connectors ? { connectors } : {}),
      });
      if (token !== this.previewToken || !this.previewOn) {
        emu.dispose();
        return;
      }
      if (this.page !== 1) showPageOn(emu, this.page);
      this.setIdle(emu, 'engine', '', key);
    } catch (err) {
      if (token !== this.previewToken) return;
      this.setIdle(null, 'failed', err instanceof Error ? err.message : String(err));
    }
  }

  /**
   * The emulator's code and its wasm (about 320 KB compressed), fetched once.
   * Both come from the device when it serves the IDE, so each is one turn of
   * the device queue: a response of that size next to the storage requests
   * of the IDE's start took the device's free memory down to 1 KB.
   */
  private loadApi(): Promise<EmulatorModule> {
    return this.loading ??= (async () => {
      const api = await oneAtATime(() => import('./emulator/session-runtime'));
      if (!this.wasm) this.wasm = await oneAtATime(() => api.loadEmulatorModule());
      this.api = api;
      return api;
    })().catch((error: unknown) => { this.loading = null; throw error; });
  }

  /** The page's media: one cache for every emulator, so a file is fetched once. */
  private mediaCache(api: EmulatorModule): MediaCache {
    if (!this.media) {
      this.media = new api.MediaCache(this.mediaSource ?? { read: virtualMedia.read });
      virtualMedia.attach(this.media);
      // A font that arrived late (or not at all) changes what the face has to say;
      // of another version of a font file the firmware says again whether it can use it.
      this.media.subscribe((bucket, key) => {
        if (bucket === 'fonts') for (const emu of [this.idle, this.emu]) if (emu) this.notes.get(emu)?.fontChanged(key);
        emit('emulator-face');
      });
    }
    return this.media;
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
      if (!this.api) this.say('Loading the virtual device…', 'info');
      const api = await this.loadApi();
      if (token !== this.runToken) return;
      const mqtt = this.mqtt ??= new api.MqttSession({
        identity: virtualDeviceId,
        changed: () => emit('emulator-mqtt'),
        statusChanged: () => this.publish(),
        say: (text, type) => this.say(text, type),
        ask: request => (this.ask ?? askInApp)(request),
        storage: browserStorage(),
        start: { ...this.early, mode: this.chosenMode() },
        will: index => this.emu?.lastWill(index),
        ...(this.socket ? { socket: this.socket } : {}),
        ...(this.securePage !== undefined ? { securePage: this.securePage } : {}),
        ...this.liveTiming,
      });
      // Other brokers than the run before: their connections end. The same: the connection stays.
      mqtt.configure(source.config);
      if (source.config.trim() !== '') virtualMedia.useConfig(JSON.parse(source.config), path => workspace.readQuietly(path));
      // A font or raster that was not there (or could not be read) is asked for again.
      virtualMedia.askAgain();
      const connectors = connectorKinds(getConnectors());
      this.emu = await api.Emulator.create({
        config: source.config.trim() === '' ? {} : source.config,
        files: source.files,
        identity: virtualDeviceId(),
        // Every publish reaches the broker link, which decides with the session's policy
        // (emulator/live-session.ts): the mode can change while the script runs.
        publishMode: 'live-publish', allowDiscovery: true, allowRetain: true,
        transport: context => mqtt.transport(context),
        media: this.mediaCache(api),
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
    this.firmwareSaid.clear();
    this.follow(emu);
    const mode = this.mqttMode();
    const brokers = this.mqtt?.status().brokers ?? [];
    const where = mode === 'offline'
      ? (brokers.length === 0 ? 'MQTT offline: config.json has no brokers' : 'MQTT offline: in-page broker')
      : `MQTT ${mode === 'live-publish' ? 'live' : 'live, read-only'}: connecting to ${brokers.map(b => b.wsUrl).join(', ')}`;
    this.say(`Virtual device ${emu.identity} started (${where})`, 'info');
    // What the firmware said while it read config.json and the fonts: no listener heard it.
    for (const entry of emu.bootLog) this.sayFirmware(entry);
    this.notesOf(emu);
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
    // The script runs: the real brokers are connected to now (not for a script that did not start).
    this.mqtt?.begin();
    this.publish();
    emit('emulator-face');
    emit('emulator-state');
    emit('emulator-mqtt');
  }

  /** Stop the script and release the engine. The device shows config.json's outlines again. */
  stop(): void {
    this.runToken++;
    const was = this.status;
    this.disposeEmulator();
    this.status = 'idle';
    this.error = '';
    this.stale = null;
    // Nothing runs: no connection to a real broker stays open. The mode stays for the next Run.
    this.mqtt?.stop();
    if (was === 'running') this.say('Virtual device stopped', 'info');
    this.publish();
    // The idle device is laid out again, on the page looked at, with config.json as it is now.
    if (this.idle) showPageOn(this.idle, this.page);
    emit('emulator-face');
    this.queuePreview();
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
      mqtt: this.mqttMode(), mqttLink: this.mqtt?.linkState() ?? 'none',
    };
    if (!emu || this.status !== 'running') {
      return {
        ...base, uptimeSeconds: null, heap: null, bytecode: null, timers: null, subscriptions: null, mqttDropped: null,
        brokers: null, sensors: null, firmware: this.built?.version || null, model: this.built?.model || null,
        timezone: this.built?.timezone || null, connectors: null, ...this.fit(),
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
      ...this.fit(stats),
    };
  }

  /** Whether what is laid out fits the device: LVGL's pool and the texts it could not draw. */
  private fit(stats = this.face()?.stats()): Pick<VirtualInfo, 'lvgl' | 'textCollapsed' | 'textSkipped'> {
    if (!stats || !this.face() || stats.lvglTotal <= 0) return { lvgl: null, textCollapsed: 0, textSkipped: 0 };
    return {
      lvgl: { free: stats.lvglFree, total: stats.lvglTotal, lowest: stats.lvglLowest },
      textCollapsed: stats.textCollapsed, textSkipped: stats.textSkipped,
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

  /** The mode chosen: in this page, else the one the browser remembers, else the default (live publish). */
  private chosenMode(): PublishMode {
    return this.mqtt?.chosenMode ?? this.early.mode ?? readStoredMode() ?? defaultStartMode();
  }

  /** The mode in use, or the one the next Run starts in. */
  private mqttMode(): PublishMode {
    return this.mqtt && (this.status === 'running' || this.status === 'starting') ? this.mqtt.currentMode : this.chosenMode();
  }

  /**
   * The mode, the brokers with their connections, and what is allowed.
   * While a script runs: of the config it runs with. Else: of config.json
   * as the workspace has it now, no connection (what the next Run uses).
   */
  mqttStatus(): MqttStatus {
    if (this.mqtt && (this.status === 'running' || this.status === 'starting')) return this.mqtt.status();
    const text = this.readConfig ? this.readConfig() : workspace.configText();
    const brokers = brokerConfigs(text ?? '').map((b, index) => ({ ...brokerSummary(b, index), state: 'disconnected' as const, error: null }));
    const optIns = this.mqtt?.optIns ?? this.early;
    return {
      mode: this.chosenMode(), sendRetained: optIns.sendRetained, announce: optIns.announce, ownStates: optIns.ownStates,
      announced: 0, securePage: this.securePage ?? (typeof location !== 'undefined' && location.protocol === 'https:'), brokers,
    };
  }

  /**
   * Live publish / live read-only / offline. Running or not: while nothing
   * runs it holds for the next Run. The browser remembers it. '' when done,
   * else why it is not in use now.
   */
  setMqttMode(mode: PublishMode): string {
    writeStoredMode(mode);
    if (this.mqtt) return this.mqtt.setMode(mode);
    this.early.mode = mode;
    if (mode !== 'live-publish') this.early = { mode, sendRetained: false, announce: false, ownStates: false };
    this.publish();
    return '';
  }

  /** What the browser keeps for a broker host (address, user name, password); null: nothing, config.json's values are used. */
  brokerSettings(host: string): BrokerSettings | null {
    return readBrokerSettings(host);
  }

  /**
   * Keep an address, user name and password for a broker host (an empty
   * field: config.json's value). A running connection that used other
   * values is made again. '' when kept, else why not.
   */
  saveBrokerSettings(host: string, settings: BrokerSettings): string {
    if (!writeBrokerSettings(host, settings)) return 'Could not store the settings (browser storage blocked?)';
    this.brokerSettingsChanged(host);
    return '';
  }

  /** Forget what the browser keeps for a broker host: config.json's values again. */
  resetBrokerSettings(host: string): void {
    clearBrokerSettings(host);
    this.brokerSettingsChanged(host);
  }

  private brokerSettingsChanged(host: string): void {
    this.mqtt?.settingsChanged();
    emit('virtual-broker-change', { host });
    this.publish();
  }

  /** live publish: keep the retain flag of what is published. */
  setSendRetained(on: boolean): void {
    if (this.mqtt) this.mqtt.setSendRetained(on);
    else this.setEarly({ sendRetained: on });
  }

  /** live publish: send Home Assistant discovery and the availability messages (never by itself: config.json "hassdiscovery" does not switch it on). */
  setAnnounce(on: boolean): void {
    if (this.mqtt) this.mqtt.setAnnounce(on);
    else this.setEarly({ announce: on });
  }

  /** live publish: let the firmware's own sensor and state reports through. */
  setOwnStates(on: boolean): void {
    if (this.mqtt) this.mqtt.setOwnStates(on);
    else this.setEarly({ ownStates: on });
  }

  /** A switch set before the first Run: only in live publish, as after it. */
  private setEarly(change: Partial<{ sendRetained: boolean; announce: boolean; ownStates: boolean }>): void {
    const publishing = this.chosenMode() === 'live-publish';
    for (const [key, on] of Object.entries(change)) this.early[key as 'sendRetained' | 'announce' | 'ownStates'] = on && publishing;
    this.publish();
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
    }),
    // config.json as the virtual device shows it changed (stored, or the editor's draft).
    on('device-config', () => this.queuePreview())];
  }

  /** Stop, and stop listening to the page (a session that is thrown away: tests). */
  destroy(): void {
    this.stop();
    this.hidePreview();
    this.media = null;
    virtualMedia.attach(null);
    this.unlisten?.forEach(off => off());
    this.unlisten = null;
    this.used = false;
    this.mqtt = null;
    this.early = { mode: null, sendRetained: false, announce: false, ownStates: false };
    this.publish();
  }

  private follow(emu: Emulator): void {
    this.offs.push(
      emu.on('console', line => this.say(line, 'log')),
      emu.on('log', entry => this.sayFirmware(entry)),
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
    if (this.idle) showPageOn(this.idle, this.page);
    emit('emulator-face');
    emit('emulator-state');
    emit('emulator-mqtt');
    this.queuePreview();
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

  private say(text: string, type: 'log' | 'info' | 'warn' | 'error' | 'result'): void {
    emit('console-line', { text, type, to: 'virtual' });
  }

  private publish(): void {
    emit('emulator-change', this.snapshot());
  }
}

/** Why the firmware's displays cannot be shown in this browser; '' when they can. */
function cannotDraw(): string {
  if (typeof WebAssembly !== 'object') return 'this browser has no WebAssembly';
  try {
    if (!document.createElement('canvas').getContext('2d')) return 'this browser has no 2D canvas';
  } catch {
    return 'this browser has no 2D canvas';
  }
  return '';
}

let drawable: boolean | null = null;
function canDraw(): boolean {
  return drawable ??= cannotDraw() === '';
}

/** For tests that give or take the canvas: ask the browser again. */
export function _forgetCanDraw(): void {
  drawable = null;
}

/**
 * Show a page of the idle device's main display. The firmware changes its
 * page only when a script asks (display.showPage): the idle device gets a
 * script of that one line (evaluated, once it has run one). Only for the
 * idle device: never for a device a user's script runs in.
 */
function showPageOn(emu: Emulator, page: number): void {
  if (!emu.state.connectors.includes(2) || emu.state.shownPage === page) return;
  const code = `fb.main.display.showPage(${Math.trunc(page)})`;
  if (emu.running) emu.eval(code); else emu.run(code);
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

/** The IDE's in-app question dialog. */
async function askInApp(request: AskRequest): Promise<string | null> {
  const { askChoice } = await import('../components/fb-dialog.js');
  return askChoice(request);
}

export const emulatorSession = new EmulatorSession();
