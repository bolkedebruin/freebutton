import { describe, it, expect } from 'vitest';
import { fontCodepoints, missingChars } from '../font-cmap';

/** A font file with only a cmap table holding the given subtables. */
function font(subtables: number[][]): Uint8Array {
  const header = 4 + subtables.length * 8;
  const cmap: number[] = [0, 0, 0, subtables.length];
  let off = header;
  for (const st of subtables) {
    cmap.push(0, 3, 0, 1, (off >>> 24) & 255, (off >>> 16) & 255, (off >>> 8) & 255, off & 255);
    off += st.length;
  }
  for (const st of subtables) cmap.push(...st);
  const dir = [0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0x63, 0x6d, 0x61, 0x70, 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, cmap.length];
  return new Uint8Array([...dir, ...cmap]);
}
const u16 = (...v: number[]) => v.flatMap(x => [(x >> 8) & 255, x & 255]);
const u32 = (...v: number[]) => v.flatMap(x => [(x >>> 24) & 255, (x >>> 16) & 255, (x >>> 8) & 255, x & 255]);

describe('fontCodepoints', () => {
  it('reads a format 4 subtable (delta segments, glyph 0 is no glyph)', () => {
    // Segments: A..C (delta maps to glyphs 1..3), U+E000 (glyph 4), 0xFFFF end.
    const st = u16(4, 0, 0, 6, 0, 0, 0, /* end */ 0x43, 0xe000, 0xffff, 0, /* start */ 0x41, 0xe000, 0xffff,
      /* delta */ (1 - 0x41) & 0xffff, (4 - 0xe000) & 0xffff, 1, /* range */ 0, 0, 0);
    expect(fontCodepoints(font([st]))).toEqual([0x41, 0x42, 0x43, 0xe000]);
  });

  it('reads a format 12 subtable', () => {
    const st = [...u16(12, 0), ...u32(28, 0, 1, 0xf0001, 0xf0003, 1)];
    expect(fontCodepoints(font([st]))).toEqual([0xf0001, 0xf0002, 0xf0003]);
  });

  it('answers null for data that is not a font', () => {
    expect(fontCodepoints(new Uint8Array([1, 2, 3]))).toBeNull();
    expect(fontCodepoints(new Uint8Array(64))).toBeNull();
  });
});

describe('missingChars', () => {
  it('lists each character the font lacks once, ignoring white space', () => {
    const has = new Set([...'Living rom 21.5 C'].map(c => c.codePointAt(0)!));
    expect(missingChars('Living room 21.5 °C', has)).toEqual(['°']);
    expect(missingChars('abca', new Set())).toEqual(['a', 'b', 'c']);
  });
});
