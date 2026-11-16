/**
 * The FreeButton emulator: the user's script running in the page, on the
 * firmware's own script engine and bindings (fb-emulator.wasm), against a
 * virtual device (DeviceState) and virtual brokers (MockBroker).
 *
 *   const emu = await Emulator.create({ config, files });
 *   emu.on('console', line => ...);
 *   emu.state.subscribe(change => ...);          // redraw what changed
 *   emu.run(source);                             // compile + start, as a deploy does
 *   emu.press('windowOpen'); emu.release('windowOpen');
 *   emu.broker(0).inject('home/temp', '21.5');   // a message from the broker
 *   emu.stop();
 *
 * The displays are drawn by the firmware's own LVGL in the wasm:
 *
 *   const emu = await Emulator.create({ config, files, media });   // fonts, images
 *   attachCanvas(emu, 'main', canvas);           // the device's pixels, 1:1 (canvas.ts)
 *   emu.layout('main');                          // where each config entry is, in pixels
 *
 * Without run() it shows the configured state (the design-time preview).
 * See docs/EMULATOR.md.
 *
 * The wasm is loaded by create(), on first use; importing this module costs
 * nothing.
 */
import { RealClock, type Clock, type TimerHandle } from './clock';
import { DeviceState, type ConnectorKind, type LayoutRect, type SensorSpec, type StateChange } from './device-state';
import { MEDIA_BUCKETS, type EmulatorHost, type KeyEventName, type LogLevel, type MediaBucket, type PublishKind, type PublishOrigin, type ScriptErrorWhere } from './host';
import { MediaCache, placeholderMedia, type MediaSource } from './media';
import { LIMIT } from './layout';
import { MockBroker, type MqttTransport, type TransportState } from './mqtt';
import {
  decidePublish, isVirtualIdentity, virtualIdentity, type PublishAction, type PublishDecision, type PublishMode,
  type TopicMatcher,
} from './publish-policy';
import { EngineInstance, loadEmulatorModule, type WasmSource } from './wasm';

export * from './clock';
export * from './device-state';
export * from './host';
export * from './mqtt';
export * from './publish-policy';
export { frontLedIndex, overlayPages } from './preview-overlay';
export { LIMIT } from './layout';
export { MediaCache, placeholderMedia, type MediaListener, type MediaSource } from './media';
export { attachCanvas, type CanvasContextLike, type CanvasLike, type FrameRect, type FrameSource } from './canvas';
export { DEFAULT_WASM_URL, EngineInstance, loadEmulatorModule, type WasmSource } from './wasm';

/** The device's JS heap (src/main.cpp). */
export const DEVICE_HEAP_BYTES = 16 * 1024;

export interface ScriptError {
  /**
   * compile / load / run: starting the script; callback: a timer, MQTT or
   * sensor handler threw; key: a key handler threw; eval: a console line;
   * interrupt: the script did not return in time; panic: the engine trapped.
   */
  where: ScriptErrorWhere | 'interrupt' | 'panic';
  message: string;
  stack?: string;
  /** 1-based position in the source, when the engine gives one. */
  line?: number;
  column?: number;
}

export type CompileResult = { ok: true; bytecode: Uint8Array } | { ok: false; error: ScriptError };
export type RunResult = { ok: true } | { ok: false; phase: 'compile' | 'load' | 'run'; error: ScriptError };
export type EvalResult = { ok: true; value: string } | { ok: false; error: ScriptError };

/** queued: the script will get it; unwanted: no subscription matches; dropped: the event ring is full; too-large: over the MQTT in-buffer; stopped: no script runs. */
export type DeliverResult = 'queued' | 'unwanted' | 'dropped' | 'too-large' | 'stopped';

export interface PublishEvent {
  broker: number;
  /** The topic the script gave. */
  requestedTopic: string;
  /** The topic used (the identity rule may have rewritten it). */
  topic: string;
  payload: string;
  qos: number;
  retain: boolean;
  time: number;
  /** Who published: a script's mqtt.publish(), or the firmware by itself. */
  origin: PublishOrigin;
  /** What it is: 'script', or of the firmware 'press' (a key's default action), 'discovery', 'availability', 'sensor', 'state'. */
  kind: PublishKind;
  /** mock: went to the in-page broker; send: to a real broker; block: nowhere (see `log`). */
  action: PublishAction;
  reason: PublishDecision['reason'];
  /** "would publish ..." for a blocked publish. */
  log?: string;
}

export interface EmulatorStats {
  /** As /api/device: the JS heap after a garbage collection. */
  jsHeapUsed: number;
  jsHeapFree: number;
  jsHeapSize: number;
  /** Pending setTimeout timers, and how many the firmware has room for. */
  timers: number;
  timerSlots: number;
  /** The script's MQTT subscriptions (all brokers). */
  subscriptions: number;
  /** MQTT events lost because the ring between MQTT and the script was full. */
  mqttDropped: number;
  mqttQueueUsed: number;
  mqttQueueBytes: number;
  running: boolean;
  /**
   * LVGL's memory pool (the device's: LV_MEM_SIZE of src/lv_conf.h), as
   * /api/device "lvglFree": free now, its size, and the least that was free.
   */
  lvglFree: number;
  lvglTotal: number;
  lvglLowest: number;
  /** Texts the firmware drew in one format, or not at all, because the pool was low (/api/device "textCollapsed", "textSkipped"). */
  textCollapsed: number;
  textSkipped: number;
  /** Bands LVGL flushed and their pixels, since boot. */
  flushes: number;
  flushedPixels: number;
  /** Bytes of fonts and images the wasm holds; font sizes made so far. */
  mediaBytes: number;
  fontInstances: number;
  /** LVGL animations running now (a list moving to its selection). */
  animations: number;
  /** Size of the wasm memory now (it only grows): the emulator's footprint in the page. */
  wasmMemoryBytes: number;
}

/** A display of the virtual device. */
export interface DisplayInfo {
  /** "main", or "key<position>" for the display of a bar key (position 1..8). */
  id: string;
  /** 0 = the main display, 1 + i = key display i (the firmware's order). */
  index: number;
  kind: 'main' | 'key';
  /** Device pixels. */
  width: number;
  height: number;
  /** For a key display: the key's position. */
  position?: number;
}

