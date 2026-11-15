/**
 * Loader of fb-emulator.wasm and the marshalling between its imports (the C
 * seam) and an EmulatorHost.
 *
 * The wasm is a plain module (tools/js/build_emulator_wasm.sh): there is no
 * Emscripten loader script. It is fetched and compiled on first use, never
 * at IDE start, and the compiled module is kept for later instances.
 *
 * Imports ("env"):
 *   - seam functions the host implements under their C name (displayitem_*,
 *     button_hw_set_label, led_hw_set_color, ...): the C declaration decides
 *     the arguments (NUL-terminated strings, pointer + length, out pointers,
 *     the DisplayItemInfo struct of layout.ts);
 *   - fbh_* (tools/js/emulator/fbh.h): what is left of a seam function after
 *     the firmware's rule in front of it ran inside the wasm.
 * tools/js/emulator/imports.txt lists them all; emulator-seam.test.ts checks
 * that this file provides exactly that list.
 */
import type { EmulatorHost, KeyEventName, LogLevel, ScriptErrorWhere } from './host';
import { DISPLAY_ITEM_INFO as INFO } from './layout';

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

/** The fbe_* exports (tools/js/emulator/runner.c, seam.cpp, seam_mqtt.cpp). */
export interface EngineExports {
  memory: WebAssembly.Memory;
  _initialize(): void;
  fbe_init(heapBytes: number): number;
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
  fbe_layout(which: number): number;
  fbe_topic_matches(filter: number, topic: number, topicLen: number): number;
  fbe_button_add(position: number): number;
  fbe_buttons_clear(): void;
  fbe_button_tick(gpio: number): number;
  fbe_button_inject(position: number): number;
  fbe_sensors_clear(): void;
  fbe_sensor_add(id: number, name: number, type: number, unit: number, online: number): void;
  fbe_sensor_set_online(id: number, online: number): void;
  fbe_sensor_notify(id: number, value: number): void;
  fbe_sensor_interval_ms(): number;
  fbe_broker_add(name: number): number;
  fbe_broker_count(): number;
  fbe_broker_set_connected(id: number, connected: number, quiet: number): void;
  fbe_mqtt_message(broker: number, topic: number, topicLen: number, payload: number, payloadLen: number): number;
}

const SCRIPT_ERROR_WHERE: readonly ScriptErrorWhere[] = ['compile', 'load', 'run', 'callback', 'key', 'eval'];
const KEY_EVENTS: readonly (KeyEventName | undefined)[] = ['click', 'longpress', 'release'];

type ImportFunction = (...args: number[]) => number | void;

/** One instance of the engine with its memory helpers. */
export class EngineInstance {
  readonly exports: EngineExports;
  /** Limits for files load() may read (file_hardware.h), set after instantiation. */
  private maxFileBytes = 128 * 1024;
  /** Block for displayitem_get_info: the struct, then its two strings. */
  private infoPtr = 0;
  private infoSize = 0;
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

  /** Copies text into out as a NUL-terminated string of at most capacity bytes (cut at a UTF-8 boundary). */
  private copyCString(text: string, out: number, capacity: number): void {
    if (out === 0 || capacity === 0) return;
    const data = this.encoder.encode(text);
    let n = Math.min(data.length, capacity - 1);
    while (n > 0 && n < data.length && (data[n]! & 0xc0) === 0x80) n--;
    const memory = this.bytes();
    memory.set(data.subarray(0, n), out);
    memory[out + n] = 0;
  }

