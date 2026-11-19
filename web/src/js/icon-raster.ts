/**
 * Icon rasters for the device (pure; the canvas part is icon-render.ts).
 *
 * The device never parses SVG: the IDE renders every icon of config.json's
 * `icons` table at every size the config needs and uploads the result to
 * the media store's `images` bucket in LVGL 9's own binary image format
 * (src/ui/MediaImage.h, docs/ICONS.md):
 *
 *   12-byte little-endian header: magic 0x19, cf, flags 0 (u16), w (u16),
 *   h (u16), stride (u16), reserved 0 (u16); then the pixels:
 *     A8       (0x0E) coverage only, stride = w. Single-colour artwork; the
 *              device tints it at draw time.
 *     RGB565A8 (0x14) RGB565 plane (stride = w * 2, little endian), then an
 *              A8 plane (stride = w). Anything else.
 *   The object size is exactly 12 + the pixel bytes.
 *
 * Keys: iconKey(name, w, h) = "ico-<name>-<w>x<h>.bin". The IDE owns the
 * `ico-` prefix: a sync uploads missing or changed rasters and deletes the
 * `ico-*` keys the config no longer wants; other keys are user files.
 *
 * Which rasters a config wants (wantedRasters): every size "slot" gets every
 * icon of the `icons` table, because MQTT (ICON / DISPLAY_ICON) and scripts
 * (setIcon) can switch an item to any icon at run time:
 *   - a key (buttons[]) with "icon" or "iconsize": iconsize x iconsize
 *     (default 48; like the firmware, iconsize <= 0 means the default)
 *   - a display item with "icon" or "iconsize": iconsize x iconsize
 *     (default 32)
 *   - an "images" entry with "icon": width x height
 * An item without either field shows no icon unless a script sets one; give
 * it an "iconsize" so its size gets rasters.
 */

export const LVGL_BIN_MAGIC = 0x19;
export const CF_A8 = 0x0e;
export const CF_RGB565 = 0x12;
export const CF_RGB565A8 = 0x14;
export const LVGL_BIN_HEADER_SIZE = 12;

/** Icon names in config.json `icons` (the firmware's kIconNameMax = 31). */
export const ICON_NAME_PATTERN = /^[A-Za-z0-9_-]{1,31}$/;
/** Raster dimensions the IDE accepts (per side, pixels). */
export const ICON_SIZE_MIN = 1;
export const ICON_SIZE_MAX = 1024;
/** Largest "iconsize" on keys and display items (the firmware's limit). */
export const ITEM_ICON_SIZE_MAX = 320;
export const DEFAULT_BUTTON_ICON_SIZE = 48;
export const DEFAULT_DISPLAY_ICON_SIZE = 32;
/** Every key this module generates starts with this; the IDE owns it. */
export const ICON_KEY_PREFIX = 'ico-';

/**
 * Mono detection: pixels with alpha below `minAlpha` are ignored (soft
 * edges); every other pixel must be within `tolerance` per channel of the
 * reference colour. Unpremultiplied values of faint pixels are imprecise
 * (the canvas stores premultiplied colour), so the allowance grows by
 * 255 / alpha.
 */
export interface MonoOptions {
  tolerance?: number;
  minAlpha?: number;
}
export const MONO_TOLERANCE = 8;
export const MONO_MIN_ALPHA = 32;

export interface EncodedRaster {
  bytes: Uint8Array;
  cf: typeof CF_A8 | typeof CF_RGB565A8;
}

export interface RasterSpec {
  name: string;
  w: number;
  h: number;
  key: string;
}

type Obj = Record<string, unknown>;

