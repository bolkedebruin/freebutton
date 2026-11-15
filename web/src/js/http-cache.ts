/** Buckets of the raw media partition (docs/MEDIA-STORE.md). */
const MEDIA_BUCKETS = ['images', 'fonts'];

/**
 * The HTTP cache mode of a device request. The device sends Last-Modified
 * without Cache-Control, so a browser may answer a GET from its cache
 * (heuristic freshness): a file then opens as it was before the last save.
 * Everything is fetched fresh, except the objects of the media buckets
 * (fonts up to 1.3 MB, image rasters): those keep the browser's cache, which
 * spares the device a download of each on every load of the IDE.
 */
export function cacheModeFor(method: string, path: string): RequestCache {
  if (method.toUpperCase() !== 'GET') return 'no-store';
  const m = /^\/([^/?]+)\/([^?]+)/.exec(path.startsWith('/') ? path : new URL(path, 'http://x').pathname);
  const media = m !== null && (MEDIA_BUCKETS.includes(m[1]));
  return media ? 'default' : 'no-store';
}
