/**
 * Device file rules (pure).
 *
 * The IDE edits the device's S3 bucket `app`: the LittleFS root minus the
 * directories of the read-only buckets (`web` = www/, `fonts` = fonts/,
 * `examples` = scripts/examples/). An `app` object key is the LittleFS path
 * without the leading slash ("config.json", "scripts/main.js"). These helpers
 * mirror the device's write rules so the IDE rejects a bad save before it
 * costs a request (the device enforces the same rules and stays the
 * authority):
 *   - extensions .js .json .txt only
 *   - not under www/, fonts/ or scripts/examples/ (those belong to the
 *     read-only buckets; the device answers 400 for such `app` keys)
 *   - key <= 63 chars, body <= 64 KiB
 *   - config.json must be valid JSON (and cannot be deleted), with valid,
 *     unique key / display item names
 */

import { validateConfigNames } from './config-model';

export const MAX_KEY_LENGTH = 63;
export const MAX_BODY_BYTES = 64 * 1024;

/** The script the firmware runs at boot (source); its bytecode is main.jsc. */
export const MAIN_SCRIPT_KEY = 'scripts/main.js';
/** Bytecode filename the device runs at boot. */
export const MAIN_BYTECODE_NAME = 'main.jsc';
export const CONFIG_KEY = 'config.json';

const EDITABLE_EXTENSIONS = ['js', 'json', 'txt'] as const;
/** `app` key prefixes that are the directories of the read-only buckets. */
const OTHER_BUCKET_PREFIXES: ReadonlyArray<readonly [prefix: string, bucket: string]> = [
  ['www/', 'web'],
  ['fonts/', 'fonts'],
  ['scripts/examples/', 'examples'],
];

export type FileKind = 'js' | 'json' | 'text' | 'other';

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
  return 'other';
}

/**
 * The read-only bucket whose directory holds this LittleFS path (www/ -> web,
 * fonts/ -> fonts, scripts/examples/ -> examples), or null for an `app` key.
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

export function canDeleteKey(key: string): boolean {
  const k = normalizeKey(key);
  return isEditableKey(k) && k !== CONFIG_KEY;
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
  if (!isEditableKey(key)) return 'Only .js, .json and .txt files can be saved';
  return null;
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

/**
 * Light structural check of a parsed config.json: things that would make
 * the firmware ignore whole sections, and invalid or duplicate key / display
 * item names (validateConfigNames). Returns a list of problems (empty = OK).
 */
export function validateConfigShape(cfg: unknown): string[] {
  const problems: string[] = [];
  if (typeof cfg !== 'object' || cfg === null || Array.isArray(cfg)) {
    return ['config.json must be a JSON object ({ ... })'];
  }
  const o = cfg as Record<string, unknown>;
  for (const key of ['buttons', 'mqttbuttons', 'displayitems', 'mqttdisplays', 'brokers']) {
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