function isObj(v: unknown): v is Obj {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

export function isIconName(name: unknown): name is string {
  return typeof name === 'string' && ICON_NAME_PATTERN.test(name);
}

export function isIconSize(n: unknown): n is number {
  return typeof n === 'number' && Number.isInteger(n) && n >= ICON_SIZE_MIN && n <= ICON_SIZE_MAX;
}

/**
 * "iconsize" of a key or display item as the firmware reads it: missing or
 * <= 0 -> the default; null when invalid (not an integer, > 320).
 */
export function itemIconSize(v: unknown, fallback: number): number | null {
  if (v === undefined) return fallback;
  if (typeof v !== 'number' || !Number.isInteger(v)) return null;
  if (v <= 0) return fallback;
  return v <= ITEM_ICON_SIZE_MAX ? v : null;
}

/** "ico-<name>-<w>x<h>.bin", or null for an invalid name or size. */
export function iconKey(name: string, w: number, h: number): string | null {
  if (!isIconName(name) || !isIconSize(w) || !isIconSize(h)) return null;
  return `${ICON_KEY_PREFIX}${name}-${w}x${h}.bin`;
}

/** Inverse of iconKey; null for anything else. */
export function parseIconKey(key: string): { name: string; w: number; h: number } | null {
  const m = /^ico-([A-Za-z0-9_-]{1,31})-([1-9][0-9]{0,3})x([1-9][0-9]{0,3})\.bin$/.exec(key);
  if (!m) return null;
  const name = m[1]!;
  const w = Number(m[2]);
  const h = Number(m[3]);
  return iconKey(name, w, h) === key ? { name, w, h } : null;
}

function checkSize(rgba: Uint8ClampedArray | Uint8Array, w: number, h: number): void {
  if (!isIconSize(w) || !isIconSize(h)) throw new RangeError(`raster size ${w}x${h} is not ${ICON_SIZE_MIN}..${ICON_SIZE_MAX}`);
  if (rgba.length !== w * h * 4) throw new RangeError(`expected ${w * h * 4} RGBA bytes, got ${rgba.length}`);
}

/**
 * The single colour of the artwork ([r, g, b]) when every visible pixel has
 * it (see MonoOptions), else null. A fully transparent image counts as mono
 * (black). Input: unpremultiplied RGBA (canvas getImageData).
 */
export function monoColor(
  rgba: Uint8ClampedArray | Uint8Array,
  opts: MonoOptions = {},
): [number, number, number] | null {
  const tolerance = opts.tolerance ?? MONO_TOLERANCE;
  const minAlpha = Math.max(1, opts.minAlpha ?? MONO_MIN_ALPHA);
  // Reference: the most opaque pixel (its colour is the most exact).
  let ref = -1;
  let refAlpha = 0;
  for (let i = 3; i < rgba.length; i += 4) {
    if (rgba[i]! > refAlpha) {
      refAlpha = rgba[i]!;
      ref = i - 3;
      if (refAlpha === 255) break;
    }
  }
  if (ref < 0) return [0, 0, 0];
  const r = rgba[ref]!;
  const g = rgba[ref + 1]!;
  const b = rgba[ref + 2]!;
  for (let i = 0; i < rgba.length; i += 4) {
    const a = rgba[i + 3]!;
    if (a < minAlpha) continue;
    const allow = tolerance + Math.ceil(255 / a) - 1;
    if (Math.abs(rgba[i]! - r) > allow || Math.abs(rgba[i + 1]! - g) > allow || Math.abs(rgba[i + 2]! - b) > allow) {
      return null;
    }
  }
  return [r, g, b];
}

export function isMono(rgba: Uint8ClampedArray | Uint8Array, opts: MonoOptions = {}): boolean {
  return monoColor(rgba, opts) !== null;
}

/** r5g6b5 of an 8-bit colour (truncating, as LVGL's converter). */
export function rgb565(r: number, g: number, b: number): number {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

function header(cf: number, w: number, h: number, stride: number, total: number): Uint8Array {
  const bytes = new Uint8Array(total);
  const v = new DataView(bytes.buffer);
  v.setUint8(0, LVGL_BIN_MAGIC);
  v.setUint8(1, cf);
  v.setUint16(2, 0, true); // flags
  v.setUint16(4, w, true);
  v.setUint16(6, h, true);
  v.setUint16(8, stride, true);
  v.setUint16(10, 0, true); // reserved
  return bytes;
}

/** Bytes after the header for a raster of this format. */
export function pixelBytes(cf: number, w: number, h: number): number {
  if (cf === CF_A8) return w * h;
  if (cf === CF_RGB565A8) return w * h * 3;
  if (cf === CF_RGB565) return w * h * 2;
  throw new RangeError(`unsupported colour format 0x${cf.toString(16)}`);
}

/**
 * Encode unpremultiplied RGBA (w*h*4 bytes, row-major) as an LVGL 9 .bin:
 * A8 when the artwork has one colour (monoColor), else RGB565A8. `cf`
 * forces a format.
 */
export function encodeLvglBin(
  rgba: Uint8ClampedArray | Uint8Array,
  w: number,
  h: number,
  opts: MonoOptions & { cf?: EncodedRaster['cf'] } = {},
): EncodedRaster {
  checkSize(rgba, w, h);
  const cf = opts.cf ?? (isMono(rgba, opts) ? CF_A8 : CF_RGB565A8);
  const n = w * h;
  if (cf === CF_A8) {
    const bytes = header(CF_A8, w, h, w, LVGL_BIN_HEADER_SIZE + n);
    for (let p = 0; p < n; p++) bytes[LVGL_BIN_HEADER_SIZE + p] = rgba[p * 4 + 3]!;
    return { bytes, cf };
  }
  const bytes = header(CF_RGB565A8, w, h, w * 2, LVGL_BIN_HEADER_SIZE + n * 3);
  const alphaAt = LVGL_BIN_HEADER_SIZE + n * 2;
  for (let p = 0; p < n; p++) {
    const a = rgba[p * 4 + 3]!;
    // Fully transparent pixels: colour 0, so equal artwork gives equal bytes.
    const c = a === 0 ? 0 : rgb565(rgba[p * 4]!, rgba[p * 4 + 1]!, rgba[p * 4 + 2]!);
    bytes[LVGL_BIN_HEADER_SIZE + p * 2] = c & 0xff;
    bytes[LVGL_BIN_HEADER_SIZE + p * 2 + 1] = c >> 8;
    bytes[alphaAt + p] = a;
  }
  return { bytes, cf };
}

/** The header of an LVGL .bin, or null when it is not one the device accepts. */
export function parseLvglBinHeader(bytes: Uint8Array): { cf: number; w: number; h: number; stride: number } | null {
  if (bytes.length < LVGL_BIN_HEADER_SIZE || bytes[0] !== LVGL_BIN_MAGIC) return null;
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const cf = v.getUint8(1);
  const w = v.getUint16(4, true);
  const h = v.getUint16(6, true);
  const stride = v.getUint16(8, true);
  if (v.getUint16(2, true) !== 0 || v.getUint16(10, true) !== 0 || w === 0 || h === 0) return null;
  const want = cf === CF_A8 ? w : cf === CF_RGB565A8 || cf === CF_RGB565 ? w * 2 : -1;
  if (stride !== want || bytes.length !== LVGL_BIN_HEADER_SIZE + pixelBytes(cf, w, h)) return null;
  return { cf, w, h, stride };
}

const KEY_LISTS = ['buttons'] as const;
const ITEM_LISTS = ['displayitems'] as const;

/** config.json `icons` table: name -> LittleFS path, valid entries only. */
export function iconTable(cfg: unknown): Map<string, string> {
  const out = new Map<string, string>();
  if (!isObj(cfg) || !isObj(cfg.icons)) return out;
  for (const [name, path] of Object.entries(cfg.icons)) {
    if (isIconName(name) && typeof path === 'string' && path.trim() !== '') out.set(name, path);
  }
  return out;
}

/** A size slot: "w x h" plus where it comes from (for messages). */
export interface SizeSlot {
  w: number;
  h: number;
  where: string;
}

/**
 * Raster sizes the config uses (see the header comment): one per item that
 * can show an icon, valid sizes only (the validator reports the others).
 */
export function iconSizeSlots(cfg: unknown): SizeSlot[] {
  if (!isObj(cfg)) return [];
  const slots: SizeSlot[] = [];
  const square = (lists: readonly string[], fallback: number) => {
    for (const list of lists) {
      const arr = cfg[list];
      if (!Array.isArray(arr)) continue;
      arr.forEach((e, i) => {
        if (!isObj(e) || (!('icon' in e) && !('iconsize' in e))) return;
        const size = itemIconSize(e.iconsize, fallback);
        if (size !== null) slots.push({ w: size, h: size, where: `${list}[${i}]` });
      });
    }
  };
  square(KEY_LISTS, DEFAULT_BUTTON_ICON_SIZE);
  square(ITEM_LISTS, DEFAULT_DISPLAY_ICON_SIZE);
  if (Array.isArray(cfg.images)) {
    cfg.images.forEach((e, i) => {
      if (!isObj(e) || !('icon' in e)) return;
      if (isIconSize(e.width) && isIconSize(e.height)) slots.push({ w: e.width, h: e.height, where: `images[${i}]` });
    });
  }
  return slots;
}

/**
 * Every raster the config wants: each icon of the `icons` table at each
 * distinct size slot. Sorted by key, no duplicates.
 */
export function wantedRasters(cfg: unknown): RasterSpec[] {
  const icons = iconTable(cfg);
  const sizes = new Map<string, { w: number; h: number }>();
  for (const s of iconSizeSlots(cfg)) sizes.set(`${s.w}x${s.h}`, { w: s.w, h: s.h });
  const out = new Map<string, RasterSpec>();
  for (const name of icons.keys()) {
    for (const { w, h } of sizes.values()) {
      const key = iconKey(name, w, h);
      if (key) out.set(key, { name, w, h, key });
    }
  }
  return [...out.values()].sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0));
}

