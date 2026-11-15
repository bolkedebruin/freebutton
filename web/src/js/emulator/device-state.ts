/**
 * DeviceState: the virtual device's state, in memory.
 *
 * It is what ConfigManager, the display and the LED strip are on the device:
 * the display items, keys, lists and images of config.json with what a
 * script changed in them, the shown page, the LED colours, the sensors. It
 * holds state only; every rule (argument checks, name validity, limits,
 * selection clamping) ran in the firmware's code inside the wasm before a
 * method here is called.
 *
 * Built from a parsed config.json. The IDE's config-model.ts is the source of
 * truth for what an entry means (mapButton, mapList, mapImage, configNames,
 * mapConnectors, the font table); this file adds only what the preview model
 * leaves out and the firmware needs (display item ids, boxtype, round, the
 * raw page), see docs/EMULATOR.md "Adapter".
 *
 * Every entry keeps the `ref` config-model gives it ({kind, list, index}), so
 * a renderer can lay the runtime state over the preview model it already
 * draws. Changes are observable: subscribe() gets one StateChange per change.
 *
 * Runtime state is never written back to config.json: restoreConfigState()
 * (the firmware's, docs/SCRIPT-RESTART.md) throws it away.
 */
import type { ConfigRef } from '../app-state';
import { configNames, mapButton, mapConnectors, mapImage, mapList, NAME_PATTERN } from '../config-model';
import { findFace, fontTable, type FontTable } from '../font-config';
import { isIconName } from '../icon-raster';
import type {
  DisplayHost, EmulatorHost, FontChoice, FontsHost, ImagesHost, KeysHost, LedsHost, ListsHost,
  SensorsHost, TopologyHost,
} from './host';

export interface ItemState {
  id: number;
  /** config.json entry; absent for an item a script created. */
  ref?: ConfigRef;
  name?: string;
  x: number;
  y: number;
  width: number;
  boxType: number;
  fontSize: number;
  align: number;
  round: number;
  /** 1-based; 0 = every page. */
  page: number;
  /** Shown caption, value and unit (config text until a script sets one; value starts empty). */
  label: string;
  value: string;
  unit: string;
  /** Icon name shown before the value; '' = none. */
  icon: string;
  /** Font a script set; null = the configured one. */
  font: FontChoice | null;
}

export interface KeyState {
  position: number;
  ref?: ConfigRef;
  name?: string;
  label: string;
  topLabel: string;
  /** Icon shown instead of the label; '' = none. */
  icon: string;
  font: FontChoice | null;
  topFont: FontChoice | null;
}

export interface LedState {
  index: number;
  r: number;
  g: number;
  b: number;
}

export interface ListState {
  index: number;
  ref?: ConfigRef;
  name: string;
  items: string[];
  /** -1 when the list is empty. */
  selected: number;
  font: FontChoice | null;
}

export interface ImageState {
  index: number;
  ref?: ConfigRef;
  name: string;
  /** What it shows: an icon name, an images-bucket key, or nothing. */
  kind: 'icon' | 'src' | 'none';
  value: string;
}

export interface SensorState {
  id: number;
  name: string;
  /** "temperature", "humidity", "pressure", "data_size". */
  type: string;
  unit: string;
  online: boolean;
  value: number;
}

export interface BrokerInfo {
  index: number;
  /** config.json "brokerid". */
  name: string;
  url: string;
  hassDiscovery: boolean;
}

/** 0 = empty, 1 = bar, 2 = main display (device_hardware.h). */
export type ConnectorKind = 0 | 1 | 2;

/** One observable change. A renderer redraws what the record names. */
export type StateChange =
  | { kind: 'item'; id: number; field: 'label' | 'value' | 'unit' | 'icon' | 'font' | 'position' | 'page' }
  | { kind: 'item-added'; id: number }
  | { kind: 'item-removed'; id: number }
  | { kind: 'page'; page: number }
  | { kind: 'key'; position: number; field: 'label' | 'topLabel' | 'icon' | 'font' | 'topFont' }
  | { kind: 'led'; index: number }
  | { kind: 'list'; index: number; field: 'items' | 'selected' | 'font' }
  | { kind: 'image'; index: number }
  | { kind: 'sensor'; id: number }
  /** Everything went back to config.json (restart) or a new config was loaded: redraw all. */
  | { kind: 'reset' };

