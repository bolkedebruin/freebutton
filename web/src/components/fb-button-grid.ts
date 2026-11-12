import { LitElement, html, svg } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import type { ButtonConfig, ItemFont } from '../js/app-state.js';
import { on } from '../js/app-state.js';
import { getStyles } from '../js/device-config.js';
import { styleFont } from '../js/font-config.js';
import { StyleElement, type ResolvedStyles } from '../js/style-config.js';
import { iconView } from './icon-view.js';
import { fontStyle, textStyleCss } from './font-view.js';

// The 160 x 80 mini display drawn as the 113 x 42 label area.
const MINI_SCALE = Math.min(113 / 160, 42 / 80);

@customElement('fb-button-grid')
export class FbButtonGrid extends LitElement {
  createRenderRoot() { return this; }

  @property({ type: Number }) currentPage = 1;
  @property({ attribute: false }) buttons: ButtonConfig[] = [];
  /**
   * Key positions to render (e.g. one connector: [5, 6]). Positions without a
   * configured button render as an empty key. Null = render the configured
   * buttons of the current page only (legacy layout).
   */
  @property({ attribute: false }) positions: number[] | null = null;
  private _offIcons?: () => void;
  private _offFonts?: () => void;

  connectedCallback() {
    super.connectedCallback();
    this._offIcons = on('icon-sources-change', () => this.requestUpdate());
    this._offFonts = on('preview-fonts-change', () => this.requestUpdate());
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._offIcons?.();
    this._offFonts?.();
  }

  /** Odd positions are the left key of a connector (position = id*2+1). */
  private _isLeftSide(position: number): boolean {
    return position % 2 === 1;
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

  private _renderButton(btn: ButtonConfig, styles: ResolvedStyles) {
    const leftSide = this._isLeftSide(btn.position);
    // Fields the key does not set: the style's key.label / key.toplabel.
    const labelFont: ItemFont = btn.labelFont ?? styleFont(styles, StyleElement.KeyLabel);
    const topFont: ItemFont = btn.topFont ?? styleFont(styles, StyleElement.KeyTopLabel);

    return html`
      <div data-position=${btn.position} class="bg-[rgb(20,20,20)] h-[57.1px] flex items-center justify-center relative font-fira text-white
        ${leftSide ? 'flex-row' : 'flex-row-reverse'}">

        <!-- Config dots -->
        <div class="absolute top-1/2 -translate-y-1/2 text-white cursor-pointer z-10
          ${leftSide ? 'left-1' : 'right-1'}"
          style="font-size: 16px;">
          ${this._dotsIcon(16)}
        </div>

        <!-- LED indicator -->
        <div class="absolute top-1/2 -translate-y-1/2
          ${leftSide ? 'left-6' : 'right-6'}">
          <div class="w-[11.75px] h-[11.75px] border-[3px] bg-transparent
            ${btn.ledOn ? 'border-[rgb(255,0,0)]' : 'border-black'}">
          </div>
        </div>

        <!-- Button label area -->
        <button class="bg-[rgb(30,30,30)] border-0 text-white cursor-pointer flex flex-col items-center justify-center
          px-1.5 py-px w-[113px] h-[42px] font-fira text-[16.8px] font-normal text-center leading-[25.2px]
          transition-colors duration-200 hover:bg-[rgba(255,255,255,0.1)]">
          ${btn.topLabel ? html`
            <div class="text-center leading-[1.3] whitespace-nowrap" data-top-label
              style="${fontStyle(topFont, MINI_SCALE)} ${textStyleCss(styles[StyleElement.KeyTopLabel], MINI_SCALE)}">
              ${btn.topLabel}
            </div>
          ` : ''}
          ${btn.icon
            ? iconView(btn.icon, btn.iconSize ?? 48, btn.iconSize ?? 48, undefined, MINI_SCALE)
            : html`<div class="leading-[1.3] whitespace-nowrap" data-label
                style="${fontStyle(labelFont, MINI_SCALE)} ${textStyleCss(styles[StyleElement.KeyLabel], MINI_SCALE)}">${btn.label}</div>`}
        </button>

        <!-- Spacer -->
        <div class="h-1"></div>
      </div>
    `;
  }

  render() {
    const styles = getStyles();
    const onPage = this.buttons.filter(b => b.page === this.currentPage);
    const pageButtons = this.positions
      ? this.positions.map(position => onPage.find(b => b.position === position)
          ?? { position, page: this.currentPage, label: '', topLabel: '', ledOn: false, ledColor: 'rgb(0,0,0)' })
      : onPage.sort((a, b) => a.position - b.position);

    return html`
      <div class="grid grid-cols-2 gap-[6.72px] w-[342px] mt-[6.72px] font-fira">
        ${pageButtons.map(btn => this._renderButton(btn, styles))}
      </div>
    `;
  }
}
