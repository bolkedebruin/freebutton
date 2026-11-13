/**
 * config.json "style" (pure): the text styles of the UI elements, cascading
 * like LVGL styles. The same defaults, cascade, item_px and property rules
 * as the firmware's src/ui/style_logic.h (style_logic.c); keep the two in
 * sync (docs/STYLE.md).
 *
 *   "style": {
 *     "fonts": [ ... ],                                  (font-config.ts)
 *     "text":  {"text_font": "montserrat", "text_color": "#ffffff"},       role
 *     "title": {"text_font": "montserrat_semibold", "text_color": "#ffd700"},
 *     "key":     {"label": {...}, "toplabel": {...}},                      elements
 *     "display": {"value": {...}, "unit": {...}, "caption": {...}},
 *     "list":    {..., "selected": {...}}
 *   }
 *
 * Layers, lowest first (LVGL: theme < added styles < local styles):
 *   1. the firmware defaults (styleDefaults: the look before "style" existed)
 *   2. the element's role ("text": key.label, display.value, display.unit,
 *      list; "title": key.toplabel, display.caption)
 *   3. the element ("key.label", ...)
 *   4. the item (config "font"/"fontsize"/"topfont"/..., list "color"/
 *      "focuscolor"/"focustextcolor", JS setFont, \f \s \c markup): local
 *      styles, applied by font-config slotFont / the previews, not here.
 * A property a layer does not set is taken from the layer below.
 * Exceptions:
 *   - display.unit has no font or size of its own by default: it takes the
 *     value's (after the item layer) unless style.display.unit sets them.
 *   - list.selected (LV_PART_SELECTED) starts from its defaults and its own
 *     properties; everything it does not set comes from the resolved list.
 *
 * Property names are LVGL's style property names. "text_size" (px) is the
 * one extension: text_font + text_size resolve together into one font
 * instance (fonts_get(face, px)).
 *
 * This module knows face indexes, not font names: text_font goes through a
 * resolver (font-config fontResolver), as on the device.
 */
import { FONT_PX_MAX, FONT_PX_MIN, STEP_PX, type FontResolver } from './text-spans';

/** Face indexes of the system fonts (font-config SYSTEM_FONTS; firmware FONT_FACE_SYSTEM / _BOLD). */
export const FACE_SYSTEM = 0;
export const FACE_SYSTEM_BOLD = 1;

export enum StyleElement {
  KeyLabel = 0,
  KeyTopLabel,
  DisplayValue,
  DisplayUnit,
  DisplayCaption,
  List,
  /** The roller's LV_PART_SELECTED. */
  ListSelected,
}
export const STYLE_ELEMENT_COUNT = 7;

export enum StyleRole { Text = 0, Title = 1 }
export const STYLE_ROLE_COUNT = 2;

export enum StyleAlign { Left = 1, Center = 2, Right = 3 }
/** text_decor bits (LV_TEXT_DECOR_*). */
export const DECOR_NONE = 0;
export const DECOR_UNDERLINE = 1;
export const DECOR_STRIKETHROUGH = 2;

export const STYLE_OPA_MAX = 255;
export const STYLE_SPACE_MIN = -100;
export const STYLE_SPACE_MAX = 100;

/** The properties one layer sets (absent = not set; style_props_t's "has" bits). */
export interface StyleProps {
  /** text_font -> face index. */
  face?: number;
  /** text_size, px. */
  px?: number;
  /** text_color 0xRRGGBB. */
  color?: number;
  /** text_opa 0..255. */
  opa?: number;
  align?: StyleAlign;
  letterSpace?: number;
  lineSpace?: number;
  /** DECOR_* bits. */
  decor?: number;
  /** bg_color (list.selected only). */
  bgColor?: number;
  /** bg_opa (list.selected only). */
  bgOpa?: number;
}

/** What config.json "style" sets (layers 2 and 3). */
export interface StyleSheet {
  role: StyleProps[];
  element: StyleProps[];
}

/** Resolved style per element (index: StyleElement). */
export type ResolvedStyles = readonly Readonly<StyleProps>[];

const PROP_KEYS: (keyof StyleProps)[] = [
  'face', 'px', 'color', 'opa', 'align', 'letterSpace', 'lineSpace', 'decor', 'bgColor', 'bgOpa',
];

