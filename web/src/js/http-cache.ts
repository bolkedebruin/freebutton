/**
 * How device requests use the browser's HTTP cache.
 *
 * The device sends Last-Modified without Cache-Control, so a browser may
 * answer a GET from its cache for a while (heuristic freshness): a file then
 * opens as it was before the last save. So every request is fetched fresh
 * ('no-store'), except a GET of an object whose current ETag is known from a
 * listing: that one carries the ETag as `?v=<etag>` and may come from the
 * cache for good ('force-cache'). The URL changes when the object does, so a
 * cached copy is never an older version, and fonts (up to 1.3 MB), image
 * rasters and icon sources are downloaded once, not on every load of the IDE.
 */

/** Buckets of the raw media partition (docs/MEDIA-STORE.md): the ETag is the MD5. */
const MEDIA_BUCKETS = ['images', 'fonts'];
/** Files the editor changes: always read fresh. Their ETag (size and mtime in
 *  seconds) can stay the same across two saves within a second. */
const EDITED = /\.(js|json|txt)$/i;

/** The `v` value for a GET of `bucket`/`key` with the listing's `etag`, or undefined: fetch fresh. */
export function versionFor(bucket: string, key: string, etag: string | undefined): string | undefined {
  if (!etag) return undefined;
  if (!MEDIA_BUCKETS.includes(bucket) && EDITED.test(key)) return undefined;
  const v = etag.replace(/[^0-9A-Za-z-]/g, '');
  return v || undefined;
}

/** The cache mode of a request; `versioned`: it carries `?v=`. */
export function cacheModeFor(method: string, versioned: boolean): RequestCache {
  return versioned && method.toUpperCase() === 'GET' ? 'force-cache' : 'no-store';
}
