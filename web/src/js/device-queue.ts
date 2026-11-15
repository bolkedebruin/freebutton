/**
 * One request to the device at a time.
 *
 * The device has very little free memory (about 40 KB), and every request it
 * handles at the same moment takes several KB of it: with the IDE's start-up
 * requests all in flight (file listing, device info, config, images, fonts)
 * it ran out and restarted. Everything the IDE itself sends to the device
 * (storage requests and API calls) goes through this queue, in the order it
 * was asked for. The browser's own loading of the IDE's files and the console
 * connection are not part of it.
 *
 * A task must not wait for another queued task: that would never finish.
 */
let tail: Promise<void> | null = null;
let waiting = 0;
let settled: Promise<void> | null = null;

/**
 * Resolves when the browser has the page's own files: the `load` event, one
 * frame for the first render, and the web fonts that render asked for (at
 * most 8 s in all). While the browser still fetches those from the device,
 * a request of the IDE's would be one response too many for the device's
 * memory. The first queued request and the console connection wait for it.
 */
export function pageSettled(): Promise<void> {
  if (settled) return settled;
  if (typeof document === 'undefined' || typeof window === 'undefined') return (settled = Promise.resolve());
  const loaded = document.readyState === 'complete'
    ? Promise.resolve()
    : new Promise<void>(r => window.addEventListener('load', () => r(), { once: true }));
  const frame = () => new Promise<void>(r =>
    (typeof requestAnimationFrame === 'function' ? requestAnimationFrame(() => r()) : r()));
  const fonts = loaded.then(frame).then(frame)
    .then(() => (document.fonts ? document.fonts.ready : undefined))
    .then(() => undefined, () => undefined);
  const limit = new Promise<void>(r => setTimeout(r, 8000));
  return (settled = Promise.race([fonts, limit]));
}

export function oneAtATime<T>(task: () => Promise<T>): Promise<T> {
  if (!tail) tail = pageSettled();
  waiting++;
  const run = tail.then(() => { waiting--; return task(); });
  tail = run.then(() => undefined, () => undefined);
  return run;
}

/** Tasks that have not started yet (for tests). */
export function queuedDeviceRequests(): number {
  return waiting;
}
