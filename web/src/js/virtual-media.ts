/**
 * Where the virtual device's media comes from: the font files (bucket
 * `fonts`) and image rasters (bucket `images`) the firmware in the page
 * reads to draw its displays (emulator/media.ts MediaCache: one per page,
 * made by emulator-session.ts when the virtual device is first drawn).
 *
 *   a device        its media store, with an S3 GetObject. Each bucket is
 *                   listed once: a file that is not listed is not asked for,
 *                   and one that is comes from the browser's cache when its
 *                   ETag is the listed one (device-storage sendGet), so a
 *                   font is downloaded once, not at every page load.
 *   an icon raster  that the device does not have (config.json not deployed
 *                   yet, or no device at all) is rendered here from the SVG
 *                   the config's "icons" table names, as an icon sync would
 *                   upload it.
 *   no device       no font files: the firmware draws text in its built-in
 *                   fallback font (hint(): said on the virtual device, not
 *                   an error).
 *
 * Nothing is read before the firmware asks for it. What the IDE uploads or
 * deletes later (a font, an icon sync) is passed on with changed(): the
 * virtual device follows as the real one does after an upload.
 *
 * A request that failed (the device was busy, the connection dropped) is not
 * an answer: the file is not held to be absent. It is asked for again a few
 * times (RETRY_AFTER_MS) and given to the virtual device when it arrives,
 * and every Run asks again for what is missing, with a fresh listing
 * (askAgain()): a font that reached the device after the page listed its
 * bucket is found without loading the page again.
 */
import { FONTS_BUCKET, IMAGES_BUCKET, getObjectBytes, listObjects } from './device-storage';
import { fontLoader } from './font-loader';
import { encodeLvglBin, iconTable, parseIconKey } from './icon-raster';
import { rasterizeSvg } from './icon-render';
import { iconSources, svgKeyFor } from './icon-sync';

export type MediaBucket = 'fonts' | 'images';

/** The part of emulator/media.ts MediaCache this feeds. */
export interface MediaCacheLike {
  set(bucket: MediaBucket, key: string, data: Uint8Array | null): void;
  forget(bucket: MediaBucket, key: string): void;
  clear(bucket?: MediaBucket): void;
}

/** When a file whose request failed is asked for again: after the failure, then after each failed retry. */
export const RETRY_AFTER_MS: readonly number[] = [2000, 6000, 20000];

export interface VirtualMediaDeps {
  list(base: string, bucket: MediaBucket): Promise<string[]>;
  get(base: string, bucket: MediaBucket, key: string): Promise<Uint8Array | null>;
  rasterize(svg: string, w: number, h: number): Promise<Uint8ClampedArray | Uint8Array>;
  /** Keys of bucket `fonts` from the listing the preview fonts already make (font-loader.ts); null: there is none. */
  knownFonts(): Promise<ReadonlySet<string> | null>;
}

const DEFAULT_DEPS: VirtualMediaDeps = {
  list: async (base, bucket) => (await listObjects(base, { bucket })).objects.map(o => o.key),
  get: async (base, bucket, key) => (await getObjectBytes(base, key, bucket))?.bytes ?? null,
  rasterize: rasterizeSvg,
  knownFonts: () => fontLoader.listedKeys(),
};

/** Dev server only: a URL prefix media is fetched from ("<prefix>fonts/medium.ttf"), kept in localStorage under this key. */
export const DEV_MEDIA_KEY = 'fb-dev-media';

export class VirtualMedia {
  private base: string | null = null;
  private cache: MediaCacheLike | null = null;
  private listings = new Map<MediaBucket, Promise<Set<string> | null>>();
  private icons = new Map<string, string>();
  private readText: ((key: string) => Promise<string | null>) | null = null;
  private missing = new Set<string>();
  /** Of `missing`: the request failed, so whether the device has the file is not known. */
  private failed = new Set<string>();
  private retries = new Map<string, ReturnType<typeof setTimeout>>();
  /** Buckets this lists itself from now on: the listing made elsewhere was not up to date. */
  private ownListing = new Set<MediaBucket>();
  /** Counts the changes of where media comes from: a display drawn before is out of date. */
  generation = 0;
  /** Called when a display drawn before is out of date (set by emulator-session.ts). */
  onStale: (() => void) | null = null;

