/**
 * The device's fonts in the browser, for the previews of the virtual device.
 *
 * For every face of the font table the preview shows (device-config
 * getFontTable: the system fonts + config.json "fonts"), the TTF is read from the
 * device's `fonts` bucket with an S3 GetObject (device-storage
 * getObjectBytes; a plain fetch of /fonts/x.ttf would hit the IDE's own
 * static files) and registered as `new FontFace("fb-<name>", bytes)` in
 * document.fonts. A preview then sets the font-family to the face's chain,
 * e.g. "fb-roboto", "fb-mdi", Montserrat, sans-serif: the browser falls
 * back per glyph, like LVGL does (fontCss).
 *
 * Caching: one ListObjectsV2 of `fonts` gives every key's ETag (the media
 * store's ETag is the MD5 of the bytes). The bytes are kept in IndexedDB
 * keyed by key + ETag, so a face is downloaded again only when its file
 * changed (an icon font such as icons.ttf is ~1.3 MB: once). Cache Storage is not used: it only
 * exists in secure contexts and the device serves the IDE over plain http.
 * Downloads go one at a time (the device has little RAM).
 *
 * Without a device, FontFace support, or a file in the bucket, nothing is
 * registered and the previews keep the CSS fallback (Montserrat /
 * sans-serif). Emits `preview-fonts-change` when a face got (re)registered.
 */
import { emit, on } from './app-state';
import { getObjectBytes, listObjects, FONTS_BUCKET, type FsObject } from './device-storage';
import { getFontTable } from './device-config';
import { fontChain, FACE_SYSTEM, FACE_SYSTEM_BOLD, type FontTable } from './font-config';

export interface FontBytesCache {
  get(key: string, etag: string): Promise<Uint8Array | null>;
  put(key: string, etag: string, bytes: Uint8Array): Promise<void>;
}

export interface FontLoaderDeps {
  /** The browser can register fonts (FontFace + document.fonts); else nothing is downloaded. */
  supported(): boolean;
  list(base: string): Promise<FsObject[]>;
  get(base: string, key: string): Promise<{ bytes: Uint8Array; etag?: string } | null>;
  cache: FontBytesCache | null;
  /** Register `bytes` as font-family `family`; replaces an earlier registration. Rejects when the browser refuses the font. */
  register(family: string, bytes: Uint8Array): Promise<void>;
  log?(text: string): void;
}

/** CSS font-family name of a face. */
export function familyName(face: string): string {
  return `fb-${face}`;
}

interface Loaded {
  src: string;
  etag: string;
}

export class FontLoader {
  private base: string | null = null;
  private listing: Map<string, string> | null = null;
  private listingFailed = false;
  private loaded = new Map<string, Loaded>();
  /** Faces whose load failed for this src+etag (not retried until it changes). */
  private failed = new Map<string, string>();
  private queue: Promise<void> = Promise.resolve();
  private wanted = '';

  constructor(private readonly deps: FontLoaderDeps) {}

  /** The device the fonts come from (null = none: previews use the CSS fallback). */
  setDevice(base: string | null): Promise<void> {
    if (base === this.base) return this.refresh();
    this.base = base;
    this.listing = null;
    this.listingFailed = false;
    this.failed.clear();
    this.wanted = '';
    return this.refresh();
  }

  /** A fresh listing of bucket `fonts` (after an upload / delete or an Assets listing). */
  setListing(objects: readonly FsObject[]): Promise<void> {
    this.listing = new Map(objects.map(o => [o.key, o.etag ?? '']));
    this.listingFailed = false;
    this.wanted = '';
    return this.refresh();
  }

  /** Keys of bucket `fonts` as last listed; null = not listed (no device, old firmware). */
  bucketKeys(): ReadonlySet<string> | null {
    return this.listing ? new Set(this.listing.keys()) : null;
  }

  /**
   * Keys of bucket `fonts`, listed now when they were not yet (the one
   * listing this loader makes; resolves after the faces it is loading).
   * Null: no device, or it could not be listed.
   */
  async listedKeys(): Promise<ReadonlySet<string> | null> {
    await this.refresh();
    return this.bucketKeys();
  }

  /** Whether "fb-<face>" is registered (with the file the table names). */
  isLoaded(face: string, src?: string): boolean {
    const l = this.loaded.get(face);
    return !!l && (src === undefined || l.src === src);
  }

  /** Bring the registered faces in line with `table` (sequential, deduplicated). */
  refresh(table: FontTable = getFontTable()): Promise<void> {
    const sig = `${this.base}|${table.faces.map(f => `${f.name}=${f.src}`).join(',')}`;
    if (sig === this.wanted) return this.queue;
    this.wanted = sig;
    this.queue = this.queue.then(() => this.run(table)).catch(() => {});
    return this.queue;
  }