/** What the images bucket lists for a key. */
export interface ExistingObject {
  key: string;
  size: number;
  etag?: string;
}

/** A rendered raster: its key, size and MD5 (hex). */
export interface RenderedRaster {
  key: string;
  size: number;
  md5: string;
}

export interface RasterDiff {
  /** Missing on the device, or different (size or MD5/ETag). */
  upload: string[];
  /** `ico-*` keys the config no longer wants. */
  remove: string[];
  /** Already on the device, identical. */
  unchanged: string[];
}

/** ETag -> lower-case hex without quotes ('"AB.."' -> 'ab..'). */
export function normalizeEtag(etag: string | undefined): string {
  return (etag ?? '').replace(/^W\//, '').replace(/"/g, '').trim().toLowerCase();
}

/** Keys "images" entries show with "src" (user files: never removed). */
export function srcKeys(cfg: unknown): Set<string> {
  const out = new Set<string>();
  if (!isObj(cfg) || !Array.isArray(cfg.images)) return out;
  for (const e of cfg.images) if (isObj(e) && typeof e.src === 'string') out.add(e.src);
  return out;
}

/**
 * Compare rendered rasters with the bucket listing. Only `ico-*` keys are
 * ever removed, and never one in `keep` (an "images" src); other keys are
 * user files.
 */
export function diffRasters(
  wanted: readonly RenderedRaster[],
  existing: readonly ExistingObject[],
  keep: ReadonlySet<string> = new Set(),
): RasterDiff {
  const have = new Map(existing.map(o => [o.key, o]));
  const wantedKeys = new Set(wanted.map(r => r.key));
  const upload: string[] = [];
  const unchanged: string[] = [];
  for (const r of wanted) {
    const o = have.get(r.key);
    if (o && o.size === r.size && normalizeEtag(o.etag) === r.md5.toLowerCase()) unchanged.push(r.key);
    else upload.push(r.key);
  }
  const remove = existing
    .map(o => o.key)
    .filter(k => k.startsWith(ICON_KEY_PREFIX) && !wantedKeys.has(k) && !keep.has(k));
  return { upload, remove, unchanged };
}
