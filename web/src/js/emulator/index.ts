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
 * Nothing here draws: a renderer follows emu.state. Nothing leaves the page:
 * stage 1 has only the in-page MockBroker. See docs/EMULATOR.md.
 *
 * The wasm is loaded by create(), on first use; importing this module costs
 * nothing.
 */
import { RealClock, type Clock, type TimerHandle } from './clock';
import { DeviceState, type ConnectorKind, type SensorSpec, type StateChange } from './device-state';
import type { EmulatorHost, KeyEventName, LogLevel, ScriptErrorWhere } from './host';
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
export { DISPLAY_ITEM_INFO, LAYOUT_ORDER, LIMIT, SENSOR_INFO } from './layout';
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
  /** What system.freeHeap() reports. Default 40000. */
  freeHeapBytes?: number;
}

type Listeners = { [K in keyof EmulatorEvents]: Set<EmulatorEvents[K]> };

const ALL_KEYS_UP = 0xff;

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
  private readonly host: EmulatorHost;
  private readonly listeners: Listeners = {
    console: new Set(), log: new Set(), error: new Set(), publish: new Set(), subscribe: new Set(),
    unsubscribe: new Set(), state: new Set(), key: new Set(), exit: new Set(),
  };
  private readonly files = new Map<string, Uint8Array>();
  private readonly bootTime: number;
  private readonly wall: () => number;
  private readonly budgetMs: number;

  private image: Uint8Array | null = null;
  private scriptRunning = false;
  /** The script's top level is running: it may already subscribe and get retained messages. */
  private starting = false;
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

  private gpio = ALL_KEYS_UP;
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
    return new Emulator(module, options, identity);
  }

  private constructor(module: WebAssembly.Module, options: EmulatorOptions, identity: string) {
    this.module = module;
    this.options = options;
    this.identity = identity;
    this.clock = options.clock ?? new RealClock();
    this.bootTime = this.clock.now();
    this.wall = options.wallClock ?? (() => performance.now());
    this.budgetMs = options.interruptBudgetMs ?? 2000;
    for (const [path, content] of Object.entries(options.files ?? {})) {
      this.files.set(normalizePath(path), typeof content === 'string' ? new TextEncoder().encode(content) : content);
    }

    const config = typeof options.config === 'string' ? safeParse(options.config) : options.config;
    this.state = new DeviceState(config, { connectors: options.connectors, sensors: options.sensors });
    this.state.subscribe(change => this.emit('state', change));
    this.host = this.makeHost();

    const engine = this.boot();
    const limit = (which: number): number => engine.exports.fbe_limit(which);
    this.limits = Object.fromEntries(
      Object.entries(LIMIT).map(([name, which]) => [name, limit(which)]),
    ) as Record<keyof typeof LIMIT, number>;
    engine.setFileLimit(this.limits.bytecodeBytes);
    this.state.setLedCount(this.limits.leds);

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
    this.gpio &= ~(1 << (position - 1));
    this.startKeyScan();
    return true;
  }

  /** The key goes up. */
  release(key: number | string): boolean {
    const position = this.keyPosition(key);
    if (position === null) return false;
    this.gpio |= 1 << (position - 1);
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
    const out: number[] = [];
    for (let i = 0; i < 8; i++) if ((this.gpio & (1 << i)) === 0) out.push(i + 1);
    return out;
  }

  // --- MQTT, sensors ------------------------------------------------------------------

  /**
   * A message arrives from broker `broker`, as from its transport. The
   * firmware's filter decides whether a script subscription wants it; the
   * script gets it on the next loop pass.
   */
  deliver(broker: number, topic: string, payload: string): DeliverResult {
    const engine = this.engine;
    if (!engine || !(this.scriptRunning || this.starting)) return 'stopped';
    if (this.inLoopPass) {
      // A publish of this pass came back (loopback): on the device the
      // answer of the broker arrives after the pass, never inside it.
      this.inbox.push({ broker, topic, payload });
      return 'queued';
    }
    const rc = engine.withString(topic, (topicPtr, topicLen) =>
      engine.withString(payload, (payloadPtr, payloadLen) =>
        engine.exports.fbe_mqtt_message(broker, topicPtr, topicLen, payloadPtr, payloadLen)));
    if (rc === 1) {
      this.scheduleLoop(0);
      return 'queued';
    }
    return rc === 0 ? 'unwanted' : rc === -2 ? 'too-large' : 'dropped';
  }

  /** A sensor's reading. The script's onChange hears it at the next sample (every second, as on the device). */
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
      };
    }
    if (this.depth > 0 && this.lastStats) return this.lastStats;
    const ptr = engine.exports.fbe_alloc(40);
    engine.exports.fbe_stats(ptr);
    const v = new Uint32Array(engine.exports.memory.buffer.slice(ptr, ptr + 40));
    engine.exports.fbe_free(ptr);
    return this.lastStats = {
      jsHeapUsed: v[0]!, jsHeapFree: v[1]!, jsHeapSize: v[2]!, timers: v[3]!, timerSlots: v[4]!,
      mqttDropped: v[5]!, mqttQueueUsed: v[6]!, mqttQueueBytes: v[7]!, subscriptions: v[8]!, running: v[9] !== 0,
    };
  }

  /** Size in bytes of the bytecode of the script that was started last (0: none). The device's limit is limits.bytecodeBytes. */
  get bytecodeSize(): number {
    return this.image?.length ?? 0;
  }

  /**
   * What the firmware in the wasm calls itself: system.version() and
   * system.model(), asked in the script's context (the wasm has no export
   * for them). Null while no script context exists or inside a callback.
   */
  firmware(): { version: string; model: string } | null {
    if (!this.scriptRunning || this.depth > 0 || !this.engine) return null;
    const version = this.eval('system.version()');
    const model = this.eval('system.model()');
    if (!version.ok || !model.ok) return null;
    return { version: version.value, model: model.value };
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
    for (const broker of this.state.brokers) engine.withString(broker.name, ptr => x.fbe_broker_add(ptr));
    this.transports.forEach((transport, index) => {
      x.fbe_broker_set_connected(index, transport.state === 'connected' ? 1 : 0, 1);
    });
    for (const position of this.state.physicalKeys()) x.fbe_button_add(position);
    for (const sensor of this.state.sensors) {
      engine.withString(sensor.name, name =>
        engine.withString(sensor.type, type =>
          engine.withString(sensor.unit, unit => x.fbe_sensor_add(sensor.id, name, type, unit, sensor.online ? 1 : 0))));
    }
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
    this.starting = true;
    let rc: number | undefined;
    try {
      rc = this.enter(() => engine.exports.fbe_start(ptr, image ? image.length : 0, 1));
    } finally {
      this.starting = false;
    }
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

  private cancel(which: 'keyTimer' | 'loopTimer' | 'sensorTimer' | 'coldConnectTimer'): void {
    const handle = this[which];
    if (handle !== null) this.clock.clearTimer(handle);
    this[which] = null;
  }

  private cancelAll(): void {
    this.cancel('keyTimer');
    this.cancel('loopTimer');
    this.cancel('sensorTimer');
    this.cancel('coldConnectTimer');
  }

  /** One timer for the next loop pass, at the earliest time anything asked for. */
  private scheduleLoop(delayMs: number): void {
    if (!this.scriptRunning || this.disposed) return;
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
    if (!engine || !this.scriptRunning) return -1;
    this.inLoopPass = true;
    let next: number | undefined;
    try {
      next = this.enter(() => engine.exports.fbe_loop_pass());
    } finally {
      this.inLoopPass = false;
    }
    // afterEntry would ask for another pass; this pass already knows when.
    this.flushConsole();
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
      this.enter(() => engine.exports.fbe_button_tick(this.gpio));
      this.afterEntry();
      // Two more scans after the last key went up: the firmware's history
      // needs them to see the release (and to take the next press as a click).
      this.idleScans = this.gpio === ALL_KEYS_UP ? this.idleScans + 1 : 0;
      if (this.idleScans < 3 && this.engine) this.keyTimer = this.clock.setTimer(scan, this.limits.keyScanMs);
    };
    this.keyTimer = this.clock.setTimer(scan, this.limits.keyScanMs);
  }

  /** sampleSensors(): every sensor's reading to the firmware, which decides about onChange. */
  private startSensors(): void {
    if (this.sensorTimer !== null || this.state.sensors.length === 0 || !this.engine) return;
    const interval = this.engine.exports.fbe_sensor_interval_ms();
    const sample = (): void => {
      this.sensorTimer = null;
      const engine = this.engine;
      if (!engine || !this.scriptRunning) return;
      this.enter(() => {
        for (const sensor of this.state.sensors) engine.exports.fbe_sensor_notify(sensor.id, sensor.value);
      });
      this.afterEntry();
      if (this.scriptRunning) this.sensorTimer = this.clock.setTimer(sample, interval);
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
    // Queued like a message; the firmware drops it while no script uses MQTT.
    engine.exports.fbe_broker_set_connected(index, state === 'connected' ? 1 : 0, 0);
    this.scheduleLoop(0);
  }

  // --- the host the wasm calls ----------------------------------------------------------------

  private keyPosition(key: number | string): number | null {
    const position = typeof key === 'string' ? this.state.host.keys.findByName(key) : key;
    return position !== null && this.state.physicalKeys().includes(position) ? position : null;
  }

  private flushConsole(): void {
    if (this.consoleLine === '') return;
    const line = this.consoleLine;
    this.consoleLine = '';
    this.emit('console', line);
  }

  private makeHost(): EmulatorHost {
    return {
      ...this.state.host,
      mqtt: {
        publish: (broker, topic, payload, qos, retain) => {
          const transport = this.transports[broker];
          if (!transport) return false;
          const decision = decidePublish(
            { broker, topic, payload, qos, retain, origin: 'script' },
            {
              mode: this.options.publishMode ?? 'offline', virtualId: this.identity, realId: this.options.realId,
              allowTopics: this.options.allowTopics, allowDiscovery: this.options.allowDiscovery,
              allowRetain: this.options.allowRetain,
            },
            this.topicMatches,
          );
          this.emit('publish', {
            broker, requestedTopic: topic, topic: decision.topic, payload, qos, retain: decision.retain,
            time: this.clock.now(), action: decision.action, reason: decision.reason,
            ...(decision.log ? { log: decision.log } : {}),
          });
          if (decision.action !== 'block') transport.publish({ topic: decision.topic, payload, qos, retain: decision.retain });
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
        deviceName: () => this.state.deviceName || this.identity,
        uptimeSeconds: () => Math.floor((this.clock.now() - this.bootTime) / 1000),
        freeHeap: () => this.options.freeHeapBytes ?? 40000,
        nowMs: () => this.clock.now(),
        shouldInterrupt: () => {
          if (this.wall() <= this.deadline) return false;
          this.interrupted = true;
          return true;
        },
      },
      files: {
        load: path => this.files.get(normalizePath(path)) ?? null,
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
      restoreConfigState: () => this.state.restoreConfigState(),
    };
  }
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
