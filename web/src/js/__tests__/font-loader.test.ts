import { describe, expect, it, vi } from 'vitest';
import { FontLoader, familyName, fontCss, type FontBytesCache, type FontLoaderDeps } from '../font-loader';
import { fontTable, FACE_SYSTEM, FACE_SYSTEM_BOLD } from '../font-config';
import type { FsObject } from '../device-storage';

function memoryCache(): FontBytesCache & { data: Map<string, { etag: string; bytes: Uint8Array }> } {
  const data = new Map<string, { etag: string; bytes: Uint8Array }>();
  return {
    data,
    async get(key, etag) { const r = data.get(key); return r && r.etag === etag ? r.bytes : null; },
    async put(key, etag, bytes) { data.set(key, { etag, bytes }); },
  };
}

function setup(objects: FsObject[], over: Partial<FontLoaderDeps> = {}) {
  const cache = memoryCache();
  const registered: string[] = [];
  const get = vi.fn(async (_base: string, key: string) => {
    const o = objects.find(x => x.key === key);
    return o ? { bytes: new Uint8Array([key.length]), ...(o.etag ? { etag: o.etag } : {}) } : null;
  });
  const list = vi.fn(async () => objects);
  const log = vi.fn();
  const deps: FontLoaderDeps = {
    supported: () => true,
    list,
    get,
    cache,
    register: async (family: string) => { registered.push(family); },
    log,
    ...over,
  };
  return { loader: new FontLoader(deps), deps: { get, list: (over.list ?? list) as typeof list, log }, cache, registered };
}

const BUCKET_OBJECTS: FsObject[] = [
  { key: 'medium.ttf', size: 1, etag: '"m1"' },
  { key: 'semibold.ttf', size: 1, etag: '"s1"' },
  { key: 'icons.ttf', size: 1, etag: '"i1"' },
];
/** The system fonts plus an icon font from style.fonts. */
const MDI = { style: { fonts: [{ name: 'mdi', src: 'icons.ttf' }] } };

