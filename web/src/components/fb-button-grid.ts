import { LitElement, html, svg } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import type { ButtonConfig, ConfigRef, ItemFont } from '../js/app-state.js';
import { on, sameConfigRef } from '../js/app-state.js';
import { nothing } from 'lit';
import { getStyles } from '../js/device-config.js';
import { SLOTS, slotFont } from '../js/font-config.js';
import { StyleElement, type ConfigStyles } from '../js/style-config.js';
import { iconView } from './icon-view.js';
import { fontStyle, richText, textStyleCss } from './font-view.js';
import { attachCanvas } from '../js/emulator/canvas.js';
import type { Emulator } from '../js/emulator/index.js';
import { BAR_H, FACE_W, KEY_H, KEY_SCALE, KEY_W, MODULE_GAP, squarePixels } from '../js/display-geometry.js';
import { buttonName } from '../js/idle-outline.js';

/**
 * CSS px per device px of a key's 160 x 80 display: less than the main
 * display's, as the key panels have the finer pixel pitch
 * (display-geometry.ts KEY_SCALE: 0.75). The display takes 120 x 60 CSS px.
 */
const MINI_SCALE = KEY_SCALE;
/** The key's outer edge (CSS px): its LED and the config dots. */
const EDGE_W = 15.64;

function px(n: number): string {
  return `${Math.round(n * 100) / 100}px`;
}

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
  /**
   * The emulator that draws the key displays while a script runs (the
   * firmware's LVGL in the page): a key whose position has a display shows
   * its canvas. While it runs, a key without a display has its label drawn
   * here from the model. The LEDs are always drawn here.
   *
   * Nothing runs (`pressable` false): a configured key's display is an
   * outline with the key's name ("Key 3" without one); no label, no icon,
   * the LEDs off. A position without a configured key stays empty.
   */
  @property({ attribute: false }) engine: Emulator | null = null;
  /**
   * Screen pixels per CSS px (the face's zoom times devicePixelRatio): a
   * display keeps square pixels only where a device pixel is a whole number
   * of them (display-geometry.ts squarePixels), else the browser smooths.
   */
  @property({ type: Number }) screenPerCss = 1;
  private _attached = new Map<HTMLCanvasElement, { engine: Emulator; display: string; detach: () => void }>();
  /** Displays that could not be shown on a canvas (no 2D context): their keys are drawn from the model. */
  @state() private _canvasFailed: Emulator | null = null;
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
    this._detachAll();
  }

  protected updated() {
    this._attach();
  }

  /** The id of the display of the key at `position` ("key3"); null: that key has none (or nothing draws displays). */
  private _displayOf(position: number): string | null {
    if (!this.engine || this._canvasFailed === this.engine) return null;
    return this.engine.displays().find(d => d.kind === 'key' && d.position === position)?.id ?? null;
  }

  /** Keep every key's canvas on its display of the engine. */
  private _attach() {
    const engine = this.engine;
    const canvases = new Set(this.querySelectorAll<HTMLCanvasElement>('canvas[data-display]'));
    for (const [canvas, link] of this._attached) {
      if (canvases.has(canvas) && link.engine === engine && link.display === canvas.dataset.display) continue;
      link.detach();
      this._attached.delete(canvas);
    }
    if (!engine) return;
    for (const canvas of canvases) {
      const display = canvas.dataset.display ?? '';
      if (this._attached.has(canvas)) continue;
      try {
        this._attached.set(canvas, { engine, display, detach: attachCanvas(engine, display, canvas) });
      } catch {
        this._canvasFailed = engine;
        return;
      }
    }
  }

  private _detachAll() {
    for (const link of this._attached.values()) link.detach();
    this._attached.clear();
  }

  protected willUpdate(changed: Map<PropertyKey, unknown>) {
    // The script stopped while a key was held: nothing is down any more.
    if (changed.has('pressable') && !this.pressable && this._down.size > 0) this._down = new Set();
    if (changed.has('engine')) this._canvasFailed = null;
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

  private _renderButton(btn: ButtonConfig, configured: boolean, styles: ConfigStyles) {
    const leftSide = this._isLeftSide(btn.position);
    const running = this.pressable;
    // The look its styles give the button (config-model mapButton); a button without an entry: the look of "button".
    // Both labels take text markup (\c \s \f \a), as on the device.
    const labelFont: ItemFont = btn.labelFont ?? slotFont(styles, SLOTS.button);
    const topFont: ItemFont = btn.topFont ?? slotFont(styles, SLOTS.buttonTop);
    const labelStyle = btn.labelStyle ?? styles.elements[StyleElement.KeyLabel];
    const topStyle = btn.topStyle ?? styles.elements[StyleElement.KeyTopLabel];

    const isSelected = sameConfigRef(btn.ref, this.selected);
    // The key's display as the running firmware draws it; null: drawn here.
    const display = running ? this._displayOf(btn.position) : null;
    const down = this._down.has(btn.position);
    const position = btn.position;
    // Nothing runs: the LEDs are off, whatever the model carries.
    const ledOn = running && btn.ledOn;
    const backLed = running ? btn.backLedColor : undefined;
    // While a script runs the display under the pointer (or with the focus) is shown at the main display's
    // scale: its text is small at its own. A transform: nothing else moves.
    const square = squarePixels(MINI_SCALE, this.screenPerCss);
    const drawnBy = display ? 'firmware' : running ? 'model' : configured ? 'outline' : 'none';

    return html`
      <div data-position=${btn.position} class="bg-[rgb(20,20,20)] flex items-center justify-center relative font-fira text-white
        ${this.pressable ? 'select-none touch-none' : ''}"
        ?data-pressed=${down}
        @pointerdown=${(e: PointerEvent) => { if (e.button === 0) this._press(position); }}
        @pointerup=${() => this._release(position)}
        @pointerleave=${() => this._release(position)}
        @pointercancel=${() => this._release(position)}
        data-ref=${btn.ref ? `${btn.ref.list}:${btn.ref.index}` : nothing} data-ref-kind="button" ?data-selected=${isSelected}
        style="height: ${px(BAR_H)};${isSelected ? ' outline: 2px solid #AF8BFF; outline-offset: -2px;' : ''}">

        <!-- LED at the back of the key (towards the wall): a glow at its outer edge, only while a script lights it -->
        ${backLed ? html`<div data-back-led class="absolute top-[3px] bottom-[3px] w-[3px] rounded-full ${leftSide ? 'left-0' : 'right-0'}"
          style="background: ${backLed}; box-shadow: 0 0 8px 1px ${backLed};"></div>` : ''}

        <!-- The key's outer edge: its LED (the colour it has; black = off) and the config dots -->
        <div class="absolute top-0 bottom-0 flex flex-col items-center justify-center gap-[10px] ${leftSide ? 'left-0' : 'right-0'}" style="width: ${px(EDGE_W)};">
          <div class="w-[10px] h-[10px] border-[2.5px] bg-transparent" data-led ?data-led-on=${ledOn}
            style="border-color: ${ledOn ? btn.ledColor : 'rgb(0,0,0)'};${ledOn ? ` box-shadow: 0 0 6px ${btn.ledColor};` : ''}">
          </div>
          <div aria-hidden="true" class="text-white flex">${this._dotsIcon(14)}</div>
        </div>

        <!-- The key's display: 160 x 80 device pixels at the key panel's scale, in the middle of its half of the bar -->
        <button title=${this.pressable ? 'Press the key (hold it for a long press)' : btn.ref ? 'Show in config.json' : configured ? 'Sample data: no config.json is loaded' : 'No key configured at this position'}
          aria-label=${`Key ${btn.position}${btn.name ? ' ' + btn.name : ''}`} aria-pressed=${this.pressable ? (down ? 'true' : 'false') : nothing}
          @keydown=${(e: KeyboardEvent) => this._onKeyDown(e, position)} @keyup=${(e: KeyboardEvent) => this._onKeyUp(e, position)}
          @blur=${() => this._release(position)}
          data-drawn-by=${drawnBy}
          class="relative ${down ? 'bg-[rgb(70,70,70)]' : 'bg-[rgb(30,30,30)]'} border-0 p-0 text-white ${btn.ref || this.pressable ? 'cursor-pointer' : 'cursor-default'} flex flex-col items-center justify-center
          shrink-0 overflow-hidden font-fira font-normal text-center
          transition-[transform,background-color] duration-100 ${display ? '' : 'hover:bg-[rgba(255,255,255,0.1)]'} outline-none focus-visible:ring-2 focus-visible:ring-white"
          style="width: ${px(KEY_W * MINI_SCALE)}; height: ${px(KEY_H * MINI_SCALE)}; font-size: ${px(16.8 * MINI_SCALE)}; line-height: ${px(25.2 * MINI_SCALE)};">
          ${display ? html`
            <canvas data-display=${display} width=${KEY_W} height=${KEY_H} class="block w-full h-full" aria-hidden="true"
              style="image-rendering: ${square ? 'pixelated' : 'auto'};"></canvas>
            ${down ? html`<span class="absolute inset-0 bg-white/15 pointer-events-none" data-key-pressed></span>` : ''}
          ` : !running ? (configured ? html`
            <span class="absolute inset-0 box-border border border-dashed border-white/45 flex items-center justify-center px-[3px] pointer-events-none">
              <span class="min-w-0 overflow-hidden whitespace-nowrap text-ellipsis text-[10px] leading-[12px] text-white/75" data-key-name>${buttonName(btn)}</span>
            </span>` : '') : html`
          ${btn.topLabel ? html`
            <div class="text-center leading-[1.3] whitespace-nowrap" data-top-label
              style="${fontStyle(topFont, MINI_SCALE)} ${textStyleCss(topStyle, MINI_SCALE)}">
              ${richText(btn.topLabel, topFont, SLOTS.buttonTop, MINI_SCALE)}
            </div>
          ` : ''}
          ${btn.icon
            ? iconView(btn.icon, btn.iconSize ?? 48, btn.iconSize ?? 48, btn.iconColor, MINI_SCALE)
            : html`<div class="leading-[1.3] whitespace-nowrap" data-label
                style="${fontStyle(labelFont, MINI_SCALE)} ${textStyleCss(labelStyle, MINI_SCALE)}">${richText(btn.label, labelFont, SLOTS.button, MINI_SCALE)}</div>`}
          `}
        </button>
      </div>
    `;
  }

  render() {
    const styles = getStyles();
    const onPage = this.buttons.filter(b => b.page === this.currentPage);
    const empty = (position: number): ButtonConfig => ({ position, page: this.currentPage, label: '', topLabel: '', ledOn: false, ledColor: 'rgb(0,0,0)' });
    const pageButtons = this.positions
      ? this.positions.map((position) => {
        const btn = onPage.find(b => b.position === position);
        return { btn: btn ?? empty(position), configured: !!btn };
      })
      : onPage.sort((a, b) => a.position - b.position).map(btn => ({ btn, configured: true }));

    return html`
      <div class="grid grid-cols-2 font-fira" style="width: ${px(FACE_W)}; gap: ${px(MODULE_GAP)}; margin-top: ${px(MODULE_GAP)};">
        ${pageButtons.map(({ btn, configured }) => this._renderButton(btn, configured, styles))}
      </div>
    `;
  }
}
