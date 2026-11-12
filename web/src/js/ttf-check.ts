/**
 * Upload-time check of a TrueType font for the device (pure). The device
 * runs its own, stricter check (src/ui/fonts/ttf_check.c) before LVGL's
 * stb_truetype parses a file; this one catches the usual mistakes before the
 * upload and explains them:
 *   - sfnt version 0x00010000 or 'true' (TrueType outlines). 'OTTO' (CFF
 *     outlines), 'ttcf' (collections), 'wOFF' / 'wOF2' (web fonts) are refused;
 *   - the table directory and every table lie inside the file;
 *   - the tables stb_truetype needs exist (cmap glyf loca head hhea hmtx maxp);
 *   - cmap has a Unicode subtable. stb_truetype uses the LAST record of
 *     (0,*), (3,1), (3,10); its format must be one stb reads (0 4 6 12 13).
 * Warning: that subtable is format 4 (BMP only) while another one maps
 * codepoints above U+FFFF (e.g. MDI's U+F0000+ icons): on the device those
 * glyphs would silently be missing.
 */

/** Largest object the media store accepts (S3Logic.h kMaxMediaObjectSize). */
export const MAX_FONT_BYTES = 2 * 1024 * 1024;

export const REQUIRED_TABLES = ['cmap', 'glyf', 'loca', 'head', 'hhea', 'hmtx', 'maxp'] as const;
const STB_CMAP_FORMATS = [0, 4, 6, 12, 13];

export type TtfCheckResult =
  | { ok: true; warnings: string[]; numGlyphs?: number }
  | { ok: false; error: string };

function fail(error: string): TtfCheckResult {
  return { ok: false, error };
}

function tag(d: DataView, o: number): string {
  return String.fromCharCode(d.getUint8(o), d.getUint8(o + 1), d.getUint8(o + 2), d.getUint8(o + 3));
}

interface Table { off: number; len: number }

/** Highest codepoint a format 12 / 13 subtable at `m` maps, or -1. */
function maxCodepoint(d: DataView, m: number): number {
  const size = d.byteLength;
  if (m + 4 > size) return -1;
  const fmt = d.getUint16(m);
  if ((fmt === 12 || fmt === 13) && m + 16 <= size) {
    const groups = d.getUint32(m + 12);
    let max = -1;
    for (let i = 0; i < groups && m + 16 + 12 * i + 12 <= size; i++) {
      max = Math.max(max, d.getUint32(m + 16 + 12 * i + 4));
    }
    return max;
  }
  return fmt === 4 || fmt === 0 || fmt === 6 ? 0xffff : -1;
}

export function checkTtf(input: Uint8Array | ArrayBuffer): TtfCheckResult {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  const d = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const size = d.byteLength;
  if (size < 12) return fail('not a font: the file is too short');

  const magic = d.getUint32(0);
  const magicTag = tag(d, 0);
  if (magicTag === 'OTTO') {
    return fail('this is an OpenType font with CFF (PostScript) outlines; the device only draws TrueType (glyf) outlines. Use the .ttf version of the font');
  }
  if (magicTag === 'ttcf') return fail('this is a font collection (.ttc); upload a single .ttf font');
  if (magicTag === 'wOFF' || magicTag === 'wOF2') {
    return fail(`this is a web font (${magicTag === 'wOFF' ? 'WOFF' : 'WOFF2'}); convert it to a plain .ttf first`);
  }
  if (magic !== 0x00010000 && magicTag !== 'true') return fail('not a TrueType font (unknown file signature)');

  const numTables = d.getUint16(4);
  if (numTables === 0 || 12 + 16 * numTables > size) return fail('damaged font: the table directory runs past the end of the file');
  const tables = new Map<string, Table>();
  for (let i = 0; i < numTables; i++) {
    const rec = 12 + 16 * i;
    const t = tag(d, rec);
    const off = d.getUint32(rec + 8);
    const len = d.getUint32(rec + 12);
    if (off > size || len > size - off) return fail(`damaged font: table "${t}" lies outside the file (truncated upload?)`);
    if (!tables.has(t)) tables.set(t, { off, len });
  }
  const missing = REQUIRED_TABLES.filter(t => !tables.has(t));
  if (missing.length) return fail(`not usable on the device: the font has no ${missing.join(', ')} table${missing.length > 1 ? 's' : ''}`);

  let numGlyphs: number | undefined;
  const maxp = tables.get('maxp')!;
  if (maxp.len >= 6) numGlyphs = d.getUint16(maxp.off + 4);

  // cmap: the subtable stb_truetype picks.
  const cmap = tables.get('cmap')!;
  const c = cmap.off;
  if (cmap.len < 4) return fail('damaged font: the cmap table is truncated');
  const records = d.getUint16(c + 2);
  if (c + 4 + 8 * records > size) return fail('damaged font: the cmap table is truncated');
  let picked = -1;
  const subtables: number[] = [];
  for (let i = 0; i < records; i++) {
    const rec = c + 4 + 8 * i;
    const plat = d.getUint16(rec);
    const enc = d.getUint16(rec + 2);
    const m = c + d.getUint32(rec + 4);
    subtables.push(m);
    if ((plat === 3 && (enc === 1 || enc === 10)) || plat === 0) picked = m;
  }
  if (picked < 0) return fail('not usable on the device: the font has no Unicode character map (cmap 0/* or 3/1, 3/10)');
  if (picked + 4 > size) return fail('damaged font: the Unicode cmap subtable lies outside the file');
  const fmt = d.getUint16(picked);
  if (!STB_CMAP_FORMATS.includes(fmt)) {
    return fail(`not usable on the device: its Unicode character map is format ${fmt}, which the device cannot read (needs 0, 4, 6, 12 or 13)`);
  }

  const warnings: string[] = [];
  if (fmt === 4) {
    const astral = subtables.some(m => m !== picked && maxCodepoint(d, m) > 0xffff);
    if (astral) {
      warnings.push('the device uses this font\'s BMP-only character map (format 4, the last Unicode cmap record): '
        + 'characters above U+FFFF that the font also maps (e.g. icons at U+F0000+) will be missing on the device');
    }
  }
  return { ok: true, warnings, ...(numGlyphs !== undefined ? { numGlyphs } : {}) };
}
