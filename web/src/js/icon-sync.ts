/**
 * Icon sync: config.json `icons` -> rasters in the device's `images` bucket.
 *
 * For a parsed config (icon-raster.ts wantedRasters): read each SVG the
 * config references from bucket `app`, render every wanted size, compare
 * with the bucket listing (the media store's ETag is the MD5 of the stored
 * bytes, so an identical raster is skipped), PUT what is missing or
 * changed and DELETE the `ico-*` keys the config no longer wants. Other keys
 * are user files and are never touched. Rasters of an icon whose SVG cannot
 * be read or rendered are left as they are (not deleted).
 *
 * Flash wear: a raster is written only when its SOURCE changed. Two browsers
 * render the same SVG to slightly different pixels, so the rendered bytes
 * cannot decide that. A small manifest on the device (app key
 * ICON_MANIFEST_KEY) records for each raster the MD5 of the SVG text it was
 * made from; a raster with the same source and size is kept without being
 * rendered. A raster the manifest does not know yet is kept when the device
 * has one of the right size (it was made by an IDE without the manifest).
 * The manifest itself is written only when it changes.
 *
 * Requests go one at a time (the device has very little RAM). Everything
 * the sync does is reported through `log` (the console pane).
 *
 * The SVG texts read here are kept (iconSources) for the virtual device
 * preview, which emits `icon-sources-change` when they change.
 */
import { emit, type LogLevel } from './app-state';
import {
  diffRasters, encodeLvglBin, iconTable, srcKeys, wantedRasters,
  CF_A8, type ExistingObject, type RenderedRaster,
} from './icon-raster';
import { svgDataUrl } from './icon-render';
import { md5Hex } from './md5';

/** `app` key of the raster manifest (JSON, see RasterManifest). */
export const ICON_MANIFEST_KEY = 'icons/rasters.json';
/** Part of every source fingerprint: raise it when the encoding of a raster changes. */
const RASTER_FORMAT = 1;

/** Raster key -> the source it was made from and the size it has on the device. */
export interface RasterManifest {
  v: 1;
  rasters: Record<string, { src: string; size: number }>;
}

/** The manifest of a JSON text; empty for no text or anything that is not one. */
export function parseManifest(text: string | null | undefined): RasterManifest {
  const empty: RasterManifest = { v: 1, rasters: {} };
  if (!text) return empty;
  try {
    const doc = JSON.parse(text) as { v?: unknown; rasters?: unknown };
    if (doc?.v !== 1 || !doc.rasters || typeof doc.rasters !== 'object') return empty;
    for (const [key, e] of Object.entries(doc.rasters as Record<string, { src?: unknown; size?: unknown }>)) {
      if (e && typeof e.src === 'string' && typeof e.size === 'number') empty.rasters[key] = { src: e.src, size: e.size };
    }
  } catch { /* not JSON: start again */ }
  return empty;
}

/** Fingerprint of a raster's source: the SVG text and the raster format. */
export function sourceFingerprint(svg: string): string {
  return `${RASTER_FORMAT}:${md5Hex(new TextEncoder().encode(svg))}`;
}

function manifestText(m: RasterManifest): string {
  const rasters: RasterManifest['rasters'] = {};
  for (const key of Object.keys(m.rasters).sort()) rasters[key] = m.rasters[key];
  return JSON.stringify({ v: 1, rasters });
}

export interface IconSyncDeps {
  /** SVG text of an `app` key ("icons/rain.svg"); null when it does not exist. */
  readSvg(key: string): Promise<string | null>;
  /** ListObjectsV2 of bucket `images` (throws S3Error NoSuchBucket on old firmware). */
  listImages(): Promise<ExistingObject[]>;
  putImage(key: string, bytes: Uint8Array): Promise<void>;
  deleteImage(key: string): Promise<void>;
  /** Unpremultiplied RGBA, w * h * 4 bytes (icon-render.ts rasterizeSvg). */
  rasterize(svg: string, w: number, h: number): Promise<Uint8ClampedArray | Uint8Array>;
  log(text: string, type: LogLevel): void;
  /** Text of the raster manifest, null when there is none. Without these two the rendered bytes decide. */
  readManifest?(): Promise<string | null>;
  writeManifest?(text: string): Promise<void>;
}

export interface IconSyncResult {
  /** False when something failed (details were logged). */
  ok: boolean;
  /** The device has no `images` bucket (firmware too old). */
  noBucket?: boolean;
  uploaded: string[];
  removed: string[];
  unchanged: string[];
  /** Keys that could not be rendered or written. */
  failed: string[];
  /** Of `unchanged`: kept because their source is the one in the manifest (not rendered). */
  sameSource: string[];
  /** Bytes of all wanted rasters. */
  totalBytes: number;
}

