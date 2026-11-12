import { describe, expect, it } from 'vitest';
import { checkTtf, REQUIRED_TABLES } from '../ttf-check';

/** A cmap subtable of the given format (just enough bytes for the check). */
function subtable(format: number, maxCp = 0x7e): number[] {
  if (format === 12) {
    // format, reserved, length, language, nGroups, 1 group {start, end, glyph}
    const u32 = (v: number) => [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
    return [0, 12, 0, 0, ...u32(28), ...u32(0), ...u32(1), ...u32(0x20), ...u32(maxCp), ...u32(1)];
  }
  return [0, format, 0, 0, 0, 0, 0, 0];
}

interface FontSpec {
  magic?: number[];
  tables?: string[];
  /** [platform, encoding, subtable] records of cmap. */
  cmap?: [number, number, number[]][];
  /** Make the last table run past the end of the file. */
  truncate?: boolean;
  numTablesOverride?: number;
}

/** A minimal sfnt with the given tables (all empty but cmap and maxp). */
function font(spec: FontSpec = {}): Uint8Array {
  const tables = spec.tables ?? [...REQUIRED_TABLES];
  const cmapRecords = spec.cmap ?? [[3, 1, subtable(4)]];
  const bodies = new Map<string, number[]>();
  for (const t of tables) bodies.set(t, []);
  if (bodies.has('maxp')) bodies.set('maxp', [0, 1, 0, 0, 0, 5]);
  if (bodies.has('cmap')) {
    const header = [0, 0, 0, cmapRecords.length];
    const recs: number[] = [];
    const subs: number[] = [];
    let off = 4 + 8 * cmapRecords.length;
    for (const [p, e, sub] of cmapRecords) {
      recs.push(0, p, 0, e, (off >>> 24) & 255, (off >>> 16) & 255, (off >>> 8) & 255, off & 255);
      subs.push(...sub);
      off += sub.length;
    }
    bodies.set('cmap', [...header, ...recs, ...subs]);
  }
  const n = tables.length;
  const dirLen = 12 + 16 * n;
  const out: number[] = [...(spec.magic ?? [0, 1, 0, 0])];
  const nt = spec.numTablesOverride ?? n;
  out.push((nt >> 8) & 255, nt & 255, 0, 0, 0, 0, 0, 0);
  let off = dirLen;
  const data: number[] = [];
  tables.forEach((t, i) => {
    const body = bodies.get(t)!;
    const len = spec.truncate && i === n - 1 ? body.length + 100 : body.length;
    out.push(...[...t].map(ch => ch.charCodeAt(0)), 0, 0, 0, 0,
      (off >>> 24) & 255, (off >>> 16) & 255, (off >>> 8) & 255, off & 255,
      (len >>> 24) & 255, (len >>> 16) & 255, (len >>> 8) & 255, len & 255);
    data.push(...body);
    off += body.length;
  });
  return new Uint8Array([...out, ...data]);
}

const ascii = (s: string) => [...s].map(c => c.charCodeAt(0));

describe('checkTtf', () => {
  it('accepts a minimal TrueType font', () => {
    expect(checkTtf(font())).toEqual({ ok: true, warnings: [], numGlyphs: 5 });
    expect(checkTtf(font({ magic: ascii('true') })).ok).toBe(true);
    expect(checkTtf(font().buffer as ArrayBuffer).ok).toBe(true);
  });

  it('refuses CFF, collections and web fonts with a clear message', () => {
    expect(checkTtf(font({ magic: ascii('OTTO') }))).toEqual({ ok: false, error: expect.stringContaining('CFF') });
    expect(checkTtf(font({ magic: ascii('ttcf') }))).toEqual({ ok: false, error: expect.stringContaining('collection') });
    expect(checkTtf(font({ magic: ascii('wOFF') }))).toEqual({ ok: false, error: expect.stringContaining('WOFF') });
    expect(checkTtf(font({ magic: ascii('wOF2') }))).toEqual({ ok: false, error: expect.stringContaining('WOFF2') });
    expect(checkTtf(font({ magic: [1, 2, 3, 4] }))).toEqual({ ok: false, error: expect.stringContaining('not a TrueType font') });
    expect(checkTtf(new TextEncoder().encode('<html>not a font</html>'))).toEqual({ ok: false, error: expect.stringContaining('not a TrueType') });
  });

  it('refuses a short file and a directory or table outside the file', () => {
    expect(checkTtf(new Uint8Array(8))).toEqual({ ok: false, error: expect.stringContaining('too short') });
    expect(checkTtf(font({ numTablesOverride: 200 }))).toEqual({ ok: false, error: expect.stringContaining('table directory') });
    expect(checkTtf(font({ truncate: true }))).toEqual({ ok: false, error: expect.stringContaining('outside the file') });
    // A truncated upload: cut in the middle of the tables.
    const f = font();
    expect(checkTtf(f.slice(0, f.length - 10)).ok).toBe(false);
  });

  it('refuses a font without a required table', () => {
    const r = checkTtf(font({ tables: REQUIRED_TABLES.filter(t => t !== 'glyf' && t !== 'loca') }));
    expect(r).toEqual({ ok: false, error: expect.stringContaining('no glyf, loca tables') });
  });

  it('refuses a font without a Unicode cmap, or with one stb cannot read', () => {
    expect(checkTtf(font({ cmap: [[1, 0, subtable(0)]] }))).toEqual({ ok: false, error: expect.stringContaining('no Unicode character map') });
    expect(checkTtf(font({ cmap: [[3, 0, subtable(4)]] })).ok).toBe(false);
    // (0,5) format 14 last: stb would pick the variation sequences table.
    expect(checkTtf(font({ cmap: [[0, 3, subtable(4)], [0, 5, subtable(14)]] })))
      .toEqual({ ok: false, error: expect.stringContaining('format 14') });
  });

  it('accepts (0,*) and (3,10) format 12 maps', () => {
    expect(checkTtf(font({ cmap: [[0, 4, subtable(12, 0xf1af0)]] }))).toMatchObject({ ok: true, warnings: [] });
    expect(checkTtf(font({ cmap: [[3, 1, subtable(4)], [3, 10, subtable(12, 0xf1af0)]] }))).toMatchObject({ ok: true, warnings: [] });
  });

  it('warns when the last Unicode record is format 4 while another maps above U+FFFF', () => {
    const r = checkTtf(font({ cmap: [[3, 10, subtable(12, 0xf1af0)], [3, 1, subtable(4)]] }));
    expect(r).toMatchObject({ ok: true, warnings: [expect.stringContaining('above U+FFFF')] });
    // Format 12 within the BMP only: no warning.
    expect(checkTtf(font({ cmap: [[0, 4, subtable(12, 0xfff0)], [3, 1, subtable(4)]] }))).toMatchObject({ ok: true, warnings: [] });
  });
});

describe('checkTtf on the real default fonts', () => {
  // media/fonts holds the files the device ships with (repo root).
  const dir = new URL('../../../../media/fonts/', import.meta.url);
  for (const name of ['medium.ttf', 'semibold.ttf', 'icons.ttf']) {
    it(`accepts ${name} without warnings`, async () => {
      const { readFileSync, existsSync } = await import('node:fs');
      const path = new URL(name, dir);
      if (!existsSync(path)) return;
      expect(checkTtf(new Uint8Array(readFileSync(path)))).toMatchObject({ ok: true, warnings: [] });
    });
  }
});
