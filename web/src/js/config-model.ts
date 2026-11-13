/**
 * config.json -> button/display model for the virtual device (pure).
 *
 * Reads:
 *   buttons[]            (legacy: mqttbuttons[]) {position, page, label, toplabel|topLabel}
 *   displayitems[]       (legacy: mqttdisplays[]) {displayitemid, x, y, width (% of 320x240),
 *                         page (0 = every page), fontsize, align (lv_align_t), label, unit}
 *   lists[]              {name, page, x, y, width (% of 320x240), rows, fontsize,
 *                         items[], selected, color, focuscolor, focustextcolor}:
 *                         rollers on the main display (mapList, LIST_DEFAULTS;
 *                         colours not set: style list / list.selected)
 *   info.connectors[]    {id (0-based), type: 1 = bar, 2 = large display, 0 = empty}
 *   icons{}              {"<icon name>": "/path/on/littlefs.svg"}: the SVGs the
 *                        IDE rasterizes for the device (icon-raster.ts)
 *   "icon"/"iconsize"    on keys (default 48) and display items (default 32,
 *                        + "iconcolor"): an icon of the `icons` table
 *   images[]             {name, page, x, y, width, height (all px on the 320x240
 *                        screen; x/y = top-left), icon | src, color}: images
 *                        on the main display
 *                        (mapImage)
 *   style{}              text styles (style-config.ts) and style.fonts[]
 *                        {id?, name, src, fallback?, lineheight?} (font-config.ts):
 *                        the fonts, sizes and colours of the previews;
 *                        "font"/"fontsize" on display items (+ "labelfont"/
 *                        "labelfontsize"), keys (+ "topfont"/"topfontsize") and
 *                        lists, and the lists' colours, override them per item
 *   "name" on keys, display items and lists: the names scripts use with
 *                        fb.key("name"), fb.main.display.setValue("name", ...)
 *                        and fb.main.list.next("name") (configNames,
 *                        validateConfigNames)
 * Key position on connector `id` is id*2+1 (left) and id*2+2 (right), the
 * same mapping the firmware uses (ButtonManager: position0 = connector*2+side).
 */
import type { ButtonConfig, ConfigItemKind, ConfigRef, ConnectorConfig, DisplayItem, ImageConfig, ListConfig, PageConfig } from './app-state';
import { DEFAULT_BUTTON_ICON_SIZE, DEFAULT_DISPLAY_ICON_SIZE, iconTable, isIconName, itemIconSize } from './icon-raster';
import { fontNames, fontStyles, fontTable, hasFontTable, slotFont, FONTSIZE_MAX, SLOTS, type FontTable } from './font-config';
import { DEFAULT_STYLES, StyleElement, styleColorHex, type ResolvedStyles } from './style-config';

export interface DeviceModel {
  pages: PageConfig[];
  /** Sorted by id. Empty when config.json has no info.connectors. */
  connectors: ConnectorConfig[];
  /** Key, display item and list names scripts can use. */
  names: ConfigNames;
  /** config.json "style.fonts" with the system fonts (font-config.ts). */
  fonts: FontTable;
  /** config.json "style" resolved per element (style-config.ts). */
  styles: ResolvedStyles;
}

/** Valid key / display item name; the firmware enforces the same rule. */
export const NAME_PATTERN = /^[A-Za-z_][A-Za-z0-9_]{0,23}$/;

/** fb.main.display methods whose first argument is an item id or name. */
export const DISPLAY_NAME_METHODS = ['remove', 'get', 'setLabel', 'setValue', 'setUnit', 'setPosition', 'setPage', 'setIcon', 'setFont'];

/** fb.main.list methods; the first argument is a list name (or its index in "lists"). */
export const LIST_NAME_METHODS = ['next', 'prev', 'select', 'getSelected', 'count', 'setItems', 'setFont'];

/** fb.main.image methods; the first argument is an image name from "images". */
export const IMAGE_NAME_METHODS = ['setIcon', 'setSrc'];

