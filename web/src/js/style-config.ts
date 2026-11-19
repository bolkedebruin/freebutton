/**
 * config.json "styles" (pure): the looks of buttons, display items and
 * lists. The same rules as the firmware's src/ui/style_logic.h
 * (style_logic.c), src/ui/box_logic.h, src/components/StyleRef.h and the
 * style loader of src/components/ConfigManager.cpp; keep them in sync
 * (docs/STYLE.md, docs/specs/named-styles.md).
 *
 *   "styles": {
 *     "button":      {"font-size": 32, "title": {"font-size": 18}},   reserved:
 *     "displayitem": {"font-size": 14},                               the look of
 *     "list":        {"selected": {"background-color": "#ffffff"}},   a kind
 *     "big":   {"font-size": 48},
 *     "alarm": {"color": "#ff0000", "title": {"color": "#ff8080"}}
 *   },
 *   "displayitems": [{"name": "co2", "style": "big alarm"}]
 *
 * A style is a block of properties for the item's main text, with optional
 * sub-blocks ("parts") of the same properties:
 *
 *   part        button         display item        list
 *   main        the label      the value           the rows
 *   "title"     the top label  the caption         -
 *   "unit"      -              the unit            -
 *   "selected"  -              -                   the selected row
 *   "icon"      the icon       the icon            -      ("color" only)
 *
 * The lookup of one property of one part of an item:
 *   1. the item's styles ("style": up to four names), the last name first:
 *      that part's block;
 *   2. the style of the item's kind ("button", "displayitem", "list"): that
 *      part's block;
 *   3. the built-in value (styleDefaults).
 * 2 and 3 are resolved once into one set of properties per kind and part:
 * the seven elements. Parts do not inherit from each other. Three
 * exceptions: the unit has the value's font and size where no "unit" block
 * sets one; the selected row has the rows' font likewise, and also the
 * rows' opacity, letter-spacing and text-decoration where no "selected"
 * block sets them (styleSelectedRow); the icon's colour, where no "icon"
 * block sets one, is the main colour. Face and size are looked up
 * separately.
 *
 * This module knows face indexes, not font names: font-family goes through
 * a resolver (font-config fontResolver), as on the device.
 */
import { FONT_PX_MAX, FONT_PX_MIN, STEP_PX, TextAlign, type FontResolver, type TextSpan } from './text-spans';

/** Face indexes of the system fonts (font-config SYSTEM_FONTS; firmware FONT_FACE_SYSTEM / _BOLD). */
export const FACE_SYSTEM = 0;
export const FACE_SYSTEM_BOLD = 1;

/** A kind and part that exists: its properties after steps 2 and 3 (style_element_t). */
export enum StyleElement {
  /** button, main */
  KeyLabel = 0,
  /** button, title */
  KeyTopLabel,
  /** displayitem, main */
  DisplayValue,
  /** displayitem, unit */
  DisplayUnit,
  /** displayitem, title */
  DisplayCaption,
  /** list, main */
  List,
  /** list, selected: the roller's LV_PART_SELECTED */
  ListSelected,
}
export const STYLE_ELEMENT_COUNT = 7;

/** The kinds, which are also the reserved style names. */
export const STYLE_KINDS = ['button', 'displayitem', 'list'] as const;
export type StyleKind = typeof STYLE_KINDS[number];

/** The parts that hold properties. */
export const STYLE_PARTS = ['main', 'title', 'unit', 'selected'] as const;
export type StylePart = typeof STYLE_PARTS[number];
/** The keys of a style that are sub-blocks, not properties. */
export const STYLE_SUB_BLOCKS = ['title', 'unit', 'selected', 'icon'] as const;

export enum StyleAlign { Left = 1, Center = 2, Right = 3 }
/** text-decoration bits (style_decor_t). */
export const DECOR_NONE = 0;
export const DECOR_UNDERLINE = 1;
export const DECOR_LINE_THROUGH = 2;

/** Styles a config may define, the three reserved ones included (STYLE_MAX). */
export const STYLE_MAX = 32;
/** The others: the firmware's style table (STYLE_NAMED_MAX). */
export const STYLE_NAMED_MAX = STYLE_MAX - STYLE_KINDS.length;
export const STYLE_NAME_MAX = 24;
/** Styles an item may list (STYLE_ITEM_MAX). */
export const STYLE_ITEM_MAX = 4;
/** A style name (style_logic_name_valid; docs/NAMES.md), case sensitive. */
export const STYLE_NAME_PATTERN = /^[A-Za-z_][A-Za-z0-9_]{0,23}$/;

/** The properties one block sets (absent = not set; style_props_t and its "has" bits). */
export interface StyleProps {
  /** font-family -> face index. */
  face?: number;
  /** font-size, px. */
  px?: number;
  /** color 0xRRGGBB. */
  color?: number;
  /** opacity, stored 0..255. */
  opa?: number;
  /** text-align. */
  align?: StyleAlign;
  /** letter-spacing, px. */
  letterSpace?: number;
  /** line-height in hundredths: 125 = 1.25 times the font's line height. */
  lineHeight?: number;
  /** text-decoration: DECOR_* bits. */
  decor?: number;
  /** background-color 0xAARRGGBB: the colour, and its opacity in the top byte. */
  bg?: number;
}

/** A style of the table: the blocks it has, and its icon colour. */
export interface Style {
  main?: StyleProps;
  title?: StyleProps;
  unit?: StyleProps;
  selected?: StyleProps;
  /** "icon": {"color"} as 0xRRGGBB. */
  icon?: number;
}

/** What config.json sets for the three reserved styles (style_sheet_t). */
export interface StyleSheet {
  element: StyleProps[];
  /** "button.icon.color", "displayitem.icon.color". */
  icon: Partial<Record<StyleKind, number>>;
}