export function emptySheet(): StyleSheet {
  return {
    role: Array.from({ length: STYLE_ROLE_COUNT }, () => ({})),
    element: Array.from({ length: STYLE_ELEMENT_COUNT }, () => ({})),
  };
}

/** The role of an element (ListSelected: Text, via the list). */
export function styleRoleOf(el: StyleElement): StyleRole {
  return el === StyleElement.KeyTopLabel || el === StyleElement.DisplayCaption ? StyleRole.Title : StyleRole.Text;
}

/** A title element: fontsize step 2 = 18 px (styleItemPx). */
export function isTitleElement(el: StyleElement): boolean {
  return styleRoleOf(el) === StyleRole.Title;
}

/**
 * Layer 1: the firmware defaults, i.e. the look before "style" existed:
 *   key.label        system 32 px, #ffffff
 *   key.toplabel     system bold 18 px, #ffd700
 *   display.value    system 14 px, #ffffff
 *   display.unit     #999999 (font and size: the value's)
 *   display.caption  system bold 14 px, #ffd700
 *   list             system 32 px, #808080
 *   list.selected    bg #ffd700 (opa 255), text #000000
 */
export function styleDefaults(): StyleProps[] {
  const out: StyleProps[] = [];
  out[StyleElement.KeyLabel] = { face: FACE_SYSTEM, px: 32, color: 0xffffff };
  out[StyleElement.KeyTopLabel] = { face: FACE_SYSTEM_BOLD, px: 18, color: 0xffd700 };
  out[StyleElement.DisplayValue] = { face: FACE_SYSTEM, px: 14, color: 0xffffff };
  out[StyleElement.DisplayUnit] = { color: 0x999999 };
  out[StyleElement.DisplayCaption] = { face: FACE_SYSTEM_BOLD, px: 14, color: 0xffd700 };
  out[StyleElement.List] = { face: FACE_SYSTEM, px: 32, color: 0x808080 };
  out[StyleElement.ListSelected] = { bgColor: 0xffd700, bgOpa: 255, color: 0x000000 };
  return out;
}

/** `dst` takes every property `src` sets (style_logic_merge), but those in `skip`. Returns `dst`. */
export function mergeStyle(dst: StyleProps, src: Readonly<StyleProps>, skip: readonly (keyof StyleProps)[] = []): StyleProps {
  for (const k of PROP_KEYS) {
    if (src[k] !== undefined && !skip.includes(k)) (dst as Record<string, number>)[k] = src[k] as number;
  }
  return dst;
}

const BG_KEYS: (keyof StyleProps)[] = ['bgColor', 'bgOpa'];
const UNIT_ROLE_SKIP: (keyof StyleProps)[] = ['bgColor', 'bgOpa', 'face', 'px'];

/**
 * Layers 1-3 for every element (style_logic_resolve): defaults <- role <-
 * element (bg_* only on list.selected; the unit takes no font or size from
 * its role: it follows the value's unless style.display.unit sets them),
 * then list.selected = the resolved list (bg excepted) <- its own
 * (defaults <- style.list.selected); the role reaches it through the list
 * only. `sheet` null = defaults only.
 */
export function resolveStyles(sheet: StyleSheet | null): StyleProps[] {
  const out = styleDefaults();
  if (sheet) {
    for (let el = 0; el < STYLE_ELEMENT_COUNT; el++) {
      if (el === StyleElement.ListSelected) continue;
      mergeStyle(out[el]!, sheet.role[styleRoleOf(el)] ?? {}, el === StyleElement.DisplayUnit ? UNIT_ROLE_SKIP : BG_KEYS);
      mergeStyle(out[el]!, sheet.element[el] ?? {}, BG_KEYS);
    }
  }
  const own = mergeStyle({ ...out[StyleElement.ListSelected]! }, sheet?.element[StyleElement.ListSelected] ?? {});
  out[StyleElement.ListSelected] = mergeStyle(mergeStyle({}, out[StyleElement.List]!, BG_KEYS), own);
  return out;
}

/** resolveStyles(null), shared (do not modify). */
export const DEFAULT_STYLES: ResolvedStyles = Object.freeze(resolveStyles(null).map(p => Object.freeze(p)));