  setFileLimit(bytes: number): void {
    this.maxFileBytes = bytes;
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
    const u32 = (ptr: number, value: number): void => this.view().setUint32(ptr, value >>> 0, true);
    const i32 = (ptr: number, value: number): void => this.view().setInt32(ptr, value | 0, true);

    return {
      // --- button_hardware.h ---
      button_hw_set_label: (position, label) => (h().keys.setLabel(position, cstr(label)) ? 0 : -1),
      button_hw_set_top_label: (position, label) => (h().keys.setTopLabel(position, cstr(label)) ? 0 : -1),
      button_hw_set_font: (position, face, size, top) => (h().keys.setFont(position, { face, size }, top !== 0) ? 0 : -1),
      button_hw_restore_font: (position, top) => (h().keys.restoreFont(position, top !== 0) ? 0 : -1),
      button_hw_get_label: (position, out, capacity) => {
        const label = h().keys.getLabel(position);
        this.copyCString(label ?? '', out, capacity);
        return label === null ? -1 : 0;
      },
      button_hw_get_top_label: (position, out, capacity) => {
        const label = h().keys.getTopLabel(position);
        this.copyCString(label ?? '', out, capacity);
        return label === null ? -1 : 0;
      },
      button_hw_find_by_name: (name, len, positionOut) => {
        const position = h().keys.findByName(str(name, len));
        if (position === null || positionOut === 0) return 0;
        i32(positionOut, position);
        return 1;
      },
      fbh_key_set_icon: (position, name, len) => (h().keys.setIcon(position, str(name, len)) ? 0 : -1),
      fbh_key_event: (position, event, prevented) => {
        const name = KEY_EVENTS[event];
        if (name) h().keyEvent(position, name, prevented !== 0);
      },

      // --- led_hardware.h ---
      led_hw_set_color: (position, r, g, b) => (h().leds.setColor(position, r & 0xff, g & 0xff, b & 0xff) ? 0 : -1),

      // --- displayitem_binding.h ---
      displayitem_create: (x, y, width, boxType, fontSize, alignment, label, unit, round, page) =>
        h().display.create({ x, y, width, boxType, fontSize, alignment, label: cstr(label), unit: cstr(unit), round, page }) >>> 0,
      displayitem_remove: id => bool(h().display.remove(id >>> 0)),
      displayitem_find_by_name: (name, len, idOut) => {
        const id = h().display.findByName(str(name, len));
        if (id === null || idOut === 0) return 0;
        u32(idOut, id);
        return 1;
      },
      displayitem_count: () => h().display.count(),
      displayitem_get_info: id => this.writeItemInfo(id >>> 0),
      displayitem_set_label: (id, label) => bool(h().display.setLabel(id >>> 0, cstr(label))),
      displayitem_set_value: (id, value) => bool(h().display.setValue(id >>> 0, cstr(value))),
      displayitem_set_unit: (id, unit) => bool(h().display.setUnit(id >>> 0, cstr(unit))),
      fbh_item_set_icon: (id, name, len) => (h().display.setIcon(id >>> 0, str(name, len)) ? 1 : 0),
      displayitem_set_font: (id, face, size) => bool(h().display.setFont(id >>> 0, { face, size })),
      displayitem_restore_font: id => bool(h().display.restoreFont(id >>> 0)),
      displayitem_set_position: (id, x, y) => bool(h().display.setPosition(id >>> 0, x, y)),
      displayitem_set_page: (id, page) => bool(h().display.setPage(id >>> 0, page)),
      displayitem_show_page: page => bool(h().display.showPage(page)),
      displayitem_get_shown_page: () => h().display.shownPage(),
      displayitem_get_all_ids: (idsOut, maxCount) => {
        if (idsOut === 0 || maxCount <= 0) return 0;
        const ids = h().display.allIds().slice(0, maxCount);
        ids.forEach((id, i) => u32(idsOut + 4 * i, id));
        return ids.length;
      },

      // --- list_binding.h ---
      list_hw_find: (name, len) => h().lists.findByName(str(name, len)),
      list_hw_set_font: (index, face, size) => bool(h().lists.setFont(index, { face, size })),
      list_hw_restore_font: index => bool(h().lists.restoreFont(index)),
      fbh_list_count: index => h().lists.count(index),
      fbh_list_get_selected: index => h().lists.selected(index),
      fbh_list_set_selected: (index, selected) => h().lists.setSelected(index, selected),
      fbh_list_set_items: (index, options, len, count, selected) =>
        h().lists.setItems(index, count > 0 ? str(options, len).split('\n') : [], selected),

      // --- image_binding.h ---
      image_hw_find: (name, len) => h().images.findByName(str(name, len)),
      fbh_image_exists: index => bool(h().images.exists(index)),
      fbh_image_set: (index, kind, value, len) =>
        h().images.set(index, kind === 0x69 /* 'i' */ ? 'icon' : 'src', str(value, len)),

      // --- ui/fonts/fonts.h ---
      fonts_find: (name, len) => h().fonts.find(str(name, len)),
      fonts_find_id: id => h().fonts.findId(id),

      // --- mqtt (MqttBroker in seam_mqtt.cpp) ---
      fbh_mqtt_publish: (broker, topic, topicLen, payload, payloadLen, qos, retain) =>
        (h().mqtt.publish(broker, str(topic, topicLen), str(payload, payloadLen), qos, retain !== 0) ? 0 : -1),
      fbh_mqtt_subscribe: (broker, topic, topicLen, qos) => h().mqtt.subscribe(broker, str(topic, topicLen), qos),
      fbh_mqtt_unsubscribe: (broker, topic, topicLen) => h().mqtt.unsubscribe(broker, str(topic, topicLen)),

      // --- sensors, topology, system ---
      fbh_sensor_sample: id => h().sensors.sample(id),
      fbh_connector_type: connector => h().topology.connectorType(connector),
      fbh_main_connector: () => h().topology.mainConnector(),
      fbh_device_id: (buf, cap) => this.copyOut(h().system.deviceId(), buf, cap),
      fbh_device_name: (buf, cap) => this.copyOut(h().system.deviceName(), buf, cap),
      fbh_uptime_seconds: () => h().system.uptimeSeconds() >>> 0,
      fbh_free_heap: () => h().system.freeHeap() >>> 0,
      fbh_now_ms: () => h().system.nowMs(),
      fbh_interrupt: () => bool(h().system.shouldInterrupt()),
      fbh_restore_config_state: () => h().restoreConfigState(),

      // --- file_hardware.h: uint8_t* file_hw_load(const char* filename, int* plen, size_t max_size) ---
      file_hw_load: (filename, lenOut, maxSize) => {
        const data = h().files.load(cstr(filename));
        const limit = maxSize > 0 ? maxSize : this.maxFileBytes;
        if (!data || data.length === 0 || data.length > limit) return 0;
        // The glue frees the block (source) or keeps it (bytecode).
        const ptr = this.allocBytes(data);
        if (lenOut !== 0) i32(lenOut, data.length);
        return ptr;
      },

      // --- console ---
      fbh_console: (text, len) => h().console.write(str(text, len)),
      fbh_log: (level, tag, text, len) => h().console.log(String.fromCharCode(level) as LogLevel, cstr(tag), str(text, len)),
      fbh_script_error: (where, message, messageLen, stack, stackLen) =>
        h().console.scriptError(SCRIPT_ERROR_WHERE[where] ?? 'callback', str(message, messageLen), str(stack, stackLen)),

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
      environ_sizes_get: (countOut, sizeOut) => {
        const view = this.view();
        view.setUint32(countOut, 0, true);
        view.setUint32(sizeOut, 0, true);
        return 0;
      },
      environ_get: () => 0,
    };
  }

