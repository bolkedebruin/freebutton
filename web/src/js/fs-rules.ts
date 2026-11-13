/**
 * Device file rules (pure).
 *
 * The IDE edits the device's S3 bucket `app`: the LittleFS root minus the
 * directories of the read-only buckets (`web` = www/, `examples` =
 * scripts/examples/; bucket `fonts` is in the media store, not on LittleFS,
 * so an `app` key under fonts/ is an ordinary user file). An `app` object
 * key is the LittleFS path without the leading slash ("config.json",
 * "scripts/main.js"). These helpers
 * mirror the device's write rules so the IDE rejects a bad save before it
 * costs a request (the device enforces the same rules and stays the
 * authority):
 *   - extensions .js .json .txt (editable) and .svg (icon sources, uploaded
 *     from disk, not edited here)
 *   - not under www/ or scripts/examples/ (those belong to the read-only
 *     buckets; the device answers 400 for such `app` keys)
 *   - key <= 63 chars, body <= 64 KiB
 *   - config.json must be valid JSON (and cannot be deleted), with valid,
 *     unique key / display item / list / image names, well-formed "lists",
 *     and consistent icons ("icons" table, "icon"/"iconsize"/"iconcolor",
 *     "images"; validateIcons) and fonts and styles ("style": its "fonts"
 *     table and text styles, item "font"/"fontsize" fields; font-config
 *     validateFonts, style-config parseStyle)
 */

import { HEX_COLOR, HEX_COLOR_ANY, LIST_MAX_ROWS, validateConfigNames } from './config-model';
import { validateFonts, FONTSIZE_MAX, FONT_NAME_PATTERN, FONT_SRC_MAX } from './font-config';
import {
  ICON_NAME_PATTERN, ICON_SIZE_MAX, ICON_SIZE_MIN, ITEM_ICON_SIZE_MAX, isIconSize, itemIconSize,
} from './icon-raster';

export const MAX_KEY_LENGTH = 63;
export const MAX_BODY_BYTES = 64 * 1024;

/** The script the firmware runs at boot (source); its bytecode is main.jsc. */
export const MAIN_SCRIPT_KEY = 'scripts/main.js';
/** Bytecode filename the device runs at boot. */
export const MAIN_BYTECODE_NAME = 'main.jsc';
export const CONFIG_KEY = 'config.json';

const EDITABLE_EXTENSIONS = ['js', 'json', 'txt'] as const;
/** Stored in `app` but not opened in the editor: SVG icon sources (uploaded). */
const UPLOAD_EXTENSIONS = ['svg'] as const;
/** Largest SVG the IDE uploads (the device's app body limit applies too). */
export const MAX_SVG_BYTES = MAX_BODY_BYTES;
/** `app` key prefixes that are the directories of the read-only buckets. */
const OTHER_BUCKET_PREFIXES: ReadonlyArray<readonly [prefix: string, bucket: string]> = [
  ['www/', 'web'],
  ['scripts/examples/', 'examples'],
];

export type FileKind = 'js' | 'json' | 'text' | 'svg' | 'other';

/** Strip surrounding whitespace and leading slashes: "/scripts/a.js" -> "scripts/a.js". */
export function normalizeKey(input: string): string {
  return input.trim().replace(/^\/+/, '');
}

export function extensionOf(key: string): string {
  const name = key.slice(key.lastIndexOf('/') + 1);
  const dot = name.lastIndexOf('.');
  return dot > 0 ? name.slice(dot + 1).toLowerCase() : '';
}

export function fileKind(key: string): FileKind {
  const ext = extensionOf(key);
  if (ext === 'js') return 'js';
  if (ext === 'json') return 'json';
  if (ext === 'txt') return 'text';
  if (ext === 'svg') return 'svg';
  return 'other';
}

/**
 * The read-only bucket whose directory holds this LittleFS path (www/ -> web,
 * scripts/examples/ -> examples), or null for an `app` key.
 */
