/**
 * The virtual device's MQTT connection settings: what config.json "brokers"
 * says, what this browser keeps over it, and the mode that was chosen.
 *
 * No imports: the IDE's first load (the back of the virtual device) and the
 * emulator's code (emulator/live-session.ts, loaded at the first Run) both
 * use it, and neither pulls the other in.
 *
 * A browser cannot speak MQTT over TCP: it connects to the broker's
 * websockets listener. The address of that listener is
 *
 *   1. what the user saved for the broker's host (settings record), else
 *   2. the address the user set before there were records ("fb-mqtt-ws:<host>"), else
 *   3. config.json's url when it is ws:// or wss:// already, else
 *   4. ws://<host>:<wsport> with config.json's "wsport", else port 9001.
 *
 * The record per broker host ("freebutton.virtualBroker:<host>", JSON
 * {url, username, password}) is kept as the device's access key is
 * (auth.ts): in localStorage, in this browser only. A field that is empty
 * is not set: config.json's value is used. Two entries of config.json with
 * one host share a record.
 *
 * The mode ("fb-mqtt-mode") is what the user chose last; nothing stored:
 * live publish (defaultStartMode()).
 */

export type MqttMode = 'offline' | 'live-readonly' | 'live-publish';

/** The websockets listener's port when config.json has no "wsport" (Mosquitto's examples use it). */
export const DEFAULT_WS_PORT = 9001;
export const MODE_KEY = 'fb-mqtt-mode';
export const SETTINGS_PREFIX = 'freebutton.virtualBroker:';
/** The WebSocket address per host as it was kept before the settings record: still read. */
export const LEGACY_URL_PREFIX = 'fb-mqtt-ws:';

export type BrokerStorage = Pick<Storage, 'getItem' | 'setItem'> & Partial<Pick<Storage, 'removeItem'>>;

/** One entry of config.json "brokers". */
export interface BrokerConfig {
  name: string;
  url: string;
  host: string;
  /** config.json "wsport"; 0: none. */
  wsPort: number;
  username: string;
  password: string;
  clientId: string;
}

/** What the browser keeps for a broker host; '' = not set (config.json's value is used). */
export interface BrokerSettings {
  url: string;
  username: string;
  password: string;
}

/** What a connection is made with. */
export interface BrokerAccess {
  url: string;
  username: string;
  password: string;
}

type Obj = Record<string, unknown>;

function text(v: unknown): string {
  return typeof v === 'string' ? v : typeof v === 'number' ? String(v) : '';
}