export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface Screenshot {
  width: number;
  height: number;
  /** RGBA, rows top to bottom; every colour is one the 16-bit panel has. */
  data: Uint8ClampedArray;
}

export interface EmulatorEvents {
  /** A line of print() / console.log() output (without the line end). */
  console: (line: string) => void;
  /** A firmware log line, as on the device's UART. */
  log: (entry: { level: LogLevel; tag: string; text: string }) => void;
  error: (error: ScriptError) => void;
  publish: (event: PublishEvent) => void;
  subscribe: (event: { broker: number; topic: string; qos: number }) => void;
  unsubscribe: (event: { broker: number; topic: string }) => void;
  /** The same records DeviceState.subscribe() gives. */
  state: (change: StateChange) => void;
  /** A key event after the script saw it; defaultPrevented = the handler returned false. */
  key: (event: { position: number; event: KeyEventName; defaultPrevented: boolean }) => void;
  /** The script no longer runs. */
  exit: (event: { reason: 'stopped' | 'restart' | 'interrupted' | 'panic' }) => void;
  /** LVGL drew: this rectangle of a display changed (readPixels has the pixels). */
  frame: (frame: { display: string } & Rect) => void;
  /** Widgets of a display moved, came or went (layout(display) has them). */
  layout: (event: { display: string; rects: readonly LayoutRect[] }) => void;
}

export interface TransportContext {
  /** Index in config.json "brokers" (the broker id scripts use). */
  index: number;
  name: string;
  url: string;
  clock: Clock;
  /** The firmware's MQTT topic filter match. */
  matches: TopicMatcher;
}

export interface EmulatorOptions {
  /** config.json, parsed or as text. */
  config: unknown;
  /** Files of the device's filesystem for load(): path -> content. */
  files?: Readonly<Record<string, string | Uint8Array>>;
  /** "virt_btn_" + 6 hex digits. Default: a new random one. Never a device's id. */
  identity?: string;
  /** JS heap size. Default: the device's 16 KB. */
  heapBytes?: number;
  /** Default: RealClock. Tests pass a ManualClock. */
  clock?: Clock;
  /** Default: fetch 'assets/fb-emulator.wasm'. */
  wasm?: WasmSource;
  /**
   * How long script code may run per entry into the engine (one start, one
   * handler, one timer) before it is stopped. Default 2000 ms.
   */
  interruptBudgetMs?: number;
  /** The clock for that budget (real time, not `clock`). Default: performance.now. */
  wallClock?: () => number;
  /**
   * 'restart' (default): the brokers are connected when the script starts,
   * as after a deploy: mqtt.onConnect handlers do not run.
   * 'cold': they connect after the script's top level ran, as after a boot.
   */
  boot?: 'restart' | 'cold';
  /** Default 'offline'. The live modes need a real transport (stage 3). */
  publishMode?: PublishMode;
  /** For the live modes: the device the config was written for (its id in topics is rewritten). */
  realId?: string;
  allowTopics?: readonly string[];
  allowDiscovery?: boolean;
  allowRetain?: boolean;
  /** One transport per broker of the config. Default: a MockBroker each. */
  transport?: (context: TransportContext) => MqttTransport;
  /** Module per connector id 0..3 (0 empty, 1 bar, 2 main display). Default: from the config. */
  connectors?: readonly ConnectorKind[];
  /** Default: DEFAULT_SENSORS. */
  sensors?: readonly SensorSpec[];
  /**
   * false: the firmware's 1 s sensor sample does not run (no sensor state is
   * published, sensor.onChange never fires). For tests that count publishes
   * or timers. Default true.
   */
  sampleSensors?: boolean;
  /**
   * The device's media store: fonts (bucket "fonts": "medium.ttf",
   * "semibold.ttf", the files of config.json "style.fonts") and image rasters
   * (bucket "images": "ico-<name>-<w>x<h>.bin", user files), as the firmware
   * reads them. A source may answer with a promise: create() waits for the
   * files the config uses. Pass one MediaCache to every emulator of a page so
   * a file is fetched once.
   *
   * Without it the device has no font files (text is drawn in the firmware's
   * built-in 12 px font, as on a device whose fonts bucket is empty) and
   * icons take their room but are not drawn (placeholderMedia).
   */
  media?: MediaSource | MediaCache;
  /**
   * Only without `media`: which icon rasters of the images bucket exist.
   * Default: every key.
   */
  mediaExists?: (key: string) => boolean;
  /** What system.freeHeap() reports. Default 40000. */
  freeHeapBytes?: number;
}

type Listeners = { [K in keyof EmulatorEvents]: Set<EmulatorEvents[K]> };

function parsePosition(error: { message: string; stack?: string }): { line?: number; column?: number } {
  const match = /:(\d+):(\d+)\)?\s*$/m.exec(error.stack ?? '') ?? /:(\d+):(\d+)\)?\s*$/m.exec(error.message);
  return match ? { line: Number(match[1]), column: Number(match[2]) } : {};
}

export class Emulator {
  /** The virtual device. Follow it with state.subscribe() or on('state'). */
  readonly state: DeviceState;
  /** The emulator's device id (system.id(), the MQTT client id). */
  readonly identity: string;
  readonly clock: Clock;
  /** The firmware's limits compiled into the wasm (LIMIT in layout.ts gives the indexes). */
  readonly limits: Readonly<Record<keyof typeof LIMIT, number>>;

  private engine: EngineInstance | null = null;
  private readonly module: WebAssembly.Module;
  private readonly options: EmulatorOptions;
  private readonly transports: MqttTransport[] = [];
  private readonly wills: Array<{ topic: string; payload: string; qos: number; retain: boolean }> = [];
  private readonly host: EmulatorHost;
  private readonly listeners: Listeners = {
    console: new Set(), log: new Set(), error: new Set(), publish: new Set(), subscribe: new Set(),
    unsubscribe: new Set(), state: new Set(), key: new Set(), exit: new Set(), frame: new Set(), layout: new Set(),
  };
  /** The fonts and images the firmware reads. */
  readonly media: MediaCache;
  private displayList: Array<DisplayInfo & { pixels: number }> = [];
  private displayTimer: TimerHandle | null = null;
  private displayDue = 0;
  private stopMedia: (() => void) | null = null;
  private readonly files = new Map<string, Uint8Array>();
  private readonly wall: () => number;
  private readonly budgetMs: number;