  /** displayitem_get_info: a pointer to a DisplayItemInfo that stays valid until the next call; 0 = not found. */
  private writeItemInfo(id: number): number {
    const info = this.host.display.getInfo(id);
    if (!info) return 0;
    const label = this.encoder.encode(info.label);
    const unit = this.encoder.encode(info.unit);
    const size = INFO.size + label.length + 1 + unit.length + 1;
    if (size > this.infoSize) {
      if (this.infoPtr !== 0) this.exports.fbe_free(this.infoPtr);
      this.infoSize = Math.max(size, 256);
      this.infoPtr = this.exports.fbe_alloc(this.infoSize);
      if (this.infoPtr === 0) {
        this.infoSize = 0;
        return 0;
      }
    }
    const base = this.infoPtr;
    const labelPtr = base + INFO.size;
    const unitPtr = labelPtr + label.length + 1;
    const memory = this.bytes();
    memory.set(label, labelPtr);
    memory[labelPtr + label.length] = 0;
    memory.set(unit, unitPtr);
    memory[unitPtr + unit.length] = 0;
    const view = this.view();
    view.setUint32(base + INFO.id, info.id >>> 0, true);
    view.setInt32(base + INFO.x, info.x, true);
    view.setInt32(base + INFO.y, info.y, true);
    view.setInt32(base + INFO.width, info.width, true);
    view.setInt32(base + INFO.boxType, info.boxType, true);
    view.setInt32(base + INFO.fontSize, info.fontSize, true);
    view.setInt32(base + INFO.alignment, info.alignment, true);
    view.setUint32(base + INFO.label, labelPtr, true);
    view.setUint32(base + INFO.unit, unitPtr, true);
    view.setInt32(base + INFO.round, info.round, true);
    view.setInt32(base + INFO.page, info.page, true);
    return base;
  }
}