/** The host of a broker url of config.json ("mqtt://10.0.0.5/", "10.0.0.5", "mqtts://broker.lan:8883"). */
export function brokerHost(url: string): string {
  const match = /^(?:[a-z][a-z0-9+.-]*:\/\/)?(?:[^@/]*@)?(\[[^\]]+\]|[^:/?#]+)/i.exec(url.trim());
  return match ? match[1]! : '';
}

/** config.json "brokers" as the live connection needs them. */
export function brokerConfigs(config: string): BrokerConfig[] {
  let parsed: unknown;
  try { parsed = JSON.parse(config); } catch { return []; }
  const list = (parsed as Obj | null)?.brokers;
  if (!Array.isArray(list)) return [];
  return list.filter((b): b is Obj => typeof b === 'object' && b !== null).map((b, index) => {
    const wsPort = Math.trunc(Number(b.wsport));
    return {
      name: text(b.brokerid) || `broker${index}`,
      url: text(b.url),
      host: brokerHost(text(b.url)),
      wsPort: Number.isFinite(wsPort) && wsPort > 0 && wsPort < 65536 ? wsPort : 0,
      username: text(b.username),
      password: text(b.password),
      clientId: text(b.clientid ?? b.clientId ?? b.client_id),
    };
  });
}

/** The WebSocket address config.json gives: its url when that is ws:// or wss://, else ws://<host>:<wsport or 9001>. */
export function defaultWsUrl(broker: Pick<BrokerConfig, 'url' | 'host' | 'wsPort'>): string {
  const url = broker.url.trim();
  if (/^wss?:\/\//i.test(url)) return url;
  return broker.host ? `ws://${broker.host}:${broker.wsPort || DEFAULT_WS_PORT}` : '';
}

/** localStorage, or null where there is none (or it is blocked). */
export function browserStorage(): BrokerStorage | null {
  try {
    return typeof localStorage === 'undefined' ? null : localStorage;
  } catch {
    return null;
  }
}

/** The record kept for `host`; null: none. */
export function readBrokerSettings(host: string, storage: BrokerStorage | null = browserStorage()): BrokerSettings | null {
  if (!host || !storage) return null;
  let record: BrokerSettings | null = null;
  try {
    const raw = storage.getItem(SETTINGS_PREFIX + host);
    if (raw) {
      const parsed = JSON.parse(raw) as Partial<BrokerSettings> | null;
      if (parsed && typeof parsed === 'object') {
        record = { url: text(parsed.url).trim(), username: text(parsed.username), password: text(parsed.password) };
      }
    }
  } catch { /* unreadable: as if there were none */ }
  if (!record?.url) {
    // The address set before there were records.
    let legacy = '';
    try { legacy = (storage.getItem(LEGACY_URL_PREFIX + host) ?? '').trim(); } catch { /* none */ }
    if (legacy) record = { url: legacy, username: record?.username ?? '', password: record?.password ?? '' };
  }
  return record && (record.url || record.username || record.password) ? record : null;
}

/** Keep `settings` for `host`; all fields empty: the record goes. False: the browser did not store it. */
export function writeBrokerSettings(host: string, settings: BrokerSettings, storage: BrokerStorage | null = browserStorage()): boolean {
  if (!host || !storage) return false;
  const record: BrokerSettings = { url: settings.url.trim(), username: settings.username, password: settings.password };
  if (!record.url && !record.username && !record.password) return clearBrokerSettings(host, storage);
  try {
    storage.setItem(SETTINGS_PREFIX + host, JSON.stringify(record));
    // The record has the address now (or says: config.json's).
    storage.removeItem?.(LEGACY_URL_PREFIX + host);
    return true;
  } catch {
    return false;
  }
}

/** Forget what is kept for `host`: config.json's values are used again. */
export function clearBrokerSettings(host: string, storage: BrokerStorage | null = browserStorage()): boolean {
  if (!host || !storage) return false;
  try {
    if (storage.removeItem) {
      storage.removeItem(SETTINGS_PREFIX + host);
      storage.removeItem(LEGACY_URL_PREFIX + host);
    } else {
      storage.setItem(SETTINGS_PREFIX + host, '');
      storage.setItem(LEGACY_URL_PREFIX + host, '');
    }
    return true;
  } catch {
    return false;
  }
}

/** What a connection to `broker` is made with: the kept value per field, else config.json's. */
export function brokerAccess(broker: BrokerConfig, storage: BrokerStorage | null = browserStorage()): BrokerAccess {
  const kept = readBrokerSettings(broker.host, storage);
  return {
    url: kept?.url || defaultWsUrl(broker),
    username: kept?.username || broker.username,
    password: kept?.password || broker.password,
  };
}

/** A broker of config.json as the settings list it: what it connects with, and what config.json gives. */
export interface BrokerSummary {
  index: number;
  name: string;
  /** Host of config.json's broker url. */
  host: string;
  /** The WebSocket address a live mode connects to: what the browser keeps for the host, else config.json's. */
  wsUrl: string;
  /** The address config.json gives (its "wsport", else 9001; a ws:// url as it is). */
  defaultWsUrl: string;
  /** The user name a live mode connects with ('' none), and config.json's. */
  username: string;
  defaultUsername: string;
  /** config.json has a password for it. */
  configPassword: boolean;
  /** The browser keeps an address, user name or password for this host. */
  overridden: boolean;
  /** config.json gives this broker a client id: it is not used here. */
  configClientId: string;
  /** A user name is sent (with the password) in the live modes. */
  hasCredentials: boolean;
}

export function brokerSummary(broker: BrokerConfig, index: number, storage: BrokerStorage | null = browserStorage()): BrokerSummary {
  const access = brokerAccess(broker, storage);
  return {
    index, name: broker.name, host: broker.host, wsUrl: access.url, defaultWsUrl: defaultWsUrl(broker),
    username: access.username, defaultUsername: broker.username, configPassword: broker.password !== '',
    overridden: readBrokerSettings(broker.host, storage) !== null,
    configClientId: broker.clientId, hasCredentials: access.username !== '',
  };
}

function isMode(value: unknown): value is MqttMode {
  return value === 'offline' || value === 'live-readonly' || value === 'live-publish';
}

/** The mode the user chose last; null: none was chosen. */
export function readStoredMode(storage: BrokerStorage | null = browserStorage()): MqttMode | null {
  try {
    const value = storage?.getItem(MODE_KEY);
    return isMode(value) ? value : null;
  } catch {
    return null;
  }
}

export function writeStoredMode(mode: MqttMode, storage: BrokerStorage | null = browserStorage()): void {
  try { storage?.setItem(MODE_KEY, mode); } catch { /* not kept */ }
}

let startMode: MqttMode = 'live-publish';

/** The mode of a Run when the user chose none: live publish (the tests set offline: no socket is opened). */
export function defaultStartMode(): MqttMode {
  return startMode;
}

/** For tests (src/test-setup.ts pins offline). */
export function setDefaultStartMode(mode: MqttMode): void {
  startMode = mode;
}
