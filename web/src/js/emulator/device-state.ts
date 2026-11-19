/**
 * DeviceState: what the virtual device shows and what its hardware is.
 *
 * Since stage 2 the device itself runs inside the wasm: the firmware's
 * ConfigManager reads config.json, its components subscribe to their topics,
 * keep their texts and draw. This class is the mirror of that, for a
 * renderer: the firmware reports every display item, key, list, image and
 * clock as a record with what it decided to show (texts cut to their slots,
 * runs with font and colour, pixel positions), and DeviceState keeps the
 * latest of each. No rule of the firmware is repeated here.
 *
 * It is also the virtual hardware the firmware's drivers read: which module
 * is on which connector, which key is held, the sensor readings.
 *
 * What it takes from config.json itself is only what the hardware is (the
 * connectors) and where each entry stands in the file, so that every entry
 * carries the `ref` ({kind, list, index}) that config-model.ts gives the
 * preview model: a renderer can lay this state over the pages it already
 * draws (preview-overlay.ts). Changes are observable: subscribe() gets one
 * StateChange per change.
 *
 * Runtime state is never written back to config.json.
 */
import type { ConfigRef } from '../app-state';
import { mapButton, mapConnectors } from '../config-model';
import type { DisplayHost, FontChoice, HardwareHost, StateKind } from './host';

/** One run of text in one format, as the firmware resolved it. */
export interface TextRun {
  text: string;
  /** Font name of config.json "fonts" (or a system font); '' = LVGL's built-in font. */
  font: string;
  /** Face index of the firmware's font table; -1 = the built-in font. */
  face: number;
  px: number;
  /** "#rrggbb" */
  color: string;
}

export interface FontState {
  font: string;
  face: number;
  px: number;
  /** Line box in px (the font's own, as LVGL lays text out). */
  lineHeight: number;
  /**
   * Drawn from its TrueType file. false: LVGL's built-in font stands in (no
   * file for the face in the fonts bucket, or not a usable font).
   */
  real?: boolean;
}

/** A text as the device draws it. */
export interface TextState {
  /** As given, markup included, cut to what the firmware keeps. */
  text: string;
  /** Without the markup. */
  plain: string;
  /** Takes no room: an empty caption or unit, a key label behind an icon. */
  hidden: boolean;
  /** A key text in several formats: a row that does not wrap. */
  rich: boolean;
  /** From the markup (\al \ac \ar, \vt \vm \vb); null = none. */
  align: 'left' | 'center' | 'right' | null;
  valign: 'top' | 'middle' | 'bottom' | null;
  /** More formats than the firmware draws: the last run took the rest. */
  spansCut: boolean;
  /** The text's own font (what a run without \f / \s has). */
  font: FontState | null;
  runs: TextRun[];
}

/** A raster the device draws: an icon ("ico-<name>-<w>x<h>.bin") or a file of the images bucket. */
export interface RasterState {
  key: string;
  /** For an icon raster: the icon's name and size. */
  name?: string;
  width?: number;
  height?: number;
  /** "#rrggbb": the colour a single-colour raster is drawn in. */
  tint: string;
}

/** A display item's box, in pixels on the 320x240 display. */
export interface BoxState {
  x: number;
  y: number;
  width: number;
  /** lv_align_t of the box. */
  align: number;
  underline: boolean;
  /** Bottom padding of the caption and of the box; null = none of its own. */
  /** The style list each part carries now (names joined by a space): the item's, or under a low LVGL pool the one before it. */
  carried: { caption: string; value: string; unit: string };
  captionPad: number | null;
  boxPad: number | null;
  caption: TextState;
  value: TextState;
  unit: TextState;
  icon: RasterState | null;
}

export interface ItemState {
  id: number;
  /** config.json entry; absent for an item a script created. */
  ref?: ConfigRef;
  name?: string;
  /** Percent of the display, as in config.json. */
  x: number;
  y: number;
  width: number;
  boxType: number;
  /** The item's styles now, and the ones config.json gives it: style names joined by a space ('' = none). */
  style: string;
  configStyle: string;
  align: number;
  round: number;
  /** 1-based; 0 = every page. */
  page: number;
  /**
   * Caption, value and unit (the raw texts, markup included): what is on the
   * display, or, while the item's page is not shown, what the item holds for
   * when it comes.
   */
  label: string;
  value: string;
  unit: string;
  /** Icon name shown before the value; '' = none. */
  icon: string;
  /** The value's font as the firmware chose it; null while not drawn. */
  font: FontChoice | null;
  /** What is drawn; null while the item's page is not shown (the device keeps no widget then). */
  box: BoxState | null;
}

