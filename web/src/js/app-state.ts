/**
 * App State - Event-based reactive communication layer
 * Components use emit() and on() to communicate via typed custom events.
 * No central state object - each component manages its own state.
 */

export type LogLevel = 'log' | 'info' | 'warn' | 'error' | 'result' | 'input';

/**
 * The two consoles. They share nothing: each has its own output, input and
 * history.
 *   device   the real device's REPL, and what the IDE reports about the
 *            device and its files (saved, deployed, storage, images, fonts)
 *   virtual  the script running in the virtual device (the emulator in the
 *            page): its print() output, its errors, lines evaluated in it
 */
export type ConsoleId = 'device' | 'virtual';

export interface ConsoleLine {
  text: string;
  type: LogLevel;
  /** The console this line belongs to: stated by whoever emits it, never guessed. */
  to: ConsoleId;
}

/** Where a typed line is evaluated: the console it was typed in. */
export type ConsoleTarget = ConsoleId;

/**
 * The emulator of the virtual device (emulator-session.ts).
 * idle: nothing runs; starting: the engine loads; running: a script runs;
 * error: it stopped (or did not start) with `error`.
 */
export interface EmulatorSnapshot {
  status: 'idle' | 'starting' | 'running' | 'error';
  /** "virt_btn_" + 6 hex digits: what system.id() gives the script. Never a real device's id. */
  identity: string;
  /** Stage 1: the in-page broker only. */
  mqtt: 'offline';
  /** Why it stopped, or the last error of a script that still runs. */
  error: string;
  /** Errors since Run. */
  errors: number;
  /** config.json or the script changed since Run: what runs is the older text. */
  stale: 'config' | 'script' | null;
  /** Run was pressed at least once since the page loaded (the virtual console and the MQTT tab exist from then on). */
  used: boolean;
}

export interface FileEntry {
  name: string;
  size: number;
  type: 'js' | 'json' | 'svg' | 'png' | 'bin' | 'other';
}

/**
 * The font a text starts with (font-config.ts slotFont): a face of the
 * font table (index; 0, 1 = the system fonts montserrat, montserrat_semibold)
 * at px device pixels. configured = the item names a config font (else the
 * style's font of the element).
 */
export interface ItemFont {
  face: number;
  px: number;
  configured: boolean;
}

export interface DisplayItem {
  /** Source entry in config.json (set by configToModel). */
  ref?: ConfigRef;
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
  /** Icon name (config.json "icon", a key of "icons"), shown before the value. */
  icon?: string;
  /** Square icon size in px (config.json "iconsize", default 32). */
  iconSize?: number;
  /** "#rrggbb" tint of a single-colour icon (config.json "iconcolor"). */
  iconColor?: string;
  /** Value font (config.json "font"/"fontsize"; the unit's too unless the style gives it its own); absent = the style's display.value at fontSize. */
  valueFont?: ItemFont;
  /** Caption font (config.json "labelfont"/"labelfontsize"); absent = the style's display.caption. */
  labelFont?: ItemFont;
}

/** A physical connector slot (config.json info.connectors). Keys: id*2+1, id*2+2. */
export interface ConnectorConfig {
  id: number;
  type: 'bar' | 'display' | 'empty';
}

export interface ButtonConfig {
  /** Source entry in config.json (set by configToModel). */
  ref?: ConfigRef;
  position: number;
  label: string;
  topLabel: string;
  ledOn: boolean;
  ledColor: string;
  /** Colour of the LED at the back of the key (towards the wall) when it is on; set by a running script only. */
  backLedColor?: string;
  page: number;
  /** Script name from config.json "name" (fb.key("name")). */
  name?: string;
  /** Icon name (config.json "icon"); shown instead of the bottom label. */
  icon?: string;
  /** Square icon size in px (config.json "iconsize", default 48). */
  iconSize?: number;
  /** Bottom label font (config.json "font"/"fontsize"); absent = the style's key.label. */
  labelFont?: ItemFont;
  /** Top label font (config.json "topfont"/"topfontsize"); absent = the style's key.toplabel. */
  topFont?: ItemFont;
}

/**
 * An image on the main display (config.json "images"): an icon raster
 * (width x height) or any key of the device's `images` bucket ("src").
 */
export interface ImageConfig {
  /** Source entry in config.json (set by configToModel). */
  ref?: ConfigRef;
  name: string;
  /** 1-based; 0 = every page. */
  page: number;
  /**
   * Pixels on the 320x240 main display: the raster's top-left corner (unlike
   * display items and lists, which use percent).
   */
  x: number;
  y: number;
  /**
   * Pixels (the raster size): pick the icon raster; absent when not given
   * (an icon then shows nothing; a src raster has its own size).
   */
  width?: number;
  height?: number;
  /** Icon name; wins over src when both are given (as on the device). */
  icon?: string;
  src?: string;
  /** "#rrggbb" tint of a single-colour raster. */
  color?: string;
}