describe('FontLoader', () => {
  it('lists the bucket once, downloads each face once and registers fb-<name>', async () => {
    const { loader, deps, registered } = setup(BUCKET_OBJECTS);
    const table = fontTable(MDI);
    await loader.setDevice('http://dev');
    await loader.refresh(table);
    expect(deps.list).toHaveBeenCalledTimes(1);
    expect(deps.get.mock.calls.map((c: unknown[]) => c[1])).toEqual(['medium.ttf', 'semibold.ttf', 'icons.ttf']);
    expect(registered).toEqual(['fb-montserrat', 'fb-montserrat_semibold', 'fb-mdi']);
    expect(loader.isLoaded('mdi', 'icons.ttf')).toBe(true);
    expect(loader.bucketKeys()).toEqual(new Set(['medium.ttf', 'semibold.ttf', 'icons.ttf']));

    // Same table again: nothing happens.
    await loader.refresh(table);
    expect(deps.get).toHaveBeenCalledTimes(3);
  });

  it('loads the system fonts always; icons.ttf only when style.fonts names it', async () => {
    const { loader, deps, registered } = setup(BUCKET_OBJECTS);
    await loader.setDevice('');
    await loader.refresh(fontTable({}));
    expect(deps.get.mock.calls.map((c: unknown[]) => c[1])).toEqual(['medium.ttf', 'semibold.ttf']);
    expect(registered).toEqual(['fb-montserrat', 'fb-montserrat_semibold']);
  });

  it('serves unchanged ETags from the cache (no download), re-downloads a changed one', async () => {
    const objects = [...BUCKET_OBJECTS];
    const first = setup(objects);
    await first.loader.setDevice('');
    await first.loader.refresh(fontTable(MDI));
    // A new session (new loader) with the same cache: no GetObject at all.
    const second = setup(objects, { cache: first.cache });
    await second.loader.setDevice('');
    await second.loader.refresh(fontTable(MDI));
    expect(second.deps.get).not.toHaveBeenCalled();
    expect(second.registered).toEqual(['fb-montserrat', 'fb-montserrat_semibold', 'fb-mdi']);

    // icons.ttf replaced on the device: only it is fetched again.
    objects[2] = { key: 'icons.ttf', size: 1, etag: '"i2"' };
    await second.loader.setListing(objects);
    await second.loader.refresh(fontTable(MDI));
    expect(second.deps.get.mock.calls.map((c: unknown[]) => c[1])).toEqual(['icons.ttf']);
    expect(first.cache.data.get('icons.ttf')!.etag).toBe('"i2"');
  });

  it('loads config faces; skips files not in the bucket; a system font keeps its file', async () => {
    const { loader, registered } = setup([...BUCKET_OBJECTS, { key: 'roboto.ttf', size: 1, etag: '"r"' }]);
    await loader.setDevice('');
    await loader.refresh(fontTable({ style: { fonts: [{ name: 'roboto', src: 'roboto.ttf' }, { name: 'mono', src: 'missing.ttf' }] } }));
    expect(registered).toEqual(['fb-montserrat', 'fb-montserrat_semibold', 'fb-roboto']);
    expect(loader.isLoaded('mono')).toBe(false);

    // A src on a system font entry is ignored.
    await loader.refresh(fontTable({ style: { fonts: [{ name: 'montserrat', src: 'roboto.ttf' }] } }));
    expect(loader.isLoaded('montserrat', 'medium.ttf')).toBe(true);
    expect(registered.filter(r => r === 'fb-montserrat')).toHaveLength(1);
  });

  it('does nothing without a device or FontFace support', async () => {
    const a = setup(BUCKET_OBJECTS);
    await a.loader.setDevice(null);
    await a.loader.refresh(fontTable({}));
    expect(a.deps.list).not.toHaveBeenCalled();
    const b = setup(BUCKET_OBJECTS, { supported: () => false });
    await b.loader.setDevice('');
    await b.loader.refresh(fontTable({}));
    expect(b.deps.list).not.toHaveBeenCalled();
    expect(b.loader.bucketKeys()).toBeNull();
  });

  it('a failed listing (old firmware) leaves the CSS fallback, logged once', async () => {
    const { loader, deps } = setup([], { list: vi.fn(async () => { throw new Error('NoSuchBucket'); }) });
    await loader.setDevice('');
    await loader.refresh(fontTable({ style: { fonts: [{ name: 'x', src: 'x.ttf' }] } }));
    expect(deps.log).toHaveBeenCalledTimes(1);
    expect(loader.bucketKeys()).toBeNull();
  });

  it('a font the browser refuses is not retried until its file changes', async () => {
    const register = vi.fn(async (family: string) => { if (family === 'fb-mdi') throw new Error('bad font'); });
    const { loader, deps } = setup(BUCKET_OBJECTS, { register });
    await loader.setDevice('');
    await loader.refresh(fontTable(MDI));
    expect(loader.isLoaded('mdi')).toBe(false);
    expect(deps.log).toHaveBeenCalledWith(expect.stringContaining('bad font'));
    await loader.refresh(fontTable({ style: { fonts: [...MDI.style.fonts, { name: 'montserrat', fallback: 'mdi' }] } }));
    expect(deps.get.mock.calls.filter((c: unknown[]) => c[1] === 'icons.ttf')).toHaveLength(1);
  });
});

describe('fontCss', () => {
  const table = fontTable({ style: { fonts: [
    { name: 'roboto', src: 'roboto.ttf', fallback: 'mono', lineheight: 1.1 }, { name: 'mono', src: 'm.ttf', fallback: 'mdi' },
    { name: 'mdi', src: 'icons.ttf' }, { name: 'montserrat_semibold', fallback: 'mdi' },
  ] } });
  const loaded = (names: string[]) => ({ isLoaded: (n: string) => names.includes(n) });

  it('lists the loaded faces of the chain, then the CSS fallback', () => {
    expect(fontCss(table, 2, loaded(['roboto', 'mono', 'mdi']))).toEqual({
      family: '"fb-roboto", "fb-mono", "fb-mdi", Montserrat, sans-serif', weight: 400, lineHeight: 1.1,
    });
    expect(fontCss(table, 2, loaded(['mdi'])).family).toBe('"fb-mdi", Montserrat, sans-serif');
  });

  it('without the device fonts the system fonts imitate their weights', () => {
    expect(fontCss(table, FACE_SYSTEM, loaded([]))).toEqual({ family: 'Montserrat, sans-serif', weight: 500 });
    expect(fontCss(table, FACE_SYSTEM_BOLD, loaded([]))).toEqual({ family: 'Montserrat, sans-serif', weight: 600 });
    expect(fontCss(table, FACE_SYSTEM_BOLD, loaded(['montserrat_semibold', 'mdi'])).weight).toBe(400);
    expect(fontCss(table, FACE_SYSTEM_BOLD, loaded(['mdi'])).family).toBe('"fb-mdi", Montserrat, sans-serif');
    // No fallback by default: montserrat's chain is montserrat only.
    expect(fontCss(table, FACE_SYSTEM, loaded(['mdi'])).family).toBe('Montserrat, sans-serif');
  });

  it('familyName', () => {
    expect(familyName('roboto')).toBe('fb-roboto');
  });
});