export interface KeyState {
  position: number;
  ref?: ConfigRef;
  name?: string;
  /** 1-based page of the config entry. */
  page: number;
  /** Label and top label as shown (raw texts); the configured ones for a key without a display. */
  label: string;
  topLabel: string;
  /** Icon shown instead of the label; '' = none. */
  icon: string;
  /** The button's styles now, and the ones config.json gives it: style names joined by a space ('' = none). */
  style: string;
  configStyle: string;
  /** The fonts the firmware chose; null for a key without a display. */
  font: FontChoice | null;
  topFont: FontChoice | null;
  /**
   * The key's own display; null for a key without one (a key of the main
   * display has none: scripts cannot set its label either).
   */
  display: {
    index: number; label: TextState; topLabel: TextState; icon: RasterState | null;
    /** The style list each label carries now (see BoxState.carried). */
    carried: { label: string; topLabel: string };
  } | null;
}

export interface LedState {
  index: number;
  r: number;
  g: number;
  b: number;
}

export interface RollerTextState { opa: number; letterSpace: number; decor: number }

export interface ListState {
  index: number;
  ref?: ConfigRef;
  name: string;
  page: number;
  items: string[];
  /** -1 when the list is empty. */
  selected: number;
  /** The list's styles now, and the ones config.json gives it: style names joined by a space ('' = none). */
  style: string;
  configStyle: string;
  /** The font the firmware chose; null while not drawn. */
  font: FontChoice | null;
  /** The roller, in pixels; null while the list's page is not shown. */
  roller: {
    x: number; y: number; width: number;
    /** As laid out: the rows' line height and line space times the visible rows. */
    height: number;
    rows: number; selected: number; font: FontState;
    /** The selected row's font as the device resolves it (a "selected" block of its styles, of "list", else the rows'). */
    selectedFont: FontState;
    lineSpace: number;
    color: string; selectedBg: string; selectedColor: string;
    /**
     * Opacity (0..255), letter spacing (px) and decoration (1 = underline, 2 = line-through) of
     * the rows and of the selected row, as LVGL resolves them: the selected row has the rows'
     * unless a "selected" block sets the property. `selectedRule`: the same from the firmware's
     * own rule (style_logic_selected_row).
     */
    text: RollerTextState; selectedText: RollerTextState; selectedRule: RollerTextState;
    /** The style list the rows and the selected row carry now (see BoxState.carried). */
    carried: { rows: string; selected: string };
    /** The roller's own background; null = none (transparent). */
    bg: string | null;
  } | null;
}

export interface ImageState {
  index: number;
  ref?: ConfigRef;
  name: string;
  page: number;
  /** Pixels. */
  x: number;
  y: number;
  width: number;
  height: number;
  /** What it wants to show: an icon name, an images-bucket key, or nothing. */
  kind: 'icon' | 'src' | 'none';
  value: string;
  /** What it draws; null while its page is not shown or the bucket has no such raster. */
  raster: RasterState | null;
}

/** The analog clock (config.json "clocks"): its hands are the device's local time (DeviceState.timezone). */
export interface ClockState {
  index: number;
  id: number;
  page: number;
  x: number;
  y: number;
  width: number;
  height: number;
  align: number;
  /** Drawn now (its page is shown; only the first clock of a config is drawn). */
  shown: boolean;
}

export interface SensorState {
  id: number;
  name: string;
  /** "temperature", "humidity", "pressure", "data_size". */
  type: string;
  unit: string;
  online: boolean;
  value: number;
  /** Which hardware reading it samples (HardwareHost.sensorRead); -1 = the firmware computes it (memory). */
  input: number;
}

export interface BrokerInfo {
  index: number;
  /** config.json "brokerid". */
  name: string;
  url: string;
  hassDiscovery: boolean;
}