/** LittleFS path in `icons` -> `app` key ("/icons/a.svg" -> "icons/a.svg"). */
export function svgKeyFor(path: string): string {
  return path.trim().replace(/^\/+/, '');
}

function errorText(err: unknown): string {
  return err instanceof Error ? err.message : String(err);
}

function isNoSuchBucket(err: unknown): boolean {
  return typeof err === 'object' && err !== null && (err as { code?: string }).code === 'NoSuchBucket';
}

// ---- preview cache ----------------------------------------------------------

const sources = new Map<string, string>();

/** SVG texts by icon name, as last read from the device. */
export function iconSources(): ReadonlyMap<string, string> {
  return sources;
}

/** Replace the cached SVGs of these icon names (null = drop). */
export function setIconSources(entries: Iterable<[string, string | null]>): void {
  let changed = false;
  for (const [name, text] of entries) {
    if (text === null) {
      changed = sources.delete(name) || changed;
    } else if (sources.get(name) !== text) {
      sources.set(name, text);
      changed = true;
    }
  }
  if (changed) {
    urlCache.clear();
    emit('icon-sources-change');
  }
}

/** Test helper. */
export function _clearIconSources(): void {
  sources.clear();
  urlCache.clear();
}

/**
 * Read the SVG of every icon in the config's `icons` table (sequentially).
 * Missing / unreadable ones map to null; `log` gets a warning for each.
 */
export async function readIconSources(
  cfg: unknown,
  readSvg: IconSyncDeps['readSvg'],
  log?: IconSyncDeps['log'],
  only?: ReadonlySet<string>,
): Promise<Map<string, string | null>> {
  const out = new Map<string, string | null>();
  for (const [name, path] of iconTable(cfg)) {
    if (only && !only.has(name)) continue;
    try {
      const text = await readSvg(svgKeyFor(path));
      if (text === null) log?.(`Icon "${name}": ${path} not found on the device`, 'warn');
      out.set(name, text);
    } catch (err) {
      log?.(`Icon "${name}": reading ${path} failed: ${errorText(err)}`, 'warn');
      out.set(name, null);
    }
  }
  return out;
}

// ---- sync -----------------------------------------------------------------------

/**
 * Bring the `images` bucket in line with the config. `cfg` is the parsed
 * config.json. Never throws; the result and the log say what happened.
 */