  private image: Uint8Array | null = null;
  private scriptRunning = false;
  /** > 0 while a call into the engine is on the stack. */
  private depth = 0;
  private inLoopPass = false;
  /** Messages that arrived while a loop pass ran. */
  private inbox: Array<{ broker: number; topic: string; payload: string }> = [];
  private deadline = Infinity;
  private interrupted = false;
  private pendingStop = false;
  private quietErrors = false;
  private lastError: ScriptError | null = null;
  private consoleLine = '';
  private lastStats: EmulatorStats | null = null;

  private idleScans = 0;
  private keyTimer: TimerHandle | null = null;
  private loopTimer: TimerHandle | null = null;
  private loopDue = 0;
  private sensorTimer: TimerHandle | null = null;
  private coldConnectTimer: TimerHandle | null = null;
  private disposed = false;

  /** Loads the wasm (first use only) and builds the virtual device from the config. No script runs yet. */
  static async create(options: EmulatorOptions): Promise<Emulator> {
    const identity = options.identity ?? virtualIdentity();
    if (!isVirtualIdentity(identity)) {
      throw new Error(`emulator identity must look like virt_btn_xxxxxx (6 hex digits), not "${identity}"`);
    }
    const module = await loadEmulatorModule(options.wasm);
    const media = options.media instanceof MediaCache
      ? options.media
      : new MediaCache(options.media ?? placeholderMedia(options.mediaExists));
    await askAhead(module, options, identity, media);
    return new Emulator(module, options, identity, media);
  }

  private constructor(module: WebAssembly.Module, options: EmulatorOptions, identity: string, media: MediaCache) {
    this.module = module;
    this.media = media;
    this.options = options;
    this.identity = identity;
    this.clock = options.clock ?? new RealClock();
    this.wall = options.wallClock ?? (() => performance.now());
    this.budgetMs = options.interruptBudgetMs ?? 2000;
    for (const [path, content] of deviceFiles(options)) this.files.set(path, content);

    const config = typeof options.config === 'string' ? safeParse(options.config) : options.config;
    this.state = new DeviceState(config, { connectors: options.connectors, sensors: options.sensors });
    this.state.subscribe(change => {
      this.emit('state', change);
      if (change.kind === 'layout') {
        const display = this.displayList[change.display];
        if (display) this.emit('layout', { display: display.id, rects: this.state.layout(change.display) });
      }
    });
    this.host = this.makeHost();

    const engine = this.boot();
    const limit = (which: number): number => engine.exports.fbe_limit(which);
    this.limits = Object.fromEntries(
      Object.entries(LIMIT).map(([name, which]) => [name, limit(which)]),
    ) as Record<keyof typeof LIMIT, number>;

    const mode = options.publishMode ?? 'offline';
    this.state.brokers.forEach((broker, index) => {
      const context: TransportContext = {
        index, name: broker.name, url: broker.url, clock: this.clock, matches: this.topicMatches,
      };
      const transport = options.transport ? options.transport(context) : new MockBroker(context);
      if (mode === 'offline' && !(transport instanceof MockBroker)) {
        throw new Error('publishMode "offline" only takes MockBroker transports');
      }
      this.transports.push(transport);
      transport.onMessage(message => { this.deliver(index, message.topic, message.payload); });
      transport.onState(state => this.onTransportState(index, state));
    });
    if ((options.boot ?? 'restart') === 'restart') this.connectTransports();
    this.startSensors();
    // A file that arrives or changes later: as an upload to the device.
    this.stopMedia = media.subscribe((bucket, key) => this.mediaChanged(bucket, key));
    // The configured state is on the displays when create() resolves.
    this.enter(() => engine.exports.fbe_display_render());
    this.scheduleDisplay();
  }

  // --- displays -------------------------------------------------------------------

  /** The displays of the device: the main display first, then the key displays. */
  displays(): DisplayInfo[] {
    return this.displayList.map(({ pixels: _pixels, ...info }) => info);
  }

  private displayOf(display: string | number): DisplayInfo & { pixels: number } {
    const found = typeof display === 'number' ? this.displayList[display] : this.displayList.find(d => d.id === display);
    if (!found) throw new Error(`the virtual device has no display "${display}"`);
    return found;
  }

  /**
   * The pixels of a display as LVGL last drew them: RGBA, rows top to
   * bottom, of the whole display or of `rect` (clipped to the display). A
   * copy; reading does not draw. May be called from a 'frame' listener.
   */
  readPixels(display: string | number, rect?: Rect): Uint8ClampedArray {
    const d = this.displayOf(display);
    const engine = this.engine;
    const x0 = Math.max(0, Math.min(d.width, Math.floor(rect?.x ?? 0)));
    const y0 = Math.max(0, Math.min(d.height, Math.floor(rect?.y ?? 0)));
    const w = Math.max(0, Math.min(d.width - x0, Math.floor(rect?.width ?? d.width)));
    const h = Math.max(0, Math.min(d.height - y0, Math.floor(rect?.height ?? d.height)));
    const out = new Uint8ClampedArray(w * h * 4);
    if (!engine || d.pixels === 0) return out;
    const memory = engine.bytes();
    if (w === d.width) {
      out.set(memory.subarray(d.pixels + y0 * d.width * 4, d.pixels + (y0 + h) * d.width * 4));
      return out;
    }
    for (let row = 0; row < h; row++) {
      const from = d.pixels + ((y0 + row) * d.width + x0) * 4;
      out.set(memory.subarray(from, from + w * 4), row * w * 4);
    }
    return out;
  }

  /**
   * Draws what is pending now, without waiting for LVGL's refresh period
   * (33 ms), as the firmware does before it takes a screenshot.
   */
  render(): void {
    this.assertNotInEngine('render');
    const engine = this.requireEngine();
    this.enter(() => engine.exports.fbe_display_render());
    this.afterEntry();
  }