export type StateListener = (change: StateChange, state: DeviceState) => void;

export type SensorSpec = Omit<SensorState, 'online' | 'value'> & { online?: boolean; value?: number };

/**
 * The sensors of a device with the sensor board (ids: SensorType in
 * src/drivers/Sensors.h), with room-like values.
 */
export const DEFAULT_SENSORS: readonly SensorSpec[] = [
  { id: 3, name: 'BME280 Pressure', type: 'pressure', unit: 'hPa', value: 1013.2 },
  { id: 4, name: 'BME280 Humidity', type: 'humidity', unit: '%', value: 45 },
  { id: 5, name: 'BME280 Temperature', type: 'temperature', unit: '°C', value: 21.5 },
];

export interface DeviceStateOptions {
  /**
   * Module per connector (index = connector id 0..3). Default: config.json
   * "info.connectors"; without it, a bar on every connector that has keys in
   * the config and the main display on the first connector without.
   */
  connectors?: readonly ConnectorKind[];
  /** Default: DEFAULT_SENSORS. */
  sensors?: readonly SensorSpec[];
}

type Obj = Record<string, unknown>;

const isObj = (v: unknown): v is Obj => typeof v === 'object' && v !== null && !Array.isArray(v);

function int(v: unknown, fallback: number): number {
  const n = typeof v === 'number' ? v : typeof v === 'string' && v.trim() !== '' ? Number(v) : NaN;
  return Number.isFinite(n) ? Math.trunc(n) : fallback;
}

const text = (v: unknown): string => (typeof v === 'string' ? v : typeof v === 'number' ? String(v) : '');

/** The firmware reads both spellings of a list, in file order (config-model.ts nameEntries). */
function entries(cfg: Obj, lists: readonly string[]): Array<{ list: string; index: number; raw: unknown }> {
  const out: Array<{ list: string; index: number; raw: unknown }> = [];
  for (const key of Object.keys(cfg)) {
    const arr = cfg[key];
    if (!lists.includes(key) || !Array.isArray(arr)) continue;
    arr.forEach((raw, index) => out.push({ list: key, index, raw }));
  }
  return out;
}

/** DisplayItem::configuredId: "displayitemid" as a number or a numeric string; 0 = none. */
function configuredItemId(raw: Obj): number {
  const v = raw.displayitemid;
  if (typeof v === 'number' && Number.isInteger(v) && v > 0) return v;
  if (typeof v === 'string' && /^[0-9]+$/.test(v)) return Number(v);
  return 0;
}

const sameFont = (a: FontChoice | null, b: FontChoice | null): boolean =>
  a === b || (!!a && !!b && a.face === b.face && a.size === b.size);

export class DeviceState {
  /** config.json "core.name" ('' = none: the device then uses its id). */
  deviceName = '';
  brokers: BrokerInfo[] = [];
  /** Module per connector id. */
  connectors: ConnectorKind[] = [0, 0, 0, 0];
  shownPage = 1;
  leds: LedState[] = [];
  lists: ListState[] = [];
  images: ImageState[] = [];
  sensors: SensorState[] = [];

  private itemList: ItemState[] = [];
  private keyList: KeyState[] = [];
  /** Config items a script removed: back at a restart. */
  private parked: ItemState[] = [];
  private nextItemId = 1;
  private itemNames = new Map<string, number>();
  private keyNames = new Map<string, number>();
  private listNames = new Map<string, number>();
  private imageNames = new Map<string, number>();
  private fonts: FontTable = { faces: [] };
  private listeners = new Set<StateListener>();
  private config: Obj = {};
  private readonly options: DeviceStateOptions;

  constructor(config: unknown, options: DeviceStateOptions = {}) {
    this.options = options;
    this.load(config);
  }

  // --- reading ----------------------------------------------------------------

  /** The display items, in display order (config order, then created ones). */
  get items(): readonly ItemState[] {
    return this.itemList;
  }

