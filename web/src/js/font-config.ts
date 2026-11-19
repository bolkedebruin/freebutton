/**
 * config.json "fonts" (pure): the font table, the font of each part of an
 * item, validation and the instance estimate. Mirrors the firmware's font
 * registry (src/ui/fonts/fonts.h) and use sites; the styles themselves
 * (properties, lookup) are style-config.ts.
 *
 *   "fonts": [
 *     {"id": 1, "name": "mdi", "src": "icons.ttf"},
 *     {"name": "roboto", "src": "roboto.ttf", "fallback": "mdi"},
 *     {"name": "montserrat", "fallback": "mdi"}
 *   ]
 *
 * A FACE is a TTF in the device's `fonts` bucket plus a fallback face; it has
 * no size. The two system fonts always exist: montserrat (medium.ttf, face
 * 0) and montserrat_semibold (semibold.ttf, face 1). An entry with a system
 * name may set only "fallback", "lineheight" and "id" (a "src" there is
 * ignored, with a warning). Fallbacks are explicit only: "fallback" absent
 * or "" = none, else a name or an id. A chain holds at most FONT_CHAIN_MAX
 * faces, the first included.
 *
 * An item has no font fields: the face and the size of each of its parts
 * come from its styles ("style": "big alarm"), else from the style of its
 * kind, else they are the built-in ones (style-config lookupPart). A font's
 * "lineheight" (em, the line box of the face) is not the style property
 * "line-height" (a multiple of that line).
 */
