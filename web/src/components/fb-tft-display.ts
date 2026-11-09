import { LitElement, html, svg } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import type { DisplayItem, ListConfig } from '../js/app-state.js';
import { FONT_PX } from '../js/config-model.js';

// The preview screen is 283 px wide for the device's 320 x 240.
const SCREEN_SCALE = 283 / 320;
// Roller row height relative to the font size (about LVGL's line height).
const ROW_LINE_HEIGHT = 1.25;

function px(n: number): string {
  return `${Math.round(n * 100) / 100}px`;
}

@customElement('fb-tft-display')
export class FbTftDisplay extends LitElement {
  createRenderRoot() { return this; }

  @property({ type: Number }) currentPage = 1;
  @property({ attribute: false }) displayItems: DisplayItem[] = [];
  /** Lists (rollers) on the current page. */
  @property({ attribute: false }) lists: ListConfig[] = [];
  @state() private timeStr = '';
  private _timer?: number;

  connectedCallback() {
    super.connectedCallback();
    this._updateTime();
    this._timer = window.setInterval(() => this._updateTime(), 1000);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    if (this._timer) clearInterval(this._timer);
  }

  private _updateTime() {
    const now = new Date();
    this.timeStr = `${now.getHours()}:${String(now.getMinutes()).padStart(2, '0')}`;
  }

  private _dotsIcon(size: number) {
    return svg`
      <svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24"
        fill="currentColor" stroke="none">
        <circle cx="12" cy="5" r="2"/>
        <circle cx="12" cy="12" r="2"/>
        <circle cx="12" cy="19" r="2"/>
      </svg>
    `;
  }

  private _isTimeItem(item: DisplayItem): boolean {
    return item.value === 'time' || item.type === 'text' && item.value === 'time';
  }

  /**
   * A list like the device's LVGL roller: `rows` rows, the selected item in
   * the middle one on a focus-colored bar, nothing wraps. A new selection
   * scrolls (CSS transition).
   */
  private _renderList(list: ListConfig) {
    const fontPx = FONT_PX[list.fontSize - 1] * SCREEN_SCALE;
    const rowH = fontPx * ROW_LINE_HEIGHT;
    const middle = (list.rows - 1) * rowH / 2;
    const shift = middle - Math.max(0, list.selected) * rowH;
    return html`
      <div class="absolute overflow-hidden" data-list=${list.name}
        style="left: ${list.x}%; top: ${list.y}%; width: ${list.width}%; height: ${px(list.rows * rowH)};
          font-size: ${px(fontPx)}; line-height: ${px(rowH)};">
        <div class="absolute left-0 right-0 rounded-[6px]" data-list-focus
          style="top: ${px(middle)}; height: ${px(rowH)}; background: ${list.focusColor};"></div>
        <div class="relative" style="transform: translateY(${px(shift)}); transition: transform 200ms ease-out;">
          ${list.items.map((text, i) => html`
            <div class="text-center whitespace-nowrap overflow-hidden text-ellipsis px-[4px]"
              data-list-item=${i} ?data-selected=${i === list.selected}
              style="height: ${px(rowH)}; color: ${i === list.selected ? list.focusTextColor : list.color};"
            >${text}</div>
          `)}
        </div>
      </div>
    `;
  }

  render() {
    return html`
      <!-- LargeDisplay outer -->
      <div class="bg-[rgb(20,20,20)] w-[342px] h-[219px] flex py-[3.36px]">
        <!-- Left config dots column -->
        <div class="flex items-end w-[29.5px] h-[212.25px] pt-[12.32px] pb-[3.7px] font-fira">
          <div class="text-white cursor-pointer flex items-center">
            ${this._dotsIcon(24)}
          </div>
        </div>

        <!-- TFT Screen -->
        <div class="bg-[rgb(30,30,30)] w-[283px] h-[212.25px] relative overflow-hidden text-[11.2px] font-[Montserrat,sans-serif] leading-[11.2px]">
          ${this.displayItems.map(item => html`
            <div class="absolute text-white font-[Montserrat,sans-serif]"
              style="left: ${item.x}%; top: ${item.y}%; width: ${item.width}%; border-bottom: 1px solid rgb(170, 170, 170);">
              <div class="text-[1.106em] font-semibold my-[2px] text-[rgb(255,211,95)] leading-[1em]">
                ${item.label}
              </div>
              <div class="font-semibold text-white flex items-end
                ${this._isTimeItem(item) ? 'text-[3.792em] pb-[3.36px]' : 'text-[1.58em] pb-[1.4px]'}">
                <div class="font-normal leading-[1.3em]">
                  ${this._isTimeItem(item) ? this.timeStr : (item.value ?? '')}
                </div>
              </div>
            </div>
          `)}
          ${this.lists.map(list => this._renderList(list))}

          <!-- Status bar -->
          <div class="absolute text-[11.2px] font-normal font-[Montserrat,sans-serif] text-white text-left leading-[11.2px]"
            style="top: 201px; left: 0;">
            btn_fab028
          </div>
        </div>

        <!-- Right config dots column -->
        <div class="flex items-end w-[29.5px] h-[212.25px] pt-[12.32px] pb-[3.7px] font-fira">
          <div class="text-white cursor-pointer flex items-center">
            ${this._dotsIcon(24)}
          </div>
        </div>
      </div>
    `;
  }
}