/** A face of the firmware's font table. */
export interface FontFaceState {
  face: number;
  name: string;
  src: string;
  id: number;
  /** Face index of the fallback, -1 = none. */
  fallback: number;
  /** Line height in em, 0 = the font's metrics. */
  lineheight: number;
  /** The face's file is in the fonts bucket and usable; false: its text is drawn in LVGL's built-in font. */
  loaded?: boolean;
}

/**
 * Where a widget is on its display, in device pixels, as LVGL laid it out
 * (lv_obj_get_coords: alignment, content size and wrapping included).
 */
export interface LayoutRect {
  /** The config.json entry (as config-model.ts and overlayPages give it); absent for an item a script created. */
  ref?: ConfigRef;
  kind: 'displayitem' | 'button' | 'list' | 'image' | 'clock';
  /** A display item's id, a key's position, the index of a list, image or clock. */
  id: number;
  x: number;
  y: number;
  width: number;
  height: number;
  /** 1-based page of the entry; 0 = every page. */
  page: number;
  /** Drawn now (the firmware keeps a widget only while its page is shown, so this is true but for a hidden part). */
  visible: boolean;
}

/**
 * A part of a kind as the firmware resolved the built-in look and config.json "styles"
 * "button" / "displayitem" / "list": DeviceState.styles has "button", "button.title",
 * "displayitem", "displayitem.unit", "displayitem.title", "list" and "list.selected".
 */
export interface StyleState {
  /** null / -1 / 0: the part has no font of its own (the unit: the value's; the selected row: the rows'). */
  font: string | null;
  face: number;
  px: number;
  color: string;
  opa: number;
  align: 'left' | 'center' | 'right' | null;
  letterSpace: number;
  /** In hundredths (125 = 1.25 lines); 0 = none. */
  lineHeight: number;
  underline: boolean;
  lineThrough: boolean;
  bgColor?: string;
  bgOpa?: number;
}

/** 0 = empty, 1 = bar, 2 = main display (the firmware's ConnectorType). */
export type ConnectorKind = 0 | 1 | 2;

/** One observable change. A renderer redraws what the record names. */
export type StateChange =
  | { kind: 'item'; id: number; field: 'label' | 'value' | 'unit' | 'icon' | 'font' | 'position' | 'page' | 'shown' }
  | { kind: 'item-added'; id: number }
  | { kind: 'item-removed'; id: number }
  | { kind: 'page'; page: number }
  | { kind: 'key'; position: number; field: 'label' | 'topLabel' | 'icon' | 'font' | 'topFont' }
  | { kind: 'led'; index: number }
  | { kind: 'list'; index: number; field: 'items' | 'selected' | 'font' | 'shown' }
  | { kind: 'image'; index: number }
  | { kind: 'clock'; index: number }
  | { kind: 'sensor'; id: number }
  /** A widget of that display (0 = the main display, 1 + i = key display i) moved, came or went. */
  | { kind: 'layout'; display: number }
  /** The device's name, brokers, sensors, fonts or styles (once, at boot). */
  | { kind: 'device' }
  /** A script (re)starts: what the old one changed goes back to config.json. Redraw all. */
  | { kind: 'reset' };

export type StateListener = (change: StateChange, state: DeviceState) => void;

/** A sensor of the virtual hardware and its reading. id: the firmware's (1, 2 STS35; 3, 4, 5 BME280). */
export interface SensorSpec {
  id: number;
  value?: number;
  /** Kept for callers of stage 1; the firmware names its sensors itself. */
  name?: string;
  type?: string;
  unit?: string;
  online?: boolean;
}

/**
 * The sensor board: a BME280 (pressure in hPa, humidity in %, temperature in
 * °C; the firmware's sensor ids 3, 4, 5), with room-like readings.
 */
export const DEFAULT_SENSORS: readonly SensorSpec[] = [
  { id: 3, value: 1013.2 },
  { id: 4, value: 45 },
  { id: 5, value: 21.5 },
];

export interface DeviceStateOptions {
  /**
   * Module per connector (index = connector id 0..3). Default: config.json
   * "info.connectors"; without it, a bar on every connector that has keys in
   * the config and the main display on the first connector without.
   */
  connectors?: readonly ConnectorKind[];
  /** The hardware sensors and their readings. Default: DEFAULT_SENSORS. [] = no sensor board. */
  sensors?: readonly SensorSpec[];
}