/**
 * Names a script can pass to fb.key() (keys), to the DISPLAY_NAME_METHODS
 * (display items) and to the LIST_NAME_METHODS (lists). Valid names only,
 * first one wins: what the firmware resolves. Keys, display items and lists
 * are separate namespaces.
 */
export interface ConfigNames {
  keys: string[];
  displayItems: string[];
  lists: string[];
  /** "images" entry names (fb.main.image.setIcon("name", ...)). */
  images?: string[];
  /** Icon names of the `icons` table (setIcon(..., "icon")). */
  icons?: string[];
  /** Font names, the system fonts (montserrat, montserrat_semibold) included; absent = no "style.fonts" table, the system fonts only (setFont(..., "font"), \f{font}). */
  fonts?: string[];
  /** Config font ids (setFont(..., 2), \f{2}). */
  fontIds?: number[];
}

/**
 * config.json "lists" entry defaults (the firmware uses the same). The
 * colours come from the style: color = list text_color, focuscolor =
 * list.selected bg_color, focustextcolor = list.selected text_color.
 */
export const LIST_DEFAULTS = {
  page: 1, x: 0, y: 0, width: 50, rows: 3, fontsize: 3, selected: 0,
} as const;
export const LIST_MAX_ROWS = 7;
/** Legacy fontsize steps 1..5 -> px on the 320x240 screen (6..72 = px; font-config fontsizePx). */
export const FONT_PX = [14, 24, 32, 48, 72] as const;
export const HEX_COLOR = /^[0-9a-fA-F]{6}$/;
/** Icon colours: "#rrggbb" (or "rrggbb" as in lists). */
export const HEX_COLOR_ANY = /^#?[0-9a-fA-F]{6}$/;

/** "#rrggbb" / "rrggbb" -> "#rrggbb" lower case. */
export function normalizeHex(v: string): string {
  return '#' + v.replace(/^#/, '').toLowerCase();
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

/** The first of `keys` that is an array, with the key that was found ('' when none). */
function firstArrayKey(o: Obj, ...keys: string[]): [string, unknown[]] {
  for (const k of keys) {
    const v = o[k];
    if (Array.isArray(v)) return [k, v];
  }
  return ['', []];
}

function withRef<T extends { ref?: ConfigRef }>(item: T | null, kind: ConfigItemKind, list: string, index: number): T | null {
  if (item) item.ref = { kind, list, index };
  return item;
}

/** The kind of entries a top-level config.json array holds (null: not drawn). */
export function configListKind(list: string): ConfigItemKind | null {
  switch (list) {
    case 'buttons': case 'mqttbuttons': return 'button';
    case 'displayitems': case 'mqttdisplays': return 'displayitem';
    case 'lists': return 'list';
    case 'images': return 'image';
    default: return null;
  }
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

export function mapButton(raw: unknown, fonts?: FontTable, styles: ResolvedStyles = DEFAULT_STYLES): ButtonConfig | null {
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
    ...iconFields(raw, DEFAULT_BUTTON_ICON_SIZE),
    // Keys without font fields draw in the style's key.label / key.toplabel font.
    ...(fonts && KEY_FONT_FIELDS.some(f => f in raw)
      ? { labelFont: slotFont(fonts, raw, SLOTS.button, styles), topFont: slotFont(fonts, raw, SLOTS.buttonTop, styles) } : {}),
  };
}

const KEY_FONT_FIELDS = ['font', 'fontsize', 'topfont', 'topfontsize'];

/** "icon" + "iconsize" of a key or display item (valid icon names only). */
function iconFields(raw: Obj, defaultSize: number): { icon?: string; iconSize?: number } {
  if (!isIconName(raw.icon)) return {};
  return { icon: raw.icon, iconSize: itemIconSize(raw.iconsize, defaultSize) ?? defaultSize };
}

/** Display item; page 0 (or missing) means "every page". */
export function mapDisplayItem(
  raw: unknown, fonts?: FontTable, styles: ResolvedStyles = DEFAULT_STYLES,
): { page: number; item: DisplayItem } | null {
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
    ...iconFields(raw, DEFAULT_DISPLAY_ICON_SIZE),
    ...(isIconName(raw.icon) && typeof raw.iconcolor === 'string' && HEX_COLOR_ANY.test(raw.iconcolor)
      ? { iconColor: normalizeHex(raw.iconcolor) } : {}),
    ...(fonts ? { valueFont: slotFont(fonts, raw, SLOTS.value, styles), labelFont: slotFont(fonts, raw, SLOTS.caption, styles) } : {}),
  };
  return { page: num(raw.page, 0), item };
}

function clampInt(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, Math.round(v)));
}

