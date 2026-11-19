/**
 * config.json -> button/display model for the virtual device (pure).
 *
 * Reads:
 *   buttons[]            {position, page, label, toplabel, style}
 *   displayitems[]       {displayitemid, x, y, width (% of 320x240),
 *                         page (0 = every page), align (lv_align_t), boxtype,
 *                         label, unit, style}
 *   lists[]              {name, page, x, y, width (% of 320x240), rows,
 *                         items[], selected, style}: rollers on the main
 *                         display (mapList, LIST_DEFAULTS)
 *   info.connectors[]    {id (0-based), type: 1 = bar, 2 = large display, 0 = empty}
 *   icons{}              {"<icon name>": "/path/on/littlefs.svg"}: the SVGs the
 *                        IDE rasterizes for the device (icon-raster.ts)
 *   "icon"/"iconsize"    on buttons (default 48) and display items (default
 *                        32): an icon of the `icons` table
 *   images[]             {name, page, x, y, width, height (all px on the 320x240
 *                        screen; x/y = top-left), icon | src, color}: images
 *                        on the main display (mapImage)
 *   clocks[]             {clockid, page, x, y, width, height (px), align}: the
 *                        analog clock; the device draws the first entry only
 *                        (mapClock)
 *   fonts[]              {id?, name, src, fallback?, lineheight?} (font-config.ts)
 *   styles{}             the looks (style-config.ts): "button", "displayitem"
 *                        and "list" are the look of a kind; an item lists
 *                        the others it wants in "style" ("big alarm": up to
 *                        four names, the last wins). The fonts, sizes,
 *                        colours and paddings of the previews are looked up
 *                        through them, as on the device.
 *   "name" on buttons, display items and lists: the names scripts use with
 *                        fb.button("name"), fb.main.display.setValue("name", ...)
 *                        and fb.main.list.next("name") (configNames,
 *                        validateConfigNames)
 * Button position on connector `id` is id*2+1 (left) and id*2+2 (right), the
 * same mapping the firmware uses (ButtonManager: position0 = connector*2+side).
 */
import type { ButtonConfig, ClockConfig, ConfigItemKind, ConfigRef, ConnectorConfig, DisplayItem, ImageConfig, ListConfig, PageConfig } from './app-state';
import { DEFAULT_BUTTON_ICON_SIZE, DEFAULT_DISPLAY_ICON_SIZE, iconTable, isIconName, itemIconSize } from './icon-raster';
import { fontNames, fontStyles, fontTable, hasFontTable, itemStyleList, slotFont, styleNames, SLOTS, type FontTable } from './font-config';
import {
  boxPadding, DEFAULT_CONFIG_STYLES, lookupIconStyle, lookupPart, paddingStep, styleColorHex, styleSelectedRow,
  type ConfigStyles, type StyleProps,
} from './style-config';

export interface DeviceModel {
  pages: PageConfig[];
  /** Sorted by id. Empty when config.json has no info.connectors. */
  connectors: ConnectorConfig[];
  /** Button, display item and list names scripts can use. */
  names: ConfigNames;
  /** config.json "fonts" with the system fonts (font-config.ts). */
  fonts: FontTable;
  /** config.json "styles": the look of each kind and the styles an item can name (style-config.ts). */
  styles: ConfigStyles;
}

/** Valid button / display item name; the firmware enforces the same rule. */
export const NAME_PATTERN = /^[A-Za-z_][A-Za-z0-9_]{0,23}$/;

/** fb.main.display methods whose first argument is an item id or name. */
export const DISPLAY_NAME_METHODS = [
  'remove', 'get', 'setLabel', 'setValue', 'setUnit', 'setPosition', 'setPage', 'setIcon', 'setStyle', 'addStyle', 'removeStyle',
];

/** fb.main.list methods; the first argument is a list name (or its index in "lists"). */
export const LIST_NAME_METHODS = ['next', 'prev', 'select', 'getSelected', 'count', 'setItems', 'setStyle'];

/**
 * The script calls that take styles: a list of names (setStyle) or one name
 * (addStyle, removeStyle), on fb.main.display, fb.main.list (setStyle only)
 * and a button's display.
 */
export const STYLE_METHODS = {
  display: ['setStyle', 'addStyle', 'removeStyle'],
  list: ['setStyle'],
  button: ['setStyle', 'addStyle', 'removeStyle'],
} as const;