export function otherBucketOf(key: string): string | null {
  const k = normalizeKey(key);
  return OTHER_BUCKET_PREFIXES.find(([p]) => k.startsWith(p))?.[1] ?? null;
}

/** Not an `app` key: lies in a read-only bucket's directory. */
export function isSystemKey(key: string): boolean {
  return otherBucketOf(key) !== null;
}

/** The IDE may open/save this key (user content with an allowed extension). */
export function isEditableKey(key: string): boolean {
  const k = normalizeKey(key);
  return !isSystemKey(k) && (EDITABLE_EXTENSIONS as readonly string[]).includes(extensionOf(k));
}

/** A user file the IDE can store: editable, or an uploaded SVG. */
export function isStorableKey(key: string): boolean {
  const k = normalizeKey(key);
  const ext = extensionOf(k);
  return !isSystemKey(k)
    && ((EDITABLE_EXTENSIONS as readonly string[]).includes(ext) || (UPLOAD_EXTENSIONS as readonly string[]).includes(ext));
}

/**
 * Every user file can be deleted, also one the IDE cannot open (a screenshot
 * a tool left behind, any unknown type): a file that is listed but can be
 * neither viewed nor removed is a dead end. Not config.json (the device
 * refuses it) and not the read-only buckets' directories.
 */
export function canDeleteKey(key: string): boolean {
  const k = normalizeKey(key);
  return k !== '' && !isSystemKey(k) && k !== CONFIG_KEY;
}

/** Validate an object key for writing. Returns an error message, or null if OK. */
export function validateKey(key: string): string | null {
  if (!key) return 'File name is empty';
  if (key.startsWith('/')) return 'Key must not start with "/"';
  const keyBytes = utf8Length(key);
  if (keyBytes > MAX_KEY_LENGTH) return `Path too long (${keyBytes} > ${MAX_KEY_LENGTH} bytes)`;
  if (/[\u0000-\u001f\u007f\\]/.test(key)) return 'Path contains invalid characters';
  const segments = key.split('/');
  if (segments.some(s => s === '' || s === '.' || s === '..')) return 'Path has an empty or relative segment';
  const other = otherBucketOf(key);
  if (other) {
    const prefix = OTHER_BUCKET_PREFIXES.find(([, b]) => b === other)![0];
    return `${prefix} is read-only (device bucket "${other}")`;
  }
  if (!isStorableKey(key)) return 'Only .js, .json, .txt and .svg files can be saved';
  return null;
}

/**
 * Key for an SVG uploaded from disk: "icons/<file name>" with the name made
 * safe for the device ([A-Za-z0-9._-]). Null when it is not an .svg file.
 */
export function svgUploadKey(fileName: string): string | null {
  const base = fileName.split(/[\\/]/).pop() ?? '';
  if (extensionOf(base) !== 'svg') return null;
  const stem = base.slice(0, base.lastIndexOf('.')).replace(/[^A-Za-z0-9_-]+/g, '_').replace(/^_+|_+$/g, '') || 'icon';
  return `icons/${stem}.svg`;
}

/**
 * Key in bucket `fonts` for a font uploaded from disk: the file name made
 * safe for the media store ([A-Za-z0-9._-], no "..", lower-case .ttf/.otf,
 * <= 96 characters). Null when it is not a .ttf / .otf file.
 */
export function fontUploadKey(fileName: string): string | null {
  const base = fileName.split(/[\\/]/).pop() ?? '';
  const ext = extensionOf(base);
  if (ext !== 'ttf' && ext !== 'otf') return null;
  const stem = base.slice(0, base.lastIndexOf('.'))
    .replace(/[^A-Za-z0-9._-]+/g, '_').replace(/\.{2,}/g, '.').replace(/^[._]+|[._]+$/g, '') || 'font';
  return `${stem.slice(0, FONT_SRC_MAX - 4).replace(/[.]+$/, '') || 'font'}.${ext}`;
}