function hexColor(v: unknown, fallback: number | undefined): string {
  return typeof v === 'string' && HEX_COLOR.test(v) ? '#' + v.toLowerCase() : styleColorHex(fallback ?? 0);
}

/** A "lists" entry with defaults; null without a valid name (the firmware skips those). */
export function mapList(raw: unknown, fonts?: FontTable, styles: ResolvedStyles = DEFAULT_STYLES): ListConfig | null {
  if (!isObj(raw) || typeof raw.name !== 'string' || !NAME_PATTERN.test(raw.name)) return null;
  const d = LIST_DEFAULTS;
  const items = Array.isArray(raw.items) ? raw.items.filter((i): i is string => typeof i === 'string') : [];
  const selected = items.length === 0 ? -1 : clampInt(num(raw.selected, d.selected), 0, items.length - 1);
  return {
    name: raw.name,
    page: Math.max(0, Math.round(num(raw.page, d.page))),
    x: num(raw.x, d.x),
    y: num(raw.y, d.y),
    width: num(raw.width, d.width),
    rows: clampInt(num(raw.rows, d.rows), 1, LIST_MAX_ROWS),
    fontSize: clampInt(num(raw.fontsize ?? raw.fontSize, d.fontsize), 1, FONTSIZE_MAX),
    items,
    selected,
    // The item's colours are local styles over the style's list / list.selected.
    color: hexColor(raw.color, styles[StyleElement.List]?.color),
    focusColor: hexColor(raw.focuscolor, styles[StyleElement.ListSelected]?.bgColor),
    focusTextColor: hexColor(raw.focustextcolor, styles[StyleElement.ListSelected]?.color),
    ...(fonts ? { font: slotFont(fonts, raw, SLOTS.list, styles) } : {}),
  };
}

/** config.json "images" entry defaults. */
export const IMAGE_DEFAULTS = { page: 1, x: 0, y: 0 } as const;

/**
 * An "images" entry; null without a valid name. width/height are kept only
 * when both are >= 1 (an "icon" without them shows nothing on the device;
 * a "src" raster is drawn at its own size).
 */