  /** The keys that exist: in config.json "buttons", on a connector with a module. */
  get keys(): readonly KeyState[] {
    return this.keyList;
  }

  /** A display item by id or config name. */
  item(idOrName: number | string): ItemState | undefined {
    const id = typeof idOrName === 'string' ? this.itemNames.get(idOrName) : idOrName;
    return id === undefined ? undefined : this.itemList.find(i => i.id === id);
  }

  /** A key by position or config name. */
  key(positionOrName: number | string): KeyState | undefined {
    const position = typeof positionOrName === 'string' ? this.keyNames.get(positionOrName) : positionOrName;
    return position === undefined ? undefined : this.keyList.find(k => k.position === position);
  }

  list(indexOrName: number | string): ListState | undefined {
    const index = typeof indexOrName === 'string' ? this.listNames.get(indexOrName) : indexOrName;
    return index === undefined ? undefined : this.lists[index];
  }

  image(indexOrName: number | string): ImageState | undefined {
    const index = typeof indexOrName === 'string' ? this.imageNames.get(indexOrName) : indexOrName;
    return index === undefined ? undefined : this.images[index];
  }

  sensor(id: number): SensorState | undefined {
    return this.sensors.find(s => s.id === id);
  }

  /** Items visible on the shown page (page 0 = every page). */
  visibleItems(): ItemState[] {
    return this.itemList.filter(i => i.page === 0 || i.page === this.shownPage);
  }