/** Config font name for a fonts-bucket key ("Roboto-Bold.ttf" -> "Roboto_Bold"; NAMES.md rules). */
export function fontNameForKey(key: string): string {
  const stem = key.replace(/\.(ttf|otf)$/i, '').replace(/[^A-Za-z0-9_]+/g, '_').replace(/^_+|_+$/g, '');
  const name = (/^[0-9]/.test(stem) ? `f_${stem}` : stem).slice(0, 24);
  return FONT_NAME_PATTERN.test(name) ? name : 'font';
}

/** Icon name for an uploaded SVG key ("icons/weather-rain.svg" -> "weather-rain"). */
export function iconNameForKey(key: string): string {
  const base = key.slice(key.lastIndexOf('/') + 1);
  const stem = base.replace(/\.svg$/i, '').replace(/[^A-Za-z0-9_-]+/g, '_').slice(0, 31);
  return ICON_NAME_PATTERN.test(stem) ? stem : 'icon';
}

export function utf8Length(text: string): number {
  return new TextEncoder().encode(text).byteLength;
}

export interface JsonError {
  message: string;
  /** 0-based character offset of the error. */
  offset: number;
  /** 1-based line/column of the error. */
  line: number;
  column: number;
}

/** Convert a 0-based offset into 1-based line/column. */
export function lineColumnAt(text: string, offset: number): { line: number; column: number } {
  let line = 1;
  let lineStart = 0;
  const end = Math.min(offset, text.length);
  for (let i = 0; i < end; i++) {
    if (text.charCodeAt(i) === 10) { line++; lineStart = i + 1; }
  }
  return { line, column: end - lineStart + 1 };
}

/**
 * Offset of the first JSON syntax error (engine-independent: current V8
 * messages often carry no position). Assumes `text` is known to be invalid;
 * returns text.length for "unexpected end".
 */
export function findJsonErrorOffset(text: string): number {
  let i = 0;
  const n = text.length;
  class Fail { constructor(readonly at: number) {} }
  const ws = () => { while (i < n && ' \t\n\r'.includes(text[i]!)) i++; };
  const fail = (): never => { throw new Fail(i); };
  const lit = (word: string) => {
    for (const ch of word) { if (text[i] !== ch) fail(); i++; }
  };
  const str = () => {
    i++; // opening quote
    while (i < n) {
      const c = text.charCodeAt(i);
      if (c === 34) { i++; return; }
      if (c < 0x20) fail();
      if (c === 92) {
        i++;
        const e = text[i];
        if (e === 'u') {
          for (let k = 1; k <= 4; k++) if (!/[0-9a-fA-F]/.test(text[i + k] ?? '')) { i += k; fail(); }
          i += 5;
          continue;
        }
        if (e === undefined || !'"\\/bfnrt'.includes(e)) fail();
      }
      i++;
    }
    fail();
  };
  const num = () => {
    const m = /^-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?/.exec(text.slice(i));
    if (!m) fail();
    i += m![0].length;
  };
  const value = (): void => {
    ws();
    const c = text[i];
    if (c === '{') {
      i++; ws();
      if (text[i] === '}') { i++; return; }
      for (;;) {
        ws();
        if (text[i] !== '"') fail();
        str(); ws();
        if (text[i] !== ':') fail();
        i++; value(); ws();
        if (text[i] === ',') { i++; continue; }
        if (text[i] === '}') { i++; return; }
        fail();
      }
    }
    if (c === '[') {
      i++; ws();
      if (text[i] === ']') { i++; return; }
      for (;;) {
        value(); ws();
        if (text[i] === ',') { i++; continue; }
        if (text[i] === ']') { i++; return; }
        fail();
      }
    }
    if (c === '"') return str();
    if (c === 't') return lit('true');
    if (c === 'f') return lit('false');
    if (c === 'n') return lit('null');
    if (c === '-' || (c !== undefined && c >= '0' && c <= '9')) return num();
    fail();
  };
  try {
    value(); ws();
    return i < n ? i : n;
  } catch (e) {
    if (e instanceof Fail) return Math.min(e.at, n);
    return 0;
  }
}