export async function syncIcons(cfg: unknown, deps: IconSyncDeps): Promise<IconSyncResult> {
  const result: IconSyncResult = { ok: true, uploaded: [], removed: [], unchanged: [], failed: [], sameSource: [], totalBytes: 0 };
  const wanted = wantedRasters(cfg);

  let existing: ExistingObject[];
  try {
    existing = await deps.listImages();
  } catch (err) {
    if (isNoSuchBucket(err)) {
      result.noBucket = true;
      result.ok = wanted.length === 0;
      if (wanted.length > 0) {
        deps.log('Icons not synced: this firmware has no "images" storage bucket (update the device firmware)', 'error');
      }
      return result;
    }
    deps.log(`Icons not synced: listing bucket images failed: ${errorText(err)}`, 'error');
    result.ok = false;
    return result;
  }

  // Sources of the icons the config uses at some size.
  const names = new Set(wanted.map(r => r.name));
  const svgs = await readIconSources(cfg, deps.readSvg, deps.log, names);
  setIconSources(svgs);

  // The manifest: which source each raster on the device was made from.
  const useManifest = !!deps.readManifest && !!deps.writeManifest;
  let manifestBefore = '';
  let manifest: RasterManifest = { v: 1, rasters: {} };
  if (useManifest) {
    try {
      const text = await deps.readManifest!();
      manifest = parseManifest(text);
      manifestBefore = text === null ? '' : manifestText(manifest);
    } catch (err) {
      deps.log(`Icon manifest not read (${errorText(err)}): every raster is compared again`, 'warn');
    }
  }
  const have = new Map(existing.map(o => [o.key, o]));
  const next: RasterManifest = { v: 1, rasters: {} };

  // Render what may have changed. Keys of icons without a usable source keep
  // what the device has.
  const keep = srcKeys(cfg);
  const rendered: RenderedRaster[] = [];
  const bytesByKey = new Map<string, Uint8Array>();
  const sources = new Map<string, string>();
  let mono = 0;
  for (const spec of wanted) {
    const svg = svgs.get(spec.name);
    if (!svg) {
      keep.add(spec.key);
      result.failed.push(spec.key);
      if (manifest.rasters[spec.key]) next.rasters[spec.key] = manifest.rasters[spec.key];
      continue;
    }
    const src = sourceFingerprint(svg);
    sources.set(spec.key, src);
    const onDevice = have.get(spec.key);
    const known = manifest.rasters[spec.key];
    if (useManifest && onDevice && known && known.src === src && known.size === onDevice.size) {
      // Same source as when it was made: kept as it is, not rendered.
      keep.add(spec.key);
      result.sameSource.push(spec.key);
      result.totalBytes += onDevice.size;
      if (onDevice.size < spec.w * spec.h * 2) mono++;
      next.rasters[spec.key] = known;
      continue;
    }
    try {
      const rgba = await deps.rasterize(svg, spec.w, spec.h);
      const { bytes, cf } = encodeLvglBin(rgba, spec.w, spec.h);
      if (cf === CF_A8) mono++;
      result.totalBytes += bytes.length;
      if (useManifest && onDevice && !known && onDevice.size === bytes.length) {
        // Made by an IDE without the manifest (maybe another browser, whose
        // pixels differ a little): taken as made from this source.
        keep.add(spec.key);
        result.sameSource.push(spec.key);
        next.rasters[spec.key] = { src, size: onDevice.size };
        continue;
      }
      rendered.push({ key: spec.key, size: bytes.length, md5: md5Hex(bytes) });
      bytesByKey.set(spec.key, bytes);
    } catch (err) {
      deps.log(`Icon "${spec.name}" at ${spec.w}x${spec.h}: ${errorText(err)}`, 'error');
      keep.add(spec.key);
      result.failed.push(spec.key);
      if (known) next.rasters[spec.key] = known;
    }
  }

  const rasterEntry = (key: string) => ({ src: sources.get(key)!, size: bytesByKey.get(key)!.length });

  const diff = diffRasters(rendered, existing, keep);
  result.unchanged = [...result.sameSource, ...diff.unchanged];
  for (const key of diff.unchanged) {
    next.rasters[key] = rasterEntry(key);
  }
  if (diff.upload.length > 0 || diff.remove.length > 0) {
    deps.log(`Syncing icons: ${diff.upload.length} to upload, ${diff.remove.length} to delete...`, 'info');
  }
  for (const key of diff.upload) {
    try {
      await deps.putImage(key, bytesByKey.get(key)!);
      result.uploaded.push(key);
      next.rasters[key] = rasterEntry(key);
    } catch (err) {
      deps.log(`Upload of images/${key} failed: ${errorText(err)}`, 'error');
      result.failed.push(key);
    }
  }
  for (const key of diff.remove) {
    try {
      await deps.deleteImage(key);
      result.removed.push(key);
    } catch (err) {
      deps.log(`Delete of images/${key} failed: ${errorText(err)}`, 'error');
      result.failed.push(key);
    }
  }

  // The manifest follows what is on the device now; written only when it changed.
  if (useManifest) {
    const text = manifestText(next);
    const empty = Object.keys(next.rasters).length === 0;
    if (text !== manifestBefore && !(empty && manifestBefore === '')) {
      try {
        await deps.writeManifest!(text);
      } catch (err) {
        deps.log(`Icon manifest not written (${errorText(err)}): the next sync compares every raster again`, 'warn');
      }
    }
  }

  result.ok = result.failed.length === 0;
  if (wanted.length > 0 || result.removed.length > 0) {
    const parts = [
      `${wanted.length} raster${wanted.length === 1 ? '' : 's'} (${mono} single-colour, ${(result.totalBytes / 1024).toFixed(1)} KB)`,
      `${result.uploaded.length} uploaded`,
      `${result.unchanged.length} unchanged`,
      `${result.removed.length} deleted`,
    ];
    if (result.failed.length) parts.push(`${result.failed.length} failed`);
    deps.log(`Icons: ${parts.join(', ')}`, result.ok ? 'result' : 'warn');
  }
  return result;
}

// ---- preview URLs ------------------------------------------------------------

const urlCache = new Map<string, string>();

/**
 * data: URL of an icon's SVG fitted to w x h (contain), or null when its
 * source has not been read (or does not parse). For the virtual device.
 */
export function iconPreviewUrl(name: string, w: number, h: number): string | null {
  const svg = sources.get(name);
  if (!svg) return null;
  const id = `${name}\u0000${w}x${h}\u0000${svg.length}`;
  const hit = urlCache.get(id);
  if (hit) return hit;
  let url: string;
  try {
    url = svgDataUrl(svg, w, h);
  } catch {
    return null;
  }
  if (urlCache.size > 256) urlCache.clear();
  urlCache.set(id, url);
  return url;
}
