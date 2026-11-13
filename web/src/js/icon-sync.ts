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
  const result: IconSyncResult = { ok: true, uploaded: [], removed: [], unchanged: [], failed: [], totalBytes: 0 };
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

  // Render. Keys of icons without a usable source keep what the device has.
  const keep = srcKeys(cfg);
  const rendered: RenderedRaster[] = [];
  const bytesByKey = new Map<string, Uint8Array>();
  let mono = 0;
  for (const spec of wanted) {
    const svg = svgs.get(spec.name);
    if (!svg) {
      keep.add(spec.key);
      result.failed.push(spec.key);
      continue;
    }
    try {
      const rgba = await deps.rasterize(svg, spec.w, spec.h);
      const { bytes, cf } = encodeLvglBin(rgba, spec.w, spec.h);
      if (cf === CF_A8) mono++;
      rendered.push({ key: spec.key, size: bytes.length, md5: md5Hex(bytes) });
      bytesByKey.set(spec.key, bytes);
      result.totalBytes += bytes.length;
    } catch (err) {
      deps.log(`Icon "${spec.name}" at ${spec.w}x${spec.h}: ${errorText(err)}`, 'error');
      keep.add(spec.key);
      result.failed.push(spec.key);
    }
  }

  const diff = diffRasters(rendered, existing, keep);
  result.unchanged = diff.unchanged;
  if (diff.upload.length > 0 || diff.remove.length > 0) {
    deps.log(`Syncing icons: ${diff.upload.length} to upload, ${diff.remove.length} to delete...`, 'info');
  }
  for (const key of diff.upload) {
    try {
      await deps.putImage(key, bytesByKey.get(key)!);
      result.uploaded.push(key);
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
