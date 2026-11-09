import { LitElement, html, svg } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on, type PageConfig, type DeviceInfoData, type ConnectorConfig, type PreviewState } from '../js/app-state.js';
import { getConfig, getConnectors, getPreviewState, loadDefaultConfig } from '../js/device-config.js';
import { positionsForConnector } from '../js/config-model.js';
import { setDeviceInfoVisible } from '../js/device-info.js';
import { SettingsIcon } from '../icons/index.js';

// One point of the free-memory history, in BYTES (as reported by the device).
interface MemorySample {
  heap: number;
  lvgl: number;
  js: number;   // 0 on firmware without the JS heap figures
}

const MAX_SAMPLES = 60;
const GRAPH_W = 282;
const GRAPH_H = 56;
const DASH = '—'; // em dash placeholder when no device is connected
// Front-face geometry (px) used to size the flip container per layout.
const TFT_H = 219;
const KEY_ROW_H = 57.1 + 6.72;
const MIN_FACE_H = 420;

function formatKB(bytes: number): string {
  return `${Math.round(bytes / 1024)} KB`;
}

/** "3.1.0-alpha.1 (30df729-dirty)"; just the version for older firmware. */
export function formatFirmware(info: { firmware: string; commit?: string; dirty?: boolean }): string {
  if (!info.commit) return info.firmware;
  return `${info.firmware} (${info.commit}${info.dirty ? '-dirty' : ''})`;
}

@customElement('fb-virtual-device')
export class FbVirtualDevice extends LitElement {
  createRenderRoot() { return this; }

  @state() private currentPage = 1;
  @state() private _pages: PageConfig[] = [];
  // Physical layout from config.json info.connectors; empty = legacy layout.
  @state() private _connectors: ConnectorConfig[] = [];
  // Showing unsaved config.json edits ('stale': the text does not parse right now).
  @state() private _preview: PreviewState | null = null;
  @state() private _flipped = false;
  @state() private _deviceName = 'btn_fab028';
  @state() private _ntpServer = 'pool.ntp.org';
  // Live device info, or null when no device is connected (dev/simulator).
  @state() private _info: DeviceInfoData | null = null;
  @state() private _memorySamples: MemorySample[] = [];

  private _unsub?: () => void;
  private _unsubInfo?: () => void;
  // The device name field mirrors the device on first contact, then stays under
  // the user's control so live polling never clobbers what they typed.
  private _deviceNameSeeded = false;

