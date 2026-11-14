import { describe, it, expect, beforeEach } from 'vitest';
import { syncIcons, svgKeyFor, iconSources, iconPreviewUrl, parseManifest, _clearIconSources, type IconSyncDeps } from '../icon-sync';
import { encodeLvglBin, type ExistingObject } from '../icon-raster';
import { md5Hex } from '../md5';
import { S3Error } from '../device-storage';
import { prepareSvg, SvgError } from '../icon-render';

const SVG = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M0 0h24v24H0z"/></svg>';

/** Fake rasterizer: a w x h image whose colour depends on the SVG (mono black or two colours). */
async function fakeRasterize(svg: string, w: number, h: number): Promise<Uint8ClampedArray> {
  const out = new Uint8ClampedArray(w * h * 4);
  const two = svg.includes('two');
  for (let p = 0; p < w * h; p++) out.set(two && p % 2 ? [255, 0, 0, 255] : [0, 0, 0, 255], p * 4);
  return out;
}

function fakeDeps(app: Record<string, string>, images: ExistingObject[] | 'no-bucket') {
  const calls: string[] = [];
  const logs: string[] = [];
  const stored = new Map<string, Uint8Array>();
  const deps: IconSyncDeps = {
    readSvg: async (key) => { calls.push(`GET app/${key}`); return app[key] ?? null; },
    listImages: async () => {
      calls.push('LIST images');
      if (images === 'no-bucket') throw new S3Error(404, 'NoSuchBucket', 'The specified bucket does not exist');
      return images;
    },
    putImage: async (key, bytes) => { calls.push(`PUT images/${key}`); stored.set(key, bytes); },
    deleteImage: async (key) => { calls.push(`DELETE images/${key}`); },
    rasterize: fakeRasterize,
    log: (text, type) => { logs.push(`${type}: ${text}`); },
  };
  return { deps, calls, logs, stored };
}

const CFG = {
  icons: { rain: '/icons/rain.svg', two: '/icons/two.svg' },
  buttons: [{ position: 1, icon: 'rain' }],
  displayitems: [{ displayitemid: 1, icon: 'two', iconsize: 16 }],
};

