import { LitElement, html, svg } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import type { DisplayItem, ImageConfig, ItemFont, ListConfig } from '../js/app-state.js';
import { on } from '../js/app-state.js';
import { getStyles } from '../js/device-config.js';
import { fontsizePx, SLOTS, styleFont, unitFont } from '../js/font-config.js';
import { StyleElement, styleItemPx, type ResolvedStyles } from '../js/style-config.js';
import { iconView } from './icon-view.js';
import { fontStyle, richText, styleColorCss, textStyleCss } from './font-view.js';

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
  /** Images (config.json "images") on the current page. */
  @property({ attribute: false }) images: ImageConfig[] = [];
  @state() private timeStr = '';
  private _timer?: number;
  private _offIcons?: () => void;
  private _offFonts?: () => void;

  connectedCallback() {
    super.connectedCallback();
    this._updateTime();
    this._timer = window.setInterval(() => this._updateTime(), 1000);
    this._offIcons = on('icon-sources-change', () => this.requestUpdate());
    this._offFonts = on('preview-fonts-change', () => this.requestUpdate());
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    if (this._timer) clearInterval(this._timer);
    this._offIcons?.();
    this._offFonts?.();
  }

  /** Value font: config "font"/"fontsize" over the style's display.value. */
  private _valueFont(item: DisplayItem, styles: ResolvedStyles): ItemFont {
    if (item.valueFont) return item.valueFont;
    const base = styleFont(styles, StyleElement.DisplayValue);
    return { ...base, px: styleItemPx(item.fontSize ?? 0, false, base.px) };
  }

  /** Caption font: config "labelfont"/"labelfontsize" over the style's display.caption. */
  private _labelFont(item: DisplayItem, styles: ResolvedStyles): ItemFont {
    return item.labelFont ?? styleFont(styles, StyleElement.DisplayCaption);
  }

  /**
   * An "images" entry (x, y, width, height in device pixels): its icon, or
   * (src = a raster the IDE cannot show) a dashed box with the key.
   */
  private _renderImage(img: ImageConfig) {
    // A src raster of unknown size: a 40 x 40 box stands in for it.
    const w = img.width ?? (img.icon ? 0 : 40);
    const h = img.height ?? (img.icon ? 0 : 40);
    const box = `left: ${px(img.x * SCREEN_SCALE)}; top: ${px(img.y * SCREEN_SCALE)}; width: ${px(w * SCREEN_SCALE)}; height: ${px(h * SCREEN_SCALE)};`;
    if (img.icon) {
      // Like the device: an icon without width/height shows nothing.
      if (!img.width || !img.height) return '';
      return html`<div class="absolute flex" data-image=${img.name} style=${box}>
        ${iconView(img.icon, img.width, img.height, img.color, SCREEN_SCALE)}
      </div>`;
    }
    return html`<div class="absolute flex items-center justify-center border border-dashed border-white/40 text-[9px] text-gray-200 overflow-hidden"
      data-image=${img.name} title=${img.src ? `images/${img.src}` : img.name} style=${box}>${img.src ?? img.name}</div>`;
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
  private _renderList(list: ListConfig, styles: ResolvedStyles) {
    const base = styleFont(styles, StyleElement.List);
    const font = list.font ?? { ...base, px: fontsizePx(list.fontSize, base.px) };
    // list.selected: the list's font unless the style gives the selected row its own.
    const ls = styles[StyleElement.List] ?? {};
    const sel = styles[StyleElement.ListSelected] ?? {};
    const selFont: ItemFont = {
      face: sel.face !== undefined && sel.face !== ls.face ? sel.face : font.face,
      px: sel.px !== undefined && sel.px !== ls.px ? sel.px : font.px,
      configured: font.configured,
    };
    const selCss = selFont.face !== font.face || selFont.px !== font.px ? fontStyle(selFont, SCREEN_SCALE) : '';
    const { color: _c, ...selText } = sel;
    const { color: _l, ...listText } = ls;
    const fontPx = font.px * SCREEN_SCALE;
    const rowH = fontPx * ROW_LINE_HEIGHT;
    const middle = (list.rows - 1) * rowH / 2;
    const shift = middle - Math.max(0, list.selected) * rowH;
    return html`
      <div class="absolute overflow-hidden" data-list=${list.name}
        style="left: ${list.x}%; top: ${list.y}%; width: ${list.width}%; height: ${px(list.rows * rowH)};
          ${fontStyle(font, SCREEN_SCALE)} line-height: ${px(rowH)};">
        <div class="absolute left-0 right-0 rounded-[6px]" data-list-focus
          style="top: ${px(middle)}; height: ${px(rowH)}; background: ${list.focusColor};${sel.bgOpa !== undefined && sel.bgOpa < 255 ? ` opacity: ${sel.bgOpa / 255};` : ''}"></div>
        <div class="relative" style="transform: translateY(${px(shift)}); transition: transform 200ms ease-out;">
          ${list.items.map((text, i) => html`
            <div class="text-center whitespace-nowrap overflow-hidden text-ellipsis px-[4px]"
              data-list-item=${i} ?data-selected=${i === list.selected}
              style="height: ${px(rowH)}; ${i === list.selected
                ? `${selCss} ${textStyleCss(selText, SCREEN_SCALE)} color: ${styleColorCss(parseInt(list.focusTextColor.slice(1), 16), sel.opa)};`
                : `${textStyleCss(listText, SCREEN_SCALE)} color: ${styleColorCss(parseInt(list.color.slice(1), 16), ls.opa)};`}"
            >${text}</div>
          `)}
        </div>
      </div>
    `;
  }

  render() {
    const styles = getStyles();
    const caption = styles[StyleElement.DisplayCaption];
    const valueStyle = styles[StyleElement.DisplayValue];
    const unitStyle = styles[StyleElement.DisplayUnit];
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
          ${this.displayItems.map(item => {
            // Caption, value and unit in the item's fonts and sizes; markup
            // (\f \s \c \a) as on the device (text-spans.ts).
            const valueFont = this._valueFont(item, styles);
            const value = this._isTimeItem(item) ? this.timeStr : (item.value ?? '');
            return html`
            <div class="absolute" data-display-item=${item.name ?? item.id ?? ''}
              style="left: ${item.x}%; top: ${item.y}%; width: ${item.width}%; border-bottom: 1px solid rgb(170, 170, 170);">
              <div class="my-[2px] leading-[1.15]" data-caption style=${textStyleCss(caption, SCREEN_SCALE)}>
                ${richText(item.label ?? '', this._labelFont(item, styles), SLOTS.caption, SCREEN_SCALE)}
              </div>
              <div class="flex items-end pb-[1.4px]" style=${textStyleCss(valueStyle, SCREEN_SCALE)}>
                ${item.icon ? html`<span class="mr-[4px] mb-[2px] flex">${iconView(item.icon, item.iconSize ?? 32, item.iconSize ?? 32, item.iconColor, SCREEN_SCALE)}</span>` : ''}
                <div class="leading-[1.2] min-w-0" data-value>
                  ${richText(value, valueFont, SLOTS.value, SCREEN_SCALE)}${item.unit
                    ? html`<span data-unit style=${textStyleCss(unitStyle, SCREEN_SCALE)}>${richText(item.unit, unitFont(valueFont, styles), SLOTS.value, SCREEN_SCALE)}</span>` : ''}
                </div>
              </div>
            </div>
          `;
          })}
          ${this.lists.map(list => this._renderList(list, styles))}
          ${this.images.map(img => this._renderImage(img))}

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
