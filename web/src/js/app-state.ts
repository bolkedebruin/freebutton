/**
 * App State - Event-based reactive communication layer
 * Components use emit() and on() to communicate via typed custom events.
 * No central state object - each component manages its own state.
 */

export type LogLevel = 'log' | 'info' | 'warn' | 'error' | 'result' | 'input';

export interface ConsoleLine {
  text: string;
  type: LogLevel;
}

export interface FileEntry {
  name: string;
  size: number;
  type: 'js' | 'json' | 'svg' | 'png' | 'bin' | 'other';
}

export interface DisplayItem {
  x: number;
  y: number;
  width: number;
  height: number;
  type: 'text' | 'number' | 'bar' | 'icon';
  label?: string;
  value?: string;
  unit?: string;
  fontSize?: number;
  color?: string;
  /** Stable id from config.json "displayitemid". */
  id?: string;
  /** Script name from config.json "name" (fb.main.display.setValue("name", ...)). */
  name?: string;
  /** lv_align_t from config.json. */
  align?: number;
}

/** A physical connector slot (config.json info.connectors). Keys: id*2+1, id*2+2. */
export interface ConnectorConfig {
  id: number;
  type: 'bar' | 'display' | 'empty';
}

export interface ButtonConfig {
  position: number;
  label: string;
  topLabel: string;
  ledOn: boolean;
  ledColor: string;
  page: number;
  /** Script name from config.json "name" (fb.key("name")). */
  name?: string;
}

/**
 * A roller on the main display (config.json "lists"; LVGL roller on the
 * device). Defaults applied (see config-model mapList).
 */
export interface ListConfig {
  /** Script name (fb.main.list.next("name")); required. */
  name: string;
  /** 1-based; 0 = every page. */
  page: number;
  /** Percent of the 320x240 screen. */
  x: number;
  y: number;
  width: number;
  /** Visible rows (1..7); the selected item sits in the middle one. */
  rows: number;
  /** 1..5 (14, 24, 32, 48, 72 px). */
  fontSize: number;
  items: string[];
  /** Selected index from config.json (clamped to the items; -1 = no items). */
  selected: number;
  /** "#rrggbb": other rows' text, selected row background, selected row text. */
  color: string;
  focusColor: string;
  focusTextColor: string;
}

/** Live preview of an unsaved config.json: 'live' = shown, 'stale' = text not valid JSON (last valid kept). */
export type PreviewState = 'live' | 'stale';

export interface PageConfig {
  number: number;
  displayItems: DisplayItem[];
  buttons: ButtonConfig[];
  /** Lists shown on this page (config.json "lists"); missing = none. */
  lists?: ListConfig[];
}

/**
 * Live device info from the firmware's GET /api/device endpoint.
 * Network fields are strings ("Not connected" when WiFi is down); memory fields
 * are raw BYTES (the UI applies the KB quantifier).
 */
export interface DeviceInfoData {
  deviceName: string;
  ipv4: string;
  ipv6: string;
  ssid: string;
  rssi: number;       // dBm
  mac: string;
  firmware: string;
  uptime: number;     // seconds
  heapFree: number;   // bytes
  heapTotal: number;  // bytes
  heapMinFree: number; // bytes
  lvglFree: number;   // bytes
  lvglTotal: number;  // bytes
  /** JS heap (newer firmware; absent before): live data after a GC, free, buffer size. */
  jsHeapUsed?: number; // bytes
  jsHeapFree?: number; // bytes
  jsHeapSize?: number; // bytes
  /** Build identity (newer firmware; absent before). */
  commit?: string;    // short git hash
  dirty?: boolean;    // built from a working tree with uncommitted changes
  model?: string;
  /** Hardware topology (firmware with S3 storage and newer; absent before). */
  mainConnector?: number;                     // -1 = no main display
  connectors?: { id: number; type: number }[]; // type 1 = bar, 2 = large display
}

export interface WorkspaceFile {
  key: string;
  size: number;
  lastModified?: Date;
}

/**
 * Where files live: 'device' = the device's S3 storage API, 'local' = no
 * device storage reachable (dev without hardware / older firmware), files are
 * kept in browser memory only. 'probing' until the first list completes.
 */
export type WorkspaceMode = 'probing' | 'device' | 'local';

