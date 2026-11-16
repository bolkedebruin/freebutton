/**
 * Where the virtual device's fonts and rasters come from (virtual-media.ts):
 * the device's media store, an icon rendered here, or nothing.
 */
import { afterEach, describe, expect, it, vi } from 'vitest';
import { VirtualMedia, type VirtualMediaDeps } from '../virtual-media';
import { _clearIconSources, setIconSources } from '../icon-sync';
import { parseLvglBinHeader } from '../icon-raster';

const bytes = (...values: number[]) => new Uint8Array(values);

function make(files: Record<string, Uint8Array> = {}, fonts: ReadonlySet<string> | null = null) {
  const calls: string[] = [];
  const deps: VirtualMediaDeps = {
    list: async (_base, bucket) => {
      calls.push(`list ${bucket}`);
      return Object.keys(files).filter(k => k.startsWith(`${bucket}/`)).map(k => k.slice(bucket.length + 1));
    },
    get: async (_base, bucket, key) => {
      calls.push(`get ${bucket}/${key}`);
      return files[`${bucket}/${key}`] ?? null;
    },
    rasterize: async (_svg, w, h) => {
      calls.push(`rasterize ${w}x${h}`);
      return new Uint8ClampedArray(w * h * 4).fill(255);
    },
    knownFonts: async () => fonts,
  };
  return { media: new VirtualMedia(deps), calls };
}

afterEach(() => { _clearIconSources(); });

describe('virtual media', () => {
  it('nothing is asked of the device before the firmware reads; a bucket is listed once and a file that is not listed is not fetched', async () => {
    const { media, calls } = make({ 'fonts/medium.ttf': bytes(1, 2), 'images/logo.bin': bytes(3) });
    media.setDevice('http://device');
    expect(calls).toEqual([]);
    expect(await media.read('fonts', 'medium.ttf')).toEqual(bytes(1, 2));
    expect(await media.read('fonts', 'pacifico.ttf')).toBeNull();
    expect(await media.read('fonts', 'medium.ttf')).toEqual(bytes(1, 2));
    expect(calls).toEqual(['list fonts', 'get fonts/medium.ttf', 'get fonts/medium.ttf']);
    expect(await media.read('images', 'logo.bin')).toEqual(bytes(3));
    expect(calls.slice(3)).toEqual(['list images', 'get images/logo.bin']);
    expect(media.missingFonts()).toEqual(['pacifico.ttf']);
    expect(media.hint()).toContain('pacifico.ttf');
  });

  it('a listing of the fonts made elsewhere is used: no second one', async () => {
    const { media, calls } = make({ 'fonts/medium.ttf': bytes(1) }, new Set(['medium.ttf']));
    media.setDevice('http://device');
    await media.read('fonts', 'medium.ttf');
    await media.read('fonts', 'other.ttf');
    expect(calls).toEqual(['get fonts/medium.ttf']);
  });

  it('no device: no fonts (a hint, the firmware\'s fallback font), and no request', async () => {
    const { media, calls } = make({ 'fonts/medium.ttf': bytes(1) });
    expect(await media.read('fonts', 'medium.ttf')).toBeNull();
    expect(calls).toEqual([]);
    expect(media.hint()).toContain('fallback font');
    expect(media.hint()).toContain('No device');
  });

  it('an icon raster the device does not have is rendered from the config\'s SVG, as an icon sync makes it', async () => {
    const { media, calls } = make();
    const read = vi.fn(async (key: string) => (key === 'icons/sun.svg' ? '<svg xmlns="http://www.w3.org/2000/svg"/>' : null));
    media.useConfig({ icons: { sun: '/icons/sun.svg' } }, read);
    const raster = await media.read('images', 'ico-sun-24x16.bin');
    expect(parseLvglBinHeader(raster!)).toMatchObject({ w: 24, h: 16 });
    expect(read).toHaveBeenCalledWith('icons/sun.svg');
    expect(calls).toEqual(['rasterize 24x16']);
    // Not an icon of the config, not an icon key: nothing.
    expect(await media.read('images', 'ico-moon-24x16.bin')).toBeNull();
    expect(await media.read('images', 'photo.bin')).toBeNull();
    // An SVG the IDE already has is not read again.
    setIconSources([['moon', '<svg xmlns="http://www.w3.org/2000/svg"/>']]);
    expect(await media.read('images', 'ico-moon-8x8.bin')).not.toBeNull();
    expect(read).toHaveBeenCalledTimes(1);
  });

  it('what the IDE uploads or deletes reaches the page\'s cache; another device or a changed SVG empties it', async () => {
    const { media } = make({}, new Set());
    const cache = { set: vi.fn(), clear: vi.fn() };
    const stale = vi.fn();
    media.attach(cache);
    media.onStale = stale;
    media.setDevice('http://device');
    expect(cache.clear).toHaveBeenCalledTimes(1);
    expect(await media.read('fonts', 'new.ttf')).toBeNull();
    expect(media.missingFonts()).toEqual(['new.ttf']);
    media.changed('fonts', 'new.ttf', bytes(9));
    expect(cache.set).toHaveBeenLastCalledWith('fonts', 'new.ttf', bytes(9));
    expect(media.missingFonts()).toEqual([]);
    media.changed('images', 'ico-sun-8x8.bin', null);
    expect(cache.set).toHaveBeenLastCalledWith('images', 'ico-sun-8x8.bin', null);
    const generation = media.generation;
    media.iconsChanged();
    expect(cache.clear).toHaveBeenLastCalledWith('images');
    expect(media.generation).toBe(generation + 1);
    media.setDevice(null);
    expect(stale).toHaveBeenCalledTimes(3);
  });
});