describe('syncIcons', () => {
  beforeEach(() => _clearIconSources());

  it('maps icon paths to app keys', () => {
    expect(svgKeyFor('/icons/rain.svg')).toBe('icons/rain.svg');
    expect(svgKeyFor(' icons/a.svg ')).toBe('icons/a.svg');
  });

  it('uploads every wanted raster to an empty bucket', async () => {
    const { deps, calls, stored, logs } = fakeDeps({ 'icons/rain.svg': SVG, 'icons/two.svg': SVG + '<!--two-->' }, []);
    const result = await syncIcons(CFG, deps);
    expect(result.ok).toBe(true);
    expect(result.uploaded).toEqual(['ico-rain-16x16.bin', 'ico-rain-48x48.bin', 'ico-two-16x16.bin', 'ico-two-48x48.bin']);
    expect(calls).toEqual([
      'LIST images', 'GET app/icons/rain.svg', 'GET app/icons/two.svg',
      'PUT images/ico-rain-16x16.bin', 'PUT images/ico-rain-48x48.bin',
      'PUT images/ico-two-16x16.bin', 'PUT images/ico-two-48x48.bin',
    ]);
    expect(stored.get('ico-rain-48x48.bin')![1]).toBe(0x0e); // mono -> A8
    expect(stored.get('ico-two-16x16.bin')![1]).toBe(0x14);  // two colours -> RGB565A8
    expect(stored.get('ico-rain-48x48.bin')!.length).toBe(12 + 48 * 48);
    expect(logs[logs.length - 1]).toMatch(/^result: Icons: 4 rasters \(2 single-colour, .* KB\), 4 uploaded, 0 unchanged, 0 deleted$/);
    expect([...iconSources().keys()]).toEqual(['rain', 'two']);
  });

  it('skips identical rasters (ETag = MD5) and deletes stale ico-* keys only', async () => {
    const same = encodeLvglBin(await fakeRasterize(SVG, 48, 48), 48, 48).bytes;
    const existing: ExistingObject[] = [
      { key: 'ico-rain-48x48.bin', size: same.length, etag: `"${md5Hex(same)}"` },
      { key: 'ico-rain-16x16.bin', size: 5, etag: '"00"' },
      { key: 'ico-old-8x8.bin', size: 76, etag: '"11"' },
      { key: 'radar.bin', size: 900 },
    ];
    const { deps, calls, logs } = fakeDeps({ 'icons/rain.svg': SVG, 'icons/two.svg': SVG }, existing);
    const result = await syncIcons(CFG, deps);
    expect(result.unchanged).toEqual(['ico-rain-48x48.bin']);
    expect(result.uploaded).toEqual(['ico-rain-16x16.bin', 'ico-two-16x16.bin', 'ico-two-48x48.bin']);
    expect(result.removed).toEqual(['ico-old-8x8.bin']);
    expect(calls).not.toContain('DELETE images/radar.bin');
    expect(logs).toContain('info: Syncing icons: 3 to upload, 1 to delete...');
  });

  it('keeps the rasters of an icon whose SVG is missing and reports it', async () => {
    const existing: ExistingObject[] = [{ key: 'ico-two-48x48.bin', size: 1, etag: '"x"' }];
    const { deps, calls, logs } = fakeDeps({ 'icons/rain.svg': SVG }, existing);
    const result = await syncIcons(CFG, deps);
    expect(result.ok).toBe(false);
    expect(result.failed).toEqual(['ico-two-16x16.bin', 'ico-two-48x48.bin']);
    expect(calls.filter(c => c.startsWith('DELETE'))).toEqual([]);
    expect(logs).toContain('warn: Icon "two": /icons/two.svg not found on the device');
    expect(logs[logs.length - 1]).toMatch(/^warn: Icons: .*2 failed$/);
  });

  it('reports a render failure for that raster only', async () => {
    const { deps, logs } = fakeDeps({ 'icons/rain.svg': SVG, 'icons/two.svg': SVG }, []);
    deps.rasterize = async (svg, w, h) => {
      if (w === 16) throw new Error('the browser could not render this SVG');
      return fakeRasterize(svg, w, h);
    };
    const result = await syncIcons(CFG, deps);
    expect(result.uploaded).toEqual(['ico-rain-48x48.bin', 'ico-two-48x48.bin']);
    expect(result.failed).toEqual(['ico-rain-16x16.bin', 'ico-two-16x16.bin']);
    expect(logs).toContain('error: Icon "rain" at 16x16: the browser could not render this SVG');
  });

  it('says the firmware is too old when the images bucket is missing', async () => {
    const { deps, calls, logs } = fakeDeps({ 'icons/rain.svg': SVG }, 'no-bucket');
    const result = await syncIcons(CFG, deps);
    expect(result).toMatchObject({ ok: false, noBucket: true, uploaded: [] });
    expect(calls).toEqual(['LIST images']);
    expect(logs).toEqual(['error: Icons not synced: this firmware has no "images" storage bucket (update the device firmware)']);
  });

  it('is quiet without icons on old firmware, and cleans up when icons were removed', async () => {
    const old = fakeDeps({}, 'no-bucket');
    expect(await syncIcons({ buttons: [] }, old.deps)).toMatchObject({ ok: true, noBucket: true });
    expect(old.logs).toEqual([]);

    const stale = fakeDeps({}, [{ key: 'ico-rain-48x48.bin', size: 1 }, { key: 'user.bin', size: 1 }]);
    const result = await syncIcons({ buttons: [] }, stale.deps);
    expect(result.removed).toEqual(['ico-rain-48x48.bin']);
    expect(stale.calls).toEqual(['LIST images', 'DELETE images/ico-rain-48x48.bin']);
  });

  it('never deletes an ico-* key an image uses as src', async () => {
    const cfg = { images: [{ name: 'pic', width: 8, height: 8, src: 'ico-mine-8x8.bin' }] };
    const { deps, calls } = fakeDeps({}, [{ key: 'ico-mine-8x8.bin', size: 76 }]);
    await syncIcons(cfg, deps);
    expect(calls).toEqual(['LIST images']);
  });

  it('gives the preview a data URL of the fitted SVG once the source was read', async () => {
    expect(iconPreviewUrl('rain', 48, 48)).toBeNull();
    const { deps } = fakeDeps({ 'icons/rain.svg': SVG, 'icons/two.svg': SVG }, []);
    await syncIcons(CFG, deps);
    const url = iconPreviewUrl('rain', 48, 48)!;
    expect(url.startsWith('data:image/svg+xml;charset=utf-8,')).toBe(true);
    const svg = decodeURIComponent(url.slice(url.indexOf(',') + 1));
    expect(svg).toContain('width="48"');
    expect(svg).toContain('preserveAspectRatio="xMidYMid meet"');
  });
});