export interface WorkspaceSnapshot {
  mode: WorkspaceMode;
  /** Human-readable status (offline reason, last error). */
  message: string;
  files: WorkspaceFile[];
  busy: boolean;
  /** Object key of the file in the editor ("scripts/main.js"). */
  currentKey: string;
  dirty: boolean;
  /** The open file does not exist in storage yet (e.g. template main.js). */
  isNew: boolean;
  /**
   * Listings of the device's list-only buckets (examples, web, fonts), keyed
   * by bucket. A bucket is absent until something asked for it
   * (Workspace.loadReadOnly); its objects are never downloaded.
   */
  readOnly?: Partial<Record<'examples' | 'web' | 'fonts', WorkspaceFile[]>>;
}

// Typed custom event map
declare global {
  interface WindowEventMap {
    // Editor events
    'editor-ready': CustomEvent<void>;
    'editor-content-change': CustomEvent<{ content: string }>;
    'cursor-position': CustomEvent<{ line: number; col: number }>;
    'validation-result': CustomEvent<{ valid: boolean; errors: string[]; errorCount: number }>;

    // Compiler events
    'compile-start': CustomEvent<void>;
    'compile-result': CustomEvent<{ success: boolean; bytecode?: Uint8Array; error?: string; size?: number }>;
    'compiler-ready': CustomEvent<void>;

    // Deploy events (compile-for-device + upload to the serving device, same-origin)
    'deploy-request': CustomEvent<void>;
    'deploy-result': CustomEvent<{ success: boolean; message: string; size?: number }>;

    // Connection events
    'connection-change': CustomEvent<{ connected: boolean; url: string }>;
    'connection-request': CustomEvent<{ url: string }>;
    'disconnect-request': CustomEvent<void>;

    // Console events
    'console-line': CustomEvent<ConsoleLine>;
    'console-clear': CustomEvent<void>;
    'console-eval': CustomEvent<{ code: string }>;

    // Device events
    // connectors: physical layout (empty = unknown, use the legacy layout).
    'device-config': CustomEvent<{ pages: PageConfig[]; connectors?: ConnectorConfig[]; preview?: PreviewState }>;
    // Live device info; data is null when no device is reachable (simulator/dev).
    'device-info': CustomEvent<{ data: DeviceInfoData | null }>;

    // Editor document changed (user edit or programmatic load).
    'editor-doc-change': CustomEvent<void>;

    // Workspace (device files + the file open in the editor)
    'workspace-change': CustomEvent<WorkspaceSnapshot>;
    'save-request': CustomEvent<void>;
    // Shown after config.json was saved: config is applied at boot.
    'reboot-suggested': CustomEvent<{ key: string }>;

    // File events
    'files-change': CustomEvent<{ files: FileEntry[]; used: number; total: number }>;
    'file-select': CustomEvent<{ name: string }>;
    'file-delete': CustomEvent<{ name: string }>;
    'file-upload': CustomEvent<{ file: File }>;

    // Navigation events
    'toggle-assets': CustomEvent<void>;
    'sidebar-panel': CustomEvent<{ panel: 'assets' | 'examples' }>;
    'load-example': CustomEvent<{ name: string }>;
    'nav-select': CustomEvent<{ item: string }>;
    'open-settings': CustomEvent<void>;

    // Device access (SigV4, src/js/auth.ts)
    // The device refused a request (403 + x-freebutton-auth: required);
    // Settings opens at "Device access" with the message.
    'auth-required': CustomEvent<{ origin: string; code: string; message: string }>;
    // Credentials for a device origin were saved or cleared.
    'device-credentials-change': CustomEvent<{ origin: string; configured: boolean }>;
  }
}

/**
 * Dispatch a typed custom event on window
 */
export function emit<K extends keyof WindowEventMap>(
  type: K,
  ...args: WindowEventMap[K] extends CustomEvent<void> ? [] : [WindowEventMap[K] extends CustomEvent<infer D> ? D : never]
): void {
  window.dispatchEvent(new CustomEvent(type, { detail: args[0] }));
}

/**
 * Listen for a typed custom event on window. Returns unsubscribe function.
 */
export function on<K extends keyof WindowEventMap>(
  type: K,
  handler: (event: WindowEventMap[K]) => void
): () => void {
  const listener = handler as EventListener;
  window.addEventListener(type, listener);
  return () => window.removeEventListener(type, listener);
}