type Obj = Record<string, unknown>;

const isObj = (v: unknown): v is Obj => typeof v === 'object' && v !== null && !Array.isArray(v);

/** The firmware reads both spellings of a list, in file order; only objects are entries. */
function entryRefs(cfg: Obj, lists: readonly string[], kind: ConfigRef['kind']): ConfigRef[] {
  const out: ConfigRef[] = [];
  for (const key of Object.keys(cfg)) {
    const arr = cfg[key];
    if (!lists.includes(key) || !Array.isArray(arr)) continue;
    arr.forEach((raw, index) => { if (isObj(raw)) out.push({ kind, list: key, index }); });
  }
  return out;
}

function refByName(cfg: Obj, list: string, kind: ConfigRef['kind'], name: string | null | undefined): ConfigRef | undefined {
  const arr = cfg[list];
  if (!name || !Array.isArray(arr)) return undefined;
  const index = arr.findIndex(e => isObj(e) && e.name === name);
  return index < 0 ? undefined : { kind, list, index };
}

const same = (a: unknown, b: unknown): boolean => JSON.stringify(a) === JSON.stringify(b);

const fontChoice = (font: FontState | null | undefined): FontChoice | null => (font ? { face: font.face, size: font.px } : null);

/** I2C address of the port expander of connector 0. */
const MCP_BASE = 0x20;
/** Sensor id -> the hardware reading it is (the BME280's three, the two STS35), and its scale to the driver's unit. */
const SENSOR_INPUT: Readonly<Record<number, { input: number; scale: number; address: number }>> = {
  5: { input: 0, scale: 1, address: 0x76 },
  3: { input: 1, scale: 100, address: 0x76 },  // hPa; the driver reads Pa
  4: { input: 2, scale: 1, address: 0x76 },
  1: { input: 3, scale: 1, address: 0x4a },
  2: { input: 4, scale: 1, address: 0x4b },
};

// The records the firmware sends (tools/js/emulator/device.cpp).
interface ItemRecord {
  id: number; order: number; name: string | null; x: number; y: number; width: number; boxType: number;
  style: string; configStyle: string; align: number; round: number; page: number; label: string; unit: string;
  now: { label: string; value: string; unit: string; icon: string };
  box: BoxState | null;
}
interface KeyRecord {
  order: number; position: number; name: string | null; page: number; label: string; topLabel: string;
  style: string; configStyle: string;
  display: KeyState['display'];
}
interface ListRecord {
  index: number; name: string | null; page: number; selected: number; items: string[];
  style: string; configStyle: string; roller: ListState['roller'];
}
interface ImageRecord {
  index: number; name: string | null; page: number; x: number; y: number; width: number; height: number;
  kind: ImageState['kind']; value: string; raster: RasterState | null;
}
interface LayoutRecord {
  display: number;
  rects: Array<{
    kind: 'item' | 'key' | 'list' | 'image' | 'clock'; id?: number; order?: number; index?: number;
    x: number; y: number; width: number; height: number; page: number; visible: boolean;
  }>;
}

interface DeviceRecord {
  id: string; name: string; firmware: string; model: string; timezone: string; connectors: number[]; keys: number[];
  keyDisplays: number[]; leds: number;
  brokers: Array<{ index: number; name: string; url: string; configured: boolean; discovery: boolean }>;
  sensors: Array<{ id: number; name: string; type: string; unit: string; online: boolean; input: number }>;
  fonts: FontFaceState[]; styles: Record<string, StyleState>; styleNames: string[];
}

export class DeviceState {
  /** The device's name: config.json "core.name", else its id. */
  deviceName = '';
  /** FIRMWARE and MODEL of the firmware in the wasm. */
  firmware = '';
  model = '';
  /** The POSIX TZ string the device's local time follows (config.json "core.timezone", else the firmware's default). */
  timezone = '';
  brokers: BrokerInfo[] = [];
  /** Module per connector id. */
  connectors: ConnectorKind[] = [0, 0, 0, 0];
  shownPage = 1;
  leds: LedState[] = [];
  lists: ListState[] = [];
  images: ImageState[] = [];
  clocks: ClockState[] = [];
  sensors: SensorState[] = [];
  /** The firmware's font table and resolved styles. */
  fonts: FontFaceState[] = [];
  styles: Record<string, StyleState> = {};
  /** The styles of config.json "styles" an item can name (not "button", "displayitem", "list"), in file order. */
  styleNames: string[] = [];
  /** Positions of the keys that have a display of their own (the keys of a bar). */
  keyDisplays: number[] = [];

