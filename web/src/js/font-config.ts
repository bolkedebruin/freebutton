/**
 * config.json "style.fonts" (pure): the font table, item font references,
 * validation and the instance estimate. Mirrors the firmware's font
 * registry (src/ui/fonts/fonts.h) and use sites; the text styles
 * themselves (defaults, cascade) are style-config.ts.
 *
 *   "style": {"fonts": [
 *     {"id": 1, "name": "mdi", "src": "icons.ttf"},
 *     {"name": "roboto", "src": "roboto.ttf", "fallback": "mdi"},
 *     {"name": "montserrat", "fallback": "mdi"}
 *   ]}
 *
 * A FACE is a TTF in the device's `fonts` bucket plus a fallback face; it has
 * no size. The two system fonts always exist: montserrat (medium.ttf, face
 * 0) and montserrat_semibold (semibold.ttf, face 1). An entry with a system
 * name may set only "fallback", "lineheight" and "id" (a "src" there is
 * ignored, with a warning). Fallbacks are explicit only: "fallback" absent
 * or "" = none, else a name or an id. A chain holds at most FONT_CHAIN_MAX
 * faces, the first included. A top-level "fonts" (the interim format) is not
 * read.
 *
 * Item fields (the local layer of the style cascade): displayitems font/
 * fontsize (value + unit), labelfont/labelfontsize (caption); buttons font/
 * fontsize (label), topfont/topfontsize (top label); lists font/fontsize.
 * font = a name or an id; fontsize 1..5 = the legacy steps 14/24/32/48/72 px
 * (step 2 of a title = 18), 6..72 = px. What an item does not set comes from
 * the resolved style (style-config.ts).
 */
import {
  parseTextSpans, STEP_PX, TEXT_FONT_UNKNOWN, TextSizeKind, FONT_PX_MAX, FONT_PX_MIN, type FontResolver,
} from './text-spans';
import {
  configStyles, DEFAULT_STYLES, FACE_SYSTEM, FACE_SYSTEM_BOLD, isTitleElement, parseStyle, styleItemPx, StyleElement,
  styleWarnings, type ResolvedStyles,
} from './style-config';

export { FACE_SYSTEM, FACE_SYSTEM_BOLD };

/** docs/NAMES.md (same as config-model NAME_PATTERN). */
export const FONT_NAME_PATTERN = /^[A-Za-z_][A-Za-z0-9_]{0,23}$/;
/** A key of bucket `fonts` (S3Logic.h checkMediaKey): flat, no "..", lower-case extension. */
export const FONT_SRC_PATTERN = /^[A-Za-z0-9._-]+\.(ttf|otf)$/;
export const FONT_SRC_MAX = 96;
export const FONTS_MAX_FACES = 8;
export const FONTS_MAX_INSTANCES = 24;
export const FONT_CHAIN_MAX = 4;
export const FONTSIZE_MAX = FONT_PX_MAX;
export const LINEHEIGHT_MIN = 0.8;
export const LINEHEIGHT_MAX = 2.0;
export const FONT_ID_MAX = 0x7fffffff;

/** The system fonts, faces 0 and 1 (FACE_SYSTEM, FACE_SYSTEM_BOLD): always present, not deletable. */
export const SYSTEM_FONTS = [
  { name: 'montserrat', src: 'medium.ttf' },
  { name: 'montserrat_semibold', src: 'semibold.ttf' },
] as const;
/** Faces style.fonts can add besides the system fonts. */
export const FONTS_MAX_CONFIG_FACES = FONTS_MAX_FACES - SYSTEM_FONTS.length;
/** For messages: the fonts every config has. */
const SYSTEM_NAMES_TEXT = SYSTEM_FONTS.map(f => f.name).join(', ');
const FONTS_HINT = `${SYSTEM_NAMES_TEXT} or a "style.fonts" entry`;

/** A key of bucket `fonts` that holds a system font (its delete is refused). */
export function isSystemFontSrc(key: string): boolean {
  return SYSTEM_FONTS.some(f => f.src === key);
}

type Obj = Record<string, unknown>;

