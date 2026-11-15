/**
 * Device update from the IDE, of one of two kinds:
 *   firmware    an app image, written to the device's next OTA slot;
 *   filesystem  a complete LittleFS image (pio buildfs). It replaces every
 *               file on the device: config.json, the scripts and the IDE.
 * Upload the file (POST /upload, multipart, `updateType` before the file),
 * apply it (POST /ota_update: the device restarts) and wait until the device
 * answers again. Progress is reported per phase; the upload has a
 * percentage (XMLHttpRequest, because fetch cannot report upload progress).
 *
 * Both requests are signed like every other write. A multipart body is sent
 * as UNSIGNED-PAYLOAD (the device cannot hash it), see auth.ts payloadHash.
 */
import { signatureHeaders } from './auth';
import { sleep as defaultSleep } from './sleep';

export type UpdateKind = 'firmware' | 'filesystem';
export type UpdatePhase = 'uploading' | 'applying' | 'done' | 'failed';

export interface UpdateProgress {
  phase: UpdatePhase;
  /** 0..100 while uploading. */
  percent?: number;
  /** What happened (done: the version now running; failed: why). */
  message?: string;
}

/** First byte of an ESP32 application image. */
const ESP_IMAGE_MAGIC = 0xe9;
const POLL_MS = 2000;
const BACK_TIMEOUT_MS = 120000;

export interface UpdateDeps {
  /** POST a multipart body; resolves with the HTTP status and body text. */
  upload?(url: string, body: FormData, headers: Record<string, string>, onPercent: (p: number) => void): Promise<{ status: number; text: string }>;
  fetchImpl?: typeof fetch;
  sign?: typeof signatureHeaders;
  sleep?(ms: number): Promise<void>;
  now?(): number;
}

interface DeviceState { uptime: number; firmware: string; commit: string }

function xhrUpload(url: string, body: FormData, headers: Record<string, string>, onPercent: (p: number) => void): Promise<{ status: number; text: string }> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', url);
    for (const [k, v] of Object.entries(headers)) xhr.setRequestHeader(k, v);
    xhr.upload.onprogress = (e) => { if (e.lengthComputable) onPercent(Math.round((e.loaded / e.total) * 100)); };
    xhr.onload = () => resolve({ status: xhr.status, text: xhr.responseText });
    xhr.onerror = () => reject(new Error('The connection to the device was lost during the upload'));
    xhr.send(body);
  });
}

// Not through the device queue (device-queue.ts): this polls a restarting device, and a request that hangs would hold up every queued one.
async function deviceState(base: string, fetchImpl: typeof fetch): Promise<DeviceState | null> {
  try {
    const res = await fetchImpl(`${base}/api/device`, { signal: AbortSignal.timeout(3000) });
    if (!res.ok) return null;
    const j = await res.json() as Record<string, unknown>;
    return { uptime: Number(j.uptime) || 0, firmware: String(j.firmware ?? ''), commit: String(j.commit ?? '') };
  } catch {
    return null;
  }
}

/** Why `file` cannot be an image of this kind, or null. */
export async function updateFileProblem(kind: UpdateKind, file: Blob & { name?: string }): Promise<string | null> {
  if (file.size === 0) return 'The file is empty.';
  const head = new Uint8Array(await file.slice(0, 16).arrayBuffer());
  if (kind === 'firmware') {
    return head[0] === ESP_IMAGE_MAGIC ? null : 'This is not an ESP32 firmware image (firmware.bin).';
  }
  // LittleFS superblock: "littlefs" at byte 8 of block 0.
  const magic = String.fromCharCode(...head.slice(8, 16));
  return magic === 'littlefs' ? null : 'This is not a LittleFS filesystem image (littlefs.bin).';
}

/**
 * Install `file` on the device. Reports every phase through `onProgress` and
 * resolves with the last one (done or failed); it does not throw.
 */
export async function updateDevice(
  baseUrl: string,
  kind: UpdateKind,
  file: Blob & { name?: string },
  onProgress: (p: UpdateProgress) => void,
  deps: UpdateDeps = {},
): Promise<UpdateProgress> {
  const base = baseUrl.replace(/\/+$/, '');
  const fetchImpl = deps.fetchImpl ?? fetch.bind(globalThis);
  const sign = deps.sign ?? signatureHeaders;
  const sleep = deps.sleep ?? defaultSleep;
  const now = deps.now ?? (() => Date.now());
  const absolute = (path: string) => (base || (typeof location !== 'undefined' ? location.origin : '')) + path;
  const end = (p: UpdateProgress) => { onProgress(p); return p; };

  const problem = await updateFileProblem(kind, file);
  if (problem) return end({ phase: 'failed', message: problem });

  const before = await deviceState(base, fetchImpl);

  // 1. Upload. The plain field goes first: the device reads it when the file starts.
  onProgress({ phase: 'uploading', percent: 0 });
  const form = new FormData();
  form.append('updateType', kind);
  form.append('updateFile', file, file.name ?? (kind === 'firmware' ? 'firmware.bin' : 'littlefs.bin'));
  try {
    const headers = (await sign(absolute('/upload'), 'POST', form)) ?? {};
    const res = await (deps.upload ?? xhrUpload)(`${base}/upload`, form, headers,
      (percent) => onProgress({ phase: 'uploading', percent }));
    if (res.status !== 200) {
      return end({ phase: 'failed', message: `The device refused the upload (HTTP ${res.status}${res.text ? ': ' + res.text.trim() : ''}). The device is unchanged.` });
    }
  } catch (err) {
    return end({ phase: 'failed', message: `${err instanceof Error ? err.message : String(err)}. The device is unchanged.` });
  }

  // 2. Apply: the device restarts, usually before it answers.
  onProgress({ phase: 'applying' });
  try {
    const headers = (await sign(absolute('/ota_update'), 'POST')) ?? {};
    await fetchImpl(`${base}/ota_update`, { method: 'POST', headers, signal: AbortSignal.timeout(10000) });
  } catch { /* no answer while it restarts: expected */ }

  // 3. Wait until it is back: it was unreachable once, or its uptime restarted.
  const deadline = now() + BACK_TIMEOUT_MS;
  let wasDown = false;
  for (;;) {
    await sleep(POLL_MS);
    const st = await deviceState(base, fetchImpl);
    if (!st) wasDown = true;
    else if (wasDown || !before || st.uptime < before.uptime) {
      return end({
        phase: 'done',
        message: kind === 'firmware'
          ? `The device is running ${st.firmware}${st.commit ? ` (${st.commit})` : ''}.`
          : 'The device restarted with the new filesystem.',
      });
    }
    if (now() > deadline) {
      return end({ phase: 'failed', message: 'The upload was accepted, but the device did not come back within 2 minutes. Check its display and power.' });
    }
  }
}