  private itemList: ItemState[] = [];
  private keyList: KeyState[] = [];
  private keyPositions: number[] = [];
  private readonly listeners = new Set<StateListener>();
  private readonly config: Obj;
  private readonly itemRefs: ConfigRef[];
  private readonly keyRefs: ConfigRef[];
  private readonly readings = new Map<number, number>();
  private readonly sensorIds: number[];
  /** Per display (0 = main, 1 + i = key display i). */
  private readonly layouts: LayoutRect[][] = [];
  /** Bit (position - 1) is 0 while that key is held. */
  private keysUp = 0xff;
  private switchPins: [number, number] = [6, 2];

  constructor(config: unknown, options: DeviceStateOptions = {}) {
    this.config = isObj(config) ? config : {};
    this.itemRefs = entryRefs(this.config, ['displayitems'], 'displayitem');
    this.keyRefs = entryRefs(this.config, ['buttons'], 'button');
    this.connectors = options.connectors
      ? [0, 1, 2, 3].map(i => options.connectors![i] ?? 0)
      : this.inferConnectors(this.config);
    const sensors = options.sensors ?? DEFAULT_SENSORS;
    this.sensorIds = sensors.map(s => s.id);
    for (const s of sensors) this.readings.set(s.id, s.value ?? 0);
  }

  // --- reading ----------------------------------------------------------------

  /** The display items, in display order (config order, then created ones). */
  get items(): readonly ItemState[] {
    return this.itemList;
  }

  /** The keys of config.json "buttons", in file order. */
  get keys(): readonly KeyState[] {
    return this.keyList;
  }

  /** A display item by id or config name. */
  item(idOrName: number | string): ItemState | undefined {
    return this.itemList.find(i => (typeof idOrName === 'string' ? i.name === idOrName : i.id === idOrName));
  }

  /** A key by position or config name (the first config entry of that position). */
  key(positionOrName: number | string): KeyState | undefined {
    return this.keyList.find(k => (typeof positionOrName === 'string' ? k.name === positionOrName : k.position === positionOrName));
  }

  list(indexOrName: number | string): ListState | undefined {
    return typeof indexOrName === 'string' ? this.lists.find(l => l.name === indexOrName) : this.lists[indexOrName];
  }

  image(indexOrName: number | string): ImageState | undefined {
    return typeof indexOrName === 'string' ? this.images.find(m => m.name === indexOrName) : this.images[indexOrName];
  }

  sensor(id: number): SensorState | undefined {
    return this.sensors.find(s => s.id === id);
  }

  /** Items of the shown page (page 0 = every page). */
  visibleItems(): ItemState[] {
    return this.itemList.filter(i => i.page === 0 || i.page === this.shownPage);
  }

  /** Key positions the firmware found a switch for (the keys of every module). */
  physicalKeys(): number[] {
    if (this.keyPositions.length) return [...this.keyPositions];
    const out: number[] = [];
    this.connectors.forEach((kind, id) => {
      if (kind !== 0) out.push(id * 2 + 1, id * 2 + 2);
    });
    return out;
  }

  /** Calls listener for every change; returns the unsubscribe function. */
  subscribe(listener: StateListener): () => void {
    this.listeners.add(listener);
    return () => { this.listeners.delete(listener); };
  }

  // --- the virtual hardware ---------------------------------------------------------

  /** A hardware sensor's reading (ids 1..5), in the sensor's unit. false: no such hardware sensor. */
  setSensor(id: number, value: number): boolean {
    if (!this.sensorIds.includes(id) || !SENSOR_INPUT[id]) return false;
    if (this.readings.get(id) !== value) {
      this.readings.set(id, value);
      const sensor = this.sensor(id);
      if (sensor) sensor.value = value;
      this.emit({ kind: 'sensor', id });
    }
    return true;
  }