  private async run(table: FontTable): Promise<void> {
    const base = this.base;
    if (base === null || !this.deps.supported()) return;
    if (!this.listing && !this.listingFailed) {
      try {
        const objects = await this.deps.list(base);
        if (base !== this.base) return;
        this.listing = new Map(objects.map(o => [o.key, o.etag ?? '']));
      } catch (err) {
        this.listingFailed = true;
        this.deps.log?.(`Preview fonts: listing bucket fonts failed (${err instanceof Error ? err.message : String(err)}); the previews use fallback fonts`);
        return;
      }
    }
    const listing = this.listing;
    if (!listing) return;
    let changed = false;
    for (const face of table.faces) {
      if (base !== this.base) return;
      if (!listing.has(face.src)) continue;
      const etag = listing.get(face.src) ?? '';
      const have = this.loaded.get(face.name);
      if (have && have.src === face.src && have.etag === etag && etag !== '') continue;
      if (this.failed.get(face.name) === `${face.src}|${etag}`) continue;
      try {
        let bytes = etag && this.deps.cache ? await this.deps.cache.get(face.src, etag).catch(() => null) : null;
        let gotEtag = etag;
        if (!bytes) {
          const got = await this.deps.get(base, face.src);
          if (!got) continue;
          bytes = got.bytes;
          gotEtag = got.etag ?? etag;
          if (gotEtag && this.deps.cache) await this.deps.cache.put(face.src, gotEtag, bytes).catch(() => {});
        }
        await this.deps.register(familyName(face.name), bytes);
        this.loaded.set(face.name, { src: face.src, etag: gotEtag });
        this.failed.delete(face.name);
        changed = true;
      } catch (err) {
        this.failed.set(face.name, `${face.src}|${etag}`);
        this.deps.log?.(`Preview font "${face.name}" (${face.src}): ${err instanceof Error ? err.message : String(err)}`);
      }
    }
    if (changed) emit('preview-fonts-change');
  }
}

// ---- CSS for the previews ----------------------------------------------------

export interface FontCss {
  /** font-family value. */
  family: string;
  /** font-weight for the CSS fallback (registered faces are drawn as they are: 400). */
  weight: number;
  /** line-height in em from the face's "lineheight", if set. */
  lineHeight?: number;
}

const CSS_FALLBACK = 'Montserrat, sans-serif';

/**
 * font-family for a face: its fallback chain (the faces that are loaded),
 * then the CSS fallback. Without a registered first face, the weight
 * imitates the system font (montserrat 500, montserrat_semibold 600).
 */
export function fontCss(table: FontTable, face: number, loader: Pick<FontLoader, 'isLoaded'> = fontLoader): FontCss {
  const chain = fontChain(table, face);
  const families = chain
    .map(i => table.faces[i]!)
    .filter(f => loader.isLoaded(f.name, f.src))
    .map(f => `"${familyName(f.name)}"`);
  const first = table.faces[chain[0] ?? FACE_SYSTEM];
  const firstLoaded = !!first && loader.isLoaded(first.name, first.src);
  const lh = first?.lineheight;
  return {
    family: [...families, CSS_FALLBACK].join(', '),
    weight: firstLoaded ? 400 : (chain[0] === FACE_SYSTEM_BOLD ? 600 : 500),
    ...(lh !== undefined ? { lineHeight: lh } : {}),
  };
}

// ---- default dependencies --------------------------------------------------------

const DB_NAME = 'freebutton-fonts';
const STORE = 'fonts';

function openDb(): Promise<IDBDatabase | null> {
  if (typeof indexedDB === 'undefined') return Promise.resolve(null);
  return new Promise(resolve => {
    try {
      const req = indexedDB.open(DB_NAME, 1);
      req.onupgradeneeded = () => req.result.createObjectStore(STORE);
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => resolve(null);
    } catch {
      resolve(null);
    }
  });
}

/** IndexedDB: one record per bucket key, {etag, bytes}; a new ETag replaces the old bytes. */
export function indexedDbFontCache(): FontBytesCache {
  let db: Promise<IDBDatabase | null> | null = null;
  const store = async (mode: IDBTransactionMode) => {
    db ??= openDb();
    const d = await db;
    return d ? d.transaction(STORE, mode).objectStore(STORE) : null;
  };
  const wrap = <T>(req: IDBRequest<T>) => new Promise<T>((resolve, reject) => {
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  return {
    async get(key, etag) {
      const s = await store('readonly');
      if (!s) return null;
      const rec = await wrap(s.get(key)) as { etag?: string; bytes?: Uint8Array } | undefined;
      return rec && rec.etag === etag && rec.bytes instanceof Uint8Array ? rec.bytes : null;
    },
    async put(key, etag, bytes) {
      const s = await store('readwrite');
      if (s) await wrap(s.put({ etag, bytes }, key));
    },
  };
}

const registered = new Map<string, FontFace>();

function fontFaceSupported(): boolean {
  return typeof FontFace !== 'undefined' && typeof document !== 'undefined' && !!document.fonts;
}

async function registerFontFace(family: string, bytes: Uint8Array): Promise<void> {
  const face = new FontFace(family, bytes.slice().buffer as ArrayBuffer);
  await face.load();
  const old = registered.get(family);
  if (old) document.fonts.delete(old);
  document.fonts.add(face);
  registered.set(family, face);
}

export const fontLoader = new FontLoader({
  supported: fontFaceSupported,
  list: async (base) => (await listObjects(base, { bucket: FONTS_BUCKET })).objects,
  get: (base, key) => getObjectBytes(base, key, FONTS_BUCKET),
  cache: indexedDbFontCache(),
  register: registerFontFace,
  log: (text) => emit('console-line', { to: 'device', text, type: 'warn' }),
});

/** Keys of the device's fonts bucket as last listed (null = unknown). For the config linter. */
export function fontBucketKeys(): ReadonlySet<string> | null {
  return fontLoader.bucketKeys();
}

let wired = false;

/**
 * Follow the previewed config (device-config emits device-config on every
 * change; a new font table loads its faces). Called by the workspace once a
 * device is known.
 */
export function wireFontLoader(): void {
  if (wired || typeof window === 'undefined') return;
  wired = true;
  on('device-config', () => { void fontLoader.refresh(); });
}
