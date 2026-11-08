/**
 * Device Info poller
 *
 * Fetches live device info from the firmware's `GET /api/device` endpoint and
 * republishes it on the app-state bus as a `device-info` event. The virtual
 * device panel subscribes to that event to show real IP/IPv6/SSID/MAC, RSSI,
 * firmware, uptime and a byte-accurate memory breakdown.
 *
 * Polling is driven by the REPL connection lifecycle: it starts when a device
 * connects (`connection-change {connected:true}`) and stops on disconnect, at
 * which point a `device-info {data:null}` is emitted so the UI falls back to
 * placeholders. This mirrors how `compiler.ts` reaches the device over HTTP
 * (same origin when the IDE is served from the device itself).
 */
import { on, emit, type DeviceInfoData } from './app-state';
import { setHardwareConnectors } from './device-config';

/** Map a REPL ws(s):// base URL to the http(s):// origin serving the REST API. */
export function httpBaseFromWs(wsUrl: string): string {
  // "ws://host" -> "http://host", "wss://host" -> "https://host". Empty stays
  // empty (same-origin relative fetch).
  return wsUrl.replace(/^ws/, 'http');
}

const STRING_KEYS = ['deviceName', 'ipv4', 'ipv6', 'ssid', 'mac', 'firmware'] as const;
const NUMBER_KEYS = ['rssi', 'uptime', 'heapFree', 'heapTotal', 'heapMinFree', 'lvglFree', 'lvglTotal'] as const;

/**
 * Validate the shape of a /api/device payload. Guards the C++/TS contract: a
 * firmware-side key rename would otherwise slip through `as DeviceInfoData` and
 * render `NaN KB` / `undefined dBm`. On mismatch we treat it like a fetch
 * failure (placeholders) rather than showing garbage.
 */
export function isDeviceInfoData(v: unknown): v is DeviceInfoData {
  if (typeof v !== 'object' || v === null) return false;
  const o = v as Record<string, unknown>;
  return STRING_KEYS.every(k => typeof o[k] === 'string')
    && NUMBER_KEYS.every(k => typeof o[k] === 'number' && Number.isFinite(o[k]));
}

/**
 * Fetch device info from `${baseUrl}/api/device`.
 * @param baseUrl http(s) origin, or "" for same-origin (default).
 */
export async function fetchDeviceInfo(baseUrl = ''): Promise<DeviceInfoData> {
  const response = await fetch(`${baseUrl}/api/device`);
  if (!response.ok) {
    throw new Error(`Failed to get device info: ${response.status} ${response.statusText}`);
  }
  const data = await response.json();
  if (!isDeviceInfoData(data)) {
    throw new Error('Malformed /api/device payload (contract drift?)');
  }
  return data;
}

let initialized = false;
let pollTimer: ReturnType<typeof setInterval> | undefined;
let currentBase = '';
let intervalMs = 2000;
let connected = false;
let visible = false;
let unsubConnection: (() => void) | undefined;

function stop(): void {
  if (pollTimer !== undefined) {
    clearInterval(pollTimer);
    pollTimer = undefined;
  }
}

/**
 * Run the interval only when a device is connected AND something is watching.
 * The only consumer is the flipped-to-back device panel, so polling while the
 * front face is showing burns a request every `intervalMs` (plus a parse, an
 * event and a re-render) for data nobody can see.
 */
function sync(): void {
  const shouldRun = connected && visible;
  if (shouldRun === (pollTimer !== undefined)) return;
  if (!shouldRun) {
    stop();
    return;
  }
  void poll(); // fetch immediately so the panel isn't empty on flip
  pollTimer = setInterval(() => void poll(), intervalMs);
}

async function poll(): Promise<void> {
  try {
    const data = await fetchDeviceInfo(currentBase);
    emit('device-info', { data });
    // Real topology from the hardware scan; older firmware has none (then
    // config.json info.connectors stays in effect). Emits only on change.
    if (data.connectors) setHardwareConnectors(data.connectors);
  } catch {
    // Device unreachable (dropped, or dev build with no device) -> placeholders.
    emit('device-info', { data: null });
  }
}

/**
 * Wire device-info polling to the connection lifecycle. Idempotent.
 *
 * This only arms the poller -- nothing is fetched until a consumer calls
 * `setDeviceInfoVisible(true)` (see the device panel's flip handler).
 * @param pollIntervalMs poll cadence while connected and visible (default 2000ms).
 */
export function initDeviceInfoPolling(pollIntervalMs = 2000): void {
  if (initialized) return;
  initialized = true;
  intervalMs = pollIntervalMs;

  unsubConnection = on('connection-change', (e) => {
    connected = e.detail.connected;
    if (connected) {
      currentBase = httpBaseFromWs(e.detail.url ?? '');
    } else {
      stop();
      emit('device-info', { data: null });
    }
    sync();
  });
}

/** Tell the poller whether anyone is currently looking at the device info. */
export function setDeviceInfoVisible(isVisible: boolean): void {
  if (visible === isVisible) return;
  visible = isVisible;
  sync();
}

/**
 * Tear down the poller: stops the interval AND drops the `connection-change`
 * listener, so a later `initDeviceInfoPolling()` cannot end up with two
 * listeners and an interval nothing holds a handle to.
 */
export function stopDeviceInfoPolling(): void {
  stop();
  unsubConnection?.();
  unsubConnection = undefined;
  initialized = false;
  connected = false;
  visible = false;
  currentBase = '';
}

/** Test-only: reset module state between test cases. */
export function _resetForTest(): void {
  stopDeviceInfoPolling();
}