function isObj(v: unknown): v is Obj {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

export type FallbackRef =
  | { kind: 'none' }
  | { kind: 'name'; name: string }
  | { kind: 'id'; id: number };

export interface FontFace {
  /** Face index (0, 1 = the system fonts, config faces follow), as on the device. */
  index: number;
  name: string;
  /** Config id; 0 = none. */
  id: number;
  /** Key in bucket `fonts`. */
  src: string;
  /** A system font (montserrat, montserrat_semibold). */
  system: boolean;
  /** A system font with a style.fonts entry (fallback / lineheight / id). */
  configured: boolean;
  fallbackRef: FallbackRef;
  /** Resolved fallback face index; -1 = none (also: unknown or cut). */
  fallback: number;
  /** Line box in em (0.8..2.0); undefined = the font's metrics. */
  lineheight?: number;
}

export interface FontTable {
  faces: FontFace[];
}

function isFontId(v: unknown): v is number {
  return typeof v === 'number' && Number.isInteger(v) && v >= 1 && v <= FONT_ID_MAX;
}

function fallbackRefOf(e: Obj): FallbackRef | null {
  if (!('fallback' in e) || e.fallback === undefined) return { kind: 'none' };
  const f = e.fallback;
  if (f === '') return { kind: 'none' };
  if (typeof f === 'string' && FONT_NAME_PATTERN.test(f)) return { kind: 'name', name: f };
  if (isFontId(f)) return { kind: 'id', id: f };
  return null;
}

function systemIndex(name: string): number {
  return SYSTEM_FONTS.findIndex(b => b.name === name);
}

/** config.json "style", if it is an object. */
function styleOf(cfg: unknown): Obj | null {
  return isObj(cfg) && isObj(cfg.style) ? cfg.style : null;
}

/** The config's "style.fonts" entries, if it is an array. */
function fontEntries(cfg: unknown): unknown[] {
  const style = styleOf(cfg);
  return style && Array.isArray(style.fonts) ? style.fonts : [];
}

/** config.json has a "style.fonts" array. */
export function hasFontTable(cfg: unknown): boolean {
  return Array.isArray(styleOf(cfg)?.fonts);
}

/**
 * The font table the device builds from config.json "style.fonts" (lenient,
 * like fonts_configure): invalid or duplicate entries and entries beyond
 * FONTS_MAX_FACES are skipped; unknown fallbacks resolve to none; a system
 * font entry's src is ignored.
 */
export function fontTable(cfg: unknown): FontTable {
  const faces: FontFace[] = SYSTEM_FONTS.map((b, index) => ({
    index, name: b.name, id: 0, src: b.src, system: true, configured: false,
    fallbackRef: { kind: 'none' }, fallback: -1,
  }));
  const ids = new Set<number>();
  for (const e of fontEntries(cfg)) {
    if (!isObj(e) || typeof e.name !== 'string' || !FONT_NAME_PATTERN.test(e.name)) continue;
    const ref = fallbackRefOf(e);
    if (!ref) continue;
    // As font_logic_merge: id 0 = none; a negative or non-integer id skips the entry.
    const id = isFontId(e.id) ? e.id : 0;
    if ('id' in e && !id && e.id !== 0) continue;
    if (id && ids.has(id)) continue;
    const lh = typeof e.lineheight === 'number' && e.lineheight >= LINEHEIGHT_MIN && e.lineheight <= LINEHEIGHT_MAX
      ? e.lineheight : undefined;
    const b = systemIndex(e.name);
    if (b >= 0) {
      // A system font: fallback / lineheight / id only (src ignored).
      const face = faces[b]!;
      if (face.configured) continue;
      Object.assign(face, { id, configured: true, fallbackRef: ref, ...(lh !== undefined ? { lineheight: lh } : {}) });
    } else {
      const src = typeof e.src === 'string' && validFontSrc(e.src) ? e.src : null;
      if (!src || faces.some(f => f.name === e.name) || faces.length >= FONTS_MAX_FACES) continue;
      faces.push({
        index: faces.length, name: e.name, id, src, system: false, configured: false,
        fallbackRef: ref, fallback: -1, ...(lh !== undefined ? { lineheight: lh } : {}),
      });
    }
    if (id) ids.add(id);
  }
  for (const face of faces) {
    const ref = face.fallbackRef;
    face.fallback = ref.kind === 'none' ? -1
      : ref.kind === 'name' ? findFace({ faces }, ref.name)
      : findFace({ faces }, ref.id);
  }
  return { faces };
}

/** Face index for a name (system fonts included) or an id; -1 when unknown. */
export function findFace(table: FontTable, ref: unknown): number {
  if (typeof ref === 'string') return table.faces.findIndex(f => f.name === ref);
  if (isFontId(ref)) return table.faces.findIndex(f => f.id === ref);
  return -1;
}

/** The text-spans resolver for this table (\f{name} / \f{id}). */
export function fontResolver(table: FontTable): FontResolver {
  return (name, isId, id) => {
    const i = isId ? findFace(table, id) : findFace(table, name);
    return i >= 0 ? i : TEXT_FONT_UNKNOWN;
  };
}

/**
 * Face indexes of a face's fallback chain, the face first: at most
 * FONT_CHAIN_MAX, cut at a cycle (as the device does).
 */
export function fontChain(table: FontTable, face: number): number[] {
  const chain: number[] = [];
  let f = face;
  while (f >= 0 && f < table.faces.length && !chain.includes(f) && chain.length < FONT_CHAIN_MAX) {
    chain.push(f);
    f = table.faces[f]!.fallback;
  }
  return chain;
}

/** A valid key for "src". */
export function validFontSrc(src: string): boolean {
  return src.length <= FONT_SRC_MAX && FONT_SRC_PATTERN.test(src) && !src.includes('..');
}

/** Config fontsize -> px: 1..5 = legacy steps, 6..72 = px, else `fallback`. */
export function fontsizePx(v: unknown, fallback: number): number {
  const n = typeof v === 'number' ? v : typeof v === 'string' && v.trim() !== '' ? Number(v) : NaN;
  if (!Number.isInteger(n)) return fallback;
  if (n >= 1 && n <= STEP_PX.length) return STEP_PX[n - 1]!;
  if (n >= FONT_PX_MIN && n <= FONT_PX_MAX) return n;
  return fallback;
}

/** A face (index into the table) at a pixel size. */
export interface TextFont {
  face: number;
  px: number;
}

/** Where a text is placed: its config fields and its style element. */
export interface FontSlot {
  fontField: string;
  sizeField: string;
  /** The element whose resolved style gives what the item does not set. */
  element: StyleElement;
}

export const SLOTS = {
  value: { fontField: 'font', sizeField: 'fontsize', element: StyleElement.DisplayValue },
  caption: { fontField: 'labelfont', sizeField: 'labelfontsize', element: StyleElement.DisplayCaption },
  button: { fontField: 'font', sizeField: 'fontsize', element: StyleElement.KeyLabel },
  buttonTop: { fontField: 'topfont', sizeField: 'topfontsize', element: StyleElement.KeyTopLabel },
  list: { fontField: 'font', sizeField: 'fontsize', element: StyleElement.List },
} as const satisfies Record<string, FontSlot>;

/** A fontsize field as a number (numeric strings too); NaN when it is none. */
function sizeNumber(v: unknown): number {
  return typeof v === 'number' ? v : typeof v === 'string' && v.trim() !== '' ? Number(v) : NaN;
}

/**
 * The font of an element's resolved style (`styles`, style-config
 * configStyles): face and px. The unit has none of its own unless the
 * style sets them (unitFont).
 */
export function styleFont(styles: ResolvedStyles, el: StyleElement): TextFont & { configured: boolean } {
  const s = styles[el] ?? {};
  return { face: s.face ?? FACE_SYSTEM, px: s.px ?? 14, configured: false };
}

/**
 * The font an item's slot starts with: the item layer ("font" / "fontsize"
 * etc.) over the element's resolved style. `configured` = the item names a
 * config font that exists.
 */
export function slotFont(
  table: FontTable, raw: Obj, slot: FontSlot, styles: ResolvedStyles = DEFAULT_STYLES,
): TextFont & { configured: boolean } {
  const base = styleFont(styles, slot.element);
  const face = slot.fontField in raw ? findFace(table, raw[slot.fontField]) : -1;
  const size = raw[slot.sizeField] ?? (slot.sizeField === 'fontsize' ? raw.fontSize : undefined);
  const px = styleItemPx(sizeNumber(size), isTitleElement(slot.element), base.px);
  return { face: face >= 0 ? face : base.face, px, configured: face >= 0 };
}

/**
 * The unit's font: the value's (`value`, after the item layer) unless
 * style.display.unit sets text_font / text_size.
 */
export function unitFont(
  value: TextFont & { configured: boolean }, styles: ResolvedStyles = DEFAULT_STYLES,
): TextFont & { configured: boolean } {
  const u = styles[StyleElement.DisplayUnit] ?? {};
  return { face: u.face ?? value.face, px: u.px ?? value.px, configured: value.configured || u.face !== undefined };
}

/**
 * The font of a text-spans span inside a slot whose base font is `base`.
 * \s1..\s5 = the legacy steps through styleItemPx (on a title element
 * step 2 = 18 px, whatever the span's font; as ui_text.c span_font);
 * \s6..\s9 = 14 px.
 */
export function spanFont(
  base: TextFont & { configured: boolean },
  slot: FontSlot,
  span: { font: number; sizeKind: TextSizeKind; size: number },
): TextFont {
  const switched = span.font >= 0;
  const face = switched ? span.font : base.face;
  if (span.sizeKind === TextSizeKind.Base) return { face, px: base.px };
  if (span.sizeKind === TextSizeKind.Px) return { face, px: span.size };
  return { face, px: span.size >= 1 && span.size <= STEP_PX.length ? styleItemPx(span.size, isTitleElement(slot.element), 14) : 14 };
}

/** The resolved styles of a config (layers 1-3; style-config configStyles with this config's fonts). */
export function fontStyles(cfg: unknown, table: FontTable = fontTable(cfg)): ResolvedStyles {
  return configStyles(cfg, fontResolver(table));
}

// ---- instance estimate --------------------------------------------------------

const ITEM_LISTS = ['displayitems', 'mqttdisplays'];
const KEY_LISTS = ['buttons', 'mqttbuttons'];

function arraysOf(o: Obj, keys: string[]): Obj[] {
  return keys.flatMap(k => (Array.isArray(o[k]) ? o[k] as unknown[] : [])).filter(isObj);
}

/**
 * (face, px) pairs the config makes the device create: the fonts of the
 * firmware screens (status bar / titles: the system fonts at 14 px), each
 * display item's value + unit and caption (inline \f / \s escapes in label
 * and unit included), each key's label and top label, each list (and its
 * selected row when the style gives that its own font); every pair expanded
 * by its fallback chain at the same px. The value text is set at runtime,
 * so only its base font counts. An estimate: modals and scripts can add more.
 */
export function fontInstances(cfg: unknown): Set<string> {
  const o: Obj = isObj(cfg) ? cfg : {};
  const table = fontTable(o);
  const styles = fontStyles(o, table);
  const resolver = fontResolver(table);
  const pairs = new Set<string>();
  const add = (f: TextFont) => {
    for (const face of fontChain(table, f.face)) pairs.add(`${table.faces[face]!.name}@${f.px}`);
  };
  add({ face: FACE_SYSTEM, px: 14 });
  add({ face: FACE_SYSTEM_BOLD, px: 14 });
  const addText = (text: unknown, base: TextFont & { configured: boolean }, slot: FontSlot) => {
    add(base);
    if (typeof text !== 'string' || !text.includes('\\')) return;
    for (const s of parseTextSpans(text, resolver).spans) add(spanFont(base, slot, s));
  };
  for (const item of arraysOf(o, ITEM_LISTS)) {
    const value = slotFont(table, item, SLOTS.value, styles);
    add(value);
    addText(item.unit, unitFont(value, styles), SLOTS.value);
    addText(item.label, slotFont(table, item, SLOTS.caption, styles), SLOTS.caption);
  }
  for (const key of arraysOf(o, KEY_LISTS)) {
    add(slotFont(table, key, SLOTS.button, styles));
    if (key.toplabel ?? key.topLabel) add(slotFont(table, key, SLOTS.buttonTop, styles));
  }
  const list = styles[StyleElement.List] ?? {};
  const sel = styles[StyleElement.ListSelected] ?? {};
  for (const l of arraysOf(o, ['lists'])) {
    const f = slotFont(table, l, SLOTS.list, styles);
    add(f);
    // list.selected with its own text_font / text_size (else it inherits the list's).
    if (sel.face !== list.face || sel.px !== list.px) {
      add({ face: sel.face !== list.face ? sel.face ?? f.face : f.face, px: sel.px !== list.px ? sel.px ?? f.px : f.px });
    }
  }
  return pairs;
}

// ---- validation -------------------------------------------------------------

function describeRef(v: unknown): string {
  return JSON.stringify(v);
}

/**
 * Errors in "style" ("style.fonts" here, the text styles via style-config
 * parseStyle) and the item font fields (font / labelfont / topfont must
 * resolve; fontsize / labelfontsize / topfontsize integer 1..72; the lists'
 * fontsize is checked by fs-rules validateLists).
 * Exported for tests; part of fs-rules validateConfigShape.
 */
export function validateFonts(o: Obj): string[] {
  const problems: string[] = [];
  const names = new Map<string, number>(SYSTEM_FONTS.map((b, i) => [b.name, i]));
  const ids = new Map<number, string>();
  /** name -> fallback ref, for the chain check. */
  const fallbacks = new Map<string, FallbackRef>(SYSTEM_FONTS.map(b => [b.name, { kind: 'none' }]));
  const configured = new Set<string>();
  let added = 0;

  const style = styleOf(o);
  if (style && 'fonts' in style && !Array.isArray(style.fonts)) {
    problems.push('"style.fonts" must be an array ([{"name": "roboto", "src": "roboto.ttf"}])');
  }
  fontEntries(o).forEach((e, i) => {
    const at = `style.fonts[${i}]`;
    if (!isObj(e)) {
      problems.push(`${at} must be an object`);
      return;
    }
    const name = e.name;
    let nameOk = false;
    if (!('name' in e)) {
      problems.push(`${at}.name is required`);
    } else if (typeof name !== 'string' || !FONT_NAME_PATTERN.test(name)) {
      problems.push(`${at}.name ${JSON.stringify(name)} is not a valid name (1-24 letters, digits or _, not starting with a digit)`);
    } else {
      nameOk = true;
    }
    const system = nameOk && systemIndex(name as string) >= 0;
    if (nameOk) {
      const n = name as string;
      if (system) {
        if (configured.has(n)) problems.push(`${at}.name "${n}": the system font is configured a second time`);
        configured.add(n);
      } else if (names.has(n)) {
        problems.push(`${at}.name "${n}" is already used (font names must be unique)`);
      } else {
        names.set(n, i);
        added++;
      }
    }
    if ('id' in e) {
      if (!isFontId(e.id)) {
        problems.push(`${at}.id must be an integer >= 1`);
      } else if (ids.has(e.id)) {
        problems.push(`${at}.id ${e.id} is already used by font "${ids.get(e.id)}" (font ids must be unique)`);
      } else {
        ids.set(e.id, nameOk ? name as string : at);
      }
    }
    // A system font's src is ignored (a warning: fontWarnings).
    if (!system) {
      if (!('src' in e)) {
        if (nameOk) problems.push(`${at}.src is required (a .ttf or .otf file in the device's fonts bucket)`);
      } else if (typeof e.src !== 'string' || !validFontSrc(e.src)) {
        problems.push(`${at}.src must be a key in the device's fonts bucket ([A-Za-z0-9._-], ending in .ttf or .otf, <= ${FONT_SRC_MAX} characters)`);
      }
    }
    const ref = fallbackRefOf(e);
    if (!ref) {
      problems.push(`${at}.fallback must be a font name, a font id or "" (no fallback)`);
    } else if (nameOk) {
      fallbacks.set(name as string, ref);
    }
    if ('lineheight' in e && (typeof e.lineheight !== 'number' || !(e.lineheight >= LINEHEIGHT_MIN && e.lineheight <= LINEHEIGHT_MAX))) {
      problems.push(`${at}.lineheight must be a number ${LINEHEIGHT_MIN}..${LINEHEIGHT_MAX} (em)`);
    }
  });

  if (added > FONTS_MAX_CONFIG_FACES) {
    problems.push(`"style.fonts" adds ${added} fonts; the device holds ${FONTS_MAX_CONFIG_FACES} besides ${SYSTEM_NAMES_TEXT} `
      + `(${FONTS_MAX_FACES} in all; a system font entry does not count)`);
  }

  // Fallback chains, by name.
  const resolveRef = (ref: FallbackRef): string | null | undefined => {
    if (ref.kind === 'none') return null;
    if (ref.kind === 'name') return names.has(ref.name) ? ref.name : undefined;
    return ids.get(ref.id) && names.has(ids.get(ref.id)!) ? ids.get(ref.id)! : undefined;
  };
  for (const [n, ref] of fallbacks) {
    const target = resolveRef(ref);
    if (target === undefined) {
      const what = ref.kind === 'id' ? `id ${ref.id}` : `"${(ref as { name: string }).name}"`;
      problems.push(`font "${n}": fallback ${what} is not a font`);
    }
  }
  const reported = new Set<string>();
  for (const start of fallbacks.keys()) {
    const chain: string[] = [];
    let cur: string | null | undefined = start;
    while (cur && !chain.includes(cur)) {
      chain.push(cur);
      cur = resolveRef(fallbacks.get(cur) ?? { kind: 'none' });
    }
    if (cur && chain.includes(cur)) {
      const cycle = chain.slice(chain.indexOf(cur));
      const id = [...cycle].sort().join(',');
      if (!reported.has(id)) {
        reported.add(id);
        problems.push(`style.fonts: fallback cycle ${[...cycle, cur].join(' -> ')}`);
      }
    } else if (chain.length > FONT_CHAIN_MAX && !reported.has(start)) {
      reported.add(start);
      problems.push(`font "${start}": fallback chain ${chain.join(' -> ')} has ${chain.length} fonts (at most ${FONT_CHAIN_MAX})`);
    }
  }

  // The text styles (style.text, style.key.label, ...).
  problems.push(...parseStyle(o, fontResolver(fontTable(o)), FONTS_HINT).errors);

  // Item references and sizes.
  const known = (v: unknown) => (typeof v === 'string' && names.has(v)) || (isFontId(v) && ids.has(v));
  const checkRef = (at: string, field: string, v: unknown) => {
    if (typeof v !== 'string' && typeof v !== 'number') {
      problems.push(`${at}.${field} must be a font name or id`);
    } else if (!known(v)) {
      problems.push(`${at}.${field} ${describeRef(v)} is not a font (${FONTS_HINT})`);
    }
  };
  const checkSize = (at: string, field: string, v: unknown) => {
    if (typeof v !== 'number' || !Number.isInteger(v) || v < 1 || v > FONTSIZE_MAX) {
      problems.push(`${at}.${field} must be an integer 1..${FONTSIZE_MAX} (1-5: the sizes 14/24/32/48/72 px, else px)`);
    }
  };
  const groups: [string[], string[], string[]][] = [
    [ITEM_LISTS, ['font', 'labelfont'], ['fontsize', 'labelfontsize']],
    [KEY_LISTS, ['font', 'topfont'], ['fontsize', 'topfontsize']],
    // lists' fontsize: fs-rules validateLists (same rule)
    [['lists'], ['font'], []],
  ];
  for (const [lists, refFields, sizeFields] of groups) {
    for (const list of lists) {
      const arr = o[list];
      if (!Array.isArray(arr)) continue;
      arr.forEach((e, i) => {
        if (!isObj(e)) return;
        const at = `${list}[${i}]`;
        for (const f of refFields) if (f in e) checkRef(at, f, e[f]);
        for (const f of sizeFields) if (f in e) checkSize(at, f, e[f]);
      });
    }
  }
  return problems;
}

/** `s` has a codepoint >= U+F0000 (the private use planes: MDI icons). */
function hasPrivateUse(s: string): boolean {
  for (const ch of s) if (ch.codePointAt(0)! >= 0xf0000) return true;
  return false;
}

/**
 * System fonts without a fallback that have to draw icons (codepoints >=
 * U+F0000, literal or \u{...}) in key labels, display captions / units and
 * list items: the device shows boxes for those.
 */
function systemFallbackWarnings(o: Obj, table: FontTable, styles: ResolvedStyles): string[] {
  const resolver = fontResolver(table);
  const where = new Map<number, string[]>();
  const check = (at: string, text: unknown, base: TextFont & { configured: boolean }, slot: FontSlot) => {
    if (typeof text !== 'string' || !text) return;
    const { text: out, spans } = text.includes('\\')
      ? parseTextSpans(text, resolver)
      : { text, spans: [{ offset: 0, len: text.length, font: -1, sizeKind: TextSizeKind.Base, size: 0 }] };
    for (const s of spans) {
      const face = spanFont(base, slot, s).face;
      const f = table.faces[face];
      if (!f?.system || f.fallback >= 0 || !hasPrivateUse(out.slice(s.offset, s.offset + s.len))) continue;
      const list = where.get(face) ?? [];
      if (!list.includes(at)) list.push(at);
      where.set(face, list);
    }
  };
  for (const listKey of KEY_LISTS) {
    arraysOf(o, [listKey]).forEach((k, i) => {
      check(`${listKey}[${i}].label`, k.label, slotFont(table, k, SLOTS.button, styles), SLOTS.button);
      check(`${listKey}[${i}].toplabel`, k.toplabel ?? k.topLabel, slotFont(table, k, SLOTS.buttonTop, styles), SLOTS.buttonTop);
    });
  }
  for (const listKey of ITEM_LISTS) {
    arraysOf(o, [listKey]).forEach((d, i) => {
      check(`${listKey}[${i}].label`, d.label, slotFont(table, d, SLOTS.caption, styles), SLOTS.caption);
      check(`${listKey}[${i}].unit`, d.unit, unitFont(slotFont(table, d, SLOTS.value, styles), styles), SLOTS.value);
    });
  }
  arraysOf(o, ['lists']).forEach((l, i) => {
    const f = slotFont(table, l, SLOTS.list, styles);
    if (Array.isArray(l.items)) l.items.forEach((t, j) => check(`lists[${i}].items[${j}]`, t, f, SLOTS.list));
  });
  return [...where].map(([face, at]) => {
    const name = table.faces[face]!.name;
    const more = at.length > 1 ? ` (and ${at.length - 1} more)` : '';
    return `font "${name}" has no fallback: the icons in ${at[0]}${more} show as boxes. `
      + `Add {"name": "${name}", "fallback": "<icon font>"} to "style.fonts".`;
  });
}

/**
 * Warnings (the config still works): no "style" / a top-level "fonts"
 * (style-config styleWarnings), a src on a system font entry (ignored),
 * system fonts without a fallback that have to draw icons, a font src that
 * is not in the device's fonts bucket (`bucketKeys`; null = not known,
 * skipped) and more font instances than the device has room for
 * (fontInstances).
 */
export function fontWarnings(cfg: unknown, bucketKeys: ReadonlySet<string> | null): string[] {
  const o: Obj = isObj(cfg) ? cfg : {};
  const warnings: string[] = [...styleWarnings(o)];
  fontEntries(o).forEach((e, i) => {
    if (isObj(e) && typeof e.name === 'string' && systemIndex(e.name) >= 0 && 'src' in e) {
      warnings.push(`style.fonts[${i}]: "${e.name}" is a system font (${SYSTEM_FONTS[systemIndex(e.name)]!.src}): `
        + 'its "src" is ignored. To use another font, add it under its own name and set "text_font" in "style".');
    }
  });
  const table = fontTable(o);
  warnings.push(...systemFallbackWarnings(o, table, fontStyles(o, table)));
  if (bucketKeys) {
    for (const f of table.faces) {
      if (f.system || bucketKeys.has(f.src)) continue;
      warnings.push(`font "${f.name}": ${f.src} is not in the device's fonts bucket (the device draws its built-in font instead)`);
    }
  }
  const n = fontInstances(o).size;
  if (n > FONTS_MAX_INSTANCES) {
    warnings.push(`about ${n} font instances (font x size, fallbacks included) are needed; the device holds `
      + `${FONTS_MAX_INSTANCES}, the rest draw the built-in font. Use fewer sizes or fonts.`);
  }
  return warnings;
}

/** Font names (system fonts included) and ids of a config, for script checks and completion. */
export function fontNames(cfg: unknown): { fonts: string[]; fontIds: number[] } {
  const t = fontTable(cfg);
  return { fonts: t.faces.map(f => f.name), fontIds: t.faces.filter(f => f.id > 0).map(f => f.id) };
}