describe('syncIcons with the raster manifest', () => {
  beforeEach(() => _clearIconSources());

  /** fakeDeps plus a manifest in memory and a count of renders. */
  function manifestDeps(app: Record<string, string>, images: ExistingObject[], manifest: string | null) {
    const f = fakeDeps(app, images);
    const state = { manifest, writes: 0, renders: 0 };
    const rasterize = f.deps.rasterize;
    f.deps.rasterize = async (svg, w, h) => { state.renders++; return rasterize(svg, w, h); };
    f.deps.readManifest = async () => state.manifest;
    f.deps.writeManifest = async (text) => { state.manifest = text; state.writes++; };
    return { ...f, state };
  }
  const APP = { 'icons/rain.svg': SVG, 'icons/two.svg': SVG.replace('<path', '<path class="two"') };
  /** What another browser left for the same sources: right sizes, other pixels (ETags). */
  function otherBrowserImages(stored: Map<string, Uint8Array>): ExistingObject[] {
    return [...stored].map(([key, bytes]) => ({ key, size: bytes.length, etag: '"' + 'ab'.repeat(16) + '"' }));
  }

  it('records the source of every raster it uploads', async () => {
    const { deps, state, stored } = manifestDeps(APP, [], null);
    const r = await syncIcons(CFG, deps);
    expect(r.uploaded.length).toBe(stored.size);
    expect(state.writes).toBe(1);
    const m = parseManifest(state.manifest);
    expect(Object.keys(m.rasters).sort()).toEqual([...stored.keys()].sort());
    for (const [key, bytes] of stored) expect(m.rasters[key].size).toBe(bytes.length);
  });

  it('keeps rasters whose source did not change, without rendering or writing', async () => {
    const first = manifestDeps(APP, [], null);
    await syncIcons(CFG, first.deps);
    const second = manifestDeps(APP, otherBrowserImages(first.stored), first.state.manifest);
    const r = await syncIcons(CFG, second.deps);
    expect(r.uploaded).toEqual([]);
    expect(r.removed).toEqual([]);
    expect(r.unchanged.length).toBe(first.stored.size);
    expect(second.state.renders).toBe(0);
    expect(second.state.writes).toBe(0);
    expect(second.calls.filter(c => c.startsWith('PUT'))).toEqual([]);
  });

  it('uploads only the rasters of the SVG that changed', async () => {
    const first = manifestDeps(APP, [], null);
    await syncIcons(CFG, first.deps);
    const changed = { ...APP, 'icons/rain.svg': SVG.replace('M0 0', 'M1 1') };
    const second = manifestDeps(changed, otherBrowserImages(first.stored), first.state.manifest);
    const r = await syncIcons(CFG, second.deps);
    expect(r.uploaded.length).toBeGreaterThan(0);
    expect(r.uploaded.every(k => k.startsWith('ico-rain-'))).toBe(true);
    expect(r.unchanged.every(k => k.startsWith('ico-two-'))).toBe(true);
    expect(second.state.writes).toBe(1);
  });

  it('takes over rasters made without a manifest instead of writing them again', async () => {
    const first = manifestDeps(APP, [], null);
    await syncIcons(CFG, first.deps);
    const second = manifestDeps(APP, otherBrowserImages(first.stored), null);
    const r = await syncIcons(CFG, second.deps);
    expect(r.uploaded).toEqual([]);
    expect(second.state.writes).toBe(1);
    expect(Object.keys(parseManifest(second.state.manifest).rasters).length).toBe(first.stored.size);
  });

  it('writes a raster again when the device has another size for it', async () => {
    const first = manifestDeps(APP, [], null);
    await syncIcons(CFG, first.deps);
    const images = otherBrowserImages(first.stored).map((o, i) => i === 0 ? { ...o, size: o.size + 1 } : o);
    const second = manifestDeps(APP, images, first.state.manifest);
    const r = await syncIcons(CFG, second.deps);
    expect(r.uploaded).toEqual([images[0].key]);
  });

  it('drops the entries of rasters the config no longer wants', async () => {
    const first = manifestDeps(APP, [], null);
    await syncIcons(CFG, first.deps);
    const cfg = { ...CFG, displayitems: [] };
    const second = manifestDeps(APP, otherBrowserImages(first.stored), first.state.manifest);
    const r = await syncIcons(cfg, second.deps);
    expect(r.removed.length).toBeGreaterThan(0);
    const left = Object.keys(parseManifest(second.state.manifest).rasters);
    expect(left.length).toBeGreaterThan(0);
    expect(left.some(k => r.removed.includes(k))).toBe(false);
  });

  it('reads a broken manifest as empty', () => {
    expect(parseManifest('not json').rasters).toEqual({});
    expect(parseManifest('{"v":2,"rasters":{"a":{"src":"x","size":1}}}').rasters).toEqual({});
    expect(parseManifest(null).rasters).toEqual({});
  });
});

describe('prepareSvg', () => {
  it('sizes the root for a contain fit and keeps the viewBox', () => {
    const out = prepareSvg(SVG, 32, 16);
    expect(out).toContain('width="32"');
    expect(out).toContain('height="16"');
    expect(out).toContain('viewBox="0 0 24 24"');
    expect(out).toContain('preserveAspectRatio="xMidYMid meet"');
  });

  it('adds a viewBox from width/height so the drawing scales', () => {
    const out = prepareSvg('<svg xmlns="http://www.w3.org/2000/svg" width="100" height="50px"><rect width="100" height="50"/></svg>', 20, 20);
    expect(out).toContain('viewBox="0 0 100 50"');
    expect(out).toContain('width="20"');
  });

  it('refuses what is not SVG', () => {
    expect(() => prepareSvg('<html><body/></html>', 8, 8)).toThrow(SvgError);
    expect(() => prepareSvg('not xml at all <', 8, 8)).toThrow(SvgError);
  });
});