/**
 * Syntax-check JSON. Returns null when valid; otherwise the message and the
 * error position (offset + 1-based line/column).
 */
export function validateJson(text: string): JsonError | null {
  try {
    JSON.parse(text);
    return null;
  } catch (err) {
    const raw = err instanceof Error ? err.message : String(err);
    const offset = findJsonErrorOffset(text);
    const { line, column } = lineColumnAt(text, offset);
    return { message: `Invalid JSON (line ${line}, column ${column}): ${raw}`, offset, line, column };
  }
}

type NumberRule = [field: string, min: number, max: number, integer: boolean];
const LIST_NUMBERS: NumberRule[] = [
  ['page', 0, Infinity, true],
  ['x', 0, 100, false],
  ['y', 0, 100, false],
  ['width', 0, 100, false],
  ['rows', 1, LIST_MAX_ROWS, true],
  ['fontsize', 1, FONTSIZE_MAX, true],
  ['selected', 0, Infinity, true],
];
const LIST_COLORS = ['color', 'focuscolor', 'focustextcolor'];
/** Media store key rules (S3Logic.h: flat [A-Za-z0-9._-], MEDIA_KEY_MAX 96). */
const IMAGE_SRC_KEY = /^[A-Za-z0-9_-][A-Za-z0-9._-]{0,91}\.bin$/;
/** "images" x / y: pixels on the 320x240 main display (top-left of the raster). */
const IMAGE_NUMBERS: NumberRule[] = [
  ['page', 0, Infinity, true],
  ['x', 0, 319, true],
  ['y', 0, 239, true],
];

function rangeText(min: number, max: number): string {
  return max === Infinity ? `>= ${min}` : `${min}..${max}`;
}

/** config.json "lists" entries (name presence; name validity is validateConfigNames). */
function validateLists(lists: unknown[]): string[] {
  const problems: string[] = [];
  lists.forEach((l, i) => {
    const at = `lists[${i}]`;
    if (typeof l !== 'object' || l === null || Array.isArray(l)) {
      problems.push(`${at} must be an object`);
      return;
    }
    const o = l as Record<string, unknown>;
    if (!('name' in o)) problems.push(`${at}.name is required`);
    if ('items' in o) {
      if (!Array.isArray(o.items)) {
        problems.push(`${at}.items must be an array of strings`);
      } else {
        o.items.forEach((item, j) => {
          if (typeof item !== 'string') problems.push(`${at}.items[${j}] must be a string`);
          else if (item.includes('\n')) problems.push(`${at}.items[${j}] must not contain a newline`);
        });
      }
    }
    for (const [field, min, max, integer] of LIST_NUMBERS) {
      if (!(field in o)) continue;
      const v = o[field];
      if (typeof v !== 'number' || !Number.isFinite(v) || (integer && !Number.isInteger(v)) || v < min || v > max) {
        problems.push(`${at}.${field} must be ${integer ? 'an integer' : 'a number'} ${rangeText(min, max)}`);
      }
    }
    for (const field of LIST_COLORS) {
      if (field in o && (typeof o[field] !== 'string' || !HEX_COLOR.test(o[field] as string))) {
        problems.push(`${at}.${field} must be 6 hex digits without "#" (e.g. "ffd45f")`);
      }
    }
  });
  return problems;
}

type Obj = Record<string, unknown>;