  /** A display as it is after everything pending is drawn (render()). */
  screenshot(display: string | number): Screenshot {
    this.render();
    const d = this.displayOf(display);
    return { width: d.width, height: d.height, data: this.readPixels(d.index) };
  }

  /**
   * Where the widgets of a display are, in device pixels, each with the
   * `ref` of its config.json entry: for outlines, hit tests and drag
   * handles over the canvas. From LVGL's own layout (lv_obj_get_coords).
   */
  layout(display: string | number): readonly LayoutRect[] {
    return this.state.layout(this.displayOf(display).index);
  }

  /** @internal For tests of the firmware's low-memory paths: takes bytes out of LVGL's pool (0 gives them back). Returns what is free. */
  holdLvglMemory(bytes: number): number {
    this.assertNotInEngine('holdLvglMemory');
    const engine = this.requireEngine();
    return this.enter(() => engine.exports.fbe_lvgl_hold(bytes)) ?? 0;
  }

  private mediaChanged(bucket: MediaBucket, key: string): void {
    if (this.disposed) return;
    if (this.depth > 0) {
      queueMicrotask(() => this.mediaChanged(bucket, key));
      return;
    }
    const engine = this.engine;
    if (!engine) return;
    engine.withString(key, ptr => this.enter(() => engine.exports.fbe_media_changed(MEDIA_BUCKETS.indexOf(bucket), ptr)));
    this.afterEntry();
  }

  /** The LVGL task: runs when LVGL has a timer due (a refresh, an animation step, the clock). */
  private scheduleDisplay(): void {
    const engine = this.engine;
    if (!engine || this.disposed) return;
    const wait = engine.exports.fbe_display_wait() >>> 0;
    if (wait === 0xffffffff) {
      this.cancel('displayTimer');
      return;
    }
    const due = this.clock.now() + wait;
    if (this.displayTimer !== null && this.displayDue <= due) return;
    this.cancel('displayTimer');
    this.displayDue = due;
    this.displayTimer = this.clock.setTimer(() => {
      this.displayTimer = null;
      const now = this.engine;
      if (!now || this.disposed) return;
      this.enter(() => now.exports.fbe_display_task());
      this.scheduleDisplay();
    }, wait);
  }

  // --- events --------------------------------------------------------------------

  /** Listens for an event; returns the function that stops listening. */
  on<K extends keyof EmulatorEvents>(event: K, listener: EmulatorEvents[K]): () => void {
    const set = this.listeners[event] as Set<EmulatorEvents[K]>;
    set.add(listener);
    return () => { set.delete(listener); };
  }

  private emit<K extends keyof EmulatorEvents>(event: K, ...args: Parameters<EmulatorEvents[K]>): void {
    for (const listener of [...this.listeners[event]]) {
      try {
        (listener as (...a: Parameters<EmulatorEvents[K]>) => void)(...args);
      } catch (error) {
        // A listener must not unwind through the engine's frames.
        queueMicrotask(() => { throw error; });
      }
    }
  }

  // --- script ---------------------------------------------------------------------

  /** True while a script context exists (also after its top level threw: its handlers still run, as on the device). */
  get running(): boolean {
    return this.scriptRunning;
  }

  /** The transport of broker `index` (config.json "brokers" order). */
  transport(index: number): MqttTransport | undefined {
    return this.transports[index];
  }

  /** The MockBroker of broker `index`; throws when that broker has another transport or does not exist. */
  broker(index: number): MockBroker {
    const transport = this.transports[index];
    if (!(transport instanceof MockBroker)) throw new Error(`broker ${index} is not a mock broker`);
    return transport;
  }

  /** The firmware's MQTT filter match ('+', '#'; src/scripting/js/mqtt_topic_match.h). */
  readonly topicMatches: TopicMatcher = (filter, topic) => {
    const engine = this.engine;
    if (!engine) return filter === topic;
    return engine.withString(filter, filterPtr =>
      engine.withString(topic, (topicPtr, topicLen) =>
        engine.exports.fbe_topic_matches(filterPtr, topicPtr, topicLen) !== 0));
  };

  /** Source to bytecode with the engine the script will run on. Emits nothing. */
  compile(source: string): CompileResult {
    const engine = this.requireEngine();
    this.assertNotInEngine('compile');
    this.quietErrors = true;
    this.lastError = null;
    try {
      const size = this.enter(() => engine.withString(source, (ptr, len) => engine.exports.fbe_compile(ptr, len)));
      if (size === undefined || size < 0) {
        const error: ScriptError = this.lastError
          ?? { where: 'compile', message: engine.readCString(engine.exports.fbe_error()) || 'Compilation failed' };
        return { ok: false, error: { ...error, where: 'compile' } };
      }
      const at = engine.exports.fbe_compiled_image();
      return { ok: true, bytecode: engine.bytes().slice(at, at + size) };
    } finally {
      this.quietErrors = false;
    }
  }

  /**
   * Starts a script as the device does after a deploy: what runs is stopped,
   * the device state goes back to config.json, then the script's top level
   * runs. source: JavaScript, or bytecode from compile() (position
   * independent; not bytecode compiled for a device's ROM address).
   */
  run(script: string | Uint8Array): RunResult {
    this.assertNotInEngine('run');
    this.requireEngine();
    if (this.scriptRunning) this.emit('exit', { reason: 'restart' });
    if (typeof script === 'string') {
      const compiled = this.compile(script);
      if (!compiled.ok) {
        this.emit('error', compiled.error);
        return { ok: false, phase: 'compile', error: compiled.error };
      }
      this.image = compiled.bytecode;
    } else {
      this.image = script.slice();
    }
    return this.start(this.image);
  }

  /**
   * The firmware's script restart (ScriptEngine::restart): handlers, timers
   * and subscriptions go, the device state goes back to config.json, the
   * same script starts in a new context on the same heap.
   */
  restart(): RunResult {
    this.assertNotInEngine('restart');
    if (this.scriptRunning) this.emit('exit', { reason: 'restart' });
    return this.start(this.image);
  }

  /** Stops the script: no handlers, timers or subscriptions. The device state stays as the script left it. */
  stop(): void {
    if (this.depth > 0) {
      this.pendingStop = true;
      return;
    }
    this.stopScript('stopped');
  }