/** The styles of a config as the device has them after boot. */
export interface ConfigStyles {
  /** Per element (index: StyleElement): the built-in look with what the kind's style sets. */
  elements: readonly Readonly<StyleProps>[];
  /** The icon colour of "button" and "displayitem", where config sets one. */
  kindIcon: Readonly<Partial<Record<StyleKind, number>>>;
  /** The other styles by name, in definition order. */
  named: ReadonlyMap<string, Readonly<Style>>;
}

const PROP_KEYS: (keyof StyleProps)[] = ['face', 'px', 'color', 'opa', 'align', 'letterSpace', 'lineHeight', 'decor', 'bg'];

function hasOwn(o: object, k: string): boolean {
  return Object.prototype.hasOwnProperty.call(o, k);
}

type Obj = Record<string, unknown>;

function isObj(v: unknown): v is Obj {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

// ---- kinds, parts and defaults -------------------------------------------------

/** "button" / "displayitem" / "list" -> the kind; null for any other name (style_logic_kind). */
export function styleKind(name: string): StyleKind | null {
  return (STYLE_KINDS as readonly string[]).includes(name) ? name as StyleKind : null;
}

/** The key of a sub-block (style_logic_part): "title" / "unit" / "selected" / "icon"; null for a property name. */
export function styleSubBlock(name: string): typeof STYLE_SUB_BLOCKS[number] | null {
  return (STYLE_SUB_BLOCKS as readonly string[]).includes(name) ? name as typeof STYLE_SUB_BLOCKS[number] : null;
}

const ELEMENTS: Record<StyleKind, Partial<Record<StylePart, StyleElement>>> = {
  button: { main: StyleElement.KeyLabel, title: StyleElement.KeyTopLabel },
  displayitem: { main: StyleElement.DisplayValue, title: StyleElement.DisplayCaption, unit: StyleElement.DisplayUnit },
  list: { main: StyleElement.List, selected: StyleElement.ListSelected },
};

/** The element of a kind's part, -1 when the kind has no such part (style_logic_element). */
export function styleElement(kind: StyleKind, part: StylePart): number {
  return ELEMENTS[kind][part] ?? -1;
}

/** A title element: step 2 of the "\s" markup = 18 px (styleItemPx). */
export function isTitleElement(el: StyleElement): boolean {
  return el === StyleElement.KeyTopLabel || el === StyleElement.DisplayCaption;
}

/**
 * Step 3, the look without "styles" (style_logic_defaults):
 *   button main       system 32 px, #ffffff          (centred)
 *   button title      system bold 18 px, #ffd700     (centred)
 *   displayitem main  system 14 px, #ffffff
 *   displayitem unit  #999999 (font and size: the value's)
 *   displayitem title system bold 14 px, #ffd700
 *   list main         system 32 px, #808080, line-height 1.25
 *   list selected     #000000 on #ffd700 (font: the rows')
 */
export function styleDefaults(): StyleProps[] {
  const out: StyleProps[] = [];
  out[StyleElement.KeyLabel] = { face: FACE_SYSTEM, px: 32, color: 0xffffff };
  out[StyleElement.KeyTopLabel] = { face: FACE_SYSTEM_BOLD, px: 18, color: 0xffd700 };
  out[StyleElement.DisplayValue] = { face: FACE_SYSTEM, px: 14, color: 0xffffff };
  out[StyleElement.DisplayUnit] = { color: 0x999999 };
  out[StyleElement.DisplayCaption] = { face: FACE_SYSTEM_BOLD, px: 14, color: 0xffd700 };
  out[StyleElement.List] = { face: FACE_SYSTEM, px: 32, color: 0x808080, lineHeight: 125 };
  out[StyleElement.ListSelected] = { color: 0x000000, bg: 0xffffd700 };
  return out;
}

/** `dst` takes every property `src` sets (style_logic_merge). Returns `dst`. */
export function mergeStyle(dst: StyleProps, src: Readonly<StyleProps>): StyleProps {
  for (const k of PROP_KEYS) {
    if (src[k] !== undefined) (dst as Record<string, number>)[k] = src[k] as number;
  }
  return dst;
}

export function emptySheet(): StyleSheet {
  return { element: Array.from({ length: STYLE_ELEMENT_COUNT }, () => ({})), icon: {} };
}

/**
 * Steps 2 and 3 for every element (style_logic_resolve): the defaults, then
 * what the kind's style sets. Each part by itself: a title, the unit and
 * the selected row take nothing from the kind's main block. `sheet` null =
 * no reserved style in the config.
 */
export function resolveStyles(sheet: StyleSheet | null): StyleProps[] {
  const out = styleDefaults();
  if (sheet) {
    for (let el = 0; el < STYLE_ELEMENT_COUNT; el++) mergeStyle(out[el]!, sheet.element[el] ?? {});
  }
  return out;
}

/** The styles of a config without "styles" (shared; do not modify). */
export const DEFAULT_CONFIG_STYLES: ConfigStyles = Object.freeze({
  elements: Object.freeze(resolveStyles(null).map(p => Object.freeze(p))),
  kindIcon: Object.freeze({}),
  named: new Map(),
});

/**
 * The size of a "\s" step in a text (style_logic_item_px): 1..5 = 14 / 24 /
 * 32 / 48 / 72, except step 2 of a title (top label, caption) = 18;
 * FONT_PX_MIN..FONT_PX_MAX = px; anything else = `fallbackPx`.
 */
export function styleItemPx(fontsize: number, title: boolean, fallbackPx: number): number {
  if (!Number.isInteger(fontsize)) return fallbackPx;
  if (fontsize >= 1 && fontsize <= STEP_PX.length) return title && fontsize === 2 ? 18 : STEP_PX[fontsize - 1]!;
  if (fontsize >= FONT_PX_MIN && fontsize <= FONT_PX_MAX) return fontsize;
  return fallbackPx;
}

// ---- properties ------------------------------------------------------------------

export type StylePropStatus = 'ok' | 'unknown' | 'bad-value' | 'unknown-font';

/** fb.style.set only (style_logic_script_set_*, ui_style.c): the style has no such block; font-family / font-size / line-height. */
export type ScriptSetStatus = StylePropStatus | 'no-part' | 'fixed';

type PropType = 'string' | 'integer' | 'decimal' | 'either';

/** Every property name, in the firmware's order, with the JSON type its value takes and the StyleProps field. */
const PROPS: Record<string, { type: PropType; key: keyof StyleProps }> = {
  color: { type: 'string', key: 'color' },
  'font-family': { type: 'either', key: 'face' },
  'font-size': { type: 'integer', key: 'px' },
  'text-align': { type: 'string', key: 'align' },
  'letter-spacing': { type: 'integer', key: 'letterSpace' },
  'text-decoration': { type: 'string', key: 'decor' },
  opacity: { type: 'decimal', key: 'opa' },
  'line-height': { type: 'decimal', key: 'lineHeight' },
  'background-color': { type: 'string', key: 'bg' },
};

/** The property names, in documentation order. */
export const STYLE_PROPERTIES = Object.keys(PROPS);
/** What fb.style.set never changes: they are per font. */
export const STYLE_FIXED_PROPERTIES = ['font-family', 'font-size', 'line-height'] as const;

export const STYLE_SPACE_MIN = -100;
export const STYLE_SPACE_MAX = 100;
export const STYLE_LINE_HEIGHT_MIN = 80;
export const STYLE_LINE_HEIGHT_MAX = 300;

function prop(name: string): { type: PropType; key: keyof StyleProps } | null {
  return hasOwn(PROPS, name) ? PROPS[name]! : null;
}

/**
 * How a JSON number for `prop` goes to setStyleNumber (style_logic_prop_scale):
 * 1 = an integer, as it is; 100 = a number of at most two decimals, times
 * 100; 0 = the property takes no number, or is not a property.
 */
export function stylePropScale(name: string): number {
  const info = prop(name);
  if (!info) return 0;
  return info.type === 'decimal' ? 100 : info.type === 'string' ? 0 : 1;
}

/**
 * A number as setStyleNumber takes it for a property of `scale`
 * (StyleRef.h scaled): null when it is no such number (a fraction, more than
 * two decimals, NaN). The tolerance is the firmware's: its JSON reader
 * keeps a short decimal as a float (1.2 = 1.2000000476837158).
 */
export function scaledNumber(value: number, scale: number): number | null {
  const x = value * scale;
  if (!(x > -1000000 && x < 1000000)) return null;
  const rounded = Math.round(x);
  return Math.abs(x - rounded) > 1e-4 ? null : rounded;
}

const COLOR = /^#[0-9a-fA-F]{6}$/;
const COLOR_ALPHA = /^#[0-9a-fA-F]{8}$/;

/** "#rrggbb" -> 0xRRGGBB; null when malformed (style_logic_parse_color). The "#" is required. */
export function parseStyleColor(s: unknown): number | null {
  return typeof s === 'string' && COLOR.test(s) ? parseInt(s.slice(1), 16) : null;
}

/** "#rrggbb" -> 0xFFRRGGBB, "#rrggbbaa" -> 0xAARRGGBB; null when malformed (style_logic_parse_background). */
export function parseStyleBackground(s: unknown): number | null {
  if (typeof s !== 'string') return null;
  if (COLOR.test(s)) return 0xff000000 + parseInt(s.slice(1), 16);
  if (COLOR_ALPHA.test(s)) return parseInt(s.slice(7), 16) * 0x1000000 + parseInt(s.slice(1, 7), 16);
  return null;
}

const ALIGNS: Record<string, StyleAlign> = { left: StyleAlign.Left, center: StyleAlign.Center, right: StyleAlign.Right };
const DECORS: Record<string, number> = {
  none: DECOR_NONE, underline: DECOR_UNDERLINE, 'line-through': DECOR_LINE_THROUGH,
  'underline line-through': DECOR_UNDERLINE | DECOR_LINE_THROUGH,
  'line-through underline': DECOR_UNDERLINE | DECOR_LINE_THROUGH,
};

/**
 * One property from a JSON string (style_logic_set_string): font-family (a
 * font name), color, text-align, text-decoration, background-color. On an
 * error `p` is unchanged. `resolve` null: no font is known.
 */
export function setStyleString(p: StyleProps, name: string, value: string, resolve: FontResolver | null): StylePropStatus {
  const info = prop(name);
  if (!info) return 'unknown';
  if (info.type !== 'string' && info.type !== 'either') return 'bad-value';
  switch (name) {
    case 'font-family': {
      if (value === '') return 'bad-value';
      const face = resolve ? resolve(value, false, 0) : -1;
      if (face < 0) return 'unknown-font';
      p.face = face;
      return 'ok';
    }
    case 'color': {
      const c = parseStyleColor(value);
      if (c === null) return 'bad-value';
      p.color = c;
      return 'ok';
    }
    case 'background-color': {
      const c = parseStyleBackground(value);
      if (c === null) return 'bad-value';
      p.bg = c;
      return 'ok';
    }
    case 'text-align':
      if (!hasOwn(ALIGNS, value)) return 'bad-value';
      p.align = ALIGNS[value]!;
      return 'ok';
    case 'text-decoration':
      if (!hasOwn(DECORS, value)) return 'bad-value';
      p.decor = DECORS[value]!;
      return 'ok';
    default:
      return 'bad-value';
  }
}

/**
 * One property from a JSON number scaled as stylePropScale says
 * (style_logic_set_number): font-family (a config font id >= 1), font-size
 * (FONT_PX_MIN..FONT_PX_MAX), letter-spacing (-100..100), opacity (0..100
 * -> stored 0..255), line-height (80..300).
 */
export function setStyleNumber(p: StyleProps, name: string, value: number, resolve: FontResolver | null): StylePropStatus {
  const info = prop(name);
  if (!info) return 'unknown';
  if (info.type === 'string') return 'bad-value';
  switch (name) {
    case 'font-family': {
      if (value < 1) return 'unknown-font'; // ids start at 1
      const face = resolve ? resolve(String(value), true, value) : -1;
      if (face < 0) return 'unknown-font';
      p.face = face;
      return 'ok';
    }
    case 'font-size':
      if (value < FONT_PX_MIN || value > FONT_PX_MAX) return 'bad-value';
      p.px = value;
      return 'ok';
    case 'opacity':
      if (value < 0 || value > 100) return 'bad-value';
      p.opa = Math.floor((value * 255 + 50) / 100); // 0..1 in hundredths -> 0..255, rounded
      return 'ok';
    case 'letter-spacing':
      if (value < STYLE_SPACE_MIN || value > STYLE_SPACE_MAX) return 'bad-value';
      p.letterSpace = value;
      return 'ok';
    case 'line-height':
      if (value < STYLE_LINE_HEIGHT_MIN || value > STYLE_LINE_HEIGHT_MAX) return 'bad-value';
      p.lineHeight = value;
      return 'ok';
    default:
      return 'bad-value';
  }
}

/**
 * One property from any JSON value, as the config loader takes it
 * (ConfigManager parseStyleProps): a string, or a number that fits the
 * property's scale; anything else is a bad value (or an unknown property).
 */
export function setStyleValue(p: StyleProps, name: string, value: unknown, resolve: FontResolver | null): StylePropStatus {
  if (typeof value === 'string') return setStyleString(p, name, value, resolve);
  const scale = stylePropScale(name);
  // Not a number property: unknown, or it takes a string.
  if (scale === 0) return setStyleNumber(p, name, 0, resolve);
  const number = typeof value === 'number' ? scaledNumber(value, scale) : null;
  return number === null ? 'bad-value' : setStyleNumber(p, name, number, resolve);
}

/** An "icon" block's one property, "color" (style_logic_icon_set_string): the colour, or the status. */
export function iconStyleValue(name: string, value: unknown): number | 'unknown' | 'bad-value' {
  if (name !== 'color') return 'unknown';
  return parseStyleColor(value) ?? 'bad-value';
}

const VALUE_RULE: Record<string, string> = {
  color: 'a colour "#rrggbb"',
  'font-family': 'a font name or id',
  'font-size': `an integer ${FONT_PX_MIN}..${FONT_PX_MAX} (px)`,
  'text-align': '"left", "center" or "right"',
  'letter-spacing': `an integer ${STYLE_SPACE_MIN}..${STYLE_SPACE_MAX} (px)`,
  'text-decoration': '"none", "underline", "line-through" or "underline line-through"',
  opacity: 'a number 0..1 with at most two decimals',
  'line-height': 'a number 0.8..3 with at most two decimals (times the font\'s line height)',
  'background-color': 'a colour "#rrggbb" or "#rrggbbaa"',
};

/** For messages: what a value of a property must be ("a colour ..."). */
export function styleValueRule(name: string): string {
  // Own properties only: "constructor" is no property name.
  return hasOwn(VALUE_RULE, name) ? VALUE_RULE[name]! : 'a valid value';
}

/** The property names a block sets, in documentation order. */
export function stylePropertyNames(p: Readonly<StyleProps>): string[] {
  return STYLE_PROPERTIES.filter(name => p[PROPS[name]!.key] !== undefined);
}

// ---- the style list of an item ("style": "big alarm") -------------------------------

/** The names of a "style" string: separated by one or more spaces (style_logic_list_next). */
export function styleListNames(text: string): string[] {
  return text.split(' ').filter(n => n !== '');
}

/** `list` without `name` (style_logic_list_remove). */
export function styleListRemove(list: readonly string[], name: string): string[] {
  return list.filter(n => n !== name);
}

/**
 * `list` with `name` as its last name (style_logic_list_add): a name the
 * list already has moves to the end. null when that would be a fifth name.
 */
export function styleListAdd(list: readonly string[], name: string): string[] | null {
  const next = styleListRemove(list, name);
  return next.length >= STYLE_ITEM_MAX ? null : [...next, name];
}

export interface ItemStyle {
  /** The styles the device gives the item, in order (the last wins). */
  list: string[];
  /** What the device logs for the names it leaves out (StyleRef.h parse), without the field's path. */
  problems: string[];
}

/**
 * The style list for an item's "style" value, as the device takes it at
 * config load (StyleRef.h parse). A name that cannot be used is left out,
 * the others stay: a reserved name, a name that is no style name, a style
 * "styles" does not define, a fifth name. A name given twice counts once, at
 * its last place. Empty when absent, null, "" or not a string.
 */
export function parseItemStyle(value: unknown, named: { has(name: string): boolean }): ItemStyle {
  if (value === undefined || value === null) return { list: [], problems: [] };
  if (typeof value !== 'string') return { list: [], problems: ['is not a string of style names, ignored'] };
  let list: string[] = [];
  const problems: string[] = [];
  for (const name of styleListNames(value)) {
    if (styleKind(name)) {
      problems.push(`"${name}" is the style of a kind and cannot be named by an item, left out`);
    } else if (!STYLE_NAME_PATTERN.test(name)) {
      problems.push(`"${name}" is not a style name, left out`);
    } else if (!named.has(name)) {
      problems.push(`unknown style "${name}", left out`);
    } else {
      const next = styleListAdd(list, name);
      if (next) list = next;
      else problems.push(`more than ${STYLE_ITEM_MAX} styles, "${name}" left out`);
    }
  }
  return { list, problems };
}

export type ScriptStyleList =
  | { ok: true; list: string[] }
  | { ok: false; error: 'reserved' | 'unknown' | 'too-many'; name: string };

/**
 * The style list a script gives (setStyle, display.create; style_hw_parse_list):
 * the first name that cannot be used ends it. The device throws TypeError
 * for 'reserved', ReferenceError for 'unknown' and RangeError for
 * 'too-many', and changes nothing. An empty list = the configured one.
 */
export function parseScriptStyleList(text: string, named: { has(name: string): boolean }): ScriptStyleList {
  let list: string[] = [];
  for (const name of styleListNames(text)) {
    if (styleKind(name)) return { ok: false, error: 'reserved', name };
    if (!named.has(name)) return { ok: false, error: 'unknown', name };
    const next = styleListAdd(list, name);
    if (!next) return { ok: false, error: 'too-many', name };
    list = next;
  }
  return { ok: true, list };
}

// ---- the lookup ------------------------------------------------------------------

/**
 * One part of an item (style_logic_lookup): `kind` is the element, `layers`
 * the blocks of that part in the item's styles, in list order (undefined
 * where a style has no such block). Every property from the last layer that
 * sets it, else from `kind`.
 */
export function styleLookup(kind: Readonly<StyleProps>, layers: readonly (Readonly<StyleProps> | undefined)[]): StyleProps {
  const out = { ...kind };
  for (const layer of layers) if (layer) mergeStyle(out, layer);
  return out;
}

/** Does one of the item's own styles (not the kind) set one of `keys` in this part (style_logic_layers_set)? */
export function styleLayersSet(layers: readonly (Readonly<StyleProps> | undefined)[], keys: readonly (keyof StyleProps)[]): boolean {
  return layers.some(l => l !== undefined && keys.some(k => l[k] !== undefined));
}

/**
 * A part whose font follows another's (style_logic_follow_font): the face
 * and the px that `part` sets, each separately, else the base's.
 */
export function styleFollowFont(part: Readonly<StyleProps>, baseFace: number, basePx: number): { face: number; px: number } {
  return { face: part.face ?? baseFace, px: part.px ?? basePx };
}

/**
 * The selected row of a list as it is drawn (style_logic_selected_row).
 * `selected` and `rows` are the lookup results of the two parts. LVGL takes
 * an inheritable property that the roller's selected part does not hold from
 * the roller itself, so the selected row has the rows' opacity,
 * letter-spacing and text-decoration where no "selected" block (of the
 * list's styles or of "list") sets them. Never the colour or the background:
 * the built-in selected row has both. The font follows the rows' in the same
 * way (styleFollowFont).
 */
export function styleSelectedRow(selected: Readonly<StyleProps>, rows: Readonly<StyleProps>): StyleProps {
  const out = { ...selected };
  for (const k of ['opa', 'letterSpace', 'decor'] as const) {
    if (out[k] === undefined && rows[k] !== undefined) out[k] = rows[k];
  }
  return out;
}

/** The text-align of a lookup result; 0 when no style sets one (style_logic_text_align). */
export function styleTextAlign(resolved: Readonly<StyleProps>): StyleAlign | 0 {
  return resolved.align ?? 0;
}

/**
 * LVGL's text_line_space for a font whose line is `fontLineHeight` px high
 * (style_logic_line_space): (line-height - 1) lines. 125 gives a quarter of
 * the line, the gap a list always had.
 */
export function styleLineSpace(fontLineHeight: number, lineHeightX100: number): number {
  return Math.trunc(fontLineHeight * (lineHeightX100 - 100) / 100);
}

/**
 * The tint of an icon (style_logic_icon_color): the "icon" colour of the
 * last of the item's styles that has one, else the kind's; null when none
 * sets one (the tint is then the main text colour).
 */
export function styleIconColor(kindIcon: number | undefined, layers: readonly (number | undefined)[]): number | null {
  let icon = kindIcon;
  for (const layer of layers) if (layer !== undefined) icon = layer;
  return icon ?? null;
}

/** The blocks of `part` in the styles of `list`, in list order (a name that is no style is left out). */
export function styleLayers(styles: ConfigStyles, part: StylePart, list: readonly string[]): (Readonly<StyleProps> | undefined)[] {
  return list.map(name => styles.named.get(name)).filter((s): s is Readonly<Style> => s !== undefined).map(s => s[part]);
}

/**
 * One part of an item of `kind` whose styles are `list` (ui_style_lookup):
 * every property, and whether one of the item's own styles sets the size.
 * A part the kind does not have: nothing.
 */
export function lookupPart(
  styles: ConfigStyles, kind: StyleKind, part: StylePart, list: readonly string[],
): { props: StyleProps; ownSize: boolean } {
  const el = styleElement(kind, part);
  if (el < 0) return { props: {}, ownSize: false };
  const layers = styleLayers(styles, part, list);
  return { props: styleLookup(styles.elements[el] ?? {}, layers), ownSize: styleLayersSet(layers, ['px']) };
}

/** The "icon" colour of an item (ui_style_icon_color): of its styles, else of its kind; null when none sets one. */
export function lookupIconStyle(styles: ConfigStyles, kind: StyleKind, list: readonly string[]): number | null {
  const layers = list.map(name => styles.named.get(name)).filter((s): s is Readonly<Style> => s !== undefined).map(s => s.icon);
  return styleIconColor(styles.kindIcon[kind], layers);
}

/** The tint of an item's icon: its "icon" colour, else the main text colour (ui_box.c, ui_button.c). */
export function lookupIconColor(styles: ConfigStyles, kind: StyleKind, list: readonly string[]): number {
  return lookupIconStyle(styles, kind, list) ?? lookupPart(styles, kind, 'main', list).props.color ?? 0xffffff;
}

// ---- the padding of a display item (src/ui/box_logic.h) -------------------------------

/**
 * The step whose padding a display item gets (box_logic_padding_step): the
 * value's resulting size as the nearest of 14 / 24 / 32 / 48 / 72, a tie to
 * the smaller. 0 (none) under 14 px, and at exactly 14 px that none of the
 * item's own styles set (`ownSize` false).
 */
export function paddingStep(px: number, ownSize: boolean): number {
  if (px < 14 || (px === 14 && !ownSize) || px > FONT_PX_MAX) return 0;
  let best = 0;
  for (let i = 1; i < STEP_PX.length; i++) {
    if (Math.abs(px - STEP_PX[i]!) < Math.abs(px - STEP_PX[best]!)) best = i;
  }
  return best + 1;
}

/** The bottom padding (px) of the caption and of the box for a padding step and the item's boxtype; null for step 0 (box_logic_padding). */
export function boxPadding(step: number, boxtype: number): { captionPad: number; boxPad: number } | null {
  switch (step) {
    case 1: return { captionPad: 2, boxPad: 1 };
    case 2: return { captionPad: 4, boxPad: 3 };
    case 3: return { captionPad: boxtype ? 0 : 4, boxPad: 8 };
    case 4: return { captionPad: boxtype ? 6 : 10, boxPad: 8 };
    case 5: return { captionPad: boxtype ? 6 : 14, boxPad: 12 };
    default: return null;
  }
}

/**
 * Where a text is placed in the width of its part (box_logic_text_align):
 * by the last \al, \ac or \ar among its spans, else by the part's
 * text-align; 0 = not aligned (a display item part is as wide as its text,
 * a button label is centred, a list is left aligned).
 */
export function boxTextAlign(spans: readonly Pick<TextSpan, 'align'>[], styleAlign: StyleAlign | 0): StyleAlign | 0 {
  let align: StyleAlign | 0 = styleAlign;
  // The two enums have the same values: left 1, centre 2, right 3.
  for (const s of spans) if (s.align !== TextAlign.Keep) align = s.align as number as StyleAlign;
  return align;
}

// ---- a script changes a value (fb.style.set) -----------------------------------------

/**
 * "color" -> main; "title.color", "unit.color", "selected.color",
 * "icon.color" -> that part (style_logic_split_prop). A prefix that is no
 * part gives the whole string as a main property (which is then unknown).
 */
export function splitStyleProp(dotted: string): { part: StylePart | 'icon'; name: string } {
  const dot = dotted.indexOf('.');
  const prefix = dot < 0 ? null : styleSubBlock(dotted.slice(0, dot));
  return prefix ? { part: prefix, name: dotted.slice(dot + 1) } : { part: 'main', name: dotted };
}

/** The properties fb.style.set may set, in documentation order: all but the fixed three (style_logic_script_settable). */
export const STYLE_SCRIPT_PROPERTIES: readonly string[] =
  STYLE_PROPERTIES.filter(name => !(STYLE_FIXED_PROPERTIES as readonly string[]).includes(name));

/**
 * The dotted properties fb.style.set can set for each style (ui_style.c
 * table_set, kind_set): every property but font-family, font-size and
 * line-height, in each block the style has. A style of the table has the
 * blocks config.json gives it (the table is sized at config load: a block
 * cannot be added), "button", "displayitem" and "list" the parts of the
 * kind. "icon.color": every style of the table, "button" and "displayitem"
 * (a list draws no icon). A property the block does not set in config is
 * added by the call.
 */
export function styleSettable(styles: ConfigStyles): Record<string, string[]> {
  // No prototype: a style may be named "constructor", and a lookup of a
  // name that is no style must not find an Object.prototype member.
  const out = Object.create(null) as Record<string, string[]>;
  const dotted = (part: StylePart) => STYLE_SCRIPT_PROPERTIES.map(n => (part === 'main' ? n : `${part}.${n}`));
  for (const kind of STYLE_KINDS) {
    out[kind] = STYLE_PARTS.flatMap(part => (styleElement(kind, part) < 0 ? [] : dotted(part)));
    if (kind !== 'list') out[kind]!.push('icon.color');
  }
  for (const [name, style] of styles.named) {
    out[name] = [...STYLE_PARTS.flatMap(part => (style[part] ? dotted(part) : [])), 'icon.color'];
  }
  return out;
}

/**
 * What fb.style.set(style, property, value) does with a style of which
 * `settable` are the properties it can take (styleSettable), in the
 * firmware's order (ui_style.c set_value, style_logic_script_set_*):
 *   1. the name is a property                      else 'unknown'
 *   2. it is not font-family, font-size or
 *      line-height                                  else 'fixed'
 *   3. the style has the block the property is in   else 'no-part'
 *   4. the value is valid, as in config.json        else 'bad-value'
 * A number that does not fit the property (14.5 for an integer, a third
 * decimal) is a bad value, in step 4. `value` null: steps 1 to 3 only. The
 * device throws TypeError for all but 'ok'.
 */
export function scriptSetStatus(settable: readonly string[], dotted: string, value: string | number | null): ScriptSetStatus {
  const { part, name } = splitStyleProp(dotted);
  if (part === 'icon') {
    if (name !== 'color') return 'unknown';
    if (!settable.includes(dotted)) return 'no-part';
    if (typeof value === 'number') return 'bad-value';
    return value === null || parseStyleColor(value) !== null ? 'ok' : 'bad-value';
  }
  if (!prop(name)) return 'unknown';
  if ((STYLE_FIXED_PROPERTIES as readonly string[]).includes(name)) return 'fixed';
  if (!settable.includes(dotted)) return 'no-part';
  if (value === null) return 'ok';
  if (typeof value === 'string') return setStyleString({}, name, value, null);
  const scale = stylePropScale(name);
  const number = scale === 0 ? 0 : scaledNumber(value, scale);
  return number === null ? 'bad-value' : setStyleNumber({}, name, number, null);
}

/**
 * The device's message for 'no-part' (freebutton_device.c): the block the
 * property is in, which the style does not have.
 */
export function scriptSetNoPartMessage(style: string, dotted: string): string {
  const { part } = splitStyleProp(dotted);
  if (styleKind(style)) return `fb.style.set(): the style of this kind has no "${part}" part`;
  return part === 'main' ? 'fb.style.set(): the style has no main block in config.json'
    : `fb.style.set(): the style has no "${part}" block in config.json`;
}

// ---- keys the config no longer has ---------------------------------------------------

export type ConfigScope = 'top' | 'button' | 'displayitem' | 'list' | 'broker';

const ITEMS: ConfigScope[] = ['button', 'displayitem', 'list'];

/** style_logic.c removed_: the key, where it was read, and what to use instead. */
const REMOVED_KEYS: readonly { key: string; scopes: readonly ConfigScope[]; hint: string }[] = [
  { key: 'style', scopes: ['top'], hint: 'use the top-level "fonts" and "styles"' },
  { key: 'mqttbuttons', scopes: ['top'], hint: 'use "buttons"' },
  { key: 'mqttdisplays', scopes: ['top'], hint: 'use "displayitems"' },
  { key: 'mqttbrokers', scopes: ['top'], hint: 'use "brokers"' },
  { key: 'font', scopes: ITEMS, hint: 'use a style with "font-family"' },
  { key: 'fontsize', scopes: ITEMS, hint: 'use a style with "font-size"' },
  { key: 'topfont', scopes: ['button'], hint: 'use a style with "title": {"font-family"}' },
  { key: 'topfontsize', scopes: ['button'], hint: 'use a style with "title": {"font-size"}' },
  { key: 'topstyle', scopes: ['button'], hint: 'use a style with a "title" block' },
  { key: 'topLabel', scopes: ['button'], hint: 'use "toplabel"' },
  { key: 'leds', scopes: ['button'], hint: 'put the topics in "topics"; the topics under "leds" are no longer subscribed' },
  { key: 'buttonuid', scopes: ['button'], hint: '"position" and "page" say where the button is' },
  { key: 'id', scopes: ['button'], hint: 'use "position"' },
  { key: 'labelfont', scopes: ['displayitem'], hint: 'use a style with "title": {"font-family"}' },
  { key: 'labelfontsize', scopes: ['displayitem'], hint: 'use a style with "title": {"font-size"}' },
  { key: 'labelstyle', scopes: ['displayitem'], hint: 'use a style with a "title" block' },
  { key: 'iconcolor', scopes: ['displayitem'], hint: 'use a style with "icon": {"color"}' },
  { key: 'color', scopes: ['list'], hint: 'use a style with "color"' },
  { key: 'focuscolor', scopes: ['list'], hint: 'use a style with "selected": {"background-color"}' },
  { key: 'focustextcolor', scopes: ['list'], hint: 'use a style with "selected": {"color"}' },
  { key: 'focusstyle', scopes: ['list'], hint: 'use a style with a "selected" block' },
  { key: 'autoDiscovery', scopes: ['broker'], hint: 'use "hassdiscovery"; Home Assistant discovery is on unless "hassdiscovery" is false' },
];

/**
 * Is `key` in `scope` one the firmware read before and no longer reads
 * (style_logic_removed_key)? The hint says what to use instead; null when
 * the key is not a removed one there ("color" is removed on a list and
 * valid on an image).
 */
export function removedKey(scope: ConfigScope, key: string): string | null {
  return REMOVED_KEYS.find(r => r.key === key && r.scopes.includes(scope))?.hint ?? null;
}

/** The top-level sections whose entries the firmware scans for removed keys, with their scope. */
const SCOPE_OF_LIST: Record<string, ConfigScope> = {
  buttons: 'button', displayitems: 'displayitem', lists: 'list', brokers: 'broker',
};

/**
 * The removed keys of a config, as the device logs them at load: once per
 * key name ('"fontsize" is no longer read: use a style with "font-size"'),
 * here with where the key is.
 *
 * The firmware finds them by depth, not by shape (ConfigManager.cpp
 * RemovedKeyScan: the keys at depth 3 under "buttons", "displayitems",
 * "lists" and "brokers", where an object and an array each count one
 * level). So besides the keys of an array's entries it reports the keys of
 * an object's member objects ({"buttons": {"a": {"font": 1}}}) and of an
 * object in an array in an array. Such a section is not a valid one, but the
 * device logs the key, and so does this.
 */
export function removedKeyProblems(cfg: unknown): string[] {
  if (!isObj(cfg)) return [];
  const found = new Map<string, { hint: string; at: string[] }>();
  const report = (scope: ConfigScope, key: string, at: string) => {
    const hint = removedKey(scope, key);
    if (hint === null) return;
    const entry = found.get(key) ?? { hint, at: [] };
    entry.at.push(at);
    found.set(key, entry);
  };
  for (const [key, value] of Object.entries(cfg)) {
    report('top', key, 'config.json');
    const scope = hasOwn(SCOPE_OF_LIST, key) ? SCOPE_OF_LIST[key]! : null;
    if (!scope) continue;
    // Depth 2: the members of the section, an array's entries or an object's values.
    const members: [string, unknown][] = Array.isArray(value) ? value.map((entry, i) => [`${key}[${i}]`, entry])
      : isObj(value) ? Object.entries(value).map(([k, entry]) => [`${key}.${k}`, entry])
      : [];
    for (const [at, member] of members) {
      // Depth 3: the keys of an object there; an array there holds objects one level further (not scanned).
      if (isObj(member)) for (const k of Object.keys(member)) report(scope, k, at);
    }
  }
  return [...found].map(([key, { hint, at }]) => {
    const where = at[0] === 'config.json' ? '' : ` (${at[0]}${at.length > 1 ? ` and ${at.length - 1} more` : ''})`;
    return `"${key}" is no longer read: ${hint}${where}`;
  });
}

// ---- config.json "styles" --------------------------------------------------------------

export interface ParsedStyles {
  /** What the device has after boot. */
  styles: ConfigStyles;
  /** What config.json sets for the reserved styles. */
  sheet: StyleSheet;
  /** What the device logs while it loads "styles" (and ignores), with what the IDE can add. */
  problems: string[];
}

const NOT_A_PROPERTY: Record<string, string> = {
  'font-weight': 'there is no font-weight: a bold face is another font, chosen with "font-family"',
  'font-style': 'there is no font-style: an italic face is another font, chosen with "font-family"',
};

/**
 * config.json "styles" -> the styles the device builds (ConfigManager
 * parseStyleTable, parseKindStyle), plus every problem in the firmware's
 * wording: an unknown property (font-weight and font-style with the
 * reason), a bad value, an unknown font, a bad style name, more than
 * STYLE_MAX styles, a block that is no object, a sub-block the kind does
 * not have. `fontsHint` names the fonts in the unknown-font message.
 */
export function parseStyles(cfg: unknown, resolve: FontResolver, fontsHint = 'a "fonts" entry'): ParsedStyles {
  const sheet = emptySheet();
  const named = new Map<string, Style>();
  const problems: string[] = [];
  const raw = isObj(cfg) ? cfg.styles : undefined;
  const result = (): ParsedStyles => ({
    styles: { elements: resolveStyles(sheet), kindIcon: sheet.icon, named }, sheet, problems,
  });
  if (raw === undefined || raw === null) return result();
  if (!isObj(raw)) {
    problems.push('"styles" is not an object, ignored');
    return result();
  }

  const warn = (status: StylePropStatus, at: string, name: string, value: unknown, inPart: boolean) => {
    if (status === 'ok') return;
    if (status === 'unknown') {
      const why = hasOwn(NOT_A_PROPERTY, name) ? NOT_A_PROPERTY[name]!
        : inPart && styleSubBlock(name) ? 'a part cannot hold another part'
        : isObj(value) ? `"${name}" is not a part: ${STYLE_SUB_BLOCKS.join(', ')}`
        : `the properties are ${STYLE_PROPERTIES.join(', ')}`;
      problems.push(`${at}: unknown property "${name}", ignored (${why})`);
    } else if (status === 'unknown-font') {
      problems.push(`${at}.${name}: unknown font, ignored (${JSON.stringify(value)} is not ${fontsHint})`);
    } else {
      problems.push(`${at}.${name}: bad value, ignored (it takes ${styleValueRule(name)})`);
    }
  };
  /** One block into `p`; `part` null = the style's main block, whose sub-block keys are not properties. */
  const props = (block: Obj, p: StyleProps, style: string, part: string | null) => {
    const at = part ? `styles.${style}.${part}` : `styles.${style}`;
    for (const [name, value] of Object.entries(block)) {
      if (part === null && styleSubBlock(name)) continue;
      warn(setStyleValue(p, name, value, resolve), at, name, value, part !== null);
    }
  };
  /** An "icon" block: its colour, or undefined. */
  const icon = (block: Obj, style: string, before: number | undefined): number | undefined => {
    let color = before;
    for (const [name, value] of Object.entries(block)) {
      const status = iconStyleValue(name, value);
      if (typeof status === 'number') color = status;
      else if (status === 'unknown') problems.push(`styles.${style}.icon: unknown property "${name}", ignored (an "icon" block takes "color" only)`);
      else problems.push(`styles.${style}.icon.${name}: bad value, ignored (it takes ${styleValueRule('color')})`);
    }
    return color;
  };
  /** The sub-block `part` of a style when it is an object; a problem when it is something else. */
  const subBlock = (block: Obj, style: string, part: string): Obj | null => {
    const sub = block[part];
    if (sub === undefined || sub === null) return null;
    if (isObj(sub)) return sub;
    problems.push(`styles.${style}.${part} is not an object, ignored`);
    return null;
  };

  // The reserved styles, into the sheet.
  for (const [name, value] of Object.entries(raw)) {
    const kind = styleKind(name);
    if (!kind) continue;
    if (!isObj(value)) {
      problems.push(`styles.${name} is not an object, ignored`);
      continue;
    }
    props(value, sheet.element[styleElement(kind, 'main')]!, name, null);
    for (const part of STYLE_SUB_BLOCKS) {
      const sub = subBlock(value, name, part);
      if (!sub) continue;
      if (part === 'icon' && kind !== 'list') {
        const color = icon(sub, name, sheet.icon[kind]);
        if (color !== undefined) sheet.icon[kind] = color;
        continue;
      }
      const el = part === 'icon' ? -1 : styleElement(kind, part);
      if (el < 0) {
        problems.push(`styles.${name}.${part}: a ${name} has no such part, ignored`);
        continue;
      }
      props(sub, sheet.element[el]!, name, part);
    }
  }

  // The others, into the table: a bad name or a block that is no object is skipped, the 30th ends the walk.
  for (const [name, value] of Object.entries(raw)) {
    if (styleKind(name)) continue;
    if (!STYLE_NAME_PATTERN.test(name)) {
      problems.push(`styles: "${name}" is not a style name (letters, digits, _; not starting with a digit; max ${STYLE_NAME_MAX}), skipped`);
      continue;
    }
    if (named.size >= STYLE_NAMED_MAX) {
      problems.push(`styles: more than ${STYLE_MAX} styles, "${name}" and the rest are left out`);
      break;
    }
    if (!isObj(value)) {
      problems.push(`styles.${name} is not an object, skipped`);
      continue;
    }
    const style: Style = {};
    // A main block exists when the style has a key that is not a sub-block.
    if (Object.keys(value).some(k => !styleSubBlock(k))) {
      style.main = {};
      props(value, style.main, name, null);
    }
    for (const part of STYLE_SUB_BLOCKS) {
      const sub = subBlock(value, name, part);
      if (!sub) continue;
      if (part === 'icon') {
        const color = icon(sub, name, undefined);
        if (color !== undefined) style.icon = color;
      } else if (Object.keys(sub).length > 0) {
        style[part] = {};
        props(sub, style[part]!, name, part);
      }
    }
    named.set(name, style);
  }
  return result();
}

/** The styles of a config, as the device has them after boot. */
export function configStyles(cfg: unknown, resolve: FontResolver): ConfigStyles {
  return parseStyles(cfg, resolve).styles;
}

/** 0xRRGGBB -> "#rrggbb". */
export function styleColorHex(c: number): string {
  return '#' + (c & 0xffffff).toString(16).padStart(6, '0');
}

/** The opacity (0..255) of a background-color value. */
export function styleBgOpa(bg: number): number {
  return Math.floor(bg / 0x1000000) & 0xff;
}
