import { describe, it, expect, beforeEach } from 'vitest';
import { syncIcons, svgKeyFor, iconSources, iconPreviewUrl, _clearIconSources, type IconSyncDeps } from '../icon-sync';
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
