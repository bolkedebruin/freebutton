/**
 * config.json -> button/display model for the virtual device (pure).
 *
 * Reads:
 *   buttons[]            (legacy: mqttbuttons[]) {position, page, label, toplabel|topLabel}
 *   displayitems[]       (legacy: mqttdisplays[]) {displayitemid, x, y, width (% of 320x240),
 *                         page (0 = every page), fontsize, align (lv_align_t), label, unit}
 *   info.connectors[]    {id (0-based), type: 1 = bar, 2 = large display, 0 = empty}
 *   "name" on keys and display items: the names scripts use with fb.key("name")
 *                        and fb.main.display.setValue("name", ...) (configNames,
 *                        validateConfigNames)
 * Key position on connector `id` is id*2+1 (left) and id*2+2 (right), the
 * same mapping the firmware uses (ButtonManager: position0 = connector*2+side).
 */
import type { ButtonConfig, ConnectorConfig, DisplayItem, PageConfig } from './app-state';

export interface DeviceModel {
  pages: PageConfig[];
  /** Sorted by id. Empty when config.json has no info.connectors. */
  connectors: ConnectorConfig[];
  /** Key and display item names scripts can use. */
  names: ConfigNames;
}

/** Valid key / display item name; the firmware enforces the same rule. */
export const NAME_PATTERN = /^[A-Za-z_][A-Za-z0-9_]{0,23}$/;

/** fb.main.display methods whose first argument is an item id or name. */
export const DISPLAY_NAME_METHODS = ['remove', 'get', 'setLabel', 'setValue', 'setUnit', 'setPosition', 'setPage'];

/**
 * Names a script can pass to fb.key() (keys) and to the
 * DISPLAY_NAME_METHODS (display items). Valid names only, first one wins:
 * what the firmware resolves. Keys and display items are separate namespaces.
 */
export interface ConfigNames {
  keys: string[];
  displayItems: string[];
}

type Obj = Record<string, unknown>;