  /** Key positions that have a module behind them (any connector that is not empty). */
  physicalKeys(): number[] {
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

  // --- writing (not from scripts) ---------------------------------------------------

  /** A sensor's reading (the emulator's setSensor). */
  setSensor(id: number, value: number): boolean {
    const sensor = this.sensor(id);
    if (!sensor) return false;
    if (sensor.value !== value) {
      sensor.value = value;
      this.emit({ kind: 'sensor', id });
    }
    return true;
  }

  /** The length of the LED strip (the firmware's NUMBER_OF_LEDS; the emulator sets it). All LEDs off. */
  setLedCount(count: number): void {
    this.leds = Array.from({ length: count }, (_, index) => ({ index, r: 0, g: 0, b: 0 }));
  }

  /** Replaces the configuration: all runtime state is gone. */
  load(config: unknown): void {
    this.config = isObj(config) ? config : {};
    const cfg = this.config;
    const core = isObj(cfg.core) ? cfg.core : {};
    this.deviceName = typeof core.name === 'string' ? core.name : '';
    this.brokers = (Array.isArray(cfg.brokers) ? cfg.brokers : []).filter(isObj).map((b, index) => ({
      index,
      name: text(b.brokerid) || `broker${index}`,
      url: text(b.url),
      hassDiscovery: b.hassdiscovery === true,
    }));
    this.fonts = fontTable(cfg);
    this.connectors = this.options.connectors ? [0, 1, 2, 3].map(i => this.options.connectors![i] ?? 0) : this.inferConnectors(cfg);
    this.setLedCount(this.leds.length || 12);
    this.sensors = (this.options.sensors ?? DEFAULT_SENSORS).map(s => ({ online: true, value: 0, ...s }));
    this.buildFromConfig();
    this.emit({ kind: 'reset' });
  }

  /**
   * ConfigManager::restoreConfigState(): what a script changed goes back to
   * config.json. Sensors keep their readings (they are not script state).
   */
  restoreConfigState(): void {
    this.buildFromConfig();
    for (const led of this.leds) led.r = led.g = led.b = 0;
    this.emit({ kind: 'reset' });
  }

  // --- the seam ----------------------------------------------------------------

  /** The groups of EmulatorHost this state implements. */
  readonly host: Pick<EmulatorHost, 'keys' | 'leds' | 'display' | 'lists' | 'images' | 'fonts' | 'sensors' | 'topology'> = {
    keys: this.keysHost(),
    leds: this.ledsHost(),
    display: this.displayHost(),
    lists: this.listsHost(),
    images: this.imagesHost(),
    fonts: this.fontsHost(),
    sensors: this.sensorsHost(),
    topology: this.topologyHost(),
  };

  private keysHost(): KeysHost {
    const set = <F extends 'label' | 'topLabel' | 'icon'>(position: number, field: F, value: string): boolean => {
      const key = this.key(position);
      if (!key) return false;
      if (key[field] !== value) {
        key[field] = value;
        this.emit({ kind: 'key', position, field });
      }
      return true;
    };
    const font = (position: number, value: FontChoice | null, top: boolean): boolean => {
      const key = this.key(position);
      if (!key) return false;
      const field = top ? 'topFont' : 'font';
      if (!sameFont(key[field], value)) {
        key[field] = value;
        this.emit({ kind: 'key', position, field });
      }
      return true;
    };
    return {
      getLabel: position => this.key(position)?.label ?? null,
      getTopLabel: position => this.key(position)?.topLabel ?? null,
      setLabel: (position, value) => set(position, 'label', value),
      setTopLabel: (position, value) => set(position, 'topLabel', value),
      setIcon: (position, name) => set(position, 'icon', name),
      setFont: (position, value, top) => font(position, { ...value }, top),
      restoreFont: (position, top) => font(position, null, top),
      findByName: name => this.keyNames.get(name) ?? null,
    };
  }

  private ledsHost(): LedsHost {
    return {
      setColor: (index, r, g, b) => {
        const led = this.leds[index];
        if (!led || !Number.isInteger(index)) return false;
        if (led.r !== r || led.g !== g || led.b !== b) {
          led.r = r;
          led.g = g;
          led.b = b;
          this.emit({ kind: 'led', index });
        }
        return true;
      },
    };
  }

  private displayHost(): DisplayHost {
    const setText = (id: number, field: 'label' | 'value' | 'unit' | 'icon', value: string): boolean => {
      const item = this.item(id);
      if (!item) return false;
      if (item[field] !== value) {
        item[field] = value;
        this.emit({ kind: 'item', id, field });
      }
      return true;
    };
    const setFont = (id: number, value: FontChoice | null): boolean => {
      const item = this.item(id);
      if (!item) return false;
      if (!sameFont(item.font, value)) {
        item.font = value;
        this.emit({ kind: 'item', id, field: 'font' });
      }
      return true;
    };
    return {
      create: spec => {
        const id = this.nextItemId++;
        const item: ItemState = {
          id, x: spec.x, y: spec.y, width: spec.width, boxType: spec.boxType, fontSize: spec.fontSize,
          align: spec.alignment, round: spec.round, page: spec.page,
          label: spec.label, value: '', unit: spec.unit, icon: '', font: null,
        };
        this.configured.set(item, { label: spec.label, unit: spec.unit });
        this.itemList.push(item);
        this.emit({ kind: 'item-added', id });
        return id;
      },
      remove: id => {
        const at = this.itemList.findIndex(i => i.id === id);
        if (at < 0) return false;
        const [item] = this.itemList.splice(at, 1);
        // A config item is only parked: it comes back at a restart.
        if (item!.ref) this.parked.push(item!);
        this.emit({ kind: 'item-removed', id });
        return true;
      },
      findByName: name => this.itemNames.get(name) ?? null,
      count: () => this.itemList.length,
      getInfo: id => {
        const item = this.item(id);
        if (!item) return null;
        const configured = this.configured.get(item);
        return {
          id: item.id, x: item.x, y: item.y, width: item.width, boxType: item.boxType, fontSize: item.fontSize,
          alignment: item.align, label: configured?.label ?? item.label, unit: configured?.unit ?? item.unit,
          round: item.round, page: item.page,
        };
      },
      allIds: () => this.itemList.map(i => i.id),
      setLabel: (id, value) => setText(id, 'label', value),
      setValue: (id, value) => setText(id, 'value', value),
      setUnit: (id, value) => setText(id, 'unit', value),
      setIcon: (id, name) => setText(id, 'icon', name),
      setFont: (id, value) => setFont(id, { ...value }),
      restoreFont: id => setFont(id, null),
      setPosition: (id, x, y) => {
        const item = this.item(id);
        if (!item) return false;
        if (item.x !== x || item.y !== y) {
          item.x = x;
          item.y = y;
          this.emit({ kind: 'item', id, field: 'position' });
        }
        return true;
      },
      setPage: (id, page) => {
        const item = this.item(id);
        if (!item) return false;
        if (item.page !== page) {
          item.page = page;
          this.emit({ kind: 'item', id, field: 'page' });
        }
        return true;
      },
      showPage: page => {
        if (page < 1) return false;
        if (this.shownPage !== page) {
          this.shownPage = page;
          this.emit({ kind: 'page', page });
        }
        return true;
      },
      shownPage: () => this.shownPage,
    };
  }

  private listsHost(): ListsHost {
    const setFont = (index: number, value: FontChoice | null): boolean => {
      const list = this.lists[index];
      if (!list) return false;
      if (!sameFont(list.font, value)) {
        list.font = value;
        this.emit({ kind: 'list', index, field: 'font' });
      }
      return true;
    };
    return {
      findByName: name => this.listNames.get(name) ?? -1,
      count: index => this.lists[index]?.items.length ?? -1,
      selected: index => this.lists[index]?.selected ?? -1,
      setSelected: (index, selected) => {
        const list = this.lists[index];
        if (!list || list.selected === selected) return;
        list.selected = selected;
        this.emit({ kind: 'list', index, field: 'selected' });
      },
      setItems: (index, items, selected) => {
        const list = this.lists[index];
        if (!list) return;
        list.items = [...items];
        this.emit({ kind: 'list', index, field: 'items' });
        if (list.selected !== selected) {
          list.selected = selected;
          this.emit({ kind: 'list', index, field: 'selected' });
        }
      },
      setFont: (index, value) => setFont(index, { ...value }),
      restoreFont: index => setFont(index, null),
    };
  }

  private imagesHost(): ImagesHost {
    return {
      findByName: name => this.imageNames.get(name) ?? -1,
      exists: index => this.images[index] !== undefined,
      set: (index, kind, value) => {
        const image = this.images[index];
        if (!image) return;
        const next = value === '' ? 'none' : kind;
        if (image.kind === next && image.value === value) return;
        image.kind = next;
        image.value = value;
        this.emit({ kind: 'image', index });
      },
    };
  }

  private fontsHost(): FontsHost {
    return {
      find: name => findFace(this.fonts, name),
      findId: id => findFace(this.fonts, id),
    };
  }

  private sensorsHost(): SensorsHost {
    return { sample: id => this.sensor(id)?.value ?? 0 };
  }

  private topologyHost(): TopologyHost {
    return {
      connectorType: connector => (connector >= 0 && connector < this.connectors.length ? this.connectors[connector]! : -1),
      mainConnector: () => this.connectors.indexOf(2),
    };
  }

  // --- from config.json -----------------------------------------------------------

  /** The configured caption and unit of an item (displayitem_get_info reports these, not the shown texts). */
  private configured = new WeakMap<ItemState, { label: string; unit: string }>();

  private inferConnectors(cfg: Obj): ConnectorKind[] {
    const out: ConnectorKind[] = [0, 0, 0, 0];
    const listed = mapConnectors(cfg);
    if (listed.length) {
      for (const c of listed) {
        if (c.id < out.length) out[c.id] = c.type === 'bar' ? 1 : c.type === 'display' ? 2 : 0;
      }
      return out;
    }
    for (const e of entries(cfg, ['buttons', 'mqttbuttons'])) {
      const button = mapButton(e.raw);
      const connector = button ? Math.floor((button.position - 1) / 2) : -1;
      if (connector >= 0 && connector < out.length) out[connector] = 1;
    }
    const free = out.indexOf(0);
    if (free >= 0) out[free] = 2;
    return out;
  }

  private buildFromConfig(): void {
    const cfg = this.config;
    // The names scripts can use: valid and first-wins, as the firmware's
    // name table (config-model.ts configNames).
    const names = configNames(cfg);
    const usable = (kind: readonly string[], name: unknown, taken: Map<string, number>): name is string =>
      typeof name === 'string' && NAME_PATTERN.test(name) && kind.includes(name) && !taken.has(name);

    // Display items, with the defaults of the firmware's DisplayItem (x 0,
    // y 0, width 100, page 1). Ids as ConfigManager gives them: the
    // configured "displayitemid", else one above every configured id; each
    // item takes a number from the counter whether it uses it or not.
    const itemEntries = entries(cfg, ['mqttdisplays', 'displayitems']).filter(e => isObj(e.raw));
    let nextId = 1 + itemEntries.reduce((max, e) => Math.max(max, configuredItemId(e.raw as Obj)), 0);
    this.itemList = [];
    this.parked = [];
    this.itemNames = new Map();
    for (const e of itemEntries) {
      const raw = e.raw as Obj;
      let id = nextId++;
      const wanted = configuredItemId(raw);
      if (wanted) id = wanted;
      if (this.itemList.some(i => i.id === id)) id = nextId++;
      const item: ItemState = {
        id,
        ref: { kind: 'displayitem', list: e.list, index: e.index },
        x: int(raw.x, 0),
        y: int(raw.y, 0),
        width: int(raw.width, 100),
        boxType: int(raw.boxtype, 0),
        fontSize: int(raw.fontsize, 0),
        align: int(raw.align, 0),
        round: int(raw.round, 0),
        page: int(raw.page, 1),
        label: text(raw.label),
        value: '',
        unit: text(raw.unit),
        icon: isIconName(raw.icon) ? raw.icon : '',
        font: null,
      };
      if (usable(names.displayItems, raw.name, this.itemNames)) {
        item.name = raw.name;
        this.itemNames.set(raw.name, id);
      }
      this.configured.set(item, { label: item.label, unit: item.unit });
      this.itemList.push(item);
    }
    // display.create() continues from here; a restart comes back to it.
    this.nextItemId = nextId;

    // Keys: the first config entry of a position, on a connector with a module.
    this.keyList = [];
    this.keyNames = new Map();
    for (const e of entries(cfg, ['buttons', 'mqttbuttons'])) {
      const button = mapButton(e.raw);
      if (!button) continue;
      const position = button.position;
      const raw = e.raw as Obj;
      if (usable(names.keys, raw.name, this.keyNames)) this.keyNames.set(raw.name, position);
      if (this.keyList.some(k => k.position === position)) continue;
      if ((this.connectors[Math.floor((position - 1) / 2)] ?? 0) === 0) continue;
      this.keyList.push({
        position,
        ref: { kind: 'button', list: e.list, index: e.index },
        ...(button.name !== undefined && this.keyNames.get(button.name) === position ? { name: button.name } : {}),
        label: button.label,
        topLabel: button.topLabel,
        icon: button.icon ?? '',
        font: null,
        topFont: null,
      });
    }
    this.keyList.sort((a, b) => a.position - b.position);

    // Lists and images: only entries with a valid name exist (config-model.ts).
    this.lists = [];
    this.listNames = new Map();
    (Array.isArray(cfg.lists) ? cfg.lists : []).forEach((raw, index) => {
      const list = mapList(raw);
      if (!list) return;
      const at = this.lists.length;
      if (!this.listNames.has(list.name)) this.listNames.set(list.name, at);
      this.lists.push({
        index: at, ref: { kind: 'list', list: 'lists', index }, name: list.name,
        items: [...list.items], selected: list.selected, font: null,
      });
    });
    this.images = [];
    this.imageNames = new Map();
    (Array.isArray(cfg.images) ? cfg.images : []).forEach((raw, index) => {
      const image = mapImage(raw);
      if (!image) return;
      const at = this.images.length;
      if (!this.imageNames.has(image.name)) this.imageNames.set(image.name, at);
      // An icon wins over a src when both are given, as on the device.
      const kind = image.icon ? 'icon' : image.src ? 'src' : 'none';
      this.images.push({
        index: at, ref: { kind: 'image', list: 'images', index }, name: image.name,
        kind, value: image.icon ?? image.src ?? '',
      });
    });

    this.shownPage = 1;
  }

  private emit(change: StateChange): void {
    for (const listener of [...this.listeners]) listener(change, this);
  }
}
