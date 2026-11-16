/**
 * EmulatorHost: the world around the virtual device.
 *
 * fb-emulator.wasm is the firmware: the script engine and its bindings,
 * ConfigManager and the components (display items, keys, lists, images,
 * clocks and their topics), the MQTT brokers with Home Assistant discovery,
 * the hardware scan, the key and sensor drivers. What it cannot have in a
 * page it asks from this host (tools/js/emulator/fbh.h): time, files, the
 * modules on the I2C buses, sensor readings, fonts and images, the MQTT
 * transport; and it tells
 * the host what the device shows and lights: the pixels LVGL drew for each
 * display, and a record per widget. wasm.ts marshals the C
 * arguments to these methods; nothing here knows about wasm memory.
 *
 * DeviceState (device-state.ts) implements `hardware` and `display`; the
 * Emulator (index.ts) supplies mqtt, system, files and console.
 */

/** EventType of include/types.h for key events. */
export const KEY_EVENT = { click: 0, longPress: 1, release: 2 } as const;
export type KeyEventName = 'click' | 'longpress' | 'release';

/**
 * A font on an entry: a face index of the font table and a size (1..5 = a
 * legacy step, else px). Since stage 2 the firmware reports the font it
 * chose, so the size is always px.
 */
export interface FontChoice {
  face: number;
  size: number;
}

/** The virtual hardware: what the firmware's drivers read and write. */
export interface HardwareHost {
  /** Does a device answer on I2C bus `bus` (0 sensors, 1 connectors) at `address`? */
  i2cProbe(bus: number, address: number): boolean;
  /** The 8 pins of the port expander of a connector (address 0x20 + connector). */
  mcpGpio(address: number): number;
  /** A sensor reading: 0 BME280 temperature (°C), 1 pressure (Pa), 2 humidity (%), 3 / 4 STS35 at 0x4A / 0x4B (°C). */
  sensorRead(input: number): number;
  /** A pixel of the LED strip, as the firmware wrote it. */
  led(index: number, r: number, g: number, b: number): void;
}

/** The buckets of the device's media store (src/media/media_store.h, in this order). */
export const MEDIA_BUCKETS = ['fonts', 'images', 'render'] as const;
export type MediaBucket = typeof MEDIA_BUCKETS[number];

/**
 * The media store's files: the TrueType fonts and the image rasters the
 * firmware reads in place. The wasm copies a file once and keeps it.
 */
export interface MediaHost {
  /** The file's bytes; null when the bucket has no such key (the firmware then runs its own fallback). */
  read(bucket: MediaBucket, key: string): Uint8Array | null;
}

/** The kinds of state record (tools/js/emulator/fbh.h FBH_STATE_*). */
export const STATE_KIND = ['device', 'item', 'item-gone', 'key', 'list', 'image', 'clock', 'page', 'sensor', 'layout'] as const;
export type StateKind = typeof STATE_KIND[number];

/** What the device shows: one record per widget that changed (device-state.ts has the shapes). */
export interface DisplayHost {
  state(kind: StateKind, record: unknown): void;
  /**
   * LVGL drew into a display's frame buffer: this rectangle changed. display:
   * 0 = the main display, 1 + i = key display i.
   */
  frame(display: number, x: number, y: number, width: number, height: number): void;
}

/** Who made a publish: a script's mqtt.publish(), or the firmware by itself. */
export type PublishOrigin = 'script' | 'firmware';
/**
 * What a publish is. 'discovery' and 'availability' are the firmware's
 * announcements (Home Assistant discovery, <device>/status); 'press' a key's
 * default action; 'sensor' a sensor's state; 'state' other firmware state.
 */
export type PublishKind = 'script' | 'press' | 'discovery' | 'availability' | 'sensor' | 'state';

/** What esp-mqtt does for a broker. broker = index in config.json "brokers". */
export interface MqttHost {
  /** The firmware's client starts: connect, with this last will. */
  start(broker: number, options: { uri: string; will: { topic: string; payload: string; qos: number; retain: boolean } }): void;
  /** false = not sent. */
  publish(broker: number, topic: string, payload: string, qos: number, retain: boolean,
    about: { origin: PublishOrigin; kind: PublishKind }): boolean;
  subscribe(broker: number, topic: string, qos: number): void;
  unsubscribe(broker: number, topic: string): void;
}

export interface SystemHost {
  deviceId(): string;
  /** Free bytes of the general heap (system.freeHeap(), the free-memory sensor). */
  freeHeap(): number;
  /** Milliseconds since the Unix epoch. */
  nowMs(): number;
  /** Polled while script code runs: true stops the script ("interrupted"). */
  shouldInterrupt(): boolean;
}

/** Files of the device's filesystem: "/config.json", files for load(). */
export interface FilesHost {
  read(path: string): Uint8Array | null;
}

export type ScriptErrorWhere = 'compile' | 'load' | 'run' | 'callback' | 'key' | 'eval';
export type LogLevel = 'E' | 'W' | 'I' | 'J';

/** print() / console.log(), the firmware's log, script errors. */
export interface ConsoleHost {
  /** A piece of print() output; lines end with '\n'. */
  write(text: string): void;
  /** A firmware log line (E / W / I) or engine output (J), as on the UART. */
  log(level: LogLevel, tag: string, text: string): void;
  scriptError(where: ScriptErrorWhere, message: string, stack: string): void;
}

export interface EmulatorHost {
  hardware: HardwareHost;
  media: MediaHost;
  display: DisplayHost;
  mqtt: MqttHost;
  system: SystemHost;
  files: FilesHost;
  console: ConsoleHost;
  /**
   * A key event went through the script. defaultPrevented = the handler
   * returned false: the firmware publishes nothing for it.
   */
  keyEvent(position: number, event: KeyEventName, defaultPrevented: boolean): void;
}
