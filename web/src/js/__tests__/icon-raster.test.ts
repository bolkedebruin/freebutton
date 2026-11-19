import { describe, it, expect } from 'vitest';
import { createHash } from 'node:crypto';
import {
  CF_A8, CF_RGB565A8, LVGL_BIN_HEADER_SIZE, diffRasters, encodeLvglBin, iconKey, iconSizeSlots, iconTable,
  isMono, monoColor, normalizeEtag, parseIconKey, parseLvglBinHeader, rgb565, srcKeys, wantedRasters,
} from '../icon-raster';
import { md5Hex } from '../md5';

/** w x h RGBA filled by fn(x, y) -> [r, g, b, a]. */
function image(w: number, h: number, fn: (x: number, y: number) => [number, number, number, number]): Uint8ClampedArray {
  const out = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) out.set(fn(x, y), (y * w + x) * 4);
  }
  return out;
}

function u16(bytes: Uint8Array, at: number): number {
  return bytes[at]! | (bytes[at + 1]! << 8);
}

describe('md5Hex', () => {
  it('matches the RFC 1321 test vectors', () => {
    const enc = new TextEncoder();
    expect(md5Hex(new Uint8Array())).toBe('d41d8cd98f00b204e9800998ecf8427e');
    expect(md5Hex(enc.encode('abc'))).toBe('900150983cd24fb0d6963f7d28e17f72');
    expect(md5Hex(enc.encode('message digest'))).toBe('f96b697d7cb7938d525a2f31aaf161d0');
    expect(md5Hex(enc.encode('12345678901234567890123456789012345678901234567890123456789012345678901234567890')))
      .toBe('57edf4a22be3c955ac49da2e2107b67a');
  });

  it('matches node:crypto around the padding boundaries and for large inputs', () => {
    for (const n of [1, 55, 56, 57, 63, 64, 65, 119, 120, 128, 4096 + 12, 100_000]) {
      const data = new Uint8Array(n).map((_, i) => (i * 31 + 7) & 0xff);
      expect(md5Hex(data), `length ${n}`).toBe(createHash('md5').update(data).digest('hex'));
    }
  });
});

describe('iconKey / parseIconKey', () => {
  it('builds ico-<name>-<w>x<h>.bin for valid names and sizes', () => {
    expect(iconKey('rain', 48, 48)).toBe('ico-rain-48x48.bin');
    expect(iconKey('weather-sunny_2', 120, 64)).toBe('ico-weather-sunny_2-120x64.bin');
    expect(iconKey('a'.repeat(31), 1, 1024)).toBe(`ico-${'a'.repeat(31)}-1x1024.bin`);
  });

  it('refuses invalid names and sizes', () => {
    expect(iconKey('', 48, 48)).toBeNull();
    expect(iconKey('a'.repeat(32), 48, 48)).toBeNull();
    expect(iconKey('rain.svg', 48, 48)).toBeNull();
    expect(iconKey('über', 48, 48)).toBeNull();
    expect(iconKey('rain', 0, 48)).toBeNull();
    expect(iconKey('rain', 48, 1025)).toBeNull();
    expect(iconKey('rain', 4.5, 48)).toBeNull();
  });

  it('parses its own keys back and nothing else', () => {
    expect(parseIconKey('ico-rain-48x32.bin')).toEqual({ name: 'rain', w: 48, h: 32 });
    expect(parseIconKey('ico-a-b-1x1.bin')).toEqual({ name: 'a-b', w: 1, h: 1 });
    expect(parseIconKey('ico-rain-048x48.bin')).toBeNull();
    expect(parseIconKey('ico-rain-2000x48.bin')).toBeNull();
    expect(parseIconKey('radar.bin')).toBeNull();
  });
});

