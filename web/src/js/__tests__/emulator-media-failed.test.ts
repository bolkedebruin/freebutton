/**
 * The virtual device's media when the device does not answer at once
 * (virtual-media.ts with emulator/media.ts MediaCache): a request that failed
 * is not "the device has no such file". The icon font (icons.ttf, 1.3 MB) is
 * the one a busy device fails on: without it every MDI glyph is LVGL's
 * missing-glyph box, while text looks right in the built-in font.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { existsSync, readFileSync } from 'node:fs';
import { BASIC_CONFIG, emulatorModule, repoPath } from './emulator-helpers';
import { Emulator, ManualClock, MediaCache, type MediaSource } from '../emulator';

const FONT_KEYS = ['icons.ttf', 'medium.ttf', 'semibold.ttf'];
const font = (key: string): Uint8Array | null => {
  const path = repoPath(`media/fonts/${key}`);
  return FONT_KEYS.includes(key) && existsSync(path) ? new Uint8Array(readFileSync(path)) : null;
};

/** The device behind the mocked device-storage.ts: what it lists, and the requests that fail. */
const net = vi.hoisted(() => ({ calls: [] as string[], listed: [] as string[], fail: new Map<string, number>() }));
vi.mock('../device-storage', async (original) => ({
  ...await original<typeof import('../device-storage')>(),
  listObjects: async (_base: string, options: { bucket?: string } = {}) => {
    net.calls.push(`list ${options.bucket}`);
    return { objects: options.bucket === 'fonts' ? net.listed.map(key => ({ key, size: 1 })) : [], prefixes: [] };
  },
  getObjectBytes: async (_base: string, key: string, bucket: string) => {
    net.calls.push(`get ${bucket}/${key}`);
    await new Promise(resolve => setTimeout(resolve, key === 'icons.ttf' ? 40 : 5));
    const left = net.fail.get(key) ?? 0;
    if (left > 0) {
      net.fail.set(key, left - 1);
      throw new Error('Device storage not reachable');
    }
    const bytes = bucket === 'fonts' ? font(key) : null;
    return bytes ? { bytes } : null;
  },
}));

import { EmulatorSession } from '../emulator-session';
import { VirtualMedia, virtualMedia, type VirtualMediaDeps } from '../virtual-media';

/** Key 3 shows an MDI glyph (volume-minus), from the icon font through the fallback of the system font. */
const CONFIG = JSON.stringify({
  ...BASIC_CONFIG,
  fonts: [
    { id: 1, name: 'mdi', src: 'icons.ttf' },
    { name: 'montserrat', fallback: 'mdi' },
    { name: 'montserrat_semibold', fallback: 'mdi' },
  ],
  buttons: [
    { position: 1, label: 'M-L' },
    { position: 2, label: 'M-R' },
    { name: 'light', position: 3, label: '\u{F075E}', toplabel: 'Volume' },
    { name: 'blind', position: 4, label: 'Blind' },
  ],
});
const AT = Date.UTC(2026, 10, 12, 9, 10, 0);
const wait = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));

function create(media: MediaSource | MediaCache, clock = new ManualClock(AT)): Promise<Emulator> {
  return Emulator.create({ config: CONFIG, wasm: emulatorModule(), identity: 'virt_btn_0a1b2c', clock, sampleSensors: false, media });
}

const key3 = (emu: Emulator): Uint8ClampedArray => emu.screenshot('key3').data;
const same = (a: Uint8ClampedArray, b: Uint8ClampedArray): boolean => a.length === b.length && a.every((value, i) => value === b[i]);

/** Key 3 with every font file given at once (as emulator-render.test.ts does), and with all but the icon font. */
async function references(): Promise<{ drawn: Uint8ClampedArray; boxes: Uint8ClampedArray }> {
  const all = await create({ read: (bucket, key) => (bucket === 'fonts' ? font(key) : null) });
  const without = await create({ read: (bucket, key) => (bucket === 'fonts' && key !== 'icons.ttf' ? font(key) : null) });
  const out = { drawn: key3(all), boxes: key3(without) };
  all.dispose();
  without.dispose();
  expect(same(out.drawn, out.boxes)).toBe(false);
  return out;
}