  /** What the firmware samples of a sensor it computes itself (memory), for display. */
  noteSensorValue(id: number, value: number): void {
    const sensor = this.sensor(id);
    if (!sensor || sensor.input >= 0 || sensor.value === value) return;
    sensor.value = value;
    this.emit({ kind: 'sensor', id });
  }

  /** A key's switch: held or not. */
  setKey(position: number, held: boolean): void {
    const bit = 1 << (position - 1);
    this.keysUp = held ? this.keysUp & ~bit : this.keysUp | bit;
  }

  /** Positions held now. */
  heldKeys(): number[] {
    const out: number[] = [];
    for (let i = 0; i < 8; i++) if ((this.keysUp & (1 << i)) === 0) out.push(i + 1);
    return out;
  }

  /** The pins of the left and right key switch on a connector's port expander (include/pins.h, from the wasm). */
  setSwitchPins(left: number, right: number): void {
    this.switchPins = [left, right];
  }

  /** A script (re)starts: tells the listeners to redraw everything. */
  noteRestart(): void {
    this.emit({ kind: 'reset' });
  }

  readonly hardware: HardwareHost = {
    i2cProbe: (bus, address) => {
      if (bus === 1) return address >= MCP_BASE && address < MCP_BASE + 4 && this.connectors[address - MCP_BASE] !== 0;
      return this.sensorIds.some(id => SENSOR_INPUT[id]?.address === address);
    },
    // Every pin reads high, except: pin 1 of a main display module (how the
    // firmware's scanner tells it from a bar), and the switch of a held key.
    mcpGpio: address => {
      const connector = address - MCP_BASE;
      let pins = 0xff;
      if (this.connectors[connector] === 2) pins &= ~(1 << 1);
      if ((this.keysUp & (1 << (connector * 2))) === 0) pins &= ~(1 << this.switchPins[0]);
      if ((this.keysUp & (1 << (connector * 2 + 1))) === 0) pins &= ~(1 << this.switchPins[1]);
      return pins;
    },
    sensorRead: input => {
      for (const [id, spec] of Object.entries(SENSOR_INPUT)) {
        if (spec.input === input) return (this.readings.get(Number(id)) ?? 0) * spec.scale;
      }
      return 0;
    },
    led: (index, r, g, b) => {
      while (this.leds.length <= index) this.leds.push({ index: this.leds.length, r: 0, g: 0, b: 0 });
      const led = this.leds[index]!;
      if (led.r === r && led.g === g && led.b === b) return;
      led.r = r;
      led.g = g;
      led.b = b;
      this.emit({ kind: 'led', index });
    },
  };

  // --- what the firmware reports ----------------------------------------------------------

  readonly display: Pick<DisplayHost, 'state'> = {
    state: (kind: StateKind, record: unknown) => {
      switch (kind) {
        case 'device': this.applyDevice(record as DeviceRecord); break;
        case 'item': this.applyItem(record as ItemRecord); break;
        case 'item-gone': this.removeItem((record as { id: number }).id); break;
        case 'key': this.applyKey(record as KeyRecord); break;
        case 'list': this.applyList(record as ListRecord); break;
        case 'image': this.applyImage(record as ImageRecord); break;
        case 'clock': this.applyClock(record as ClockState); break;
        case 'page': this.applyPage((record as { page: number }).page); break;
        case 'layout': this.applyLayout(record as LayoutRecord); break;
        default: break;
      }
    },
  };

  private applyDevice(r: DeviceRecord): void {
    this.deviceName = r.name;
    this.firmware = r.firmware;
    this.model = r.model;
    this.timezone = r.timezone;
    this.connectors = [0, 1, 2, 3].map(i => (r.connectors[i] ?? 0) as ConnectorKind);
    this.keyPositions = [...r.keys];
    this.keyDisplays = [...r.keyDisplays];
    while (this.leds.length < r.leds) this.leds.push({ index: this.leds.length, r: 0, g: 0, b: 0 });
    this.brokers = r.brokers.map(b => ({ index: b.index, name: b.name, url: b.url, hassDiscovery: b.discovery }));
    this.sensors = r.sensors.map(s => ({
      ...s,
      value: s.input >= 0 ? this.readings.get(s.id) ?? 0 : this.sensor(s.id)?.value ?? 0,
    }));
    this.fonts = r.fonts;
    this.styles = r.styles;
    this.styleNames = r.styleNames ?? [];
    this.emit({ kind: 'device' });
  }