  /** A console line, evaluated in the running context (the device's REPL). */
  eval(code: string): EvalResult {
    this.assertNotInEngine('eval');
    const engine = this.requireEngine();
    // No script: a context with fb only, as a device without a boot script.
    if (!this.scriptRunning) this.start(null);
    this.lastError = null;
    const rc = this.enter(() => engine.withString(code, (ptr, len) => engine.exports.fbe_eval(ptr, len)));
    this.afterEntry();
    if (rc === 0) return { ok: true, value: engine.readCString(engine.exports.fbe_eval_result()) };
    const error: ScriptError = this.lastError
      ?? { where: 'eval', message: this.engine ? engine.readCString(engine.exports.fbe_error()) : 'the engine stopped' };
    return { ok: false, error };
  }

  // --- keys -----------------------------------------------------------------------

  /**
   * A key goes down (position 1..8 or its config name). The firmware's key
   * scan sees it at its next pass (every limits.keyScanMs) and from then on
   * produces click, long press and repeats from the time the key stays down.
   * false: no such key on this device.
   */
  press(key: number | string): boolean {
    const position = this.keyPosition(key);
    if (position === null) return false;
    this.state.setKey(position, true);
    this.startKeyScan();
    return true;
  }

  /** The key goes up. */
  release(key: number | string): boolean {
    const position = this.keyPosition(key);
    if (position === null) return false;
    this.state.setKey(position, false);
    this.startKeyScan();
    return true;
  }

  /** A short press without timing (as a press sent from Home Assistant): click, then release, now. */
  click(key: number | string): boolean {
    const position = this.keyPosition(key);
    const engine = this.engine;
    if (position === null || !engine) return false;
    this.assertNotInEngine('click');
    const rc = this.enter(() => engine.exports.fbe_button_inject(position));
    this.afterEntry();
    return rc === 0;
  }

  /** Keys that are held down now. */
  get pressedKeys(): number[] {
    return this.state.heldKeys();
  }

  // --- MQTT, sensors ------------------------------------------------------------------

  /**
   * A message arrives from broker `broker`, as from its transport. The
   * firmware takes it: its own subscribers (a config topic of an item or key,
   * a Home Assistant command) at once, a script subscription on the next
   * loop pass. 'unwanted': the device has no subscription for the topic.
   */
  deliver(broker: number, topic: string, payload: string): DeliverResult {
    const engine = this.engine;
    if (!engine) return 'stopped';
    if (this.inLoopPass) {
      // A publish of this pass came back (loopback): on the device the
      // answer of the broker arrives after the pass, never inside it.
      this.inbox.push({ broker, topic, payload });
      return 'queued';
    }
    const rc = engine.withString(topic, (topicPtr, topicLen) =>
      engine.withString(payload, (payloadPtr, payloadLen) =>
        engine.exports.fbe_mqtt_message(broker, topicPtr, topicLen, payloadPtr, payloadLen)));
    if (rc === 1 || rc === 2) {
      this.scheduleLoop(0);
      return 'queued';
    }
    // -3: no such broker, or its client is not connected: nothing is subscribed there.
    return rc === 0 || rc === -3 ? 'unwanted' : rc === -2 ? 'too-large' : 'dropped';
  }

  /**
   * A hardware sensor's reading (the BME280: ids 3 pressure in hPa, 4 humidity, 5 temperature; the STS35: 1, 2).
   * The firmware samples it at its next pass (every second): it publishes the state and tells the script's onChange.
   */
  setSensor(id: number, value: number): boolean {
    return this.state.setSensor(id, value);
  }

  // --- status --------------------------------------------------------------------------

  /** Engine numbers; jsHeap* are what /api/device reports. Inside a handler: the last measurement. */
  stats(): EmulatorStats {
    const engine = this.engine;
    if (!engine) {
      return this.lastStats = {
        jsHeapUsed: 0, jsHeapFree: 0, jsHeapSize: this.options.heapBytes ?? DEVICE_HEAP_BYTES, timers: 0,
        timerSlots: this.limits.timers, subscriptions: 0, mqttDropped: 0, mqttQueueUsed: 0,
        mqttQueueBytes: this.limits.mqttQueueBytes, running: false,
        lvglFree: 0, lvglTotal: 0, lvglLowest: 0, textCollapsed: 0, textSkipped: 0, flushes: 0, flushedPixels: 0,
        mediaBytes: 0, fontInstances: 0, animations: 0, wasmMemoryBytes: 0,
      };
    }
    if (this.depth > 0 && this.lastStats) return this.lastStats;
    const ptr = engine.exports.fbe_alloc(80);
    engine.exports.fbe_stats(ptr);
    engine.exports.fbe_lvgl_stats(ptr + 40);
    const v = new Uint32Array(engine.exports.memory.buffer.slice(ptr, ptr + 80));
    engine.exports.fbe_free(ptr);
    return this.lastStats = {
      jsHeapUsed: v[0]!, jsHeapFree: v[1]!, jsHeapSize: v[2]!, timers: v[3]!, timerSlots: v[4]!,
      mqttDropped: v[5]!, mqttQueueUsed: v[6]!, mqttQueueBytes: v[7]!, subscriptions: v[8]!, running: v[9] !== 0,
      lvglFree: v[10]!, lvglTotal: v[11]!, lvglLowest: v[12]!, flushes: v[13]!, flushedPixels: v[14]!,
      textCollapsed: v[15]!, textSkipped: v[16]!, mediaBytes: v[17]!, fontInstances: v[18]!, animations: v[19]!,
      wasmMemoryBytes: engine.exports.memory.buffer.byteLength,
    };
  }

  /** Size in bytes of the bytecode of the script that was started last (0: none). The device's limit is limits.bytecodeBytes. */
  get bytecodeSize(): number {
    return this.image?.length ?? 0;
  }

  /**
   * Seconds east of UTC of the device's local time at `atMs` (default: now on
   * the emulator's clock): the firmware's time zone rule (config.json
   * "core.timezone", a POSIX TZ string) with its daylight saving changes. A
   * renderer draws the clock's hands with it.
   */
  utcOffsetSeconds(atMs: number = this.clock.now()): number {
    return this.requireEngine().exports.fbe_utc_offset(Math.floor(atMs / 1000));
  }

