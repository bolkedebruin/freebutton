import { LitElement, html, svg } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on, type PageConfig } from '../js/app-state.js';
import { getConfig, loadDefaultConfig } from '../js/device-config.js';
import { SettingsIcon } from '../icons/index.js';

interface MemorySample {
  heap: number;
  lvgl: number;
  js: number;
}

const MAX_SAMPLES = 60;
const GRAPH_W = 282;
const GRAPH_H = 56;

@customElement('fb-virtual-device')
export class FbVirtualDevice extends LitElement {
  createRenderRoot() { return this; }

  @state() private currentPage = 1;
  @state() private _pages: PageConfig[] = [];
  @state() private _flipped = false;
  @state() private _deviceName = 'btn_fab028';
  @state() private _uptimeSeconds = 720; // 12 minutes
  @state() private _memorySamples: MemorySample[] = [];

  private _unsub?: () => void;
  private _uptimeInterval?: ReturnType<typeof setInterval>;
  private _memoryInterval?: ReturnType<typeof setInterval>;

  // Simulated base values (KB free)
  private _heapBase = 180;
  private _lvglBase = 48;
  private _jsBase = 32;

  connectedCallback() {
    super.connectedCallback();
    this._unsub = on('device-config', (e) => {
      this._pages = e.detail.pages;
    });
    const cfg = getConfig();
    if (cfg.length > 0) {
      this._pages = cfg;
    } else {
      loadDefaultConfig();
    }
    this._uptimeInterval = setInterval(() => {
      this._uptimeSeconds++;
    }, 1000);
    // Seed initial samples and start updating
    this._seedMemorySamples();
    this._memoryInterval = setInterval(() => this._pushMemorySample(), 2000);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsub?.();
    if (this._uptimeInterval) clearInterval(this._uptimeInterval);
    if (this._memoryInterval) clearInterval(this._memoryInterval);
  }

  private _jitter(base: number, range: number): number {
    return base + (Math.random() - 0.5) * range;
  }

  private _seedMemorySamples() {
    const samples: MemorySample[] = [];
    for (let i = 0; i < MAX_SAMPLES; i++) {
      samples.push({
        heap: this._jitter(this._heapBase, 20),
        lvgl: this._jitter(this._lvglBase, 8),
        js: this._jitter(this._jsBase, 6),
      });
    }
    this._memorySamples = samples;
  }

  private _pushMemorySample() {
    const prev = this._memorySamples[this._memorySamples.length - 1];
    // Random walk from previous value, clamped
    const clamp = (v: number, min: number, max: number) => Math.max(min, Math.min(max, v));
    const sample: MemorySample = {
      heap: clamp(prev.heap + (Math.random() - 0.5) * 8, 140, 220),
      lvgl: clamp(prev.lvgl + (Math.random() - 0.5) * 4, 30, 64),
      js: clamp(prev.js + (Math.random() - 0.5) * 3, 16, 48),
    };
    const next = [...this._memorySamples, sample];
    this._memorySamples = next.length > MAX_SAMPLES ? next.slice(next.length - MAX_SAMPLES) : next;
  }

  private _selectPage(page: number) {
    this.currentPage = page;
  }

  private _toggleFlip() {
    this._flipped = !this._flipped;
  }

  private _formatUptime(totalSeconds: number): string {
    const days = Math.floor(totalSeconds / 86400);
    const hours = Math.floor((totalSeconds % 86400) / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    return `${days}d ${hours}h ${minutes}m`;
  }

  private _toPolyline(samples: MemorySample[], key: keyof MemorySample, maxVal: number): string {
    const stepX = GRAPH_W / (MAX_SAMPLES - 1);
    return samples
      .map((s, i) => `${(i * stepX).toFixed(1)},${(GRAPH_H - (s[key] / maxVal) * GRAPH_H).toFixed(1)}`)
      .join(' ');
  }

  private _renderMemoryGraph() {
    const samples = this._memorySamples;
    if (samples.length < 2) return html``;

    const maxVal = 320; // 320KB total heap
    const latest = samples[samples.length - 1];

    const lines: { key: keyof MemorySample; color: string; label: string }[] = [
      { key: 'heap', color: '#6B26FF', label: 'Heap' },
      { key: 'lvgl', color: '#19D95D', label: 'LVGL' },
      { key: 'js', color: '#F2A359', label: 'JS' },
    ];

    return html`
      <div class="flex flex-col gap-1" data-testid="memory-graph">
        <div class="flex items-center justify-between">
          <span class="text-[11px] text-gray-200">Memory (KB free)</span>
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
          <svg width="${GRAPH_W}" height="${GRAPH_H}" viewBox="0 0 ${GRAPH_W} ${GRAPH_H}" class="w-full" preserveAspectRatio="none">
            <!-- Grid lines -->
            ${[0.25, 0.5, 0.75].map(frac => svg`
              <line x1="0" y1="${GRAPH_H * frac}" x2="${GRAPH_W}" y2="${GRAPH_H * frac}"
                stroke="rgba(255,255,255,0.06)" stroke-width="1" />
            `)}
            <!-- Data lines -->
            ${lines.map(l => svg`
              <polyline
                points="${this._toPolyline(samples, l.key, maxVal)}"
                fill="none" stroke="${l.color}" stroke-width="1.5"
                stroke-linejoin="round" stroke-linecap="round"
                data-series="${l.key}" />
            `)}
          </svg>
        </div>
        <div class="flex gap-3 text-[10px] text-gray-200 font-mono">
          ${lines.map(l => html`
            <span><span style="color:${l.color}">${Math.round(latest[l.key])}</span> ${l.label}</span>
          `)}
        </div>
      </div>
    `;
  }

  private _renderSettingsCard() {
    const infoRows: [string, string][] = [
      ['IPv4', '192.168.1.42'],
      ['IPv6', 'fe80::1'],
      ['SSID', 'HomeNetwork'],
      ['MAC', 'AA:BB:CC:DD:EE:FF'],
      ['Firmware', '3.2.0'],
      ['Uptime', this._formatUptime(this._uptimeSeconds)],
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
          <button
            class="p-2 rounded-lg hover:bg-white/10 cursor-pointer transition-colors text-gray-200 hover:text-white"
            @click=${() => this._toggleFlip()}
            data-testid="gear-icon"
          >
            ${SettingsIcon('w-5 h-5')}
          </button>
        </div>

        <!-- Flip container -->
        <div class="device-flip-container w-[342px]" style="height: 420px;">
          <div class="device-flip-inner ${this._flipped ? 'flipped' : ''} relative h-full">
            <!-- Front face -->
            <div class="device-flip-front w-full font-fira text-[11.2px]">
              <fb-tft-display .currentPage=${this.currentPage} .displayItems=${currentPageConfig?.displayItems ?? []}></fb-tft-display>
              <fb-button-grid .currentPage=${this.currentPage} .buttons=${currentPageConfig?.buttons ?? []}></fb-button-grid>
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
