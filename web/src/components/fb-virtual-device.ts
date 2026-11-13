import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import {
  emit, on, sameConfigRef, type ConfigRef, type PageConfig, type ConnectorConfig, type PreviewState,
} from '../js/app-state.js';
import './fb-device-info.js';
import { getConfig, getConnectors, getPreviewState, loadDefaultConfig } from '../js/device-config.js';
import { positionsForConnector } from '../js/config-model.js';
import { setDeviceInfoVisible } from '../js/device-info.js';
import { InfoIcon } from '../icons/index.js';

// Front-face geometry (px) used to size the flip container per layout.
const TFT_H = 219;
const KEY_ROW_H = 57.1 + 6.72;
const MIN_FACE_H = 420;

// Kept for callers that import it from here.
export { formatFirmware } from './fb-device-info.js';

/** Who polls the device info for the flip side (device-info.ts). */
const INFO_WATCHER = 'flip';

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
  // The device's own name, once it reported it ('' until then).
  @state() private _deviceName = '';
  // The config.json entry under the editor cursor (outlined on the device).
  @state() private _selected: ConfigRef | null = null;

  private _unsubs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._unsubs.push(
      on('device-config', (e) => {
        this._pages = e.detail.pages;
        this._connectors = e.detail.connectors ?? [];
        this._preview = e.detail.preview ?? null;
        if (!this._pages.some(p => p.number === this.currentPage)) {
          this.currentPage = this._pages[0]?.number ?? 1;
        }
      }),
      on('device-info', (e) => { this._deviceName = e.detail.data?.deviceName ?? ''; }),
      on('config-selection', (e) => this._select(e.detail.ref)),
    );
    this._connectors = getConnectors();
    this._preview = getPreviewState();
    const cfg = getConfig();
    if (cfg.length > 0) {
      this._pages = cfg;
    } else {
      loadDefaultConfig();
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
    setDeviceInfoVisible(false, INFO_WATCHER);
  }

  /** Outline the entry the editor cursor is in; show a page it is on. */
  private _select(ref: ConfigRef | null) {
    this._selected = ref;
    if (!ref) return;
    const has = (p: PageConfig) => [...p.buttons, ...p.displayItems, ...(p.lists ?? []), ...(p.images ?? [])]
      .some(item => sameConfigRef(item.ref, ref));
    const pages = this._pages.length > 0 ? this._pages : getConfig();
    if (pages.find(p => p.number === this.currentPage && has(p))) return;
    const page = pages.find(has);
    if (page) this.currentPage = page.number;
    // The front shows it: an outline on the back side would be invisible.
    if (page && this._flipped) this._toggleFlip();
  }

  /** A click on a drawn entry (data-ref="list:index"): select it in config.json. */
  private _onFrontClick(e: Event) {
    const el = (e.target as Element | null)?.closest?.('[data-ref]');
    const [list, index] = (el?.getAttribute('data-ref') ?? '').split(':');
    const kind = el?.getAttribute('data-ref-kind') as ConfigRef['kind'] | null;
    if (!list || !kind || index === undefined) return;
    emit('config-reveal', { ref: { kind, list, index: Number(index) } });
  }

  private _selectPage(page: number) {
    this.currentPage = page;
  }

  private _toggleFlip() {
    this._flipped = !this._flipped;
    // Don't poll the device for the back face while the front is showing.
    setDeviceInfoVisible(this._flipped, INFO_WATCHER);
  }

  /** Back face: read-only device info (the Device view in the main pane has the actions). */
  private _renderInfoCard() {
    return html`
      <div class="w-full h-full bg-[rgb(20,20,20)] rounded-2xl p-3.5 flex flex-col gap-2.5 overflow-y-auto custom-scrollbar">
        <div class="flex items-center justify-between">
          <span class="text-[13px] font-medium text-gray-100">Device info</span>
          <button
            class="p-1 rounded-lg hover:bg-white/10 cursor-pointer transition-colors text-gray-200 hover:text-white"
            title="Back to the device" aria-label="Back to the device"
            @click=${() => this._toggleFlip()}
            data-testid="settings-back"
          >
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <line x1="18" y1="6" x2="6" y2="18" /><line x1="6" y1="6" x2="18" y2="18" />
            </svg>
          </button>
        </div>
        <fb-device-info></fb-device-info>
      </div>
    `;
  }

  /**
   * Front face. With a known layout every connector renders top to bottom in
   * id order, as on the device: a large display is the TFT only (its two keys,
   * fb.main.left / right, are presses on the left / right half of the screen,
   * with no key row or mini displays of their own), a bar shows its two keys.
   * Without a layout, the TFT plus the configured keys (legacy).
   */
  private _renderFront(page: PageConfig | undefined) {
    const buttons = page?.buttons ?? [];
    const items = page?.displayItems ?? [];
    const lists = page?.lists ?? [];
    const images = page?.images ?? [];
    const slots = this._connectors.filter(c => c.type !== 'empty');
    if (slots.length === 0) {
      return html`
        <fb-tft-display .currentPage=${this.currentPage} .displayItems=${items} .deviceName=${this._deviceName}
          .lists=${lists} .images=${images} .selected=${this._selected}></fb-tft-display>
        <fb-button-grid .currentPage=${this.currentPage} .buttons=${buttons} .selected=${this._selected}></fb-button-grid>
      `;
    }
    return slots.map(c => c.type === 'display'
      ? html`<fb-tft-display data-connector=${c.id} .currentPage=${this.currentPage} .displayItems=${items}
          .deviceName=${this._deviceName} .lists=${lists} .images=${images} .selected=${this._selected}></fb-tft-display>`
      : html`<fb-button-grid data-connector=${c.id} .currentPage=${this.currentPage} .buttons=${buttons}
          .positions=${positionsForConnector(c.id)} .selected=${this._selected}></fb-button-grid>`);
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
    const h = slots.reduce((sum, c) => sum + (c.type === 'display' ? TFT_H : KEY_ROW_H), 0);
    return Math.max(MIN_FACE_H, Math.ceil(h));
  }

  render() {
    const pages = this._pages.length > 0 ? this._pages : getConfig();
    const currentPageConfig = pages.find(p => p.number === this.currentPage);

    return html`
      <div class="flex flex-col items-center">
        <!-- Tab bar row with the info toggle. The tabs belong to the front face:
             hidden (space kept) while the back is showing. -->
        <div class="flex items-center justify-between w-[342px] mb-4">
          <div class="flex w-max rounded-xl p-1 bg-white/5 backdrop-blur-md border border-white/10 ${this._flipped ? 'invisible' : ''}"
            data-testid="page-tabs" aria-hidden=${this._flipped ? 'true' : 'false'}>
            ${pages.map(page => html`
              <button
                class="px-5 py-2 text-sm rounded-lg font-medium cursor-pointer transition-all duration-200
                  ${page.number === this.currentPage
                    ? 'bg-white/95 text-bg-default shadow-sm'
                    : 'bg-transparent text-gray-100 hover:text-text-default'}"
                ?disabled=${this._flipped}
                @click=${() => this._selectPage(page.number)}
              >Page ${page.number}</button>
            `)}
          </div>
          ${this._flipped ? '' : this._renderPreviewHint()}
          <button
            class="p-2 rounded-lg hover:bg-white/10 cursor-pointer transition-colors ${this._flipped ? 'text-white bg-white/10' : 'text-gray-200 hover:text-white'}"
            title=${this._flipped ? 'Back to the device' : 'Device info'}
            aria-label="Device info" aria-pressed=${this._flipped ? 'true' : 'false'}
            @click=${() => this._toggleFlip()}
            data-testid="gear-icon"
          >
            ${InfoIcon('w-5 h-5')}
          </button>
        </div>

        <!-- Flip container -->
        <div class="device-flip-container w-[342px]" style="height: ${this._faceHeight()}px;">
          <div class="device-flip-inner ${this._flipped ? 'flipped' : ''} relative h-full">
            <!-- Front face -->
            <div class="device-flip-front w-full font-fira text-[11.2px]" @click=${this._onFrontClick}>
              ${this._renderFront(currentPageConfig)}
            </div>
            <!-- Back face -->
            <div class="device-flip-back">
              ${this._renderInfoCard()}
            </div>
          </div>
        </div>
      </div>
    `;
  }
}