function memoryStorage(): Storage {
  const map = new Map<string, string>();
  return {
    getItem: (k: string) => map.get(k) ?? null,
    setItem: (k: string, v: string) => { map.set(k, String(v)); },
    removeItem: (k: string) => { map.delete(k); },
    clear: () => map.clear(),
    key: (i: number) => [...map.keys()][i] ?? null,
    get length() { return map.size; },
  } as Storage;
}

describe('a font request that failed', () => {
  it('is asked again; the device that runs gets the font when it arrives, as after an upload', async () => {
    const ref = await references();
    let fail = 1;
    const deps: VirtualMediaDeps = {
      list: async (_base, bucket) => (bucket === 'fonts' ? FONT_KEYS : []),
      get: async (_base, bucket, key) => {
        await wait(key === 'icons.ttf' ? 40 : 5);
        if (key === 'icons.ttf' && fail-- > 0) throw new Error('Device storage not reachable');
        return bucket === 'fonts' ? font(key) : null;
      },
      rasterize: async (_svg, w, h) => new Uint8ClampedArray(w * h * 4),
      knownFonts: async () => new Set(FONT_KEYS),
    };
    const media = new VirtualMedia(deps, [30]);
    media.setDevice('http://device');
    const cache = new MediaCache({ read: media.read });
    media.attach(cache);
    const clock = new ManualClock(AT);
    const emu = await create(cache, clock);
    expect(same(key3(emu), ref.boxes)).toBe(true);
    expect(media.failedFonts()).toEqual(['icons.ttf']);
    expect(media.hint()).toContain('Could not be read from the device: icons.ttf');
    expect(media.hint()).not.toContain('Not in the device');

    // The retry (30 ms, then the request); the firmware reads its fonts again 1.5 s after the change.
    await wait(200);
    expect(media.hint()).toBe('');
    clock.advance(2000);
    expect(same(key3(emu), ref.drawn)).toBe(true);
    emu.dispose();
  }, 30000);
});

describe('the IDE\'s virtual device (EmulatorSession) on a device that fails a request', () => {
  let session: EmulatorSession;
  const running = (): Emulator => (session as unknown as { emu: Emulator }).emu;

  beforeEach(() => {
    vi.stubGlobal('localStorage', memoryStorage());
    net.calls.length = 0;
    net.listed = [...FONT_KEYS];
    net.fail.clear();
    session = new EmulatorSession();
    session.wasm = emulatorModule();
    session.readSource = async () => ({ script: '', config: CONFIG });
    virtualMedia.setDevice('http://device');
  });

  afterEach(() => {
    session.destroy();
    virtualMedia.setDevice(null);
    vi.unstubAllGlobals();
  });

  it('the next Run asks again for the font: its glyphs are drawn', async () => {
    const ref = await references();
    net.fail.set('icons.ttf', 1);
    await session.run();
    expect(session.snapshot().status).toBe('running');
    expect(same(key3(running()), ref.boxes)).toBe(true);
    expect(virtualMedia.hint()).toContain('Could not be read from the device: icons.ttf');

    await session.run();
    expect(same(key3(running()), ref.drawn)).toBe(true);
    expect(virtualMedia.hint()).toBe('');
    // The fonts that were here are not fetched again.
    expect(net.calls.filter(call => call === 'get fonts/medium.ttf')).toHaveLength(1);
    expect(net.calls.filter(call => call === 'get fonts/icons.ttf')).toHaveLength(2);
  }, 30000);

  it('a font that reached the device after its bucket was listed is found by the next Run', async () => {
    const ref = await references();
    net.listed = ['medium.ttf', 'semibold.ttf'];
    await session.run();
    expect(same(key3(running()), ref.boxes)).toBe(true);
    expect(virtualMedia.hint()).toContain('Not in the device\'s fonts: icons.ttf');
    expect(net.calls).not.toContain('get fonts/icons.ttf');

    net.listed = [...FONT_KEYS];
    await session.run();
    expect(same(key3(running()), ref.drawn)).toBe(true);
    expect(virtualMedia.hint()).toBe('');
  }, 30000);

  it('with every answer at once nothing is asked twice, on the first Run and on the second', async () => {
    const ref = await references();
    await session.run();
    expect(same(key3(running()), ref.drawn)).toBe(true);
    await session.run();
    expect(same(key3(running()), ref.drawn)).toBe(true);
    expect(net.calls.filter(call => call.includes('fonts'))).toEqual(['list fonts', 'get fonts/medium.ttf', 'get fonts/semibold.ttf', 'get fonts/icons.ttf']);
  }, 30000);
});