function isObj(v: unknown): v is Obj {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function num(v: unknown, fallback: number): number {
  if (typeof v === 'number' && Number.isFinite(v)) return v;
  if (typeof v === 'string' && v.trim() !== '' && Number.isFinite(Number(v))) return Number(v);
  return fallback;
}

function str(v: unknown): string {
  if (typeof v === 'string') return v;
  if (typeof v === 'number') return String(v);
  return '';
}

function firstArray(o: Obj, ...keys: string[]): unknown[] {
  for (const k of keys) {
    const v = o[k];
    if (Array.isArray(v)) return v;
  }
  return [];
}

/** Key positions on a connector (1-based, left then right). */
export function positionsForConnector(id: number): [number, number] {
  return [id * 2 + 1, id * 2 + 2];
}

export function connectorType(raw: unknown): ConnectorConfig['type'] {
  const t = num(raw, 0);
  if (t === 1) return 'bar';
  if (t === 2) return 'display';
  return 'empty';
}

export function mapButton(raw: unknown): ButtonConfig | null {
  if (!isObj(raw)) return null;
  const position = num(raw.position, NaN);
  if (!Number.isInteger(position) || position < 1) return null;
  const page = num(raw.page, 1);
  // "toplabel" is current; "topLabel" appears in older files.
  const topLabel = str(raw.toplabel ?? raw.topLabel);
  return {
    position,
    page: page >= 1 ? page : 1,
    label: str(raw.label),
    topLabel,
    ledOn: false,
    ledColor: 'rgb(0,0,0)',
    ...(typeof raw.name === 'string' ? { name: raw.name } : {}),
  };
}

/** Display item; page 0 (or missing) means "every page". */
export function mapDisplayItem(raw: unknown): { page: number; item: DisplayItem } | null {
  if (!isObj(raw)) return null;
  const item: DisplayItem = {
    x: num(raw.x, 0),
    y: num(raw.y, 0),
    width: num(raw.width, 100),
    height: num(raw.height, 0),
    type: 'text',
    label: str(raw.label),
    value: '',
    unit: str(raw.unit) || undefined,
    fontSize: num(raw.fontsize ?? raw.fontSize, 1),
    align: raw.align === undefined ? undefined : num(raw.align, 0),
    id: raw.displayitemid === undefined ? undefined : str(raw.displayitemid),
    ...(typeof raw.name === 'string' ? { name: raw.name } : {}),
  };
  return { page: num(raw.page, 0), item };
}

export function mapConnectors(cfg: Obj): ConnectorConfig[] {
  const info = isObj(cfg.info) ? cfg.info : {};
  const list = Array.isArray(info.connectors) ? info.connectors : [];
  const byId = new Map<number, ConnectorConfig>();
  for (const c of list) {
    if (!isObj(c)) continue;
    const id = num(c.id, NaN);
    if (!Number.isInteger(id) || id < 0 || id > 7) continue;
    byId.set(id, { id, type: connectorType(c.type) });
  }
  return [...byId.values()].sort((a, b) => a.id - b.id);
}

/** Map a parsed config.json object to the virtual-device model. */
export function configToModel(cfg: unknown): DeviceModel {
  const o: Obj = isObj(cfg) ? cfg : {};
  const buttons = firstArray(o, 'buttons', 'mqttbuttons')
    .map(mapButton)
    .filter((b): b is ButtonConfig => b !== null);
  const items = firstArray(o, 'displayitems', 'mqttdisplays')
    .map(mapDisplayItem)
    .filter((d): d is { page: number; item: DisplayItem } => d !== null);

  const pageNumbers = new Set<number>([1]);
  buttons.forEach(b => pageNumbers.add(b.page));
  items.forEach(d => { if (d.page >= 1) pageNumbers.add(d.page); });

  const pages: PageConfig[] = [...pageNumbers].sort((a, b) => a - b).map(n => ({
    number: n,
    buttons: buttons.filter(b => b.page === n).sort((a, b) => a.position - b.position),
    displayItems: items.filter(d => d.page < 1 || d.page === n).map(d => d.item),
  }));

  return { pages, connectors: mapConnectors(o), names: configNames(o) };
}

// The firmware reads both spellings of each list, in file order.
const KEY_LISTS = ['buttons', 'mqttbuttons'];
const ITEM_LISTS = ['mqttdisplays', 'displayitems'];

interface NameEntry {
  /** "buttons[2]" */
  where: string;
  name: unknown;
}

/** Entries that have a "name" field, from every spelling of the list, in file order. */
function nameEntries(o: Obj, lists: string[]): NameEntry[] {
  const out: NameEntry[] = [];
  for (const key of Object.keys(o)) {
    const arr = o[key];
    if (!lists.includes(key) || !Array.isArray(arr)) continue;
    arr.forEach((e, i) => {
      if (isObj(e) && 'name' in e) out.push({ where: `${key}[${i}]`, name: e.name });
    });
  }
  return out;
}

function validNames(entries: NameEntry[]): string[] {
  const names = new Set<string>();
  for (const e of entries) {
    if (typeof e.name === 'string' && NAME_PATTERN.test(e.name)) names.add(e.name);
  }
  return [...names];
}

/** Key and display item names in a parsed config.json (see ConfigNames). */
export function configNames(cfg: unknown): ConfigNames {
  const o: Obj = isObj(cfg) ? cfg : {};
  return { keys: validNames(nameEntries(o, KEY_LISTS)), displayItems: validNames(nameEntries(o, ITEM_LISTS)) };
}

/**
 * Problems with "name" fields: a name that does not match NAME_PATTERN, and
 * a name used twice among keys or twice among display items (the firmware
 * ignores those; the first one wins). Empty list = OK.
 */
export function validateConfigNames(cfg: unknown): string[] {
  if (!isObj(cfg)) return [];
  const problems: string[] = [];
  const groups: [string, string[]][] = [['key', KEY_LISTS], ['display item', ITEM_LISTS]];
  for (const [what, lists] of groups) {
    const firstAt = new Map<string, string>();
    for (const { where, name } of nameEntries(cfg, lists)) {
      if (typeof name !== 'string' || !NAME_PATTERN.test(name)) {
        problems.push(`${where}.name ${JSON.stringify(name)} is not a valid name `
          + '(1-24 letters, digits or _, not starting with a digit)');
        continue;
      }
      const first = firstAt.get(name);
      if (first) {
        problems.push(`${where}.name "${name}" is already used by ${first} (${what} names must be unique)`);
      } else {
        firstAt.set(name, where);
      }
    }
  }
  return problems;
}

/** Parse config.json text. Returns null if it is not valid JSON. */
export function parseConfigText(text: string): DeviceModel | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return null;
  }
  return configToModel(parsed);
}
