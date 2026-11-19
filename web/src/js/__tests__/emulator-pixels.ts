/**
 * For the tests that look at the emulator's pixels: the repo's fonts as the
 * device's media store, PNG files without a dependency (node:zlib), and the
 * comparison with a golden image.
 *
 * Golden images live in __tests__/golden/. A test fails when a pixel differs
 * (no tolerance: the wasm is deterministic) or when the golden image is
 * missing. To make or renew them after an intended change of the firmware's
 * drawing: UPDATE_GOLDEN=1 npx vitest run src/js/__tests__/emulator-render.test.ts
 * and look at every changed PNG before committing it.
 */
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname } from 'node:path';
import { deflateSync, inflateSync } from 'node:zlib';
import type { MediaSource, Screenshot } from '../emulator';
import { repoPath } from './emulator-helpers';

/**
 * media/fonts of the repo (the device's fonts bucket after a factory flash),
 * plus extra files ("bucket/key"). With `icons`, every icon raster the
 * firmware asks for ("ico-<name>-<w>x<h>.bin") is made by testRaster at that
 * size: in colour when the icon's name starts with "color", else one colour.
 */
export function repoMedia(extra: Readonly<Record<string, Uint8Array>> = {}, options: { icons?: boolean } = {}): MediaSource {
  return {
    read: (bucket, key) => {
      const own = extra[`${bucket}/${key}`];
      if (own) return own;
      if (bucket === 'images' && options.icons) {
        const icon = /^ico-(.+)-(\d+)x(\d+)\.bin$/.exec(key);
        return icon ? testRaster(Number(icon[2]), Number(icon[3]), icon[1]!.startsWith('color') ? 'rgb565a8' : 'a8') : null;
      }
      if (bucket !== 'fonts' || !/^[a-z0-9_.-]+\.ttf$/i.test(key)) return null;
      const path = repoPath(`media/fonts/${key}`);
      return existsSync(path) ? new Uint8Array(readFileSync(path)) : null;
    },
  };
}

/**
 * An LVGL .bin raster made by a rule, in the two formats the IDE writes:
 * A8 (one colour: drawn in the item's text colour) and RGB565A8.
 */
export function testRaster(width: number, height: number, kind: 'a8' | 'rgb565a8'): Uint8Array {
  const n = width * height;
  const bytes = new Uint8Array(12 + (kind === 'a8' ? n : n * 3));
  const view = new DataView(bytes.buffer);
  view.setUint8(0, 0x19);
  view.setUint8(1, kind === 'a8' ? 0x0e : 0x14);
  view.setUint16(4, width, true);
  view.setUint16(6, height, true);
  view.setUint16(8, kind === 'a8' ? width : width * 2, true);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const p = y * width + x;
      // A disc with a soft edge, and for the colour raster four saturated quadrants.
      const dx = x + 0.5 - width / 2;
      const dy = y + 0.5 - height / 2;
      const edge = Math.min(width, height) / 2 - Math.hypot(dx, dy);
      const alpha = Math.max(0, Math.min(255, Math.round(edge * 128)));
      if (kind === 'a8') {
        bytes[12 + p] = alpha;
        continue;
      }
      const color = dx < 0 ? (dy < 0 ? 0xf800 : 0x07e0) : (dy < 0 ? 0x001f : 0xffe0);
      bytes[12 + p * 2] = color & 0xff;
      bytes[12 + p * 2 + 1] = color >> 8;
      bytes[12 + n * 2 + p] = alpha;
    }
  }
  return bytes;
}

// --- PNG (8-bit RGBA, no interlace) --------------------------------------------------

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(bytes: Uint8Array): number {
  let c = 0xffffffff;
  for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xff]! ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type: string, data: Uint8Array): Uint8Array {
  const out = new Uint8Array(12 + data.length);
  const view = new DataView(out.buffer);
  view.setUint32(0, data.length);
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  view.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
  return out;
}

const PNG_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