/**
 * A roller on the main display (config.json "lists"; LVGL roller on the
 * device). Defaults applied (see config-model mapList).
 */
export interface ListConfig {
  /** Source entry in config.json (set by configToModel). */
  ref?: ConfigRef;
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
  /** 1..5 (14, 24, 32, 48, 72 px), 6..72 = px. */
  fontSize: number;
  /** config.json "font"/"fontsize"; absent = the style's list font at fontSize. */
  font?: ItemFont;
  items: string[];
  /** Selected index from config.json (clamped to the items; -1 = no items). */
  selected: number;
  /** "#rrggbb": other rows' text, selected row background, selected row text. */
  color: string;
  focusColor: string;
  focusTextColor: string;
}

/** The kinds of config.json entries the virtual device draws. */
export type ConfigItemKind = 'button' | 'displayitem' | 'list' | 'image';

/** Where an item of the virtual device comes from in config.json: `list`[`index`]. */
export interface ConfigRef {
  kind: ConfigItemKind;
  /** The array's key as written in the file ("buttons", "mqttdisplays", ...). */
  list: string;
  index: number;
}

export function sameConfigRef(a: ConfigRef | null | undefined, b: ConfigRef | null | undefined): boolean {
  return !!a && !!b && a.list === b.list && a.index === b.index;
}

/** What the main pane shows: the code editor or one of the views. */
export type MainView = 'editor' | 'fonts' | 'image' | 'example' | 'device' | 'docs';
export type SidebarPanel = 'app' | 'images' | 'fonts' | 'examples';

/** Live preview of an unsaved config.json: 'live' = shown, 'stale' = text not valid JSON (last valid kept). */
export type PreviewState = 'live' | 'stale';

export interface PageConfig {
  number: number;
  displayItems: DisplayItem[];
  buttons: ButtonConfig[];
  /** Lists shown on this page (config.json "lists"); missing = none. */
  lists?: ListConfig[];
  /** Images shown on this page (config.json "images"); missing = none. */
  images?: ImageConfig[];
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
  /** An icon sync (config.json icons -> bucket images) is running. */
  iconSyncing?: boolean;
  /** Space on the device, from the usage headers of its listings; a part is absent until its bucket was listed (or on older firmware). */
  storage?: StorageUsage;
}

/** Bytes of the device's two stores: the filesystem (app files, IDE) and the media store (fonts, images). */
export interface StorageUsage {
  fs?: { total: number; used: number };
  media?: { total: number; used: number; free: number };
}

// Typed custom event map
declare global {
  interface WindowEventMap {
    // Editor events
    'editor-ready': CustomEvent<void>;
    'cursor-position': CustomEvent<{ line: number; col: number }>;
    'validation-result': CustomEvent<{ valid: boolean; errors: string[]; errorCount: number }>;

    // Deploy events (compile-for-device + upload to the serving device, same-origin)
    'deploy-request': CustomEvent<void>;
    'deploy-result': CustomEvent<{ success: boolean; message: string; size?: number; note?: string }>;

    // Connection events
    'connection-change': CustomEvent<{ connected: boolean; url: string }>;
    'connection-request': CustomEvent<{ url: string }>;
    'disconnect-request': CustomEvent<void>;

    // Console events
    'console-line': CustomEvent<ConsoleLine>;
    // console: the one to clear; absent = the one that is shown.
    'console-clear': CustomEvent<{ console?: ConsoleId }>;
    'console-eval': CustomEvent<{ code: string; target: ConsoleTarget }>;

    // The emulator of the virtual device: its status, the device state a
    // script changed (redraw), its MQTT log.
    'emulator-change': CustomEvent<EmulatorSnapshot>;
    'emulator-state': CustomEvent<void>;
    'emulator-mqtt': CustomEvent<void>;

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
    // The SVG sources of config.json "icons" read from the device changed
    // (icon-sync.ts iconSources; the virtual device preview shows them).
    'icon-sources-change': CustomEvent<void>;
    // A device font got registered for the previews (font-loader.ts).
    'preview-fonts-change': CustomEvent<void>;

    // Navigation events
    'sidebar-panel': CustomEvent<{ panel: SidebarPanel; open?: boolean }>;
    'open-settings': CustomEvent<void>;
    // Main pane: show a view (key = the file for 'image'; 'editor' = back to the code).
    'main-view': CustomEvent<{ view: MainView; key?: string }>;
    // What the main pane and the sidebar show now (the nav bar's active state).
    'layout-change': CustomEvent<{ view: MainView; panel: SidebarPanel | null }>;
    // The config.json entry under the editor cursor (null: none / other file).
    'config-selection': CustomEvent<{ ref: ConfigRef | null }>;
    // An item of the virtual device was clicked: select its config.json entry.
    'config-reveal': CustomEvent<{ ref: ConfigRef }>;
    // Jump to the first problem the linter reports (status bar).
    'goto-diagnostic': CustomEvent<void>;

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
