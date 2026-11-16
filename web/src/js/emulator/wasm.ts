/**
 * Loader of fb-emulator.wasm and the marshalling between its imports and an
 * EmulatorHost.
 *
 * The wasm is a plain module (tools/js/build_emulator_wasm.sh): there is no
 * Emscripten loader script. It is fetched and compiled on first use, never
 * at IDE start, and the compiled module is kept for later instances.
 *
 * Imports ("env", tools/js/emulator/fbh.h): time, console, files, the
 * virtual hardware, the MQTT transport, and fbh_state: the device's display
 * as JSON records. tools/js/emulator/imports.txt lists them all;
 * emulator-seam.test.ts checks that this file provides exactly that list.
 */
import { STATE_KIND, type EmulatorHost, type KeyEventName, type LogLevel, type PublishKind, type ScriptErrorWhere } from './host';

/** Where the wasm comes from: bytes, a compiled module, or a URL to fetch. */
export type WasmSource = ArrayBuffer | Uint8Array | WebAssembly.Module | string;

/** Relative to the page, as the compiler's assets are. */
export const DEFAULT_WASM_URL = 'assets/fb-emulator.wasm';

let cached: { source: WasmSource; module: Promise<WebAssembly.Module> } | null = null;

/** Compiles the wasm once per source; later calls with the same source reuse the module. */
export function loadEmulatorModule(source: WasmSource = DEFAULT_WASM_URL): Promise<WebAssembly.Module> {
  if (source instanceof WebAssembly.Module) return Promise.resolve(source);
  if (cached && cached.source === source) return cached.module;
  const module = (async () => {
    if (typeof source !== 'string') return WebAssembly.compile(source as BufferSource);
    const response = await fetch(source);
    if (!response.ok) throw new Error(`cannot load the emulator (${source}): HTTP ${response.status}`);
    return WebAssembly.compile(await response.arrayBuffer());
  })();
  cached = { source, module };
  module.catch(() => { if (cached?.module === module) cached = null; });
  return module;
}

/** The fbe_* exports (tools/js/emulator/runner.c, device.cpp, mqtt_client_shim.cpp, tz.c). */
export interface EngineExports {
  memory: WebAssembly.Memory;
  _initialize(): void;
  fbe_init(heapBytes: number): number;
  fbe_device_boot(): number;
  fbe_alloc(size: number): number;
  fbe_free(ptr: number): void;
  fbe_error(): number;
  fbe_error_stack(): number;
  fbe_compile(source: number, len: number): number;
  fbe_compiled_image(): number;
  fbe_stdlib_id(): number;
  fbe_start(image: number, len: number, restore: number): number;
  fbe_stop(): void;
  fbe_running(): number;
  fbe_loop_pass(): number;
  fbe_eval(source: number, len: number): number;
  fbe_eval_result(): number;
  fbe_stats(out: number): void;
  fbe_limit(which: number): number;
  fbe_topic_matches(filter: number, topic: number, topicLen: number): number;
  fbe_button_tick(): number;
  fbe_button_inject(position: number): number;
  fbe_switch_pin(side: number): number;
  fbe_sample_sensors(): void;
  fbe_sensor_interval_ms(): number;
  fbe_sensor_value(id: number): number;
  fbe_mqtt_connected(broker: number, connected: number): void;
  fbe_mqtt_message(broker: number, topic: number, topicLen: number, payload: number, payloadLen: number): number;
  fbe_utc_offset(utcSeconds: number): number;
}

const SCRIPT_ERROR_WHERE: readonly ScriptErrorWhere[] = ['compile', 'load', 'run', 'callback', 'key', 'eval'];
const KEY_EVENTS: readonly (KeyEventName | undefined)[] = ['click', 'longpress', 'release'];
/** fbh.h FBH_PUBLISH_*. */
const PUBLISH_KINDS: readonly PublishKind[] = ['script', 'press', 'discovery', 'availability', 'sensor', 'state'];

type ImportFunction = (...args: number[]) => number | void;

/** One instance of the engine with its memory helpers. */
export class EngineInstance {
  readonly exports: EngineExports;
  private readonly encoder = new TextEncoder();
  private readonly decoder = new TextDecoder();

  constructor(module: WebAssembly.Module, private readonly host: EmulatorHost) {
    const instance = new WebAssembly.Instance(module, {
      env: this.envImports(),
      wasi_snapshot_preview1: this.wasiImports(),
    });
    this.exports = instance.exports as unknown as EngineExports;
    this.exports._initialize();
  }

  // --- memory ------------------------------------------------------------------

  /** A view of the memory as it is now (the buffer changes when the memory grows). */
  bytes(): Uint8Array {
    return new Uint8Array(this.exports.memory.buffer);
  }

  private view(): DataView {
    return new DataView(this.exports.memory.buffer);
  }

  readString(ptr: number, len: number): string {
    return this.decoder.decode(this.bytes().subarray(ptr, ptr + len));
  }

  readCString(ptr: number): string {
    if (ptr === 0) return '';
    const memory = this.bytes();
    let end = ptr;
    while (memory[end] !== 0) end++;
    return this.decoder.decode(memory.subarray(ptr, end));
  }

  /** Copies bytes into a new block (fbe_alloc), NUL-terminated. The caller frees it. */
  allocBytes(data: Uint8Array): number {
    const ptr = this.exports.fbe_alloc(data.length + 1);
    if (ptr === 0) throw new Error('emulator: out of wasm memory');
    const memory = this.bytes();
    memory.set(data, ptr);
    memory[ptr + data.length] = 0;
    return ptr;
  }

