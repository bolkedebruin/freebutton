/**
 * Device Configuration Manager
 * Manages PageConfig[] (+ the physical connector layout) and emits
 * device-config events on changes. Starts with sample data; the workspace
 * replaces it with the device's config.json (see config-model.ts) when one is
 * available.
 */
import { emit, type PageConfig, type DisplayItem, type ButtonConfig, type ConnectorConfig } from './app-state';

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
// Empty = layout unknown (sample data / config without info.connectors).
let currentConnectors: ConnectorConfig[] = [];

export function getConfig(): PageConfig[] {
  return currentConfig;
}

export function getConnectors(): ConnectorConfig[] {
  return currentConnectors;
}

export function setConfig(config: PageConfig[], connectors: ConnectorConfig[] = []): void {
  currentConfig = config;
  currentConnectors = connectors;
  emit('device-config', { pages: config, connectors });
}

export function loadDefaultConfig(): void {
  setConfig(structuredClone(defaultConfig));
}

export function updateDisplayItem(pageNumber: number, index: number, item: Partial<DisplayItem>): void {
  const page = currentConfig.find(p => p.number === pageNumber);
  if (!page || index < 0 || index >= page.displayItems.length) return;
  Object.assign(page.displayItems[index], item);
  emit('device-config', { pages: currentConfig, connectors: currentConnectors });
}

export function updateButton(pageNumber: number, position: number, updates: Partial<ButtonConfig>): void {
  const page = currentConfig.find(p => p.number === pageNumber);
  if (!page) return;
  const btn = page.buttons.find(b => b.position === position);
  if (!btn) return;
  Object.assign(btn, updates);
  emit('device-config', { pages: currentConfig, connectors: currentConnectors });
}
