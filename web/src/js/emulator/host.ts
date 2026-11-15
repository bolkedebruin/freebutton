/**
 * EmulatorHost: the device behind the script bindings.
 *
 * fb-emulator.wasm is the firmware's script engine and JavaScript glue. What
 * the glue calls on the device (the C "seam": src/scripting/*_hardware.h and
 * *_binding.h) ends here, one group per header. wasm.ts marshals the C
 * arguments (strings, structs) to these methods; nothing here knows about
 * wasm memory.
 *
 * DeviceState (device-state.ts) implements the groups that hold device
 * state; the Emulator (index.ts) supplies mqtt, system, files and console.
 *
 * The firmware's rules run before these methods: an icon name is already
 * valid or empty, a list selection already clamped, list items already cut to
 * their limits. Return values are what the C declaration returns, as a
 * boolean or a value / null.
 */

/** EventType of include/types.h for key events. */
export const KEY_EVENT = { click: 0, longPress: 1, release: 2 } as const;
export type KeyEventName = 'click' | 'longpress' | 'release';

/** A font set by a script: a face index of the font table and a fontsize (1..5 = step, else px). */
export interface FontChoice {
  face: number;
  size: number;
}

/** button_hardware.h: the keys and their small displays. Positions are 1-based. */
export interface KeysHost {
  /** The text shown (last runtime text, else the configured one); null when there is no such key. */
  getLabel(position: number): string | null;
  getTopLabel(position: number): string | null;
  /** false when there is no such key. */
  setLabel(position: number, text: string): boolean;
  setTopLabel(position: number, text: string): boolean;
  /** '' hides the icon. */
  setIcon(position: number, name: string): boolean;
  setFont(position: number, font: FontChoice, top: boolean): boolean;
  restoreFont(position: number, top: boolean): boolean;
  /** Position for a config.json "name"; null when no key has it. */
  findByName(name: string): number | null;
}

/** led_hardware.h: the LED strip (4 per bar). */
export interface LedsHost {
  /** false when index is out of range. */
  setColor(index: number, r: number, g: number, b: number): boolean;
}

/** DisplayItemInfo of displayitem_binding.h. label / unit are the configured texts. */
export interface DisplayItemInfo {
  id: number;
  x: number;
  y: number;
  width: number;
  boxType: number;
  fontSize: number;
  alignment: number;
  label: string;
  unit: string;
  round: number;
  page: number;
}

/** displayitem_binding.h: the items of the main display, and its pages. */
export interface DisplayHost {
  /** The new item's id; 0 on failure. */
  create(spec: Omit<DisplayItemInfo, 'id'>): number;
  remove(id: number): boolean;
  findByName(name: string): number | null;
  count(): number;
  getInfo(id: number): DisplayItemInfo | null;
  allIds(): number[];
  /** The setters return false when there is no item with that id. */
  setLabel(id: number, text: string): boolean;
  setValue(id: number, text: string): boolean;
  setUnit(id: number, text: string): boolean;
  setIcon(id: number, name: string): boolean;
  setFont(id: number, font: FontChoice): boolean;
  restoreFont(id: number): boolean;
  setPosition(id: number, x: number, y: number): boolean;
  setPage(id: number, page: number): boolean;
  /** false for page < 1. */
  showPage(page: number): boolean;
  shownPage(): number;
}

/** list_binding.h: the lists (rollers) of the main display, by index in config.json "lists". */
export interface ListsHost {
  /** -1 when no list has that name. */
  findByName(name: string): number;
  /** -1 when there is no list at index. */
  count(index: number): number;
  selected(index: number): number;
  setSelected(index: number, selected: number): void;
  setItems(index: number, items: string[], selected: number): void;
  setFont(index: number, font: FontChoice): boolean;
  restoreFont(index: number): boolean;
}

/** image_binding.h: the images of the main display, by index in config.json "images". */
export interface ImagesHost {
  findByName(name: string): number;
  exists(index: number): boolean;
  /** value '' shows nothing. */
  set(index: number, kind: 'icon' | 'src', value: string): void;
}

/** src/ui/fonts/fonts.h fonts_find / fonts_find_id: face index, -1 when unknown. */
export interface FontsHost {
  find(name: string): number;
  findId(id: number): number;
}

/** sensor_binding.h: the sample of a sensor (the firmware's sensor->sample()). */
export interface SensorsHost {
  sample(id: number): number;
}

/** device_hardware.h: which module is on which connector. */
export interface TopologyHost {
  /** 0 = empty, 1 = bar, 2 = main display; -1 = no such connector. */
  connectorType(connector: number): number;
  /** Connector of the main display, -1 when there is none. */
  mainConnector(): number;
}

/** mqtt_binding.h: what a broker hands to its client. broker = index in config.json "brokers". */
export interface MqttHost {
  /** false = not sent. */
  publish(broker: number, topic: string, payload: string, qos: number, retain: boolean): boolean;
  subscribe(broker: number, topic: string, qos: number): void;
  unsubscribe(broker: number, topic: string): void;
}

/** system_binding.h, and the clock of the engine. */
export interface SystemHost {
  deviceId(): string;
  deviceName(): string;
  uptimeSeconds(): number;
  freeHeap(): number;
  /** Milliseconds since the Unix epoch. */
  nowMs(): number;
  /** Polled while script code runs: true stops the script ("interrupted"). */
  shouldInterrupt(): boolean;
}

/** file_hardware.h: files of the device's filesystem, for load(). */
export interface FilesHost {
  load(path: string): Uint8Array | null;
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
  keys: KeysHost;
  leds: LedsHost;
  display: DisplayHost;
  lists: ListsHost;
  images: ImagesHost;
  fonts: FontsHost;
  sensors: SensorsHost;
  topology: TopologyHost;
  mqtt: MqttHost;
  system: SystemHost;
  files: FilesHost;
  console: ConsoleHost;
  /**
   * A key event went through the script (ButtonManager). defaultPrevented =
   * the handler returned false: the firmware publishes nothing for it.
   */
  keyEvent(position: number, event: KeyEventName, defaultPrevented: boolean): void;
  /** ConfigManager::restoreConfigState(): before a script restarts. */
  restoreConfigState(): void;
}