  constructor(private deps: VirtualMediaDeps = DEFAULT_DEPS, private retryAfterMs: readonly number[] = RETRY_AFTER_MS) {}

  /** The page's MediaCache, once there is one. */
  attach(cache: MediaCacheLike | null): void {
    this.cache = cache;
  }

  /** The device the media comes from (null: none). Another device: everything is asked again. */
  setDevice(base: string | null): void {
    if (base === this.base) return;
    this.base = base;
    this.listings.clear();
    this.ownListing.clear();
    this.missing.clear();
    this.failed.clear();
    this.stopRetries();
    this.cache?.clear();
    this.generation++;
    this.onStale?.();
  }

  /** An SVG of the workspace changed: a raster rendered here from it is out of date. */
  iconsChanged(): void {
    this.cache?.clear(IMAGES_BUCKET);
    this.generation++;
    this.onStale?.();
  }

  /** The config the virtual device is about to draw (its "icons" table) and how a workspace file is read. */
  useConfig(config: unknown, readText: (key: string) => Promise<string | null>): void {
    this.icons = iconTable(config);
    this.readText = readText;
  }

  /** The IDE wrote (bytes) or deleted (null) a file of the device's media store. */
  changed(bucket: MediaBucket, key: string, bytes: Uint8Array | null): void {
    const listing = this.listings.get(bucket);
    if (listing) {
      this.listings.set(bucket, listing.then((keys) => {
        if (bytes) keys?.add(key); else keys?.delete(key);
        return keys;
      }));
    }
    // A deleted file the firmware did not ask for is not missed.
    this.settled(bucket, key, bytes !== null || !this.missing.has(`${bucket}/${key}`));
    this.cache?.set(bucket, key, bytes);
  }

  /**
   * Run: what is missing is asked for again, from a listing made now. Not
   * without a device (nothing to ask) and not while nothing is missing.
   */
  askAgain(): void {
    if (this.base === null || this.missing.size === 0) return;
    const names = [...this.missing];
    this.missing.clear();
    this.failed.clear();
    this.stopRetries();
    for (const name of names) {
      const slash = name.indexOf('/');
      const bucket = name.slice(0, slash) as MediaBucket;
      this.listings.delete(bucket);
      this.ownListing.add(bucket);
      this.cache?.forget(bucket, name.slice(slash + 1));
    }
  }

  /** Font files the firmware asked for whose request failed: asked for again, their text is in the fallback font until then. */
  failedFonts(): string[] {
    return [...this.failed].filter(name => name.startsWith('fonts/')).map(name => name.slice(6)).sort();
  }

  /** Font files the firmware asked for that are nowhere: their text is in its fallback font. */
  missingFonts(): string[] {
    return [...this.missing].filter(name => name.startsWith('fonts/')).map(name => name.slice(6)).sort();
  }

  /** What to say about the media, '' when nothing: not an error. */
  hint(): string {
    const fonts = this.missingFonts();
    if (fonts.length === 0) return '';
    if (this.base === null) return 'No device: text is in the firmware\'s fallback font (the font files are on a device)';
    const failed = this.failedFonts();
    const absent = fonts.filter(name => !failed.includes(name));
    return [
      ...(failed.length ? [`Could not be read from the device: ${failed.join(', ')} (the firmware's fallback font shows; Run asks again)`] : []),
      ...(absent.length ? [`Not in the device's fonts: ${absent.join(', ')} (the firmware's fallback font shows)`] : []),
    ].join('. ');
  }