import {
  parseTextSpans, STEP_PX, TEXT_FONT_UNKNOWN, TextSizeKind, type FontResolver,
} from './text-spans';
import {
  FACE_SYSTEM, FACE_SYSTEM_BOLD, isTitleElement, lookupPart, parseItemStyle, parseStyles, removedKeyProblems,
  styleElement, styleFollowFont, styleSettable, styleItemPx, STYLE_ELEMENT_COUNT, STYLE_KINDS, STYLE_PARTS, StyleElement,
  type ConfigStyles, type StyleKind, type StylePart,
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
export const LINEHEIGHT_MIN = 0.8;
export const LINEHEIGHT_MAX = 2.0;
export const FONT_ID_MAX = 0x7fffffff;

/** The system fonts, faces 0 and 1 (FACE_SYSTEM, FACE_SYSTEM_BOLD): always present, not deletable. */
export const SYSTEM_FONTS = [
  { name: 'montserrat', src: 'medium.ttf' },
  { name: 'montserrat_semibold', src: 'semibold.ttf' },
] as const;
/** Faces "fonts" can add besides the system fonts. */
export const FONTS_MAX_CONFIG_FACES = FONTS_MAX_FACES - SYSTEM_FONTS.length;
/** For messages: the fonts every config has. */
const SYSTEM_NAMES_TEXT = SYSTEM_FONTS.map(f => f.name).join(', ');
const FONTS_HINT = `${SYSTEM_NAMES_TEXT} or a "fonts" entry`;

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
  /** A system font with a "fonts" entry (fallback / lineheight / id). */
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

/** The config's "fonts" entries, if it is an array. */
function fontEntries(cfg: unknown): unknown[] {
  return isObj(cfg) && Array.isArray(cfg.fonts) ? cfg.fonts : [];
}

/** config.json has a "fonts" array. */
export function hasFontTable(cfg: unknown): boolean {
  return isObj(cfg) && Array.isArray(cfg.fonts);
}

/**
 * The font table the device builds from config.json "fonts" (lenient,
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

/** A face (index into the table) at a pixel size. */
export interface TextFont {
  face: number;
  px: number;
}

/** A text of an item: the kind and the part of the styles it takes, and that part's element. */
export interface FontSlot {
  kind: StyleKind;
  part: StylePart;
  element: StyleElement;
}

function slot(kind: StyleKind, part: StylePart): FontSlot {
  return { kind, part, element: styleElement(kind, part) as StyleElement };
}

export const SLOTS = {
  value: slot('displayitem', 'main'),
  unit: slot('displayitem', 'unit'),
  caption: slot('displayitem', 'title'),
  button: slot('button', 'main'),
  buttonTop: slot('button', 'title'),
  list: slot('list', 'main'),
  listSelected: slot('list', 'selected'),
} as const satisfies Record<string, FontSlot>;

/**
 * The font of a part of an item whose styles are `list` (the names the
 * device took: style-config parseItemStyle). Face and size are looked up
 * separately through the item's styles, the kind's style and the built-in
 * look. The unit and the selected row have no font of their own: what
 * their blocks do not set is the value's / the rows' (ui_box.c, List.cpp:
 * style_logic_follow_font).
 */
export function slotFont(styles: ConfigStyles, s: FontSlot, list: readonly string[] = []): TextFont {
  const part = lookupPart(styles, s.kind, s.part, list).props;
  if (s.part === 'unit' || s.part === 'selected') {
    const base = lookupPart(styles, s.kind, 'main', list).props;
    return styleFollowFont(part, base.face ?? FACE_SYSTEM, base.px ?? 14);
  }
  return { face: part.face ?? FACE_SYSTEM, px: part.px ?? 14 };
}

/**
 * The font of a text-spans span inside a slot whose base font is `base`.
 * \s1..\s5 = the steps through styleItemPx (on a title element step 2 =
 * 18 px, whatever the span's font; as ui_text.c span_font); \s6..\s9 = 14 px.
 */
export function spanFont(
  base: TextFont,
  s: FontSlot,
  span: { font: number; sizeKind: TextSizeKind; size: number },
): TextFont {
  const switched = span.font >= 0;
  const face = switched ? span.font : base.face;
  if (span.sizeKind === TextSizeKind.Base) return { face, px: base.px };
  if (span.sizeKind === TextSizeKind.Px) return { face, px: span.size };
  return { face, px: span.size >= 1 && span.size <= STEP_PX.length ? styleItemPx(span.size, isTitleElement(s.element), 14) : 14 };
}

/** The styles of a config (style-config parseStyles with this config's fonts). */
export function fontStyles(cfg: unknown, table: FontTable = fontTable(cfg)): ConfigStyles {
  return parseStyles(cfg, fontResolver(table)).styles;
}

/** The style list the device gives a config entry (its "style" field; the names it cannot use left out). */
export function itemStyleList(raw: Obj, styles: ConfigStyles): string[] {
  return parseItemStyle(raw.style, styles.named).list;
}

// ---- instance estimate --------------------------------------------------------

function arrayOf(o: Obj, key: string): Obj[] {
  return (Array.isArray(o[key]) ? o[key] as unknown[] : []).filter(isObj);
}

/**
 * (face, px) pairs the config makes the device create: the fonts of the
 * firmware screens (status bar / titles: the system fonts at 14 px); the
 * font of every kind's part (the shared style of a kind holds it from
 * boot: src/ui/ui_style.c); every part of a style that sets both
 * font-family and font-size, used or not (its shared style holds that
 * instance from boot too); each display item's value, unit and caption
 * (inline \f / \s escapes in label and unit included), each button's label
 * and top label, each list's rows and selected row, as their styles give
 * them. Every pair is expanded by its fallback chain at the same px. The
 * value text is set at runtime, so only its base font counts. An estimate:
 * modals and scripts can add more.
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
  for (let el = 0; el < STYLE_ELEMENT_COUNT; el++) {
    const p = styles.elements[el]!;
    if (p.face !== undefined && p.px !== undefined) add({ face: p.face, px: p.px });
  }
  for (const style of styles.named.values()) {
    for (const part of STYLE_PARTS) {
      const p = style[part];
      if (p && p.face !== undefined && p.px !== undefined) add({ face: p.face, px: p.px });
    }
  }
  const addText = (text: unknown, base: TextFont, s: FontSlot) => {
    add(base);
    if (typeof text !== 'string' || !text.includes('\\')) return;
    for (const span of parseTextSpans(text, resolver).spans) add(spanFont(base, s, span));
  };
  for (const item of arrayOf(o, 'displayitems')) {
    const list = itemStyleList(item, styles);
    add(slotFont(styles, SLOTS.value, list));
    addText(item.unit, slotFont(styles, SLOTS.unit, list), SLOTS.unit);
    addText(item.label, slotFont(styles, SLOTS.caption, list), SLOTS.caption);
  }
  for (const button of arrayOf(o, 'buttons')) {
    const list = itemStyleList(button, styles);
    add(slotFont(styles, SLOTS.button, list));
    if (button.toplabel) add(slotFont(styles, SLOTS.buttonTop, list));
  }
  for (const l of arrayOf(o, 'lists')) {
    const list = itemStyleList(l, styles);
    add(slotFont(styles, SLOTS.list, list));
    add(slotFont(styles, SLOTS.listSelected, list));
  }
  return pairs;
}

// ---- validation -------------------------------------------------------------

/**
 * Problems in "fonts" (the table's entries, fallback chains), in "styles"
 * (style-config parseStyles: unknown property, bad value, unknown font, bad
 * name, too many, a block that is no object), in the items' "style" fields
 * (an unknown style, a reserved name, a name that is no style name, a fifth
 * name) and the keys the firmware no longer reads, each with what to use
 * instead (style-config removedKeyProblems). The messages are the
 * firmware's log lines where it logs the same thing. Exported for tests;
 * part of fs-rules validateConfigShape.
 */
export function validateFonts(o: Obj): string[] {
  const problems: string[] = [];
  const names = new Map<string, number>(SYSTEM_FONTS.map((b, i) => [b.name, i]));
  const ids = new Map<number, string>();
  /** name -> fallback ref, for the chain check. */
  const fallbacks = new Map<string, FallbackRef>(SYSTEM_FONTS.map(b => [b.name, { kind: 'none' }]));
  const configured = new Set<string>();
  let added = 0;

  if ('fonts' in o && !Array.isArray(o.fonts)) {
    problems.push('"fonts" must be an array ([{"name": "roboto", "src": "roboto.ttf"}])');
  }
  fontEntries(o).forEach((e, i) => {
    const at = `fonts[${i}]`;
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
    problems.push(`"fonts" adds ${added} fonts; the device holds ${FONTS_MAX_CONFIG_FACES} besides ${SYSTEM_NAMES_TEXT} `
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
        problems.push(`fonts: fallback cycle ${[...cycle, cur].join(' -> ')}`);
      }
    } else if (chain.length > FONT_CHAIN_MAX && !reported.has(start)) {
      reported.add(start);
      problems.push(`font "${start}": fallback chain ${chain.join(' -> ')} has ${chain.length} fonts (at most ${FONT_CHAIN_MAX})`);
    }
  }

  // "styles", and the items' "style" fields.
  const parsed = parseStyles(o, fontResolver(fontTable(o)), FONTS_HINT);
  problems.push(...parsed.problems);
  for (const key of ITEM_ARRAYS) {
    arrayEntries(o, key).forEach(([e, i]) => {
      for (const problem of parseItemStyle(e.style, parsed.styles.named).problems) problems.push(`${key}[${i}].style: ${problem}`);
    });
  }
  problems.push(...removedKeyProblems(o));
  return problems;
}

/** The arrays whose entries take a "style", with their kind. */
const ITEM_ARRAYS = ['buttons', 'displayitems', 'lists'] as const;
const KIND_OF_ARRAY: Record<typeof ITEM_ARRAYS[number], StyleKind> = { buttons: 'button', displayitems: 'displayitem', lists: 'list' };

/** The object entries of a top-level array, each with its index in the file. */
function arrayEntries(o: Obj, key: string): [Obj, number][] {
  const arr = Array.isArray(o[key]) ? o[key] as unknown[] : [];
  return arr.map((e, i) => [e, i] as [unknown, number]).filter((p): p is [Obj, number] => isObj(p[0]));
}

/** `s` has a codepoint >= U+F0000 (the private use planes: MDI icons). */
function hasPrivateUse(s: string): boolean {
  for (const ch of s) if (ch.codePointAt(0)! >= 0xf0000) return true;
  return false;
}

/**
 * System fonts without a fallback that have to draw icons (codepoints >=
 * U+F0000, literal or \u{...}) in button labels, display captions / units
 * and list items: the device shows boxes for those.
 */
function systemFallbackWarnings(o: Obj, table: FontTable, styles: ConfigStyles): string[] {
  const resolver = fontResolver(table);
  const where = new Map<number, string[]>();
  const check = (at: string, text: unknown, base: TextFont, s: FontSlot) => {
    if (typeof text !== 'string' || !text) return;
    const { text: out, spans } = text.includes('\\')
      ? parseTextSpans(text, resolver)
      : { text, spans: [{ offset: 0, len: text.length, font: -1, sizeKind: TextSizeKind.Base, size: 0 }] };
    for (const span of spans) {
      const face = spanFont(base, s, span).face;
      const f = table.faces[face];
      if (!f?.system || f.fallback >= 0 || !hasPrivateUse(out.slice(span.offset, span.offset + span.len))) continue;
      const list = where.get(face) ?? [];
      if (!list.includes(at)) list.push(at);
      where.set(face, list);
    }
  };
  arrayEntries(o, 'buttons').forEach(([k, i]) => {
    const list = itemStyleList(k, styles);
    check(`buttons[${i}].label`, k.label, slotFont(styles, SLOTS.button, list), SLOTS.button);
    check(`buttons[${i}].toplabel`, k.toplabel, slotFont(styles, SLOTS.buttonTop, list), SLOTS.buttonTop);
  });
  arrayEntries(o, 'displayitems').forEach(([d, i]) => {
    const list = itemStyleList(d, styles);
    check(`displayitems[${i}].label`, d.label, slotFont(styles, SLOTS.caption, list), SLOTS.caption);
    check(`displayitems[${i}].unit`, d.unit, slotFont(styles, SLOTS.unit, list), SLOTS.unit);
  });
  arrayEntries(o, 'lists').forEach(([l, i]) => {
    const f = slotFont(styles, SLOTS.list, itemStyleList(l, styles));
    if (Array.isArray(l.items)) l.items.forEach((t, j) => check(`lists[${i}].items[${j}]`, t, f, SLOTS.list));
  });
  return [...where].map(([face, at]) => {
    const name = table.faces[face]!.name;
    const more = at.length > 1 ? ` (and ${at.length - 1} more)` : '';
    return `font "${name}" has no fallback: the icons in ${at[0]}${more} show as boxes. `
      + `Add {"name": "${name}", "fallback": "<icon font>"} to "fonts".`;
  });
}

/**
 * Properties that load and have no effect where they are used (the config
 * works; a note): "background-color" in a main, "title" or "unit" block of
 * a style a button or a display item uses, or of "button" / "displayitem"
 * (a background is drawn for a list only); "line-height" for a display
 * item (it applies to lists and button labels).
 */
function noEffectNotes(o: Obj, table: FontTable): string[] {
  const { styles, sheet } = parseStyles(o, fontResolver(table));
  const notes: string[] = [];
  const TEXT_PARTS = ['main', 'title', 'unit'] as const;
  const blockName = (style: string, part: StylePart) => (part === 'main' ? `styles.${style}` : `styles.${style}.${part}`);
  // What the kinds' own styles set.
  for (const kind of STYLE_KINDS) {
    for (const part of TEXT_PARTS) {
      const el = styleElement(kind, part);
      const set = el < 0 ? undefined : sheet.element[el];
      if (!set) continue;
      if (kind !== 'list' && set.bg !== undefined) {
        notes.push(`${blockName(kind, part)}: "background-color" has no effect on a ${kind === 'button' ? 'button' : 'display item'} (a background is drawn for a list only)`);
      }
      if (kind === 'displayitem' && set.lineHeight !== undefined) {
        notes.push(`${blockName(kind, part)}: "line-height" has no effect on a display item (it applies to lists and button labels)`);
      }
    }
  }
  // What the items' styles set: one note per style and property, with the first item that uses it.
  const seen = new Set<string>();
  for (const key of ['buttons', 'displayitems'] as const) {
    const kind = KIND_OF_ARRAY[key];
    const what = kind === 'button' ? 'a button' : 'a display item';
    arrayEntries(o, key).forEach(([e, i]) => {
      for (const name of itemStyleList(e, styles)) {
        const style = styles.named.get(name)!;
        for (const part of TEXT_PARTS) {
          if (styleElement(kind, part) < 0) continue;
          const set = style[part];
          if (set?.bg !== undefined && !seen.has(`bg ${kind} ${name} ${part}`)) {
            seen.add(`bg ${kind} ${name} ${part}`);
            notes.push(`${blockName(name, part)}: "background-color" has no effect on ${what} (${key}[${i}] uses the style; a background is drawn for a list only)`);
          }
          if (kind === 'displayitem' && set?.lineHeight !== undefined && !seen.has(`lh ${name} ${part}`)) {
            seen.add(`lh ${name} ${part}`);
            notes.push(`${blockName(name, part)}: "line-height" has no effect on a display item (${key}[${i}] uses the style; it applies to lists and button labels)`);
          }
        }
      }
    });
  }
  return notes;
}

/**
 * Warnings (the config still works): a src on a system font entry
 * (ignored), system fonts without a fallback that have to draw icons, a
 * font src that is not in the device's fonts bucket (`bucketKeys`; null =
 * not known, skipped), more font instances than the device has room for
 * (fontInstances), and style properties without an effect where they are
 * used (noEffectNotes).
 */
export function fontWarnings(cfg: unknown, bucketKeys: ReadonlySet<string> | null): string[] {
  const o: Obj = isObj(cfg) ? cfg : {};
  const warnings: string[] = [];
  fontEntries(o).forEach((e, i) => {
    if (isObj(e) && typeof e.name === 'string' && systemIndex(e.name) >= 0 && 'src' in e) {
      warnings.push(`fonts[${i}]: "${e.name}" is a system font (${SYSTEM_FONTS[systemIndex(e.name)]!.src}): `
        + 'its "src" is ignored. To use another font, add it under its own name and set "font-family" in a style.');
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
  warnings.push(...noEffectNotes(o, table));
  return warnings;
}

/**
 * The styles of a config for script checks and completion: each name, the
 * reserved ones included, with the dotted properties fb.style.set can set
 * for it (style-config styleSettable): every property but font-family,
 * font-size and line-height, in each block the style has.
 */
export function styleNames(cfg: unknown): Record<string, string[]> {
  return styleSettable(fontStyles(cfg));
}

/** Font names (system fonts included) and ids of a config, for script checks and completion. */
export function fontNames(cfg: unknown): { fonts: string[]; fontIds: number[] } {
  const t = fontTable(cfg);
  return { fonts: t.faces.map(f => f.name), fontIds: t.faces.filter(f => f.id > 0).map(f => f.id) };
}