  private applyItem(r: ItemRecord): void {
    const old = this.item(r.id);
    const box = r.box;
    const item: ItemState = {
      id: r.id,
      ...(r.order >= 0 && this.itemRefs[r.order] ? { ref: this.itemRefs[r.order] } : {}),
      ...(r.name ? { name: r.name } : {}),
      x: r.x, y: r.y, width: r.width, boxType: r.boxType, style: r.style, configStyle: r.configStyle, align: r.align, round: r.round,
      page: r.page,
      // Drawn: what is on the display. Not drawn: what the item holds for when its page comes.
      label: box ? box.caption.text : r.now.label,
      value: box ? box.value.text : r.now.value,
      unit: box ? box.unit.text : r.now.unit,
      icon: box ? box.icon?.name ?? box.icon?.key ?? '' : r.now.icon,
      font: box ? fontChoice(box.value.font) : old?.font ?? null,
      box,
    };
    if (!old) {
      this.itemList.push(item);
      this.emit({ kind: 'item-added', id: r.id });
      return;
    }
    // The same object stays the item (a renderer may hold it); its fields change.
    const was = { ...old };
    Object.assign(old, item);
    const changed = (field: Extract<StateChange, { kind: 'item' }>['field']): void =>
      this.emit({ kind: 'item', id: r.id, field });
    // A part that is drawn differently (other runs, fonts, colours) changed too;
    // a box that only came or went with its page did not change the texts.
    const redrawn = <K extends 'caption' | 'value' | 'unit' | 'icon'>(part: K): boolean =>
      !!was.box && !!box && !same(was.box[part], box[part]);
    if (was.label !== item.label || redrawn('caption')) changed('label');
    if (was.value !== item.value || redrawn('value')) changed('value');
    if (was.unit !== item.unit || redrawn('unit')) changed('unit');
    if (was.icon !== item.icon || redrawn('icon')) changed('icon');
    if (!same(was.font, item.font)) changed('font');
    if (was.x !== item.x || was.y !== item.y) changed('position');
    if (was.page !== item.page) changed('page');
    if (!was.box !== !box) changed('shown');
  }

  private removeItem(id: number): void {
    const at = this.itemList.findIndex(i => i.id === id);
    if (at < 0) return;
    this.itemList.splice(at, 1);
    this.emit({ kind: 'item-removed', id });
  }

  private applyKey(r: KeyRecord): void {
    const d = r.display;
    const key: KeyState = {
      position: r.position,
      ...(this.keyRefs[r.order] ? { ref: this.keyRefs[r.order] } : {}),
      ...(r.name ? { name: r.name } : {}),
      page: r.page,
      label: d ? d.label.text : r.label,
      topLabel: d ? d.topLabel.text : r.topLabel,
      icon: d ? d.icon?.name ?? d.icon?.key ?? '' : '',
      style: r.style, configStyle: r.configStyle,
      font: d ? fontChoice(d.label.font) : null,
      topFont: d ? fontChoice(d.topLabel.font) : null,
      display: d,
    };
    const current = this.keyList[r.order];
    if (!current) {
      this.keyList[r.order] = key;
      return;
    }
    const old = { ...current };
    Object.assign(current, key);
    const changed = (field: Extract<StateChange, { kind: 'key' }>['field']): void =>
      this.emit({ kind: 'key', position: r.position, field });
    if (!same(old.display?.label, d?.label) || old.label !== key.label) changed('label');
    if (!same(old.display?.topLabel, d?.topLabel) || old.topLabel !== key.topLabel) changed('topLabel');
    if (!same(old.display?.icon, d?.icon)) changed('icon');
    if (!same(old.font, key.font)) changed('font');
    if (!same(old.topFont, key.topFont)) changed('topFont');
  }

