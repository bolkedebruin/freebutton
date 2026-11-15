import { LitElement, html } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { on } from '../js/app-state';
import { emulatorSession, type VirtualInfo } from '../js/emulator-session';
import { CONFIG_KEY, MAIN_SCRIPT_KEY } from '../js/fs-rules';
import { workspace } from '../js/workspace';

/** The values are measured this often while the info side is shown. */
export const INFO_INTERVAL_MS = 1000;

const CONNECTOR = ['none', 'bar', 'display'] as const;
const CONNECTOR_LONG = ['none', 'bar (two keys)', 'main display'] as const;

/** "42 s", "3 min 05 s", "2 h 07 min": the seconds show that it runs. */
export function formatRunTime(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  const two = (n: number) => String(n).padStart(2, '0');
  if (s < 60) return `${s} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min ${two(s % 60)} s`;
  return `${Math.floor(s / 3600)} h ${two(Math.floor((s % 3600) / 60))} min`;
}

function kbNumber(bytes: number): string {
  const k = (bytes / 1024).toFixed(1);
  return k.endsWith('.0') ? k.slice(0, -2) : k;
}

function kb(bytes: number): string {
  return kbNumber(bytes) + ' KB';
}

/**
 * The info side of the virtual device: what the emulator in this page is
 * and does (emulatorSession.info()). Nothing here is the real device's and
 * nothing is asked of it; the real device has the Device view.
 *
 * Measured when `active` (the side is shown), then once a second.
 */
@customElement('fb-virtual-info')
export class FbVirtualInfo extends LitElement {
  createRenderRoot() { return this; }

  /** The side is shown: measure. */
  @property({ type: Boolean }) active = false;
  @state() private _info: VirtualInfo = emulatorSession.info();
  private _timer?: ReturnType<typeof setInterval>;
  private _offs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._offs.push(
      on('emulator-change', () => { if (this.active) this._measure(); }),
      on('workspace-change', () => { if (this.active) this.requestUpdate(); }),
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._offs.forEach(off => off());
    this._offs = [];
    this._stop();
  }

  protected willUpdate(changed: Map<PropertyKey, unknown>) {
    if (!changed.has('active')) return;
    this._stop();
    if (this.active) {
      this._info = emulatorSession.info();
      this._timer = setInterval(() => this._measure(), INFO_INTERVAL_MS);
    }
  }

  private _measure() {
    this._info = emulatorSession.info();
  }

  private _stop() {
    if (this._timer) clearInterval(this._timer);
    this._timer = undefined;
  }

  /** What Run takes for a file: the editor's text when it is the open file, else the stored one. */
  private _source(key: string): string {
    const ws = workspace.snapshot();
    if (ws.currentKey === key) return ws.dirty || ws.isNew ? 'the editor (unsaved changes)' : 'the editor';
    return ws.files.some(f => f.key === key) ? 'the stored file' : 'missing';
  }

  /** One line: a label and its value; a long value is cut short, with the whole of it as tooltip. */
  private _row(label: string, value: unknown, title = '') {
    return html`
      <div class="flex justify-between gap-3 items-baseline min-w-0">
        <span class="text-[11px] text-gray-200 shrink-0">${label}</span>
        <span class="text-[11px] text-gray-100 font-mono text-right truncate min-w-0" title=${title}>${value}</span>
      </div>`;
  }

  /** used of limit: label and value on one line, a thin bar under it (red when nearly full). Two share a row. */
  private _meter(label: string, text: string, used: number, limit: number, field: string, title: string) {
    const part = limit > 0 ? Math.min(1, Math.max(0, used / limit)) : 0;
    const tight = part > 0.9;
    return html`
      <div class="flex flex-col gap-[3px] min-w-0" title=${title}>
        <div class="flex justify-between gap-2 items-baseline min-w-0">
          <span class="text-[11px] text-gray-200 truncate">${label}</span>
          <span class="text-[11px] font-mono shrink-0 ${tight ? 'text-status-red' : 'text-gray-100'}" data-field=${field}>${text}</span>
        </div>
        <div class="h-[3px] rounded-full bg-white/10 overflow-hidden" role="progressbar" aria-label=${label}
          aria-valuemin="0" aria-valuemax=${limit} aria-valuenow=${used}>
          <div class="h-full rounded-full ${tight ? 'bg-status-red' : 'bg-purple-80'}" style="width: ${(part * 100).toFixed(1)}%;"></div>
        </div>
      </div>`;
  }