/** fb.main.image methods; the first argument is an image name from "images". */
export const IMAGE_NAME_METHODS = ['setIcon', 'setSrc'];

/**
 * Names a script can pass to fb.button() (keys), to the DISPLAY_NAME_METHODS
 * (display items) and to the LIST_NAME_METHODS (lists). Valid names only,
 * first one wins: what the firmware resolves. Buttons, display items and
 * lists are separate namespaces.
 */
export interface ConfigNames {
  /** "buttons" entry names (fb.button("name")). */
  keys: string[];
  displayItems: string[];
  lists: string[];
  /** "images" entry names (fb.main.image.setIcon("name", ...)). */
  images?: string[];
  /** Icon names of the `icons` table (setIcon(..., "icon")). */
  icons?: string[];
  /** Font names, the system fonts (montserrat, montserrat_semibold) included; absent = no "fonts" table, the system fonts only (\f{font}). */
  fonts?: string[];
  /** Config font ids (\f{2}). */
  fontIds?: number[];
  /**
   * The styles, "button", "displayitem" and "list" included, each with the
   * dotted properties fb.style.set can set for it ("color", "title.color",
   * "icon.color": every property but the fixed three, in each block the
   * style has; style-config styleSettable). Absent = not known: style names
   * in a script are not checked.
   */
  styles?: Record<string, string[]>;
}

/** config.json "lists" entry defaults (the firmware uses the same). */
export const LIST_DEFAULTS = {
  page: 1, x: 0, y: 0, width: 50, rows: 3, selected: 0,
} as const;
export const LIST_MAX_ROWS = 7;
/** Image colours: "#rrggbb" (or "rrggbb"). */
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

function withRef<T extends { ref?: ConfigRef }>(item: T | null, kind: ConfigItemKind, list: string, index: number): T | null {
  if (item) item.ref = { kind, list, index };
  return item;
}

/** The kind of entries a top-level config.json array holds (null: not drawn). */
export function configListKind(list: string): ConfigItemKind | null {
  switch (list) {
    case 'buttons': return 'button';
    case 'displayitems': return 'displayitem';
    case 'lists': return 'list';
    case 'images': return 'image';
    case 'clocks': return 'clock';
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

/** A "buttons" entry; null without a position (the firmware does not show it). */
export function mapButton(raw: unknown, styles: ConfigStyles = DEFAULT_CONFIG_STYLES): ButtonConfig | null {
  if (!isObj(raw)) return null;
  const position = num(raw.position, NaN);
  if (!Number.isInteger(position) || position < 1) return null;
  const page = num(raw.page, 1);
  const list = itemStyleList(raw, styles);
  return {
    position,
    page: page >= 1 ? page : 1,
    label: str(raw.label),
    topLabel: str(raw.toplabel),
    ledOn: false,
    ledColor: 'rgb(0,0,0)',
    ...(typeof raw.name === 'string' ? { name: raw.name } : {}),
    ...iconFields(raw, DEFAULT_BUTTON_ICON_SIZE),
    labelFont: slotFont(styles, SLOTS.button, list),
    topFont: slotFont(styles, SLOTS.buttonTop, list),
    labelStyle: lookupPart(styles, 'button', 'main', list).props,
    topStyle: lookupPart(styles, 'button', 'title', list).props,
    ...iconTint(styles, 'button', list),
  };
}

/** The "icon" colour the styles give an item's icon; none: the preview shows the SVG's own colours. */
function iconTint(styles: ConfigStyles, kind: 'button' | 'displayitem', list: readonly string[]): { iconColor?: string } {
  const color = lookupIconStyle(styles, kind, list);
  return color === null ? {} : { iconColor: styleColorHex(color) };
}

/** "icon" + "iconsize" of a button or display item (valid icon names only). */
function iconFields(raw: Obj, defaultSize: number): { icon?: string; iconSize?: number } {
  if (!isIconName(raw.icon)) return {};
  return { icon: raw.icon, iconSize: itemIconSize(raw.iconsize, defaultSize) ?? defaultSize };
}

/** Display item; page 0 (or missing) means "every page". */
export function mapDisplayItem(raw: unknown, styles: ConfigStyles = DEFAULT_CONFIG_STYLES): { page: number; item: DisplayItem } | null {
  if (!isObj(raw)) return null;
  const list = itemStyleList(raw, styles);
  const value = lookupPart(styles, 'displayitem', 'main', list);
  // The padding that goes with the value's size (src/ui/box_logic.h).
  const pad = boxPadding(paddingStep(value.props.px ?? 14, value.ownSize), num(raw.boxtype, 0));
  const item: DisplayItem = {
    x: num(raw.x, 0),
    y: num(raw.y, 0),
    width: num(raw.width, 100),
    height: num(raw.height, 0),
    type: 'text',
    label: str(raw.label),
    value: '',
    unit: str(raw.unit) || undefined,
    align: raw.align === undefined ? undefined : num(raw.align, 0),
    id: raw.displayitemid === undefined ? undefined : str(raw.displayitemid),
    ...(typeof raw.name === 'string' ? { name: raw.name } : {}),
    ...iconFields(raw, DEFAULT_DISPLAY_ICON_SIZE),
    ...iconTint(styles, 'displayitem', list),
    valueFont: slotFont(styles, SLOTS.value, list),
    unitFont: slotFont(styles, SLOTS.unit, list),
    labelFont: slotFont(styles, SLOTS.caption, list),
    valueStyle: value.props,
    captionStyle: lookupPart(styles, 'displayitem', 'title', list).props,
    unitStyle: lookupPart(styles, 'displayitem', 'unit', list).props,
    ...(pad ? { pad } : {}),
  };
  return { page: num(raw.page, 0), item };
}

function clampInt(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, Math.round(v)));
}