  private applyList(r: ListRecord): void {
    const old = this.lists[r.index];
    const name = r.name ?? '';
    const ref = refByName(this.config, 'lists', 'list', r.name);
    const list: ListState = {
      index: r.index, ...(ref ? { ref } : {}), name, page: r.page, items: r.items, selected: r.selected,
      style: r.style, configStyle: r.configStyle,
      font: fontChoice(r.roller?.font), roller: r.roller,
    };
    if (!old) {
      this.lists[r.index] = list;
      return;
    }
    const was = { ...old };
    Object.assign(old, list);
    const changed = (field: Extract<StateChange, { kind: 'list' }>['field']): void =>
      this.emit({ kind: 'list', index: r.index, field });
    if (!same(was.items, list.items)) changed('items');
    if (was.selected !== list.selected) changed('selected');
    if (was.roller && list.roller && !same(was.roller.font, list.roller.font)) changed('font');
    if (!was.roller !== !list.roller) changed('shown');
  }

  private applyImage(r: ImageRecord): void {
    const old = this.images[r.index];
    const ref = refByName(this.config, 'images', 'image', r.name);
    const image: ImageState = {
      index: r.index, ...(ref ? { ref } : {}), name: r.name ?? '', page: r.page, x: r.x, y: r.y, width: r.width,
      height: r.height, kind: r.kind, value: r.value, raster: r.raster,
    };
    if (!old) {
      this.images[r.index] = image;
      return;
    }
    Object.assign(old, image);
    this.emit({ kind: 'image', index: r.index });
  }

  private applyClock(r: ClockState): void {
    const old = this.clocks[r.index];
    if (!old) {
      this.clocks[r.index] = r;
      return;
    }
    Object.assign(old, r);
    this.emit({ kind: 'clock', index: r.index });
  }

  /** The widgets of a display (0 = the main display, 1 + i = key display i) and where they are. */
  layout(display: number): readonly LayoutRect[] {
    return this.layouts[display] ?? [];
  }

  private applyLayout(r: LayoutRecord): void {
    const rects: LayoutRect[] = [];
    for (const e of r.rects) {
      const base = { x: e.x, y: e.y, width: e.width, height: e.height, page: e.page, visible: e.visible };
      if (e.kind === 'item') {
        const ref = this.item(e.id ?? -1)?.ref;
        rects.push({ ...(ref ? { ref } : {}), kind: 'displayitem', id: e.id ?? -1, ...base });
      } else if (e.kind === 'key') {
        const key = this.keyList[e.order ?? -1];
        rects.push({ ...(key?.ref ? { ref: key.ref } : {}), kind: 'button', id: key?.position ?? 0, ...base });
      } else if (e.kind === 'list') {
        const ref = this.lists[e.index ?? -1]?.ref;
        rects.push({ ...(ref ? { ref } : {}), kind: 'list', id: e.index ?? -1, ...base });
      } else if (e.kind === 'image') {
        const ref = this.images[e.index ?? -1]?.ref;
        rects.push({ ...(ref ? { ref } : {}), kind: 'image', id: e.index ?? -1, ...base });
      } else {
        rects.push({ kind: 'clock', id: e.index ?? -1, ...base });
      }
    }
    this.layouts[r.display] = rects;
    this.emit({ kind: 'layout', display: r.display });
  }

  private applyPage(page: number): void {
    if (this.shownPage === page) return;
    this.shownPage = page;
    this.emit({ kind: 'page', page });
  }

  // --- from config.json: the hardware ---------------------------------------------------

  private inferConnectors(cfg: Obj): ConnectorKind[] {
    const out: ConnectorKind[] = [0, 0, 0, 0];
    const listed = mapConnectors(cfg);
    if (listed.length) {
      for (const c of listed) {
        if (c.id < out.length) out[c.id] = c.type === 'bar' ? 1 : c.type === 'display' ? 2 : 0;
      }
      return out;
    }
    for (const raw of Array.isArray(cfg.buttons) ? cfg.buttons : []) {
      const button = mapButton(raw);
      const connector = button ? Math.floor((button.position - 1) / 2) : -1;
      if (connector >= 0 && connector < out.length) out[connector] = 1;
    }
    const free = out.indexOf(0);
    if (free >= 0) out[free] = 2;
    return out;
  }

  private emit(change: StateChange): void {
    for (const listener of [...this.listeners]) listener(change, this);
  }
}
