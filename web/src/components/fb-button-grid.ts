import { LitElement, html, svg } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import type { ButtonConfig, ConfigRef, ItemFont } from '../js/app-state.js';
import { on, sameConfigRef } from '../js/app-state.js';
import { nothing } from 'lit';
import { getStyles } from '../js/device-config.js';
import { SLOTS, styleFont } from '../js/font-config.js';
import { StyleElement, type ResolvedStyles } from '../js/style-config.js';
import { iconView } from './icon-view.js';
import { fontStyle, richText, textStyleCss } from './font-view.js';

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
  /** The config.json entry under the editor cursor: outlined. */
  @property({ attribute: false }) selected: ConfigRef | null = null;
  /**
   * A script runs in the virtual device: a key is pressed while the pointer
   * (or Space / Enter) holds it down, and reports `key-press` / `key-release`
   * with its position. How long it is held decides click, long press and
   * repeat, as on the device. False: a click shows the key in config.json.
   */
  @property({ type: Boolean }) pressable = false;
  /** Positions held down now. */
  @state() private _down: ReadonlySet<number> = new Set();
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

  protected willUpdate(changed: Map<PropertyKey, unknown>) {
    // The script stopped while a key was held: nothing is down any more.
    if (changed.has('pressable') && !this.pressable && this._down.size > 0) this._down = new Set();
  }

  private _press(position: number) {
    if (!this.pressable || this._down.has(position)) return;
    this._down = new Set([...this._down, position]);
    this.dispatchEvent(new CustomEvent('key-press', { bubbles: true, detail: { position } }));
  }

  private _release(position: number) {
    if (!this._down.has(position)) return;
    this._down = new Set([...this._down].filter(p => p !== position));
    this.dispatchEvent(new CustomEvent('key-release', { bubbles: true, detail: { position } }));
  }

  private _onKeyDown(e: KeyboardEvent, position: number) {
    if (!this.pressable || (e.key !== ' ' && e.key !== 'Enter')) return;
    e.preventDefault();          // no click, no page scroll
    if (!e.repeat) this._press(position);
  }

  private _onKeyUp(e: KeyboardEvent, position: number) {
    if (e.key !== ' ' && e.key !== 'Enter') return;
    e.preventDefault();
    this._release(position);
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
    // Both labels take text markup (\c \s \f \a), as on the device.
    const labelFont: ItemFont = btn.labelFont ?? styleFont(styles, StyleElement.KeyLabel);
    const topFont: ItemFont = btn.topFont ?? styleFont(styles, StyleElement.KeyTopLabel);

    const isSelected = sameConfigRef(btn.ref, this.selected);
    const down = this._down.has(btn.position);
    const position = btn.position;

    return html`
      <div data-position=${btn.position} class="bg-[rgb(20,20,20)] h-[57.1px] flex items-center justify-center relative font-fira text-white
        ${leftSide ? 'flex-row' : 'flex-row-reverse'} ${this.pressable ? 'select-none touch-none' : ''}"
        ?data-pressed=${down}
        @pointerdown=${(e: PointerEvent) => { if (e.button === 0) this._press(position); }}
        @pointerup=${() => this._release(position)} @pointerleave=${() => this._release(position)}
        @pointercancel=${() => this._release(position)}
        data-ref=${btn.ref ? `${btn.ref.list}:${btn.ref.index}` : nothing} data-ref-kind="button" ?data-selected=${isSelected}
        style=${isSelected ? 'outline: 2px solid #AF8BFF; outline-offset: -2px;' : ''}>

        <!-- Config dots -->
        <div aria-hidden="true" class="absolute top-1/2 -translate-y-1/2 text-white z-10
          ${leftSide ? 'left-1' : 'right-1'}"
          style="font-size: 16px;">
          ${this._dotsIcon(16)}
        </div>

        <!-- LED at the back of the key (towards the wall): a glow at its outer edge, only while a script lights it -->
        ${btn.backLedColor ? html`<div data-back-led class="absolute top-[3px] bottom-[3px] w-[3px] rounded-full ${leftSide ? 'left-0' : 'right-0'}"
          style="background: ${btn.backLedColor}; box-shadow: 0 0 8px 1px ${btn.backLedColor};"></div>` : ''}

        <!-- LED indicator: the colour the LED has (black = off) -->
        <div class="absolute top-1/2 -translate-y-1/2
          ${leftSide ? 'left-6' : 'right-6'}">
          <div class="w-[11.75px] h-[11.75px] border-[3px] bg-transparent" data-led ?data-led-on=${btn.ledOn}
            style="border-color: ${btn.ledOn ? btn.ledColor : 'rgb(0,0,0)'};${btn.ledOn ? ` box-shadow: 0 0 6px ${btn.ledColor};` : ''}">
          </div>
        </div>

        <!-- Button label area -->
        <button title=${this.pressable ? 'Press the key (hold it for a long press)' : btn.ref ? 'Show in config.json' : 'No key configured at this position'}
          aria-label=${`Key ${btn.position}${btn.name ? ' ' + btn.name : ''}`} aria-pressed=${this.pressable ? (down ? 'true' : 'false') : nothing}
          @keydown=${(e: KeyboardEvent) => this._onKeyDown(e, position)} @keyup=${(e: KeyboardEvent) => this._onKeyUp(e, position)}
          @blur=${() => this._release(position)}
          class="${down ? 'bg-[rgb(70,70,70)]' : 'bg-[rgb(30,30,30)]'} border-0 text-white ${btn.ref || this.pressable ? 'cursor-pointer' : 'cursor-default'} flex flex-col items-center justify-center
          px-1.5 py-px w-[113px] h-[42px] font-fira text-[16.8px] font-normal text-center leading-[25.2px]
          transition-colors duration-100 hover:bg-[rgba(255,255,255,0.1)] outline-none focus-visible:ring-2 focus-visible:ring-white">
          ${btn.topLabel ? html`
            <div class="text-center leading-[1.3] whitespace-nowrap" data-top-label
              style="${fontStyle(topFont, MINI_SCALE)} ${textStyleCss(styles[StyleElement.KeyTopLabel], MINI_SCALE)}">
              ${richText(btn.topLabel, topFont, SLOTS.buttonTop, MINI_SCALE)}
            </div>
          ` : ''}
          ${btn.icon
            ? iconView(btn.icon, btn.iconSize ?? 48, btn.iconSize ?? 48, undefined, MINI_SCALE)
            : html`<div class="leading-[1.3] whitespace-nowrap" data-label
                style="${fontStyle(labelFont, MINI_SCALE)} ${textStyleCss(styles[StyleElement.KeyLabel], MINI_SCALE)}">${richText(btn.label, labelFont, SLOTS.button, MINI_SCALE)}</div>`}
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