  connectedCallback() {
    super.connectedCallback();
    this._unsub = on('device-config', (e) => {
      this._pages = e.detail.pages;
      this._connectors = e.detail.connectors ?? [];
      this._preview = e.detail.preview ?? null;
      if (!this._pages.some(p => p.number === this.currentPage)) {
        this.currentPage = this._pages[0]?.number ?? 1;
      }
    });
    this._connectors = getConnectors();
    this._preview = getPreviewState();
    this._unsubInfo = on('device-info', (e) => this._applyInfo(e.detail.data));
    const cfg = getConfig();
    if (cfg.length > 0) {
      this._pages = cfg;
    } else {
      loadDefaultConfig();
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsub?.();
    this._unsubInfo?.();
    setDeviceInfoVisible(false);
  }

  /** Apply a device-info update: null clears to placeholders, data appends a memory sample. */
  private _applyInfo(data: DeviceInfoData | null) {
    if (!data) {
      // Already cleared: assigning a fresh [] would change identity and force a
      // full re-render of byte-identical output on every failed poll.
      if (this._info === null && this._memorySamples.length === 0) return;
      this._info = null;
      this._memorySamples = [];
      return;
    }
    this._info = data;
    if (!this._deviceNameSeeded && data.deviceName) {
      this._deviceName = data.deviceName;
      this._deviceNameSeeded = true;
    }
    const next = [...this._memorySamples, { heap: data.heapFree, lvgl: data.lvglFree, js: data.jsHeapFree ?? 0 }];
    this._memorySamples = next.length > MAX_SAMPLES ? next.slice(next.length - MAX_SAMPLES) : next;
  }

  private _selectPage(page: number) {
    this.currentPage = page;
  }

  private _toggleFlip() {
    this._flipped = !this._flipped;
    // The back face is the only consumer of device info -- don't poll the
    // device while the front face is showing.
    setDeviceInfoVisible(this._flipped);
  }

  private _formatUptime(totalSeconds: number): string {
    const days = Math.floor(totalSeconds / 86400);
    const hours = Math.floor((totalSeconds % 86400) / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    return `${days}d ${hours}h ${minutes}m`;
  }

  private _toPolyline(samples: MemorySample[], key: keyof MemorySample, maxVal: number): string {
    const stepX = GRAPH_W / (MAX_SAMPLES - 1);
    // Each series is normalized to its OWN total, so Heap (~320 KB) and LVGL
    // (~64 KB) are both fully visible; clamp to the plot so a value can never
    // render above the chart.
    return samples
      .map((s, i) => {
        const frac = Math.max(0, Math.min(1, s[key] / maxVal));
        return `${(i * stepX).toFixed(1)},${(GRAPH_H - frac * GRAPH_H).toFixed(1)}`;
      })
      .join(' ');
  }

  private _renderMemoryGraph() {
    const samples = this._memorySamples;

    // Each line scales to its own total so both series use the full plot height.
    const lines: { key: keyof MemorySample; color: string; label: string; total: number }[] = [
      { key: 'heap', color: '#6B26FF', label: 'Heap', total: this._info?.heapTotal ?? 320 * 1024 },
      { key: 'lvgl', color: '#19D95D', label: 'LVGL', total: this._info?.lvglTotal ?? 64 * 1024 },
    ];
    // JS heap: firmware that reports it (jsHeapSize); free = for objects and the stack.
    if (this._info?.jsHeapSize) {
      lines.push({ key: 'js', color: '#F5A524', label: 'JS', total: this._info.jsHeapSize });
    }

    const latest = samples[samples.length - 1];

    return html`
      <div class="flex flex-col gap-1" data-testid="memory-graph">
        <div class="flex items-center justify-between">
          <span class="text-[11px] text-gray-200">Memory (free)</span>
          <div class="flex gap-2.5">
            ${lines.map(l => html`
              <span class="flex items-center gap-1">
                <span class="inline-block w-1.5 h-1.5 rounded-full" style="background:${l.color}"></span>
                <span class="text-[10px] text-gray-200">${l.label}</span>
              </span>
            `)}
          </div>
        </div>
        <div class="bg-white/5 rounded-lg p-1.5 border border-white/5">
          ${samples.length < 1
            ? html`<div class="h-[56px] flex items-center justify-center text-[11px] text-gray-200"
                        data-testid="memory-empty">Waiting for device…</div>`
            : html`
              <svg width="${GRAPH_W}" height="${GRAPH_H}" viewBox="0 0 ${GRAPH_W} ${GRAPH_H}" class="w-full" preserveAspectRatio="none">
                <!-- Grid lines -->
                ${[0.25, 0.5, 0.75].map(frac => svg`
                  <line x1="0" y1="${GRAPH_H * frac}" x2="${GRAPH_W}" y2="${GRAPH_H * frac}"
                    stroke="rgba(255,255,255,0.06)" stroke-width="1" />
                `)}
                <!-- Data lines -->
                ${lines.map(l => svg`
                  <polyline
                    points="${this._toPolyline(samples, l.key, l.total)}"
                    fill="none" stroke="${l.color}" stroke-width="1.5"
                    stroke-linejoin="round" stroke-linecap="round"
                    data-series="${l.key}" />
                `)}
              </svg>
            `}
        </div>
        <div class="flex gap-3 text-[10px] text-gray-200 font-mono">
          ${lines.map(l => html`
            <span><span style="color:${l.color}">${latest ? formatKB(latest[l.key]) : DASH}</span> ${l.label}</span>
          `)}
          <span title="Lowest free internal heap since boot" data-testid="heap-min">min
            <span style="color:#6B26FF">${this._info ? formatKB(this._info.heapMinFree) : DASH}</span></span>
        </div>
      </div>
    `;
  }

  private _renderSettingsCard() {
    const i = this._info;
    const infoRows: [string, string][] = [
      ['IPv4', i?.ipv4 ?? DASH],
      ['IPv6', i?.ipv6 ?? DASH],
      ['SSID', i?.ssid ?? DASH],
      ['MAC', i?.mac ?? DASH],
      ['Signal', i ? `${i.rssi} dBm` : DASH],
      ['Firmware', i ? formatFirmware(i) : DASH],
      ['Model', i?.model || DASH],
      ['Uptime', i ? this._formatUptime(i.uptime) : DASH],
    ];

    return html`
      <div class="w-full h-full bg-[rgb(20,20,20)] rounded-2xl p-3.5 flex flex-col gap-2.5">
        <!-- Header -->
        <div class="flex items-center justify-between">
          <span class="text-[13px] font-medium text-gray-100">Device Info</span>
          <button
            class="p-1 rounded-lg hover:bg-white/10 cursor-pointer transition-colors text-gray-200 hover:text-white"
            @click=${() => this._toggleFlip()}
            data-testid="settings-back"
          >
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <line x1="18" y1="6" x2="6" y2="18" /><line x1="6" y1="6" x2="18" y2="18" />
            </svg>
          </button>
        </div>

        <!-- Device name -->
        <div class="flex flex-col gap-1">
          <label class="text-[11px] text-gray-200">Device Name</label>
          <input
            type="text"
            .value=${this._deviceName}
            @input=${(e: Event) => { this._deviceName = (e.target as HTMLInputElement).value; }}
            class="bg-white/5 border border-white/10 rounded-lg px-2.5 py-1.5 text-[13px] text-white outline-none focus:border-purple-80 transition-colors"
            data-testid="device-name-input"
          />
        </div>

        <!-- NTP Server -->
        <div class="flex flex-col gap-1">
          <label class="text-[11px] text-gray-200">NTP Server</label>
          <input
            type="text"
            .value=${this._ntpServer}
            @input=${(e: Event) => { this._ntpServer = (e.target as HTMLInputElement).value; }}
            class="bg-white/5 border border-white/10 rounded-lg px-2.5 py-1.5 text-[13px] text-white font-mono outline-none focus:border-purple-80 transition-colors"
            placeholder="pool.ntp.org"
            data-testid="ntp-server-input"
          />
        </div>

        <!-- Info rows -->
        <div class="flex flex-col gap-1.5">
          ${infoRows.map(([label, value]) => html`
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-gray-200">${label}</span>
              <span class="text-[11px] text-white font-mono" data-field=${label}>${value}</span>
            </div>
          `)}
        </div>

        <!-- Memory graph -->
        ${this._renderMemoryGraph()}
      </div>
    `;
  }

  /**
   * Front face. With a known layout every connector renders top to bottom in
   * id order: a large display shows the TFT above its two keys, a bar shows
   * its two keys. Without one, the TFT plus the configured keys (legacy).
   */
  private _renderFront(page: PageConfig | undefined) {
    const buttons = page?.buttons ?? [];
    const items = page?.displayItems ?? [];
    const lists = page?.lists ?? [];
    const slots = this._connectors.filter(c => c.type !== 'empty');
    if (slots.length === 0) {
      return html`
        <fb-tft-display .currentPage=${this.currentPage} .displayItems=${items}
          .lists=${lists}></fb-tft-display>
        <fb-button-grid .currentPage=${this.currentPage} .buttons=${buttons}></fb-button-grid>
      `;
    }
    return slots.map(c => html`
      ${c.type === 'display'
        ? html`<fb-tft-display .currentPage=${this.currentPage} .displayItems=${items}
          .lists=${lists}></fb-tft-display>`
        : ''}
      <fb-button-grid data-connector=${c.id} .currentPage=${this.currentPage} .buttons=${buttons}
        .positions=${positionsForConnector(c.id)}></fb-button-grid>
    `);
  }

  /** Subtle note while the preview shows unsaved config.json edits. */
  private _renderPreviewHint() {
    if (!this._preview) return '';
    const stale = this._preview === 'stale';
    return html`<span class="ml-auto mr-1 text-[10px] font-mono ${stale ? 'text-status-orange' : 'text-gray-200'}"
      data-testid="preview-hint" data-state=${this._preview}
      title=${stale ? 'config.json is not valid JSON: showing the last valid preview' : 'Preview of unsaved config.json edits'}
      >${stale ? 'unsaved · invalid JSON' : 'unsaved'}</span>`;
  }

  private _faceHeight(): number {
    const slots = this._connectors.filter(c => c.type !== 'empty');
    if (slots.length === 0) return MIN_FACE_H;
    const h = slots.reduce((sum, c) => sum + KEY_ROW_H + (c.type === 'display' ? TFT_H : 0), 0);
    return Math.max(MIN_FACE_H, Math.ceil(h));
  }

  render() {
    const pages = this._pages.length > 0 ? this._pages : getConfig();
    const currentPageConfig = pages.find(p => p.number === this.currentPage);

    return html`
      <div class="flex flex-col items-center">
        <!-- Tab bar row with gear icon -->
        <div class="flex items-center justify-between w-[342px] mb-4">
          <div class="flex w-max rounded-xl p-1 bg-white/5 backdrop-blur-md border border-white/10">
            ${pages.map(page => html`
              <button
                class="px-5 py-2 text-sm rounded-lg font-medium cursor-pointer transition-all duration-200
                  ${page.number === this.currentPage
                    ? 'bg-white/95 text-bg-default shadow-sm'
                    : 'bg-transparent text-gray-100 hover:text-text-default'}"
                @click=${() => this._selectPage(page.number)}
              >Page ${page.number}</button>
            `)}
          </div>
          ${this._renderPreviewHint()}
          <button
            class="p-2 rounded-lg hover:bg-white/10 cursor-pointer transition-colors text-gray-200 hover:text-white"
            @click=${() => this._toggleFlip()}
            data-testid="gear-icon"
          >
            ${SettingsIcon('w-5 h-5')}
          </button>
        </div>

        <!-- Flip container -->
        <div class="device-flip-container w-[342px]" style="height: ${this._faceHeight()}px;">
          <div class="device-flip-inner ${this._flipped ? 'flipped' : ''} relative h-full">
            <!-- Front face -->
            <div class="device-flip-front w-full font-fira text-[11.2px]">
              ${this._renderFront(currentPageConfig)}
            </div>
            <!-- Back face -->
            <div class="device-flip-back">
              ${this._renderSettingsCard()}
            </div>
          </div>
        </div>
      </div>
    `;
  }
}