function isObj(v: unknown): v is Obj {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

const SIZE_TEXT = `an integer ${ICON_SIZE_MIN}..${ICON_SIZE_MAX}`;

/** "icon" must name an entry of `icons` ("" = no icon). */
function checkIconRef(at: string, v: unknown, icons: ReadonlySet<string> | null, problems: string[]): void {
  if (typeof v !== 'string') {
    problems.push(`${at}.icon must be a string (an icon name from "icons")`);
  } else if (v !== '' && !ICON_NAME_PATTERN.test(v)) {
    problems.push(`${at}.icon "${v}" is not a valid icon name (1-31 letters, digits, _ or -)`);
  } else if (v !== '' && icons && !icons.has(v)) {
    problems.push(`${at}.icon "${v}" is not in "icons"`);
  }
}

/**
 * Icons: the `icons` table ({"name": "/path.svg"}), "icon" / "iconsize"
 * (+ "iconcolor" on display items) on keys and display items, and the
 * "images" entries. Exported for tests.
 */
export function validateIcons(o: Obj): string[] {
  const problems: string[] = [];
  let icons: Set<string> | null = null;
  if ('icons' in o) {
    if (!isObj(o.icons)) {
      problems.push('"icons" must be an object ({"name": "/icons/name.svg"})');
    } else {
      icons = new Set();
      for (const [name, path] of Object.entries(o.icons)) {
        if (!ICON_NAME_PATTERN.test(name)) {
          problems.push(`icons: "${name}" is not a valid icon name (1-31 letters, digits, _ or -)`);
          continue;
        }
        icons.add(name);
        if (typeof path !== 'string' || extensionOf(normalizeKey(path)) !== 'svg') {
          problems.push(`icons.${name} must be the path of an .svg file on the device (e.g. "/icons/${name}.svg")`);
        } else {
          const keyProblem = validateKey(normalizeKey(path));
          if (keyProblem) problems.push(`icons.${name}: ${keyProblem}`);
        }
      }
    }
  }
  // Without an "icons" table every reference is unknown.
  const known = icons ?? new Set<string>();

  const items: [string, boolean][] = [['buttons', false], ['mqttbuttons', false], ['displayitems', true], ['mqttdisplays', true]];
  for (const [list, isDisplay] of items) {
    const arr = o[list];
    if (!Array.isArray(arr)) continue;
    arr.forEach((e, i) => {
      if (!isObj(e)) return;
      const at = `${list}[${i}]`;
      if ('icon' in e) checkIconRef(at, e.icon, known, problems);
      if ('iconsize' in e && itemIconSize(e.iconsize, 1) === null) {
        problems.push(`${at}.iconsize must be an integer <= ${ITEM_ICON_SIZE_MAX} (<= 0: the default)`);
      }
      if (isDisplay && 'iconcolor' in e && (typeof e.iconcolor !== 'string' || !HEX_COLOR_ANY.test(e.iconcolor))) {
        problems.push(`${at}.iconcolor must be a colour "#rrggbb"`);
      }
    });
  }

  if (Array.isArray(o.images)) {
    o.images.forEach((e, i) => {
      const at = `images[${i}]`;
      if (!isObj(e)) {
        problems.push(`${at} must be an object`);
        return;
      }
      if (!('name' in e)) problems.push(`${at}.name is required`);
      // An icon needs the raster size; a src raster has its own (optional here).
      for (const field of ['width', 'height']) {
        if ('icon' in e ? !isIconSize(e[field]) : field in e && !isIconSize(e[field])) {
          problems.push(`${at}.${field} must be ${SIZE_TEXT} (pixels)`);
        }
      }
      for (const [field, min, max, integer] of IMAGE_NUMBERS) {
        if (!(field in e)) continue;
        const v = e[field];
        if (typeof v !== 'number' || !Number.isFinite(v) || (integer && !Number.isInteger(v)) || v < min || v > max) {
          problems.push(`${at}.${field} must be ${integer ? 'an integer' : 'a number'} ${rangeText(min, max)}`
            + (field === 'page' ? '' : ' (pixels)'));
        }
      }
      // Both given: the device shows the icon. Refused here as ambiguous.
      if ('icon' in e && 'src' in e) problems.push(`${at} has both "icon" and "src" (use one; the device would show the icon)`);
      if ('icon' in e) checkIconRef(at, e.icon, known, problems);
      if ('src' in e && (typeof e.src !== 'string' || !IMAGE_SRC_KEY.test(e.src))) {
        problems.push(`${at}.src must be a key in the device's images bucket ([A-Za-z0-9._-], ending in .bin)`);
      }
      if ('color' in e && (typeof e.color !== 'string' || !HEX_COLOR_ANY.test(e.color))) {
        problems.push(`${at}.color must be a colour "#rrggbb"`);
      }
    });
  }
  return problems;
}

/**
 * Light structural check of a parsed config.json: things that would make
 * the firmware ignore whole sections, malformed "lists" entries, and invalid
 * or duplicate key / display item / list names (validateConfigNames).
 * Returns a list of problems (empty = OK).
 */
export function validateConfigShape(cfg: unknown): string[] {
  const problems: string[] = [];
  if (typeof cfg !== 'object' || cfg === null || Array.isArray(cfg)) {
    return ['config.json must be a JSON object ({ ... })'];
  }
  const o = cfg as Record<string, unknown>;
  for (const key of ['buttons', 'mqttbuttons', 'displayitems', 'mqttdisplays', 'brokers', 'lists', 'images']) {
    if (key in o && !Array.isArray(o[key])) problems.push(`"${key}" must be an array`);
  }
  for (const key of ['buttons', 'mqttbuttons']) {
    const arr = o[key];
    if (!Array.isArray(arr)) continue;
    arr.forEach((b, i) => {
      if (typeof b !== 'object' || b === null) {
        problems.push(`${key}[${i}] must be an object`);
        return;
      }
      const pos = (b as Record<string, unknown>).position;
      if (!Number.isInteger(pos) || (pos as number) < 1) {
        problems.push(`${key}[${i}].position must be a positive integer`);
      }
    });
  }
  if (Array.isArray(o.lists)) problems.push(...validateLists(o.lists));
  problems.push(...validateIcons(o));
  problems.push(...validateFonts(o));
  problems.push(...validateConfigNames(o));
  return problems;
}

/**
 * Validate a file body before it is sent. Returns an error message or null.
 * JSON files must parse; config.json additionally gets the shape check.
 */
export function validateBody(key: string, text: string): string | null {
  const bytes = utf8Length(text);
  if (bytes > MAX_BODY_BYTES) return `File too large (${bytes} bytes > ${MAX_BODY_BYTES})`;
  if (fileKind(key) === 'json') {
    const err = validateJson(text);
    if (err) return err.message;
    if (normalizeKey(key) === CONFIG_KEY) {
      const problems = validateConfigShape(JSON.parse(text));
      if (problems.length) return 'config.json: ' + problems.join('; ');
    }
  }
  return null;
}

/**
 * Turn a name typed by the user into an object key: a bare "foo.js" goes in
 * scripts/, a name without an extension gets ".js".
 */
export function resolveNewFileKey(input: string): string {
  let key = normalizeKey(input);
  if (!key) return '';
  if (!extensionOf(key)) key += '.js';
  if (!key.includes('/') && fileKind(key) === 'js') key = 'scripts/' + key;
  return key;
}

/** Initial content for a newly created file. */
export function initialContentFor(key: string): string {
  return fileKind(key) === 'json' ? '{}\n' : '';
}

/** Starter content when scripts/main.js does not exist on the device yet. */
export const MAIN_SCRIPT_TEMPLATE = `// main.js - runs at boot.
// Save (Ctrl/Cmd+S) stores this source on the device as /scripts/main.js.
// Deploy saves it, compiles it and uploads the bytecode as main.jsc.

print("main.js started");
`;