describe('mono detection', () => {
  it('accepts one colour with anti-aliased edges', () => {
    // Red disc: opaque core, soft edge with slightly drifting unpremultiplied colour.
    const rgba = image(16, 16, (x, y) => {
      const d = Math.hypot(x - 7.5, y - 7.5);
      if (d < 5) return [255, 0, 0, 255];
      if (d < 7) return [250, 4, 3, Math.round(255 * (7 - d) / 2)];
      return [0, 0, 0, 0];
    });
    expect(monoColor(rgba)).toEqual([255, 0, 0]);
    expect(isMono(rgba)).toBe(true);
  });

  it('ignores very faint pixels and allows more drift as alpha drops', () => {
    const rgba = image(4, 1, (x) => [
      [0, 0, 0, 255],        // reference
      [200, 200, 200, 10],   // below minAlpha: ignored
      [14, 14, 14, 40],      // 8 + ceil(255/40) - 1 = 14: allowed
      [0, 0, 0, 0],
    ][x] as [number, number, number, number]);
    expect(isMono(rgba)).toBe(true);
    rgba[8] = 15; // one more than allowed at alpha 40
    expect(isMono(rgba)).toBe(false);
  });

  it('rejects two colours and counts an empty image as mono', () => {
    const two = image(2, 1, (x) => (x === 0 ? [255, 255, 255, 255] : [0, 0, 255, 255]));
    expect(isMono(two)).toBe(false);
    expect(monoColor(new Uint8ClampedArray(16))).toEqual([0, 0, 0]);
  });

  it('honours a custom tolerance', () => {
    const near = image(2, 1, (x) => (x === 0 ? [100, 100, 100, 255] : [112, 100, 100, 255]));
    expect(isMono(near)).toBe(false);
    expect(isMono(near, { tolerance: 12 })).toBe(true);
  });
});

describe('encodeLvglBin', () => {
  it('writes A8 for single-colour art: header, stride = w, alpha bytes', () => {
    const rgba = image(3, 2, (x, y) => [255, 255, 255, x * 100 + y]);
    rgba[3] = 0; // (0,0) transparent
    const { bytes, cf } = encodeLvglBin(rgba, 3, 2);
    expect(cf).toBe(CF_A8);
    expect(bytes.length).toBe(LVGL_BIN_HEADER_SIZE + 3 * 2);
    expect([...bytes.slice(0, 12)]).toEqual([0x19, 0x0e, 0, 0, 3, 0, 2, 0, 3, 0, 0, 0]);
    expect([...bytes.slice(12)]).toEqual([0, 100, 200, 1, 101, 201]);
    expect(parseLvglBinHeader(bytes)).toEqual({ cf: CF_A8, w: 3, h: 2, stride: 3 });
  });

  it('writes RGB565A8 for multi-colour art: RGB565 plane (LE, stride w*2), then A8', () => {
    // (0,0) orange 50% alpha, (1,0) blue opaque, (0,1) transparent, (1,1) white.
    const px: [number, number, number, number][] = [[255, 128, 0, 128], [0, 0, 255, 255], [9, 9, 9, 0], [255, 255, 255, 255]];
    const rgba = image(2, 2, (x, y) => px[y * 2 + x]!);
    const { bytes, cf } = encodeLvglBin(rgba, 2, 2);
    expect(cf).toBe(CF_RGB565A8);
    expect(bytes.length).toBe(12 + 2 * 2 * 2 + 2 * 2);
    expect([...bytes.slice(0, 12)]).toEqual([0x19, 0x14, 0, 0, 2, 0, 2, 0, 4, 0, 0, 0]);
    expect(rgb565(255, 128, 0)).toBe((31 << 11) | (32 << 5));
    expect(u16(bytes, 12)).toBe(rgb565(255, 128, 0));
    expect(u16(bytes, 14)).toBe(0x001f); // blue
    expect(u16(bytes, 16)).toBe(0);      // transparent -> colour 0
    expect(u16(bytes, 18)).toBe(0xffff); // white
    expect([...bytes.slice(20)]).toEqual([128, 255, 0, 255]);
    expect(parseLvglBinHeader(bytes)).toEqual({ cf: CF_RGB565A8, w: 2, h: 2, stride: 4 });
  });

  it('can be forced to RGB565A8 and encodes non-square sizes', () => {
    const rgba = image(5, 3, () => [10, 20, 30, 255]);
    const { bytes, cf } = encodeLvglBin(rgba, 5, 3, { cf: CF_RGB565A8 });
    expect(cf).toBe(CF_RGB565A8);
    expect(u16(bytes, 4)).toBe(5);
    expect(u16(bytes, 6)).toBe(3);
    expect(u16(bytes, 8)).toBe(10);
    expect(bytes.length).toBe(12 + 5 * 3 * 3);
  });

  it('refuses a wrong buffer size or an out-of-range size', () => {
    expect(() => encodeLvglBin(new Uint8ClampedArray(15), 2, 2)).toThrow(RangeError);
    expect(() => encodeLvglBin(new Uint8ClampedArray(0), 0, 0)).toThrow(RangeError);
  });

  it('parseLvglBinHeader rejects bad magic, stride and sizes', () => {
    const { bytes } = encodeLvglBin(image(2, 2, () => [0, 0, 0, 255]), 2, 2);
    expect(parseLvglBinHeader(bytes.slice(0, bytes.length - 1))).toBeNull();
    const bad = bytes.slice();
    bad[0] = 0x12;
    expect(parseLvglBinHeader(bad)).toBeNull();
    const badStride = bytes.slice();
    badStride[8] = 3;
    expect(parseLvglBinHeader(badStride)).toBeNull();
  });
});