  /** Calls fn with `text` as a NUL-terminated UTF-8 string in wasm memory. */
  withString<T>(text: string, fn: (ptr: number, len: number) => T): T {
    const data = this.encoder.encode(text);
    const ptr = this.allocBytes(data);
    try {
      return fn(ptr, data.length);
    } finally {
      this.exports.fbe_free(ptr);
    }
  }

  /** Copies text into buf (at most cap bytes, no terminator); returns the bytes written. */
  private copyOut(text: string, buf: number, cap: number): number {
    const data = this.encoder.encode(text);
    const n = Math.min(data.length, cap);
    this.bytes().set(data.subarray(0, n), buf);
    return n;
  }

  // --- imports ---------------------------------------------------------------------

  /** The names of the "env" imports this loader provides (for the seam test). */
  static envImportNames(): string[] {
    const probe = Object.create(EngineInstance.prototype) as EngineInstance;
    return Object.keys(probe.envImports()).sort();
  }

  static wasiImportNames(): string[] {
    const probe = Object.create(EngineInstance.prototype) as EngineInstance;
    return Object.keys(probe.wasiImports()).sort();
  }

  private envImports(): Record<string, ImportFunction> {
    const h = (): EmulatorHost => this.host;
    const str = (ptr: number, len: number): string => this.readString(ptr, len);
    const cstr = (ptr: number): string => this.readCString(ptr);
    const bool = (value: boolean): number => (value ? 1 : 0);

    return {
      // --- time, console, runaway scripts ---
      fbh_now_ms: () => h().system.nowMs(),
      fbh_console: (text, len) => h().console.write(str(text, len)),
      fbh_log: (level, tag, text, len) => h().console.log(String.fromCharCode(level) as LogLevel, cstr(tag), str(text, len)),
      fbh_interrupt: () => bool(h().system.shouldInterrupt()),
      fbh_script_error: (where, message, messageLen, stack, stackLen) =>
        h().console.scriptError(SCRIPT_ERROR_WHERE[where] ?? 'callback', str(message, messageLen), str(stack, stackLen)),

      // --- identity, heap, files ---
      fbh_device_id: (buf, cap) => this.copyOut(h().system.deviceId(), buf, cap),
      fbh_free_heap: () => h().system.freeHeap() >>> 0,
      // uint8_t* fbh_file_read(const char* path, size_t path_len, uint32_t* len): a block the wasm frees.
      fbh_file_read: (path, pathLen, lenOut) => {
        const data = h().files.read(str(path, pathLen));
        if (!data) return 0;
        const ptr = this.allocBytes(data);
        this.view().setUint32(lenOut, data.length, true);
        return ptr;
      },
      fbh_media_exists: (key, len) => bool(h().hardware.mediaExists(str(key, len))),

      // --- the virtual hardware ---
      fbh_i2c_probe: (bus, address) => bool(h().hardware.i2cProbe(bus, address)),
      fbh_mcp_gpio: address => h().hardware.mcpGpio(address) & 0xff,
      fbh_sensor_read: input => h().hardware.sensorRead(input),
      fbh_led: (index, r, g, b) => h().hardware.led(index, r & 0xff, g & 0xff, b & 0xff),

      // --- the display ---
      fbh_state: (kind, json, len) => {
        const name = STATE_KIND[kind];
        if (name) h().display.state(name, JSON.parse(str(json, len)) as unknown);
      },

      // --- keys ---
      fbh_key_event: (position, event, prevented) => {
        const name = KEY_EVENTS[event];
        if (name) h().keyEvent(position, name, prevented !== 0);
      },

      // --- MQTT (mqtt_client_shim.cpp) ---
      fbh_mqtt_start: (broker, uri, uriLen, willTopic, willTopicLen, willPayload, willPayloadLen, willQos, willRetain) =>
        h().mqtt.start(broker, {
          uri: str(uri, uriLen),
          will: { topic: str(willTopic, willTopicLen), payload: str(willPayload, willPayloadLen), qos: willQos, retain: willRetain !== 0 },
        }),
      fbh_mqtt_publish: (broker, topic, topicLen, payload, payloadLen, qos, retain, origin, kind) =>
        (h().mqtt.publish(broker, str(topic, topicLen), str(payload, payloadLen), qos, retain !== 0, {
          origin: origin === 0 ? 'script' : 'firmware',
          kind: PUBLISH_KINDS[kind] ?? 'state',
        }) ? 0 : -1),
      fbh_mqtt_subscribe: (broker, topic, topicLen, qos) => h().mqtt.subscribe(broker, str(topic, topicLen), qos),
      fbh_mqtt_unsubscribe: (broker, topic, topicLen) => h().mqtt.unsubscribe(broker, str(topic, topicLen)),

      // --- emscripten runtime ---
      emscripten_notify_memory_growth: () => undefined,
    };
  }

  /**
   * libc's stdio. print() also writes to stdout (the device's UART); the
   * console callback already has that text, so stdout is dropped.
   */
  private wasiImports(): Record<string, ImportFunction> {
    return {
      fd_write: (_fd, iov, iovCount, writtenOut) => {
        const view = this.view();
        let total = 0;
        for (let i = 0; i < iovCount; i++) total += view.getUint32(iov + 8 * i + 4, true);
        view.setUint32(writtenOut, total, true);
        return 0;
      },
      fd_close: () => 0,
      fd_seek: () => 70 /* ESPIPE */,
    };
  }
}