export function encodePng(shot: Screenshot): Uint8Array {
  const { width, height, data } = shot;
  const raw = new Uint8Array(height * (1 + width * 4));
  for (let y = 0; y < height; y++) {
    raw.set(data.subarray(y * width * 4, (y + 1) * width * 4), y * (1 + width * 4) + 1);
  }
  const header = new Uint8Array(13);
  const view = new DataView(header.buffer);
  view.setUint32(0, width);
  view.setUint32(4, height);
  header[8] = 8;  // bit depth
  header[9] = 6;  // RGBA
  const parts = [new Uint8Array(PNG_SIGNATURE), chunk('IHDR', header), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', new Uint8Array(0))];
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let at = 0;
  for (const p of parts) {
    out.set(p, at);
    at += p.length;
  }
  return out;
}

/** Reads a PNG that encodePng wrote (filter 0 on every row). */
export function decodePng(bytes: Uint8Array): Screenshot {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let at = 8;
  let width = 0;
  let height = 0;
  const data: Uint8Array[] = [];
  while (at < bytes.length) {
    const length = view.getUint32(at);
    const type = String.fromCharCode(...bytes.subarray(at + 4, at + 8));
    const body = bytes.subarray(at + 8, at + 8 + length);
    if (type === 'IHDR') {
      width = view.getUint32(at + 8);
      height = view.getUint32(at + 12);
      if (body[8] !== 8 || body[9] !== 6) throw new Error('golden PNG: not 8-bit RGBA');
    } else if (type === 'IDAT') {
      data.push(body);
    }
    at += 12 + length;
  }
  const packed = new Uint8Array(data.reduce((n, p) => n + p.length, 0));
  let offset = 0;
  for (const p of data) {
    packed.set(p, offset);
    offset += p.length;
  }
  const raw = inflateSync(packed);
  const out = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) {
    const row = y * (1 + width * 4);
    if (raw[row] !== 0) throw new Error('golden PNG: a filtered row (not written by encodePng)');
    out.set(raw.subarray(row + 1, row + 1 + width * 4), y * width * 4);
  }
  return { width, height, data: out };
}

export function writePng(path: string, shot: Screenshot): void {
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, encodePng(shot));
}

export const goldenPath = (name: string): string => repoPath(`web/src/js/__tests__/golden/${name}.png`);

/**
 * null when the screenshot is the golden image, else what differs. With
 * UPDATE_GOLDEN=1 the golden image is (re)written and the result is null.
 */
export function compareGolden(name: string, shot: Screenshot): string | null {
  const path = goldenPath(name);
  if (process.env.UPDATE_GOLDEN === '1') {
    if (!existsSync(path) || Buffer.compare(readFileSync(path), Buffer.from(encodePng(shot))) !== 0) writePng(path, shot);
    return null;
  }
  if (!existsSync(path)) return `no golden image ${name}.png (run with UPDATE_GOLDEN=1 and look at it)`;
  const golden = decodePng(new Uint8Array(readFileSync(path)));
  if (golden.width !== shot.width || golden.height !== shot.height) {
    return `${name}: ${shot.width}x${shot.height}, the golden image is ${golden.width}x${golden.height}`;
  }
  let different = 0;
  let first = -1;
  for (let p = 0; p < shot.width * shot.height; p++) {
    const i = p * 4;
    if (shot.data[i] !== golden.data[i] || shot.data[i + 1] !== golden.data[i + 1] || shot.data[i + 2] !== golden.data[i + 2]
      || shot.data[i + 3] !== golden.data[i + 3]) {
      different++;
      if (first < 0) first = p;
    }
  }
  if (different === 0) return null;
  return `${name}: ${different} pixels differ from the golden image, the first at (${first % shot.width}, ${Math.floor(first / shot.width)})`;
}

/** How many pixels are not black. */
export function litPixels(shot: Screenshot): number {
  let lit = 0;
  for (let i = 0; i < shot.data.length; i += 4) {
    if (shot.data[i] !== 0 || shot.data[i + 1] !== 0 || shot.data[i + 2] !== 0) lit++;
  }
  return lit;
}

/** Every channel value is one a 16-bit panel has (5, 6, 5 bits widened by repeating the top bits). */
export function isPanelColours(shot: Screenshot): boolean {
  for (let i = 0; i < shot.data.length; i += 4) {
    const r = shot.data[i]!;
    const g = shot.data[i + 1]!;
    const b = shot.data[i + 2]!;
    if (r !== (((r >> 3) << 3) | (r >> 5)) || g !== (((g >> 2) << 2) | (g >> 6)) || b !== (((b >> 3) << 3) | (b >> 5))
      || shot.data[i + 3] !== 255) return false;
  }
  return true;
}