  /**
   * The last will the firmware's MQTT client of broker `index` connects with
   * (<device>/status "offline", retained): what a transport hands its broker.
   */
  lastWill(index: number): { topic: string; payload: string; qos: number; retain: boolean } | undefined {
    return this.wills[index];
  }

  /** The ROM atoms id of the engine (compare with the device's /api/bytecode/next_address atoms_id). */
  get stdlibId(): number {
    return this.requireEngine().exports.fbe_stdlib_id() >>> 0;
  }

  /**
   * Runs loop passes now until nothing is due (queued MQTT events, due
   * timers), without waiting for the clock. For tests and "step" buttons;
   * the page does not need it.
   */
  flush(maxPasses = 1000): void {
    this.assertNotInEngine('flush');
    let next = 0;
    for (let i = 0; i < maxPasses && this.scriptRunning && next === 0; i++) next = this.runLoopPass();
    if (next >= 0) this.scheduleLoop(next === 0 ? this.limits.loopDelayMs : next);
  }

  /** Stops everything and releases the engine. The emulator cannot be used afterwards. */
  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    if (this.depth === 0) this.stopScript('stopped');
    for (const transport of this.transports) transport.disconnect();
    this.cancel('keyTimer');
    this.cancel('coldConnectTimer');
    this.cancel('displayTimer');
    this.stopMedia?.();
    this.stopMedia = null;
    this.engine = null;
    for (const set of Object.values(this.listeners)) set.clear();
  }

  // --- engine lifecycle ---------------------------------------------------------------

  private boot(): EngineInstance {
    const engine = new EngineInstance(this.module, this.host);
    this.engine = engine;
    const x = engine.exports;
    if (x.fbe_init(this.options.heapBytes ?? DEVICE_HEAP_BYTES) !== 0) {
      this.engine = null;
      throw new Error('emulator: cannot allocate the JS heap');
    }
    this.state.setSwitchPins(x.fbe_switch_pin(0), x.fbe_switch_pin(1));
    // The transports of an earlier engine (after a panic) are still there.
    this.transports.forEach((transport, index) => {
      x.fbe_mqtt_connected(index, transport.state === 'connected' ? 1 : 0);
    });
    // setup(): hardware scan, config.json, brokers, discovery sources.
    this.enter(() => x.fbe_device_boot());
    if (!this.engine) throw new Error('emulator: the firmware did not start');
    this.displayList = readDisplays(engine);
    return engine;
  }

  private requireEngine(): EngineInstance {
    if (this.disposed) throw new Error('emulator: disposed');
    // After a panic the instance is gone: a new one, with the same device.
    return this.engine ?? this.boot();
  }

  private assertNotInEngine(what: string): void {
    if (this.depth > 0) {
      throw new Error(`emulator: ${what}() called from inside a script callback; call it after the event returns`);
    }
  }

  /** A call into the engine: time budget, trap handling. undefined = the engine is gone. */
  private enter<T>(fn: () => T): T | undefined {
    if (!this.engine) return undefined;
    this.depth++;
    if (this.depth === 1) {
      this.deadline = this.wall() + this.budgetMs;
      this.interrupted = false;
    }
    try {
      return fn();
    } catch (error) {
      if (!(error instanceof WebAssembly.RuntimeError)) throw error;
      this.depth = 1;
      this.panic(error);
      return undefined;
    } finally {
      this.depth--;
      if (this.depth === 0) this.deadline = Infinity;
    }
  }

  /** After a call into the engine returned: stop requests, runaway scripts, new timers. */
  private afterEntry(): void {
    if (this.depth > 0) return;
    this.flushConsole();
    this.scheduleDisplay();
    if (this.interrupted) {
      this.interrupted = false;
      this.emit('error', {
        where: 'interrupt',
        message: `the script ran for more than ${this.budgetMs} ms without returning and was stopped. `
          + 'On the device it would block everything else (MQTT, timers, the console) until it returns.',
      });
      this.stopScript('interrupted');
      return;
    }
    if (this.pendingStop) {
      this.pendingStop = false;
      this.stopScript('stopped');
      return;
    }
    // The script may have set a timer: a pass finds out when it is due.
    if (this.scriptRunning) this.scheduleLoop(this.limits.loopDelayMs);
  }

  private panic(error: WebAssembly.RuntimeError): void {
    this.engine = null;
    const was = this.scriptRunning;
    this.scriptRunning = false;
    this.cancelAll();
    this.flushConsole();
    this.emit('error', {
      where: 'panic',
      message: `the script engine stopped (${error.message}). On the device this is a crash and a reboot. `
        + 'The emulator starts a new engine at the next run.',
    });
    if (was) this.emit('exit', { reason: 'panic' });
  }

  private start(image: Uint8Array | null): RunResult {
    const engine = this.requireEngine();
    this.cancel('loopTimer');
    this.cancel('sensorTimer');
    this.cancel('coldConnectTimer');
    const cold = (this.options.boot ?? 'restart') === 'cold';
    if (cold) for (const transport of this.transports) transport.disconnect();
    this.lastError = null;

    const ptr = image ? engine.allocBytes(image) : 0;
    this.state.noteRestart();
    const rc = this.enter(() => engine.exports.fbe_start(ptr, image ? image.length : 0, 1));
    if (this.engine && ptr !== 0) engine.exports.fbe_free(ptr);
    if (rc === undefined) {
      const error: ScriptError = { where: 'panic', message: 'the script engine stopped while starting the script' };
      return { ok: false, phase: 'run', error };
    }
    this.scriptRunning = rc !== -1;

    let result: RunResult = { ok: true };
    if (rc !== 0) {
      let error = this.lastError as ScriptError | null;
      if (!error) {
        error = { where: rc === -4 ? 'run' : 'load', message: engine.readCString(engine.exports.fbe_error()) };
        this.emit('error', error);
      }
      result = { ok: false, phase: rc === -4 ? 'run' : 'load', error };
    }
    if (cold) this.coldConnectTimer = this.clock.setTimer(() => { this.coldConnectTimer = null; this.connectTransports(); }, 0);
    this.afterEntry();
    if (this.scriptRunning) {
      this.scheduleLoop(0);
      this.startSensors();
    }
    return result;
  }

  private stopScript(reason: 'stopped' | 'interrupted'): void {
    const was = this.scriptRunning;
    this.scriptRunning = false;
    this.inbox = [];
    this.cancel('loopTimer');
    this.cancel('sensorTimer');
    this.cancel('coldConnectTimer');
    const engine = this.engine;
    if (engine) this.enter(() => engine.exports.fbe_stop());
    this.flushConsole();
    if (was) this.emit('exit', { reason });
  }

  // --- timers -----------------------------------------------------------------------------

  private cancel(which: 'keyTimer' | 'loopTimer' | 'sensorTimer' | 'coldConnectTimer' | 'displayTimer'): void {
    const handle = this[which];
    if (handle !== null) this.clock.clearTimer(handle);
    this[which] = null;
  }

  private cancelAll(): void {
    this.cancel('keyTimer');
    this.cancel('loopTimer');
    this.cancel('sensorTimer');
    this.cancel('coldConnectTimer');
    this.cancel('displayTimer');
  }

  /** One timer for the next loop pass, at the earliest time anything asked for. */
  private scheduleLoop(delayMs: number): void {
    if (!this.engine || this.disposed) return;
    const due = this.clock.now() + delayMs;
    if (this.loopTimer !== null && this.loopDue <= due) return;
    this.cancel('loopTimer');
    this.loopDue = due;
    this.loopTimer = this.clock.setTimer(() => {
      this.loopTimer = null;
      const next = this.runLoopPass();
      if (next === 0) this.scheduleLoop(this.limits.loopDelayMs);
      else if (next > 0) this.scheduleLoop(next);
    }, delayMs);
  }

  /** The firmware's loop(): queued MQTT events, one due timer. Returns ms to the next pass; 0 = more now; -1 = idle. */
  private runLoopPass(): number {
    const engine = this.engine;
    if (!engine) return -1;
    this.inLoopPass = true;
    let next: number | undefined;
    try {
      next = this.enter(() => engine.exports.fbe_loop_pass());
    } finally {
      this.inLoopPass = false;
    }
    // afterEntry would ask for another pass; this pass already knows when.
    this.flushConsole();
    this.scheduleDisplay();
    if (this.interrupted || this.pendingStop) {
      this.inbox = [];
      this.afterEntry();
      return -1;
    }
    if (this.inbox.length) {
      const arrived = this.inbox;
      this.inbox = [];
      let queued = false;
      for (const m of arrived) queued = this.deliver(m.broker, m.topic, m.payload) === 'queued' || queued;
      // deliver() asked for a pass at once; the device's next pass is one loop delay away.
      if (queued) {
        this.cancel('loopTimer');
        return 0;
      }
    }
    return next ?? -1;
  }

  private startKeyScan(): void {
    this.idleScans = 0;
    if (this.keyTimer !== null || this.disposed) return;
    const scan = (): void => {
      this.keyTimer = null;
      const engine = this.engine;
      if (!engine) return;
      this.enter(() => engine.exports.fbe_button_tick());
      this.afterEntry();
      // Two more scans after the last key went up: the firmware's history
      // needs them to see the release (and to take the next press as a click).
      this.idleScans = this.state.heldKeys().length === 0 ? this.idleScans + 1 : 0;
      if (this.idleScans < 3 && this.engine) this.keyTimer = this.clock.setTimer(scan, this.limits.keyScanMs);
    };
    this.keyTimer = this.clock.setTimer(scan, this.limits.keyScanMs);
  }

  /**
   * sampleSensors() of the firmware's loop(), every second from boot, script
   * or not: each sensor's state is published and a script's onChange hears
   * of a change.
   */
  private startSensors(): void {
    if (this.sensorTimer !== null || !this.engine || this.disposed || this.options.sampleSensors === false) return;
    const interval = this.engine.exports.fbe_sensor_interval_ms();
    const sample = (): void => {
      this.sensorTimer = null;
      const engine = this.engine;
      if (!engine || this.disposed) return;
      this.enter(() => engine.exports.fbe_sample_sensors());
      if (this.engine) {
        for (const sensor of this.state.sensors) {
          if (sensor.input < 0) this.state.noteSensorValue(sensor.id, engine.exports.fbe_sensor_value(sensor.id));
        }
      }
      this.afterEntry();
      if (this.engine) this.sensorTimer = this.clock.setTimer(sample, interval);
    };
    this.sensorTimer = this.clock.setTimer(sample, interval);
  }

  // --- MQTT -------------------------------------------------------------------------------

  private connectTransports(): void {
    for (const transport of this.transports) {
      if (transport.state === 'disconnected') transport.connect({ clientId: this.identity });
    }
  }

  private onTransportState(index: number, state: TransportState): void {
    const engine = this.engine;
    if (!engine || state === 'connecting') return;
    // The firmware's client: birth message and subscriptions at once, discovery on later passes.
    const tell = (): void => engine.exports.fbe_mqtt_connected(index, state === 'connected' ? 1 : 0);
    if (this.depth > 0) tell();
    else this.enter(tell);
    this.scheduleLoop(0);
    if (this.depth === 0) this.scheduleDisplay();
  }

  // --- the host the wasm calls ----------------------------------------------------------------

  private keyPosition(key: number | string): number | null {
    const position = typeof key === 'string' ? this.state.key(key)?.position : key;
    return position !== undefined && this.state.physicalKeys().includes(position) ? position : null;
  }

  private flushConsole(): void {
    if (this.consoleLine === '') return;
    const line = this.consoleLine;
    this.consoleLine = '';
    this.emit('console', line);
  }

  private makeHost(): EmulatorHost {
    return {
      hardware: this.state.hardware,
      media: { read: (bucket, key) => this.media.get(bucket, key) ?? null },
      display: {
        state: this.state.display.state,
        frame: (display, x, y, width, height) => {
          const info = this.displayList[display];
          if (info) this.emit('frame', { display: info.id, x, y, width, height });
        },
      },
      mqtt: {
        // The transports connect by the emulator's boot mode (connectTransports);
        // what the firmware's client asked for is kept for them (lastWill).
        start: (broker, options) => { this.wills[broker] = options.will; },
        publish: (broker, topic, payload, qos, retain, about) => {
          const transport = this.transports[broker];
          if (!transport) return false;
          const decision = decidePublish(
            { broker, topic, payload, qos, retain, origin: about.origin, kind: about.kind },
            {
              mode: this.options.publishMode ?? 'offline', virtualId: this.identity, realId: this.options.realId,
              allowTopics: this.options.allowTopics, allowDiscovery: this.options.allowDiscovery,
              allowRetain: this.options.allowRetain,
            },
            this.topicMatches,
          );
          this.emit('publish', {
            broker, requestedTopic: topic, topic: decision.topic, payload, qos, retain: decision.retain,
            time: this.clock.now(), origin: about.origin, kind: about.kind,
            action: decision.action, reason: decision.reason,
            ...(decision.log ? { log: decision.log } : {}),
          });
          if (decision.action !== 'block') {
            transport.publish({ topic: decision.topic, payload, qos, retain: decision.retain }, about);
          }
          return true;
        },
        subscribe: (broker, topic, qos) => {
          this.emit('subscribe', { broker, topic, qos });
          this.transports[broker]?.subscribe(topic, qos);
        },
        unsubscribe: (broker, topic) => {
          this.emit('unsubscribe', { broker, topic });
          this.transports[broker]?.unsubscribe(topic);
        },
      },
      system: {
        deviceId: () => this.identity,
        freeHeap: () => this.options.freeHeapBytes ?? 40000,
        nowMs: () => this.clock.now(),
        shouldInterrupt: () => {
          if (this.wall() <= this.deadline) return false;
          this.interrupted = true;
          return true;
        },
      },
      files: {
        read: path => this.files.get(normalizePath(path)) ?? null,
      },
      console: {
        write: text => {
          const lines = (this.consoleLine + text).split('\n');
          this.consoleLine = lines.pop() ?? '';
          for (const line of lines) this.emit('console', line);
        },
        log: (level, tag, text) => this.emit('log', { level, tag, text: text.replace(/\n$/, '') }),
        scriptError: (where, message, stack) => {
          const error: ScriptError = { where, message, ...(stack ? { stack } : {}) };
          Object.assign(error, parsePosition(error));
          this.lastError = error;
          if (!this.quietErrors) this.emit('error', error);
        },
      },
      keyEvent: (position, event, defaultPrevented) => this.emit('key', { position, event, defaultPrevented }),
    };
  }
}

