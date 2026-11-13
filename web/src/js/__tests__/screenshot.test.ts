import { describe, it, expect, vi, afterEach } from 'vitest';
import { captureScreenshots, decodeScreenshot, isScreenshotKey } from '../screenshot';
import * as storage from '../device-storage';

function file(w: number, h: number, pixels: number[]): Uint8Array {
  const out = new Uint8Array(8 + pixels.length * 2);
  out.set([0x46, 0x42, 0x53, 0x53, w & 255, w >> 8, h & 255, h >> 8]);
  pixels.forEach((p, i) => { out[8 + i * 2] = p >> 8; out[9 + i * 2] = p & 255; });
  return out;
}

afterEach(() => vi.restoreAllMocks());

describe('screenshot files', () => {
  it('recognises the device file names', () => {
    expect(isScreenshotKey('screenshot-main.rgb565')).toBe(true);
    expect(isScreenshotKey('screenshot-mini0.rgb565')).toBe(true);
    expect(isScreenshotKey('scripts/screenshot-main.rgb565')).toBe(false);
    expect(isScreenshotKey('screenshot.png')).toBe(false);
  });

  it('decodes RGB565 big-endian to RGBA', () => {
    const shot = decodeScreenshot('main', file(2, 1, [0xF800, 0x07E0]))!;
    expect(shot.width).toBe(2);
    expect(shot.height).toBe(1);
    expect([...shot.rgba]).toEqual([255, 0, 0, 255, 0, 255, 0, 255]);
  });

  it('pads a short file with black and rejects other data', () => {
    const shot = decodeScreenshot('main', file(2, 1, [0x001F]))!;
    expect([...shot.rgba]).toEqual([0, 0, 255, 255, 0, 0, 0, 255]);
    expect(decodeScreenshot('main', new Uint8Array([1, 2, 3]))).toBeNull();
    expect(decodeScreenshot('main', new Uint8Array(16))).toBeNull();
  });
});

describe('captureScreenshots', () => {
  it('removes old files, waits for stable files, downloads and deletes them', async () => {
    const stale = { key: 'screenshot-main.rgb565', size: 5 };
    const main = { key: 'screenshot-main.rgb565', size: 10 };
    const mini = { key: 'screenshot-mini0.rgb565', size: 10 };
    const other = { key: 'config.json', size: 3 };
    const listings = [[stale, other], [other], [main, other], [main, mini, other], [main, mini, other], [main, mini, other]];
    const list = vi.spyOn(storage, 'listObjects').mockImplementation(async () => ({ objects: listings.shift() ?? [], prefixes: [] }) as never);
    const del = vi.spyOn(storage, 'deleteObject').mockResolvedValue(undefined);
    vi.spyOn(storage, 'getObjectBytes').mockImplementation(async (_b, key) => ({ bytes: file(1, 1, [key.includes('main') ? 0xFFFF : 0]) }));
    const fetchImpl = vi.fn(async () => new Response('{}', { status: 202 })) as unknown as typeof fetch;

    const shots = await captureScreenshots('http://dev', { fetchImpl, sleep: async () => undefined });

    expect(fetchImpl).toHaveBeenCalledWith('http://dev/api/screenshot', { method: 'POST' });
    expect(shots.map(s => s.name)).toEqual(['main', 'mini0']);
    expect(list).toHaveBeenCalledTimes(6);
    // The stale file before the request, then both new files after the download.
    expect(del.mock.calls.map(c => c[1])).toEqual(['screenshot-main.rgb565', 'screenshot-main.rgb565', 'screenshot-mini0.rgb565']);
  });

  it('works when the device refuses the delete: only a changed file is the new capture', async () => {
    const t0 = new Date(1000), t1 = new Date(5000);
    const stale = { key: 'screenshot-main.rgb565', size: 10, lastModified: t0 };
    const fresh = { key: 'screenshot-main.rgb565', size: 10, lastModified: t1 };
    const listings = [[stale], [stale], [stale], [stale], [fresh], [fresh], [fresh]];
    const list = vi.spyOn(storage, 'listObjects').mockImplementation(async () => ({ objects: listings.shift() ?? [], prefixes: [] }) as never);
    vi.spyOn(storage, 'deleteObject').mockRejectedValue(new Error('InvalidArgument'));
    vi.spyOn(storage, 'getObjectBytes').mockResolvedValue({ bytes: file(1, 1, [0xFFFF]) } as never);
    const fetchImpl = vi.fn(async () => new Response('{}', { status: 202 })) as unknown as typeof fetch;

    const shots = await captureScreenshots('http://dev', { fetchImpl, sleep: async () => undefined });

    expect(shots.map(s => s.name)).toEqual(['main']);
    // Two listings before the request, two of the unchanged old file, three of the new one.
    expect(list).toHaveBeenCalledTimes(7);
  });

  it('reports a refused request', async () => {
    vi.spyOn(storage, 'listObjects').mockResolvedValue({ objects: [], prefixes: [] } as never);
    const fetchImpl = vi.fn(async () => new Response('', { status: 403 })) as unknown as typeof fetch;
    await expect(captureScreenshots('http://dev', { fetchImpl, sleep: async () => undefined })).rejects.toThrow(/HTTP 403/);
  });

  it('times out when the device writes nothing', async () => {
    vi.spyOn(storage, 'listObjects').mockResolvedValue({ objects: [], prefixes: [] } as never);
    const fetchImpl = vi.fn(async () => new Response('{}', { status: 202 })) as unknown as typeof fetch;
    let t = 0;
    await expect(captureScreenshots('http://dev', { fetchImpl, sleep: async () => undefined, now: () => (t += 5000) }))
      .rejects.toThrow(/Timed out/);
  });
});