  /** emulator/media.ts MediaSource.read. */
  read = async (bucket: MediaBucket, key: string): Promise<Uint8Array | null> => {
    const base = this.base;
    let bytes: Uint8Array | null = null;
    let failed = false;
    try {
      if (import.meta.env.DEV) bytes = await devMedia(bucket, key);
      if (!bytes && base !== null && (await this.listed(base, bucket))?.has(key) !== false) {
        try {
          bytes = await this.deps.get(base, bucket, key);
        } catch {
          // No answer: the device may well have the file.
          failed = true;
        }
      }
      if (!bytes && bucket === IMAGES_BUCKET) bytes = await this.renderIcon(key);
    } catch {
      bytes = null;
    }
    if (base !== this.base) return null;
    this.settled(bucket, key, bytes !== null, failed);
    if (!bytes && failed && base !== null) this.retryLater(base, bucket, key, 0);
    return bytes;
  };

  /** What is known of a file now: here, absent, or (failed) not known. */
  private settled(bucket: MediaBucket, key: string, here: boolean, failed = false): void {
    const name = `${bucket}/${key}`;
    if (here) this.missing.delete(name); else this.missing.add(name);
    if (!here && failed) this.failed.add(name); else this.failed.delete(name);
    const retry = this.retries.get(name);
    if (retry !== undefined) {
      clearTimeout(retry);
      this.retries.delete(name);
    }
  }

  /** Asks the device again for a file whose request failed; when it comes, the virtual device gets it as after an upload. */
  private retryLater(base: string, bucket: MediaBucket, key: string, attempt: number): void {
    const after = this.retryAfterMs[attempt];
    if (after === undefined) return;
    const name = `${bucket}/${key}`;
    this.retries.set(name, setTimeout(() => {
      this.retries.delete(name);
      void this.deps.get(base, bucket, key).then(
        (bytes) => {
          if (base !== this.base || !this.failed.has(name) || this.retries.has(name)) return;
          if (bytes) this.changed(bucket, key, bytes);
          else this.settled(bucket, key, false);
        },
        () => {
          if (base === this.base && this.failed.has(name) && !this.retries.has(name)) this.retryLater(base, bucket, key, attempt + 1);
        },
      );
    }, after));
  }

  private stopRetries(): void {
    for (const retry of this.retries.values()) clearTimeout(retry);
    this.retries.clear();
  }

  /** The keys of a bucket (asked once per device); null: it could not be listed (the file is then asked for). */
  private listed(base: string, bucket: MediaBucket): Promise<Set<string> | null> {
    let listing = this.listings.get(bucket);
    if (!listing) {
      const own = (): Promise<Set<string> | null> => this.deps.list(base, bucket).then(keys => new Set(keys), () => null);
      listing = bucket === FONTS_BUCKET && !this.ownListing.has(bucket)
        ? this.deps.knownFonts().then(known => (known ? new Set(known) : own()), own)
        : own();
      this.listings.set(bucket, listing);
    }
    return listing;
  }

  /** "ico-<name>-<w>x<h>.bin" from the SVG of the config's icon <name>, as an icon sync makes it. */
  private async renderIcon(key: string): Promise<Uint8Array | null> {
    const spec = parseIconKey(key);
    if (!spec) return null;
    const path = this.icons.get(spec.name);
    let svg = iconSources().get(spec.name) ?? null;
    if (!svg && path && this.readText) svg = await this.readText(svgKeyFor(path));
    if (!svg) return null;
    const rgba = await this.deps.rasterize(svg, spec.w, spec.h);
    return encodeLvglBin(rgba, spec.w, spec.h).bytes;
  }
}

async function devMedia(bucket: MediaBucket, key: string): Promise<Uint8Array | null> {
  let prefix: string | null = null;
  try { prefix = localStorage.getItem(DEV_MEDIA_KEY); } catch { /* no storage */ }
  if (!prefix) return null;
  const response = await fetch(`${prefix}${bucket}/${encodeURIComponent(key)}`);
  return response.ok ? new Uint8Array(await response.arrayBuffer()) : null;
}

export const virtualMedia = new VirtualMedia();