export function mapImage(raw: unknown): ImageConfig | null {
  if (!isObj(raw) || typeof raw.name !== 'string' || !NAME_PATTERN.test(raw.name)) return null;
  const width = num(raw.width, NaN);
  const height = num(raw.height, NaN);
  const sized = width >= 1 && height >= 1;
  return {
    name: raw.name,
    page: Math.max(0, Math.round(num(raw.page, IMAGE_DEFAULTS.page))),
    x: Math.round(num(raw.x, IMAGE_DEFAULTS.x)),
    y: Math.round(num(raw.y, IMAGE_DEFAULTS.y)),
    ...(sized ? { width: Math.round(width), height: Math.round(height) } : {}),
    ...(isIconName(raw.icon) ? { icon: raw.icon } : {}),
    ...(typeof raw.src === 'string' && raw.src ? { src: raw.src } : {}),
    ...(typeof raw.color === 'string' && HEX_COLOR_ANY.test(raw.color) ? { color: normalizeHex(raw.color) } : {}),
  };
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
  const fonts = fontTable(o);
  const styles = fontStyles(o, fonts);
  // Every mapped entry keeps where it came from (`ref`), so the virtual device
  // and the editor can point at each other (config-locate.ts).
  const [buttonKey, buttonList] = firstArrayKey(o, 'buttons', 'mqttbuttons');
  const buttons = buttonList
    .map((b, index) => withRef(mapButton(b, fonts, styles), 'button', buttonKey, index))
    .filter((b): b is ButtonConfig => b !== null);
  const [itemKey, itemList] = firstArrayKey(o, 'displayitems', 'mqttdisplays');
  const items = itemList
    .map((d, index) => {
      const mapped = mapDisplayItem(d, fonts, styles);
      if (mapped) mapped.item.ref = { kind: 'displayitem', list: itemKey, index };
      return mapped;
    })
    .filter((d): d is { page: number; item: DisplayItem } => d !== null);
  const lists = (Array.isArray(o.lists) ? o.lists : [])
    .map((l, index) => withRef(mapList(l, fonts, styles), 'list', 'lists', index))
    .filter((l): l is ListConfig => l !== null);
  const images = (Array.isArray(o.images) ? o.images : [])
    .map((m, index) => withRef(mapImage(m), 'image', 'images', index))
    .filter((m): m is ImageConfig => m !== null);

  const pageNumbers = new Set<number>([1]);
  buttons.forEach(b => pageNumbers.add(b.page));
  items.forEach(d => { if (d.page >= 1) pageNumbers.add(d.page); });
  lists.forEach(l => { if (l.page >= 1) pageNumbers.add(l.page); });
  images.forEach(m => { if (m.page >= 1) pageNumbers.add(m.page); });

  const pages: PageConfig[] = [...pageNumbers].sort((a, b) => a - b).map(n => ({
    number: n,
    buttons: buttons.filter(b => b.page === n).sort((a, b) => a.position - b.position),
    displayItems: items.filter(d => d.page < 1 || d.page === n).map(d => d.item),
    lists: lists.filter(l => l.page < 1 || l.page === n),
    ...(images.length ? { images: images.filter(m => m.page < 1 || m.page === n) } : {}),
  }));

  return { pages, connectors: mapConnectors(o), names: configNames(o), fonts, styles };
}

// The firmware reads both spellings of each list, in file order.
const KEY_LISTS = ['buttons', 'mqttbuttons'];
const ITEM_LISTS = ['mqttdisplays', 'displayitems'];
const LIST_LISTS = ['lists'];
const IMAGE_LISTS = ['images'];

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

/** Key, display item and list names in a parsed config.json (see ConfigNames). */
export function configNames(cfg: unknown): ConfigNames {
  const o: Obj = isObj(cfg) ? cfg : {};
  return {
    keys: validNames(nameEntries(o, KEY_LISTS)),
    displayItems: validNames(nameEntries(o, ITEM_LISTS)),
    lists: validNames(nameEntries(o, LIST_LISTS)),
    images: validNames(nameEntries(o, IMAGE_LISTS)),
    icons: [...iconTable(o).keys()],
    // Without a "style.fonts" table: the system fonts only (validator SYSTEM_FONT_NAMES).
    ...(hasFontTable(o) ? fontNames(o) : {}),
  };
}

/**
 * Problems with "name" fields: a name that does not match NAME_PATTERN, and
 * a name used twice among keys, display items, lists or images (the firmware
 * ignores those; the first one wins). A list without a name is a shape
 * problem (fs-rules validateConfigShape). Empty list = OK.
 */
export function validateConfigNames(cfg: unknown): string[] {
  if (!isObj(cfg)) return [];
  const problems: string[] = [];
  const groups: [string, string[]][] = [
    ['key', KEY_LISTS], ['display item', ITEM_LISTS], ['list', LIST_LISTS], ['image', IMAGE_LISTS],
  ];
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