/**
 * Item layer size (style_logic_item_px): config / script "fontsize" -> px.
 * 1..5 = the legacy steps 14/24/32/48/72, except step 2 of a title element
 * (key.toplabel, display.caption) = 18; FONT_PX_MIN..FONT_PX_MAX = px;
 * 0 or anything else = `fallbackPx`.
 */
export function styleItemPx(fontsize: number, title: boolean, fallbackPx: number): number {
  if (!Number.isInteger(fontsize)) return fallbackPx;
  if (fontsize >= 1 && fontsize <= STEP_PX.length) return title && fontsize === 2 ? 18 : STEP_PX[fontsize - 1]!;
  if (fontsize >= FONT_PX_MIN && fontsize <= FONT_PX_MAX) return fontsize;
  return fallbackPx;
}

// ---- parsing ---------------------------------------------------------------------

function hasOwn(o: object, k: string): boolean {
  return Object.prototype.hasOwnProperty.call(o, k);
}

const ELEMENTS: Record<string, Record<string, StyleElement>> = {
  key: { label: StyleElement.KeyLabel, toplabel: StyleElement.KeyTopLabel },
  display: { value: StyleElement.DisplayValue, unit: StyleElement.DisplayUnit, caption: StyleElement.DisplayCaption },
  list: { selected: StyleElement.ListSelected },
};

/**
 * Element of style.<group>.<name> (style_logic_element); `name` null =
 * style.list itself. -1 when unknown.
 */
export function styleElement(group: string, name: string | null): number {
  if (name === null) return group === 'list' ? StyleElement.List : -1;
  return hasOwn(ELEMENTS, group) && hasOwn(ELEMENTS[group]!, name) ? ELEMENTS[group]![name]! : -1;
}

/** "text" / "title" -> role; -1 when unknown (style_logic_role). */
export function styleRole(name: string): number {
  return name === 'text' ? StyleRole.Text : name === 'title' ? StyleRole.Title : -1;
}

export type StylePropStatus = 'ok' | 'unknown' | 'bad-value' | 'unknown-font';

/** The property names, in documentation order. */
export const STYLE_PROPERTIES = [
  'text_font', 'text_size', 'text_color', 'text_opa', 'text_align', 'text_letter_space', 'text_line_space',
  'text_decor', 'bg_color', 'bg_opa',
] as const;
const BG_PROPERTIES = new Set(['bg_color', 'bg_opa']);

const COLOR = /^#?[0-9a-fA-F]{6}$/;