/** The device's files: what the options give, and config.json as the firmware reads it. */
function deviceFiles(options: EmulatorOptions): Map<string, Uint8Array> {
  const files = new Map<string, Uint8Array>();
  for (const [path, content] of Object.entries(options.files ?? {})) {
    files.set(normalizePath(path), typeof content === 'string' ? new TextEncoder().encode(content) : content);
  }
  files.set('/config.json', new TextEncoder().encode(configText(options)));
  return files;
}

function configText(options: EmulatorOptions): string {
  return typeof options.config === 'string' ? options.config : JSON.stringify(options.config ?? {});
}

function readDisplays(engine: EngineInstance): Array<DisplayInfo & { pixels: number }> {
  const x = engine.exports;
  const out: Array<DisplayInfo & { pixels: number }> = [];
  const ptr = x.fbe_alloc(16);
  const count = x.fbe_display_count();
  for (let index = 0; index < count; index++) {
    if (!x.fbe_display_info(index, ptr)) continue;
    const v = new Int32Array(x.memory.buffer.slice(ptr, ptr + 16));
    const position = v[2]! * 2 + 1 + v[3]!;
    out.push(v[2]! < 0
      ? { id: 'main', index, kind: 'main', width: v[0]!, height: v[1]!, pixels: x.fbe_display_pixels(index) }
      : { id: `key${position}`, index, kind: 'key', width: v[0]!, height: v[1]!, position, pixels: x.fbe_display_pixels(index) });
  }
  x.fbe_free(ptr);
  return out;
}