  render() {
    const i = this._info;
    const running = i.status === 'running';
    const state = i.status === 'running' ? 'running' : i.status === 'starting' ? 'starting' : i.status === 'error' ? 'stopped with an error' : 'not running';
    const stateColor = running ? 'text-status-green' : i.status === 'error' ? 'text-status-red' : 'text-gray-100';
    const modules = i.connectors ? i.connectors.map((kind, id) => (kind === 0 ? '' : `${id}: ${CONNECTOR[kind]}`)).filter(Boolean) : [];
    const brokers = i.brokers ? (i.brokers.length ? i.brokers.join(', ') : 'none in config.json') : '';
    return html`
      <div class="flex flex-col gap-1.5 min-w-0" data-testid="virtual-info" data-status=${i.status}>
        ${this._row('Device', html`<span data-field="identity">${i.identity}</span>`, 'system.id() in the script: this browser\'s virtual device, never the real one')}
        ${this._row('State', html`<span class=${stateColor} data-field="state">${state}</span>${running && i.uptimeSeconds !== null
          ? html`<span class="text-gray-200"> · </span><span data-field="uptime">${formatRunTime(i.uptimeSeconds)}</span>` : ''}`,
          running ? 'The script, and how long it has run' : '')}
        ${i.error ? html`<p class="text-[11px] leading-snug text-status-red break-words line-clamp-2" data-field="error" title=${i.error}>${i.error}</p>` : ''}

        ${running ? html`
          <div class="grid grid-cols-2 gap-x-4 gap-y-1.5 pt-0.5">
            ${i.heap ? this._meter('Heap free', `${kbNumber(i.heap.free)} / ${kb(i.heap.size)}`, i.heap.size - i.heap.free, i.heap.size, 'heap',
              'JavaScript heap of the script: free of the device\'s, after a garbage collection. The bar is what is used.') : ''}
            ${i.bytecode ? this._meter('Bytecode', `${kbNumber(i.bytecode.bytes)} / ${kb(i.bytecode.limit)}`, i.bytecode.bytes, i.bytecode.limit, 'bytecode',
              'Size of the running script\'s bytecode, of what the device stores') : ''}
            ${i.timers ? this._meter('Timers', `${i.timers.used} / ${i.timers.limit}`, i.timers.used, i.timers.limit, 'timers',
              'Pending setTimeout timers, of the firmware\'s slots') : ''}
            ${i.subscriptions && i.subscriptions.limit > 0
              ? this._meter('Subscriptions', `${i.subscriptions.used} / ${i.subscriptions.limit}`, i.subscriptions.used, i.subscriptions.limit, 'subscriptions',
                'MQTT subscriptions of the script, of the firmware\'s room for them') : ''}
          </div>
          ${i.mqttDropped ? this._row('MQTT events lost', html`<span class="text-status-red" data-field="dropped">${i.mqttDropped}</span>`,
            'Events lost because the script did not keep up') : ''}
        ` : html`
          <p class="text-[11px] leading-snug text-gray-100" data-field="hint">Run (in the toolbar) starts the script in this page, with:</p>
          ${this._row(`/${MAIN_SCRIPT_KEY}`, html`<span data-field="source-script">${this._source(MAIN_SCRIPT_KEY)}</span>`)}
          ${this._row(`/${CONFIG_KEY}`, html`<span data-field="source-config">${this._source(CONFIG_KEY)}</span>`)}
        `}

        <div class="border-t border-white/10"></div>
        ${this._row('MQTT', html`<span data-field="mqtt">offline: in-page broker</span>${brokers
          ? html`<span class="text-gray-200"> · </span><span data-field="brokers">${brokers}</span>` : ''}`,
          `Nothing is sent to a real broker.${brokers ? ' Brokers of config.json: ' + brokers : ''}`)}
        ${modules.length ? this._row('Modules', html`<span data-field="modules">${modules.join(' · ')}</span>`, `Module per connector: ${i.connectors!.map((kind, id) => (kind === 0 ? '' : `${id}: ${CONNECTOR_LONG[kind]}`)).filter(Boolean).join(', ')}`) : ''}
        ${i.sensors !== null ? this._row('Sensors', html`<span data-field="sensors">${i.sensors}</span>`) : ''}
        ${i.model ? this._row('Model', html`<span data-field="model">${i.model}</span>`, i.model) : ''}
        ${i.firmware ? this._row('Engine built from', html`<span data-field="firmware">${i.firmware}</span>`, `The firmware the script engine in this page was built from: ${i.firmware}`) : ''}
        <p class="text-[10px] leading-snug text-gray-200">The real device is in the Device view (left bar).</p>
      </div>
    `;
  }
}