/**
 * The selected row of a list as the device draws it: the lookup of the
 * "selected" blocks, with the rows' opacity, letter-spacing and
 * text-decoration where no "selected" block sets them (styleSelectedRow),
 * and the rows' text-align: the roller places every row alike, and
 * "text-align" in "selected" is ignored.
 */
export function listSelectedRow(styles: ConfigStyles, list: readonly string[]): StyleProps {
  const rows = lookupPart(styles, 'list', 'main', list).props;
  const row = styleSelectedRow(lookupPart(styles, 'list', 'selected', list).props, rows);
  if (rows.align === undefined) delete row.align;
  else row.align = rows.align;
  return row;
}

/** A "lists" entry with defaults; null without a valid name (the firmware skips those). */
export function mapList(raw: unknown, styles: ConfigStyles = DEFAULT_CONFIG_STYLES): ListConfig | null {
  if (!isObj(raw) || typeof raw.name !== 'string' || !NAME_PATTERN.test(raw.name)) return null;
  const d = LIST_DEFAULTS;
  const list = itemStyleList(raw, styles);
  const items = Array.isArray(raw.items) ? raw.items.filter((i): i is string => typeof i === 'string') : [];
  const selected = items.length === 0 ? -1 : clampInt(num(raw.selected, d.selected), 0, items.length - 1);
  return {
    name: raw.name,
    page: Math.max(0, Math.round(num(raw.page, d.page))),
    x: num(raw.x, d.x),
    y: num(raw.y, d.y),
    width: num(raw.width, d.width),
    rows: clampInt(num(raw.rows, d.rows), 1, LIST_MAX_ROWS),
    font: slotFont(styles, SLOTS.list, list),
    selectedFont: slotFont(styles, SLOTS.listSelected, list),
    items,
    selected,
    style: lookupPart(styles, 'list', 'main', list).props,
    selectedStyle: listSelectedRow(styles, list),
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

/** config.json "clocks" entry defaults (Clock.h: the same). */
export const CLOCK_DEFAULTS = { page: 1, x: 0, y: 0, width: 260, height: 260, align: 1 } as const;

/** A "clocks" entry with the firmware's defaults (Clock.h configure). */
export function mapClock(raw: unknown): ClockConfig | null {
  if (!isObj(raw)) return null;
  const d = CLOCK_DEFAULTS;
  return {
    page: Math.round(num(raw.page, d.page)),
    x: Math.round(num(raw.x, d.x)),
    y: Math.round(num(raw.y, d.y)),
    width: Math.round(num(raw.width, d.width)),
    height: Math.round(num(raw.height, d.height)),
    align: Math.round(num(raw.align, d.align)),
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
  const buttons = (Array.isArray(o.buttons) ? o.buttons : [])
    .map((b, index) => withRef(mapButton(b, styles), 'button', 'buttons', index))
    .filter((b): b is ButtonConfig => b !== null);
  const items = (Array.isArray(o.displayitems) ? o.displayitems : [])
    .map((d, index) => {
      const mapped = mapDisplayItem(d, styles);
      if (mapped) mapped.item.ref = { kind: 'displayitem', list: 'displayitems', index };
      return mapped;
    })
    .filter((d): d is { page: number; item: DisplayItem } => d !== null);
  const lists = (Array.isArray(o.lists) ? o.lists : [])
    .map((l, index) => withRef(mapList(l, styles), 'list', 'lists', index))
    .filter((l): l is ListConfig => l !== null);
  const images = (Array.isArray(o.images) ? o.images : [])
    .map((m, index) => withRef(mapImage(m), 'image', 'images', index))
    .filter((m): m is ImageConfig => m !== null);
  // The device draws one clock: the first entry.
  const clock = withRef(mapClock((Array.isArray(o.clocks) ? o.clocks : [])[0]), 'clock', 'clocks', 0);

  const pageNumbers = new Set<number>([1]);
  buttons.forEach(b => pageNumbers.add(b.page));
  items.forEach(d => { if (d.page >= 1) pageNumbers.add(d.page); });
  lists.forEach(l => { if (l.page >= 1) pageNumbers.add(l.page); });
  images.forEach(m => { if (m.page >= 1) pageNumbers.add(m.page); });
  if (clock && clock.page >= 1) pageNumbers.add(clock.page);

  const pages: PageConfig[] = [...pageNumbers].sort((a, b) => a - b).map(n => ({
    number: n,
    buttons: buttons.filter(b => b.page === n).sort((a, b) => a.position - b.position),
    displayItems: items.filter(d => d.page < 1 || d.page === n).map(d => d.item),
    lists: lists.filter(l => l.page < 1 || l.page === n),
    ...(images.length ? { images: images.filter(m => m.page < 1 || m.page === n) } : {}),
    ...(clock && (clock.page < 1 || clock.page === n) ? { clocks: [clock] } : {}),
  }));

  return { pages, connectors: mapConnectors(o), names: configNames(o), fonts, styles };
}


interface NameEntry {
  /** "buttons[2]" */
  where: string;
  name: unknown;
}

/** The entries of the array `key` that have a "name" field, in file order. */
function nameEntries(o: Obj, key: string): NameEntry[] {
  const arr = o[key];
  const out: NameEntry[] = [];
  if (!Array.isArray(arr)) return out;
  arr.forEach((e, i) => {
    if (isObj(e) && 'name' in e) out.push({ where: `${key}[${i}]`, name: e.name });
  });
  return out;
}

function validNames(entries: NameEntry[]): string[] {
  const names = new Set<string>();
  for (const e of entries) {
    if (typeof e.name === 'string' && NAME_PATTERN.test(e.name)) names.add(e.name);
  }
  return [...names];
}

/** Button, display item and list names in a parsed config.json (see ConfigNames). */
export function configNames(cfg: unknown): ConfigNames {
  const o: Obj = isObj(cfg) ? cfg : {};
  return {
    keys: validNames(nameEntries(o, 'buttons')),
    displayItems: validNames(nameEntries(o, 'displayitems')),
    lists: validNames(nameEntries(o, 'lists')),
    images: validNames(nameEntries(o, 'images')),
    icons: [...iconTable(o).keys()],
    // Without a "fonts" table: the system fonts only (validator SYSTEM_FONT_NAMES).
    ...(hasFontTable(o) ? fontNames(o) : {}),
    styles: styleNames(o),
  };
}

/**
 * Problems with "name" fields: a name that does not match NAME_PATTERN, and
 * a name used twice among buttons, display items, lists or images (the firmware
 * ignores those; the first one wins). A list without a name is a shape
 * problem (fs-rules validateConfigShape). Empty list = OK.
 */
export function validateConfigNames(cfg: unknown): string[] {
  if (!isObj(cfg)) return [];
  const problems: string[] = [];
  const groups: [string, string][] = [
    ['button', 'buttons'], ['display item', 'displayitems'], ['list', 'lists'], ['image', 'images'],
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
