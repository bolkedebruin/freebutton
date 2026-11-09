/**
 * Device Configuration Manager
 * Manages PageConfig[] (+ the physical connector layout and the config.json
 * key / display item names) and emits device-config events on changes.
 * Starts with sample data; the workspace replaces it with the device's
 * config.json (see config-model.ts) when one is available.
 *
 * While config.json is edited, an unsaved draft can be shown on top of the
 * stored config (setPreviewConfig, driven by config-preview.ts). The draft
 * only drives the preview: nothing here writes it anywhere, and
 * clearPreviewConfig goes back to the stored config.
 */
import { emit, type PageConfig, type DisplayItem, type ButtonConfig, type ConnectorConfig, type PreviewState } from './app-state';
import { mapConnectors, type ConfigNames, type DeviceModel } from './config-model';

const defaultConfig: PageConfig[] = [
  {
    number: 1,
    displayItems: [
      { x: 0, y: 5, width: 100, height: 50, type: 'text', label: 'Amsterdam', value: 'time', fontSize: 42 },
      { x: 0, y: 50, width: 100, height: 50, type: 'number', label: 'Button+ Likes', value: '67005', fontSize: 18 },
    ],
    buttons: [
      { position: 5, label: 'Tolomeo', topLabel: '', ledOn: true, ledColor: 'rgb(255,0,0)', page: 1 },
      { position: 6, label: 'Green Lamp', topLabel: 'Virtual', ledOn: true, ledColor: 'rgb(255,0,0)', page: 1 },
      { position: 7, label: 'Like!', topLabel: 'Button+', ledOn: false, ledColor: 'rgb(0,0,0)', page: 1 },
      { position: 8, label: 'Kantoor', topLabel: '', ledOn: true, ledColor: 'rgb(255,0,0)', page: 1 },
    ],
  },
  { number: 2, displayItems: [], buttons: [] },
];

let currentConfig: PageConfig[] = structuredClone(defaultConfig);
// Layout from config.json info.connectors (stored info, can be stale).
let configConnectors: ConnectorConfig[] = [];
// Layout the firmware reports from its hardware scan (/api/device); wins
// over config.json when present.
let hardwareConnectors: ConnectorConfig[] = [];
// Effective layout. Empty = unknown (sample data / no topology anywhere).
let currentConnectors: ConnectorConfig[] = [];
// The stored config is the built-in sample (no runtime values worth keeping).
let isSample = true;
// Unsaved config.json draft shown instead of the stored config; null = none.
let draft: DeviceModel | null = null;
// 'live' = draft shown; 'stale' = the editor text is not valid JSON, the last
// valid preview stays up. null = the stored config is shown.
let previewState: PreviewState | null = null;
// Names from the stored config.json; null = no config.json loaded (sample data).
let configNames: ConfigNames | null = null;

function effectiveConnectors(): ConnectorConfig[] {
  if (hardwareConnectors.length > 0) return hardwareConnectors;
  return draft ? draft.connectors : configConnectors;
}

function publish(): void {
  emit('device-config', {
    pages: getConfig(),
    connectors: currentConnectors,
    ...(previewState ? { preview: previewState } : {}),
  });
}

/** What the preview shows: the unsaved draft if there is one, else the stored config. */
export function getConfig(): PageConfig[] {
  return draft ? draft.pages : currentConfig;
}

export function getPreviewState(): PreviewState | null {
  return previewState;
}

export function getConnectors(): ConnectorConfig[] {
  return currentConnectors;
}

/**
 * Key and display item names of the stored config.json, for script
 * completion and validation. null when no config.json is loaded.
 */
export function getConfigNames(): ConfigNames | null {
  return configNames;
}

/** Stored config (config.json as saved/loaded). A draft preview stays on top until cleared. */
export function setConfig(config: PageConfig[], connectors: ConnectorConfig[] = [], names: ConfigNames | null = null): void {
  currentConfig = config;
  configConnectors = connectors;
  configNames = names;
  isSample = false;
  currentConnectors = effectiveConnectors();
  publish();
}

/**
 * Show an unsaved config.json draft. Values set at runtime (display values,
 * key LEDs; config.json does not carry them) are kept for items that are
 * still there: display items by displayitemid, else page + index; keys by
 * page + position.
 */
export function setPreviewConfig(model: DeviceModel): void {
  if (!isSample) carryRuntimeState(getConfig(), model.pages);
  draft = model;
  previewState = 'live';
  currentConnectors = effectiveConnectors();
  publish();
}

/** The draft does not parse: keep the last valid preview, flag it as stale. */
export function markPreviewStale(): void {
  if (previewState === 'stale') return;
  previewState = 'stale';
  publish();
}

/** Back to the stored config (draft saved, discarded or closed). */
export function clearPreviewConfig(): void {
  if (!draft && !previewState) return;
  draft = null;
  previewState = null;
  currentConnectors = effectiveConnectors();
  publish();
}

function carryRuntimeState(from: PageConfig[], to: PageConfig[]): void {
  for (const page of to) {
    const old = from.find(p => p.number === page.number);
    if (!old) continue;
    page.displayItems.forEach((item, i) => {
      const prev = item.id !== undefined
        ? old.displayItems.find(d => d.id === item.id)
        : old.displayItems[i]?.id === undefined ? old.displayItems[i] : undefined;
      if (!prev) return;
      if (prev.value) item.value = prev.value;
      if (prev.color !== undefined && item.color === undefined) item.color = prev.color;
    });
    for (const btn of page.buttons) {
      const prev = old.buttons.find(b => b.position === btn.position);
      if (!prev) continue;
      btn.ledOn = prev.ledOn;
      btn.ledColor = prev.ledColor;
    }
  }
}

/**
 * Hardware topology from /api/device ({id, type} per connector). Emits only
 * when it changes (device info is polled), so the device view does not
 * re-render every poll.
 */
export function setHardwareConnectors(raw: { id: number; type: number }[] | undefined): void {
  const next = raw ? mapConnectors({ info: { connectors: raw } }) : [];
  if (JSON.stringify(next) === JSON.stringify(hardwareConnectors)) return;
  hardwareConnectors = next;
  currentConnectors = effectiveConnectors();
  publish();
}

export function loadDefaultConfig(): void {
  draft = null;
  previewState = null;
  setConfig(structuredClone(defaultConfig));
  isSample = true;
}

export function updateDisplayItem(pageNumber: number, index: number, item: Partial<DisplayItem>): void {
  const page = getConfig().find(p => p.number === pageNumber);
  if (!page || index < 0 || index >= page.displayItems.length) return;
  Object.assign(page.displayItems[index], item);
  publish();
}

export function updateButton(pageNumber: number, position: number, updates: Partial<ButtonConfig>): void {
  const page = getConfig().find(p => p.number === pageNumber);
  if (!page) return;
  const btn = page.buttons.find(b => b.position === position);
  if (!btn) return;
  Object.assign(btn, updates);
  publish();
}
