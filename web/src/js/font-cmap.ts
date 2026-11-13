/**
 * Which characters a TrueType / OpenType font has: the code points of its
 * `cmap` table (subtable formats 4, 6 and 12, which is what fonts use for
 * Unicode). The Fonts view needs this because a browser draws a character
 * the font lacks with another font, and so would show text "in" an icon font
 * that the device cannot draw with it.
 */

const MAX_CODEPOINTS = 70000;

/** Sorted code points with a glyph, or null when the data is not a readable font. */
export function fontCodepoints(bytes: Uint8Array): number[] | null {
  try {
    const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const numTables = v.getUint16(4);
    let cmap = -1;
    for (let i = 0; i < numTables; i++) {
      const rec = 12 + i * 16;
      if (v.getUint32(rec) === 0x636d6170) { cmap = v.getUint32(rec + 8); break; } // 'cmap'
    }
    if (cmap < 0) return null;
    const found = new Set<number>();
    const add = (cp: number) => { if (found.size < MAX_CODEPOINTS) found.add(cp); };
    const subtables = new Set<number>();
    const n = v.getUint16(cmap + 2);
    for (let i = 0; i < n; i++) subtables.add(cmap + v.getUint32(cmap + 4 + i * 8 + 4));

    for (const off of subtables) {
      const format = v.getUint16(off);
      if (format === 4) {
        const segCount = v.getUint16(off + 6) / 2;
        const ends = off + 14, starts = ends + segCount * 2 + 2;
        const deltas = starts + segCount * 2, ranges = deltas + segCount * 2;
        for (let s = 0; s < segCount; s++) {
          const start = v.getUint16(starts + s * 2), end = v.getUint16(ends + s * 2);
          const delta = v.getUint16(deltas + s * 2), range = v.getUint16(ranges + s * 2);
          for (let c = start; c <= end && c < 0xffff; c++) {
            let gid: number;
            if (range === 0) {
              gid = (c + delta) & 0xffff;
            } else {
              const at = ranges + s * 2 + range + (c - start) * 2;
              gid = at + 2 <= v.byteLength ? v.getUint16(at) : 0;
              if (gid !== 0) gid = (gid + delta) & 0xffff;
            }
            if (gid !== 0) add(c);
          }
        }
      } else if (format === 6) {
        const first = v.getUint16(off + 6), count = v.getUint16(off + 8);
        for (let i = 0; i < count; i++) if (v.getUint16(off + 10 + i * 2) !== 0) add(first + i);
      } else if (format === 12) {
        const groups = v.getUint32(off + 12);
        for (let g = 0; g < groups && found.size < MAX_CODEPOINTS; g++) {
          const start = v.getUint32(off + 16 + g * 12), end = v.getUint32(off + 20 + g * 12);
          if (end < start || end > 0x10ffff) continue;
          for (let c = start; c <= end && found.size < MAX_CODEPOINTS; c++) add(c);
        }
      }
    }
    return [...found].sort((a, b) => a - b);
  } catch {
    return null;
  }
}

/** The characters of `text` (no white space) that are not in `codepoints`, each once. */
export function missingChars(text: string, codepoints: ReadonlySet<number>): string[] {
  const out: string[] = [];
  for (const ch of text) {
    if (/\s/.test(ch) || codepoints.has(ch.codePointAt(0)!) || out.includes(ch)) continue;
    out.push(ch);
  }
  return out;
}