const SAMPLE = {
  icons: { rain: '/icons/rain.svg', sun: '/icons/sun.svg', 'bad name': '/x.svg', empty: '' },
  buttons: [
    { position: 1, icon: 'rain' },                 // 48
    { position: 2, icon: 'sun', iconsize: 48 },    // 48 again
    { position: 3, label: 'no icon' },
    { position: 4, iconsize: 40 },                 // dynamic icon later: 40
    { position: 5, iconsize: 0 },                  // <= 0: the default, 48
  ],
  displayitems: [
    { displayitemid: 1, icon: 'sun' },             // 32
    { displayitemid: 2, icon: 'rain', iconsize: 2000 }, // invalid: skipped
  ],
  images: [
    { name: 'radar', width: 120, height: 80, icon: 'rain' },
    { name: 'photo', width: 64, height: 64, src: 'ico-keep-64x64.bin' },
  ],
};

describe('wantedRasters', () => {
  it('reads the icons table (valid entries only)', () => {
    expect([...iconTable(SAMPLE).entries()]).toEqual([['rain', '/icons/rain.svg'], ['sun', '/icons/sun.svg']]);
    expect(iconTable({ icons: [] }).size).toBe(0);
  });

  it('collects size slots from keys, display items and images', () => {
    expect(iconSizeSlots(SAMPLE).map(s => `${s.where}:${s.w}x${s.h}`)).toEqual([
      'buttons[0]:48x48', 'buttons[1]:48x48', 'buttons[3]:40x40', 'buttons[4]:48x48', 'displayitems[0]:32x32', 'images[0]:120x80',
    ]);
  });

  it('wants every icon at every size slot, sorted and deduplicated', () => {
    expect(wantedRasters(SAMPLE).map(r => r.key)).toEqual([
      'ico-rain-120x80.bin', 'ico-rain-32x32.bin', 'ico-rain-40x40.bin', 'ico-rain-48x48.bin',
      'ico-sun-120x80.bin', 'ico-sun-32x32.bin', 'ico-sun-40x40.bin', 'ico-sun-48x48.bin',
    ]);
    expect(wantedRasters(SAMPLE)[0]).toEqual({ name: 'rain', w: 120, h: 80, key: 'ico-rain-120x80.bin' });
  });

  it('wants nothing without icons or without slots', () => {
    expect(wantedRasters({ buttons: [{ position: 1, icon: 'rain' }] })).toEqual([]);
    expect(wantedRasters({ icons: { rain: '/r.svg' }, buttons: [{ position: 1 }] })).toEqual([]);
    expect(wantedRasters(null)).toEqual([]);
  });

  it('collects src keys of images', () => {
    expect([...srcKeys(SAMPLE)]).toEqual(['ico-keep-64x64.bin']);
  });
});

describe('diffRasters', () => {
  const md5 = 'a'.repeat(32);
  it('uploads missing and changed rasters, keeps identical ones, removes stale ico-* keys only', () => {
    const diff = diffRasters(
      [
        { key: 'ico-rain-48x48.bin', size: 100, md5 },
        { key: 'ico-sun-48x48.bin', size: 100, md5 },
        { key: 'ico-moon-48x48.bin', size: 100, md5 },
        { key: 'ico-wind-48x48.bin', size: 100, md5 },
      ],
      [
        { key: 'ico-rain-48x48.bin', size: 100, etag: `"${md5.toUpperCase()}"` }, // same
        { key: 'ico-sun-48x48.bin', size: 100, etag: `"${'b'.repeat(32)}"` },     // other bytes
        { key: 'ico-moon-48x48.bin', size: 99, etag: `"${md5}"` },                // other size
        { key: 'ico-old-32x32.bin', size: 50, etag: '"x"' },                      // stale
        { key: 'ico-keep-64x64.bin', size: 50 },                                  // an images src
        { key: 'radar.bin', size: 5000 },                                         // user file
      ],
      new Set(['ico-keep-64x64.bin']),
    );
    expect(diff).toEqual({
      upload: ['ico-sun-48x48.bin', 'ico-moon-48x48.bin', 'ico-wind-48x48.bin'],
      remove: ['ico-old-32x32.bin'],
      unchanged: ['ico-rain-48x48.bin'],
    });
  });

  it('normalizes ETags', () => {
    expect(normalizeEtag('"ABC"')).toBe('abc');
    expect(normalizeEtag('W/"abc"')).toBe('abc');
    expect(normalizeEtag(undefined)).toBe('');
  });
});