/** "#rrggbb" / "rrggbb" -> 0xRRGGBB; null when malformed (style_logic_parse_color). */
export function parseStyleColor(s: unknown): number | null {
  return typeof s === 'string' && COLOR.test(s) ? parseInt(s.replace(/^#/, ''), 16) : null;
}

const ALIGNS: Record<string, StyleAlign> = { left: StyleAlign.Left, center: StyleAlign.Center, right: StyleAlign.Right };
const DECORS: Record<string, number> = {
  none: DECOR_NONE, underline: DECOR_UNDERLINE, strikethrough: DECOR_STRIKETHROUGH,
  'underline strikethrough': DECOR_UNDERLINE | DECOR_STRIKETHROUGH,
};

function known(prop: string, allowBg: boolean): boolean {
  return (STYLE_PROPERTIES as readonly string[]).includes(prop) && (allowBg || !BG_PROPERTIES.has(prop));
}

/**
 * One property from a JSON string value (style_logic_set_string): text_font
 * (a name), text_color / bg_color, text_align, text_decor. On an error `p`
 * is unchanged.
 */
export function setStyleString(p: StyleProps, prop: string, value: string, allowBg: boolean, resolve: FontResolver): StylePropStatus {
  if (!known(prop, allowBg)) return 'unknown';
  switch (prop) {
    case 'text_font': {
      if (value === '') return 'bad-value';
      const face = resolve(value, false, 0);
      if (face < 0) return 'unknown-font';
      p.face = face;
      return 'ok';
    }
    case 'text_color':
    case 'bg_color': {
      const c = parseStyleColor(value);
      if (c === null) return 'bad-value';
      if (prop === 'text_color') p.color = c; else p.bgColor = c;
      return 'ok';
    }
    case 'text_align':
      if (!hasOwn(ALIGNS, value)) return 'bad-value';
      p.align = ALIGNS[value]!;
      return 'ok';
    case 'text_decor':
      if (!hasOwn(DECORS, value)) return 'bad-value';
      p.decor = DECORS[value]!;
      return 'ok';
    default:
      return 'bad-value';
  }
}

/**
 * One property from a JSON integer value (style_logic_set_number):
 * text_font (a config font id), text_size (FONT_PX_MIN..FONT_PX_MAX),
 * text_opa / bg_opa (0..255), text_letter_space / text_line_space
 * (-100..100). A non-integer is a bad value.
 */
export function setStyleNumber(p: StyleProps, prop: string, value: number, allowBg: boolean, resolve: FontResolver): StylePropStatus {
  if (!known(prop, allowBg)) return 'unknown';
  if (!Number.isInteger(value)) return 'bad-value';
  const inRange = (lo: number, hi: number) => value >= lo && value <= hi;
  switch (prop) {
    case 'text_font': {
      if (value < 1) return 'unknown-font'; // ids start at 1
      const face = resolve(String(value), true, value);
      if (face < 0) return 'unknown-font';
      p.face = face;
      return 'ok';
    }
    case 'text_size':
      if (!inRange(FONT_PX_MIN, FONT_PX_MAX)) return 'bad-value';
      p.px = value;
      return 'ok';
    case 'text_opa':
    case 'bg_opa':
      if (!inRange(0, STYLE_OPA_MAX)) return 'bad-value';
      if (prop === 'text_opa') p.opa = value; else p.bgOpa = value;
      return 'ok';
    case 'text_letter_space':
    case 'text_line_space':
      if (!inRange(STYLE_SPACE_MIN, STYLE_SPACE_MAX)) return 'bad-value';
      if (prop === 'text_letter_space') p.letterSpace = value; else p.lineSpace = value;
      return 'ok';
    default:
      return 'bad-value';
  }
}

/** One property from any JSON value: strings and numbers dispatch, anything else is a bad value. */
export function setStyleValue(p: StyleProps, prop: string, value: unknown, allowBg: boolean, resolve: FontResolver): StylePropStatus {
  if (typeof value === 'string') return setStyleString(p, prop, value, allowBg, resolve);
  if (typeof value === 'number') return setStyleNumber(p, prop, value, allowBg, resolve);
  return known(prop, allowBg) ? 'bad-value' : 'unknown';
}

const BAD_VALUE: Record<string, string> = {
  text_font: 'must be a font name or id',
  text_size: `must be an integer ${FONT_PX_MIN}..${FONT_PX_MAX} (px)`,
  text_color: 'must be a colour "#rrggbb"',
  bg_color: 'must be a colour "#rrggbb"',
  text_opa: `must be an integer 0..${STYLE_OPA_MAX}`,
  bg_opa: `must be an integer 0..${STYLE_OPA_MAX}`,
  text_align: 'must be "left", "center" or "right"',
  text_letter_space: `must be an integer ${STYLE_SPACE_MIN}..${STYLE_SPACE_MAX} (px)`,
  text_line_space: `must be an integer ${STYLE_SPACE_MIN}..${STYLE_SPACE_MAX} (px)`,
  text_decor: 'must be "none", "underline", "strikethrough" or "underline strikethrough"',
};

export const STYLE_SECTIONS = ['fonts', 'text', 'title', 'key', 'display', 'list'] as const;

type Obj = Record<string, unknown>;

function isObj(v: unknown): v is Obj {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

export interface ParsedStyle {
  /** What the device applies: every valid property (invalid ones are skipped, as ConfigManager does). */
  sheet: StyleSheet;
  /** Problems, with their JSON path ("style.key.label.text_size ..."). */
  errors: string[];
  /** config.json has a "style" object. */
  present: boolean;
}

/**
 * config.json "style" -> the sheet the device builds, plus every problem
 * (unknown section, element or property, a bad value, an unknown font).
 * `fontsHint` names the fonts in the unknown-font message.
 * "style.fonts" is font-config's (validateFonts); it is skipped here.
 */
export function parseStyle(cfg: unknown, resolve: FontResolver, fontsHint = 'a "style.fonts" entry'): ParsedStyle {
  const sheet = emptySheet();
  const errors: string[] = [];
  const style = isObj(cfg) ? cfg.style : undefined;
  if (style === undefined) return { sheet, errors, present: false };
  if (!isObj(style)) return { sheet, errors: ['"style" must be an object ({"text": {...}, "key": {...}, ...})'], present: false };

  const props = (target: StyleProps, at: string, body: Obj, allowBg: boolean, skip?: string) => {
    for (const [prop, value] of Object.entries(body)) {
      if (prop === skip) continue;
      const status = setStyleValue(target, prop, value, allowBg, resolve);
      if (status === 'ok') continue;
      if (status === 'unknown') {
        errors.push(BG_PROPERTIES.has(prop)
          ? `${at}.${prop}: bg_color and bg_opa are allowed in style.list.selected only`
          : `${at}.${prop} is not a style property (${STYLE_PROPERTIES.filter(p => allowBg || !BG_PROPERTIES.has(p)).join(', ')})`);
      } else if (status === 'unknown-font') {
        errors.push(`${at}.${prop} ${JSON.stringify(value)} is not a font (${fontsHint})`);
      } else {
        errors.push(`${at}.${prop} ${BAD_VALUE[prop] ?? 'has a bad value'}`);
      }
    }
  };
  const body = (at: string, v: unknown): Obj | null => {
    if (isObj(v)) return v;
    errors.push(`${at} must be an object of style properties ({"text_color": "#ffffff", ...})`);
    return null;
  };

  for (const [section, value] of Object.entries(style)) {
    const at = `style.${section}`;
    const role = styleRole(section);
    if (section === 'fonts') continue;
    if (role >= 0) {
      const b = body(at, value);
      if (b) props(sheet.role[role]!, at, b, false);
    } else if (section === 'key' || section === 'display') {
      if (!isObj(value)) {
        errors.push(`${at} must be an object of elements (${Object.keys(ELEMENTS[section]!).join(', ')})`);
        continue;
      }
      for (const [name, v] of Object.entries(value)) {
        const el = styleElement(section, name);
        if (el < 0) {
          errors.push(`${at}.${name} is not an element of "${section}" (${Object.keys(ELEMENTS[section]!).join(', ')})`);
          continue;
        }
        const b = body(`${at}.${name}`, v);
        if (b) props(sheet.element[el]!, `${at}.${name}`, b, false);
      }
    } else if (section === 'list') {
      const b = body(at, value);
      if (!b) continue;
      props(sheet.element[StyleElement.List]!, at, b, false, 'selected');
      if ('selected' in b) {
        const s = body(`${at}.selected`, b.selected);
        if (s) props(sheet.element[StyleElement.ListSelected]!, `${at}.selected`, s, true);
      }
    } else {
      errors.push(`${at} is not a style section (${STYLE_SECTIONS.join(', ')})`);
    }
  }
  return { sheet, errors, present: true };
}

/** The resolved styles (layers 1-3) of a config: what the device draws items without fields with. */
export function configStyles(cfg: unknown, resolve: FontResolver): StyleProps[] {
  return resolveStyles(parseStyle(cfg, resolve).sheet);
}

/**
 * Config-level style warnings (the config still works): no "style" at all
 * (the default look; the system fonts have no fallback), and a top-level
 * "fonts" (the interim format: not read any more).
 */
export function styleWarnings(cfg: unknown): string[] {
  if (!isObj(cfg)) return [];
  const w: string[] = [];
  if ('fonts' in cfg) {
    w.push('top-level "fonts" is not read any more: move "fonts" into "style" ("style": {"fonts": [...]})');
  }
  if (!('style' in cfg)) {
    w.push('config.json has no "style": the device uses its default look, and the system fonts have no fallback '
      + '(icons in labels show as boxes). See docs/STYLE.md.');
  }
  return w;
}

/** 0xRRGGBB -> "#rrggbb". */
export function styleColorHex(c: number): string {
  return '#' + (c & 0xffffff).toString(16).padStart(6, '0');
}