/**
 * Which fonts and images does this config make the firmware open? The
 * firmware answers: it is started once on the side (no broker, no listener
 * hears of it), and what it asked the media source for is awaited. Again
 * while that brought new requests (a font's fallback). Only for a source
 * that answers later; a cache that already has the config's files skips it.
 */
async function askAhead(module: WebAssembly.Module, options: EmulatorOptions, identity: string, media: MediaCache): Promise<void> {
  const text = configText(options);
  if (!media.needsAsking(text)) return;
  const files = deviceFiles(options);
  const config = typeof options.config === 'string' ? safeParse(options.config) : options.config;
  const now = (): number => options.clock?.now() ?? Date.now();
  for (let round = 0; round < 4; round++) {
    const state = new DeviceState(config, { connectors: options.connectors, sensors: options.sensors });
    const host: EmulatorHost = {
      hardware: state.hardware,
      media: { read: (bucket, key) => media.get(bucket, key) ?? null },
      display: { state: () => undefined, frame: () => undefined },
      mqtt: { start: () => undefined, publish: () => true, subscribe: () => undefined, unsubscribe: () => undefined },
      system: { deviceId: () => identity, freeHeap: () => options.freeHeapBytes ?? 40000, nowMs: now, shouldInterrupt: () => false },
      files: { read: path => files.get(normalizePath(path)) ?? null },
      console: { write: () => undefined, log: () => undefined, scriptError: () => undefined },
      keyEvent: () => undefined,
    };
    try {
      const engine = new EngineInstance(module, host);
      if (engine.exports.fbe_init(options.heapBytes ?? DEVICE_HEAP_BYTES) !== 0) return;
      state.setSwitchPins(engine.exports.fbe_switch_pin(0), engine.exports.fbe_switch_pin(1));
      engine.exports.fbe_device_boot();
      engine.exports.fbe_display_render();
    } catch (error) {
      // The real start reports what is wrong.
      if (!(error instanceof WebAssembly.RuntimeError)) throw error;
      return;
    }
    if (media.pending === 0) break;
    await media.settle();
  }
  media.noteSettled(text);
}

function normalizePath(path: string): string {
  return '/' + path.replace(/^\/+/, '');
}

function safeParse(text: string): unknown {
  try {
    return JSON.parse(text) as unknown;
  } catch {
    return {};
  }
}
