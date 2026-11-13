/**
 * Screenshots of the device's displays.
 *
 * POST /api/screenshot makes the device repaint every display once and write
 * what it flushes to bucket `app` as screenshot-<main|miniN>.rgb565: an 8-byte
 * header ("FBSS", uint16 LE width, uint16 LE height) and RGB565 big-endian
 * pixels (same format as tools/scripts/screenshot.py). A frame does not fit
 * the device's RAM, so the firmware has to write it somewhere; the IDE
 * downloads the files and deletes them from the device right away, so nothing
 * stays there.
 */
import { signedFetch } from './auth';
import { deleteObject, getObjectBytes, listObjects, type FsObject } from './device-storage';

export const SCREENSHOT_KEY = /^screenshot-([A-Za-z0-9_-]+)\.rgb565$/;
const HEADER_BYTES = 8;
const POLL_MS = 700;
const TIMEOUT_MS = 20000;
/** Polls without any change before the files count as complete. */
const STABLE_POLLS = 2;

export interface Screenshot {
  /** "main", "mini0", ... */
  name: string;
  width: number;
  height: number;
  /** RGBA, 4 bytes per pixel. */
  rgba: Uint8ClampedArray;
}

export function isScreenshotKey(key: string): boolean {
  return SCREENSHOT_KEY.test(key);
}

/** Decode a screenshot file; null when it is not one. Missing pixels are black. */
export function decodeScreenshot(name: string, bytes: Uint8Array): Screenshot | null {
  if (bytes.length < HEADER_BYTES || bytes[0] !== 0x46 || bytes[1] !== 0x42 || bytes[2] !== 0x53 || bytes[3] !== 0x53) return null;
  const width = bytes[4] | (bytes[5] << 8);
  const height = bytes[6] | (bytes[7] << 8);
  if (width < 1 || height < 1) return null;
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let i = 0, p = HEADER_BYTES; i < width * height; i++, p += 2) {
    const v = p + 1 < bytes.length ? (bytes[p] << 8) | bytes[p + 1] : 0;
    const r = (v >> 11) & 0x1f, g = (v >> 5) & 0x3f, b = v & 0x1f;
    rgba[i * 4] = (r << 3) | (r >> 2);
    rgba[i * 4 + 1] = (g << 2) | (g >> 4);
    rgba[i * 4 + 2] = (b << 3) | (b >> 2);
    rgba[i * 4 + 3] = 255;
  }
  return { name, width, height, rgba };
}

export interface CaptureDeps {
  fetchImpl?: typeof fetch;
  sleep?: (ms: number) => Promise<void>;
  now?: () => number;
}

function shotsOf(objects: FsObject[]): FsObject[] {
  return objects.filter(o => isScreenshotKey(o.key));
}

function signature(shots: FsObject[]): string {
  return shots.map(o => `${o.key}:${o.size}:${o.lastModified?.getTime() ?? ''}`).sort().join('|');
}

/**
 * Take a screenshot of every display: ask the device, wait until its files
 * stopped changing, download them and delete them from the device. The main
 * display comes first. Throws with a readable message on failure.
 */
export async function captureScreenshots(base: string, deps: CaptureDeps = {}): Promise<Screenshot[]> {
  const fetchImpl = deps.fetchImpl ?? signedFetch;
  const sleep = deps.sleep ?? ((ms: number) => new Promise<void>(r => setTimeout(r, ms)));
  const now = deps.now ?? (() => Date.now());

  // Files of an earlier run would make "complete" ambiguous: remove them first.
  // Firmware before the any-extension delete refuses this: then the old files
  // stay and only a changed list counts as the new capture.
  for (const old of shotsOf((await listObjects(base)).objects)) await deleteObject(base, old.key).catch(() => undefined);
  const before = signature(shotsOf((await listObjects(base)).objects));

  const res = await fetchImpl(base + '/api/screenshot', { method: 'POST' });
  if (!res.ok) throw new Error(`The device refused the screenshot (HTTP ${res.status})`);

  const deadline = now() + TIMEOUT_MS;
  let last = '';
  let stable = 0;
  let shots: FsObject[] = [];
  for (;;) {
    await sleep(POLL_MS);
    shots = shotsOf((await listObjects(base)).objects);
    const sig = signature(shots);
    stable = shots.length > 0 && sig !== before && sig === last ? stable + 1 : 0;
    last = sig;
    if (stable >= STABLE_POLLS) break;
    if (now() > deadline) throw new Error('Timed out waiting for the device to write the screenshot');
  }

  const out: Screenshot[] = [];
  try {
    for (const shot of shots) {
      const got = await getObjectBytes(base, shot.key);
      const decoded = got ? decodeScreenshot(SCREENSHOT_KEY.exec(shot.key)![1], got.bytes) : null;
      if (decoded) out.push(decoded);
    }
  } finally {
    // Never leave them on the device, also when a download failed.
    for (const shot of shots) await deleteObject(base, shot.key).catch(() => undefined);
  }
  if (out.length === 0) throw new Error('The device wrote no readable screenshot');
  return out.sort((a, b) => (a.name === 'main' ? -1 : b.name === 'main' ? 1 : a.name.localeCompare(b.name)));
}

/** Draw a screenshot on a canvas at its own size. */
export function drawScreenshot(canvas: HTMLCanvasElement, shot: Screenshot): void {
  canvas.width = shot.width;
  canvas.height = shot.height;
  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  const image = ctx.createImageData(shot.width, shot.height);
  image.data.set(shot.rgba);
  ctx.putImageData(image, 0, 0);
}

/** Save a canvas as <name>.png through the browser's download. */
export function saveCanvasPng(canvas: HTMLCanvasElement, fileName: string): void {
  const a = document.createElement('a');
  a.href = canvas.toDataURL('image/png');
  a.download = fileName;
  a.click();
}
