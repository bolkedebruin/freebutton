import { LitElement, html, svg } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import type { ConfigRef, DisplayItem, ImageConfig, ItemFont, ListConfig } from '../js/app-state.js';
import { on, sameConfigRef } from '../js/app-state.js';
import { nothing } from 'lit';
import { getStyles } from '../js/device-config.js';
import { fontsizePx, SLOTS, styleFont, unitFont } from '../js/font-config.js';
import { StyleElement, styleItemPx, type ResolvedStyles } from '../js/style-config.js';
import { iconView } from './icon-view.js';
import { fontStyle, richText, styleColorCss, textStyleCss } from './font-view.js';
import {
  alignAnchor, boxOrigin, clampUnits, dragTo, hitTest, realign, unitRange, unitsAt, SCREEN_W,
  type PlacedKind, type Rect, type Size, type UnitRange, type Units,
} from '../js/display-geometry.js';

// The preview screen is 283 px wide for the device's 320 x 240.
const SCREEN_SCALE = 283 / 320;
/** A press that moves less than this (CSS px) is a click, not a drag. */
export const DRAG_THRESHOLD_PX = 4;
/** A touch held this long without moving opens the item's menu. */
const LONG_PRESS_MS = 550;
/** Shift + arrow: this many units (display items and lists: %; images: px). */
const BIG_STEP: Record<PlacedKind, number> = { displayitem: 5, list: 5, image: 10 };
const ITEM_HINT = 'Click: show in config.json. Drag or arrow keys: move. Right-click: properties.';

/** A drawn entry with what a move needs: its values and its box in device px. */
interface Placed {
  ref: ConfigRef;
  kind: PlacedKind;
  align: number | undefined;
  at: Units;
  size: Size;
}

/** A press on an item: a click until it moves DRAG_THRESHOLD_PX, then a drag. */
interface Press {
  item: Placed;
  el: HTMLElement;
  clientX: number;
  clientY: number;
  scale: Units;
  range: UnitRange;
  moved: boolean;
  timer?: number;
}

/**
 * The position shown instead of the model's: while an item is dragged
 * (`active`), and after a drop or a nudge until the edited config.json
 * comes back as the new model.
 */
interface Moving extends Placed {
  active: boolean;
}
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
  /** The device's name for the status line ('' = not known: nothing shown). */
  @property() deviceName = '';
  /** The config.json entry under the editor cursor: outlined. */
  @property({ attribute: false }) selected: ConfigRef | null = null;
  /**
   * Items can be moved (false: config.json is not valid JSON right now; a
   * drag or a nudge then only reports `edit-blocked`).
   */
  @property({ type: Boolean }) editable = true;
  /**
   * The boxes of the drawn entries in device px, when what draws the screen
   * supplies them (a renderer that is not this component's DOM). Null: the
   * press is matched to the element under it and the height is measured
   * from that element (_itemEl, _placed: the only places that ask the DOM).
   */
  @property({ attribute: false }) itemRects: Array<{ ref: ConfigRef; rect: Rect }> | null = null;
  @state() private timeStr = '';
  @state() private _moving: Moving | null = null;
  private _press: Press | null = null;
  /** The click that ends a drag is not a click on the item. */
  private _swallowClick = false;
  private _timer?: number;
  private _offIcons?: () => void;
  private _offFonts?: () => void;

  connectedCallback() {
    super.connectedCallback();
    this._updateTime();
    this._timer = window.setInterval(() => this._updateTime(), 1000);
    this._offIcons = on('icon-sources-change', () => this.requestUpdate());
    this._offFonts = on('preview-fonts-change', () => this.requestUpdate());
    // Capture: before the virtual device's click handler (select in config.json).
    this.addEventListener('click', this._onClickCapture, true);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeEventListener('click', this._onClickCapture, true);
    this._endPress();
    if (this._timer) clearInterval(this._timer);
    this._offIcons?.();
    this._offFonts?.();
  }

  protected willUpdate(changed: Map<PropertyKey, unknown>) {
    // The edited config.json came back as the model: it has the new position.
    if (this._moving && !this._moving.active
      && (changed.has('displayItems') || changed.has('lists') || changed.has('images'))) {
      this._moving = null;
    }
  }

  // ---- moving items -------------------------------------------------------

  private _screen(): HTMLElement | null {
    return this.querySelector<HTMLElement>('[data-screen]');
  }

  /** CSS px per device px of the drawn screen (the constant where nothing is laid out). */
  private _scale(): Units {
    const r = this._screen()?.getBoundingClientRect();
    return r && r.width > 0 && r.height > 0
      ? { x: r.width / SCREEN_W, y: r.height / (SCREEN_W * 0.75) }
      : { x: SCREEN_SCALE, y: SCREEN_SCALE };
  }

  /** The entry `ref` with its box: the width the firmware gives it, the height as drawn. */
  private _placed(ref: ConfigRef, el: HTMLElement | null): Placed | null {
    const given = this.itemRects?.find(r => sameConfigRef(r.ref, ref))?.rect;
    const heightPx = given ? given.height : (el?.offsetHeight ?? 0) / SCREEN_SCALE;
    const item = this.displayItems.find(d => sameConfigRef(d.ref, ref));
    if (item) {
      return {
        ref, kind: 'displayitem', align: item.align, at: { x: item.x, y: item.y },
        size: { width: Math.trunc(item.width * SCREEN_W / 100), height: heightPx },
      };
    }
    const list = this.lists.find(l => sameConfigRef(l.ref, ref));
    if (list) {
      return {
        ref, kind: 'list', align: undefined, at: { x: list.x, y: list.y },
        size: { width: Math.trunc(list.width * SCREEN_W / 100), height: heightPx },
      };
    }
    const img = this.images.find(m => sameConfigRef(m.ref, ref));
    if (img) {
      return {
        ref, kind: 'image', align: undefined, at: { x: img.x, y: img.y },
        size: { width: img.width ?? 40, height: img.height ?? 40 },
      };
    }
    return null;
  }

  private _itemEl(target: EventTarget | null, point?: { clientX: number; clientY: number }): { el: HTMLElement; ref: ConfigRef } | null {
    if (this.itemRects && point) {
      const screen = this._screen();
      const r = screen?.getBoundingClientRect();
      const scale = this._scale();
      const hit = r ? hitTest(this.itemRects, { x: (point.clientX - r.left) / scale.x, y: (point.clientY - r.top) / scale.y }) : null;
      if (!hit || !screen) return null;
      return { el: this.querySelector<HTMLElement>(`[data-ref="${hit.ref.list}:${hit.ref.index}"]`) ?? screen, ref: hit.ref };
    }
    const el = (target as Element | null)?.closest?.<HTMLElement>('[data-ref]');
    const [list, index] = (el?.getAttribute('data-ref') ?? '').split(':');
    const kind = el?.getAttribute('data-ref-kind') as ConfigRef['kind'] | null;
    if (!el || !list || !kind || index === undefined) return null;
    return { el, ref: { kind, list, index: Number(index) } };
  }

  private _onPointerDown = (e: PointerEvent) => {
    if (e.button !== 0 || e.isPrimary === false) return;
    const hit = this._itemEl(e.target, e);
    const item = hit ? this._placed(hit.ref, hit.el) : null;
    if (!hit || !item) return;
    this._endPress();
    this._swallowClick = false;
    this._press = {
      item, el: hit.el, clientX: e.clientX, clientY: e.clientY, scale: this._scale(),
      range: unitRange(item.kind, item.align, item.size), moved: false,
    };
    if (e.pointerType === 'touch') {
      // A long press: the item's menu (touch has no right button).
      this._press.timer = window.setTimeout(() => {
        const el = this._press?.el;
        this._endPress();
        this._swallowClick = true;
        el?.dispatchEvent(new CustomEvent('item-menu', { bubbles: true }));
      }, LONG_PRESS_MS);
    }
    window.addEventListener('pointermove', this._onPointerMove);
    window.addEventListener('pointerup', this._onPointerUp);
    window.addEventListener('pointercancel', this._onPointerCancel);
    window.addEventListener('keydown', this._onDragKey, true);
  };

  private _onPointerMove = (e: PointerEvent) => {
    const p = this._press;
    if (!p) return;
    const dx = e.clientX - p.clientX;
    const dy = e.clientY - p.clientY;
    if (!p.moved) {
      if (Math.hypot(dx, dy) < DRAG_THRESHOLD_PX) return;
      if (p.timer) clearTimeout(p.timer);
      this._swallowClick = true;
      if (!this.editable) {
        this._endPress();
        this.dispatchEvent(new CustomEvent('edit-blocked', { bubbles: true }));
        return;
      }
      p.moved = true;
    }
    const at = dragTo(p.item.kind, p.item.at, { dx, dy }, p.scale, p.range, e.shiftKey);
    this._moving = { ...p.item, at, active: true };
  };

  private _onPointerUp = () => {
    const p = this._press;
    const moving = this._moving;
    this._endPress();
    if (!p?.moved || !moving) return;
    this._commitMove(p.item, moving.at);
    p.el.focus();
  };

  private _onPointerCancel = () => {
    this._endPress();
    this._moving = null;
  };

  /** Escape while dragging: the item goes back, nothing is written. */
  private _onDragKey = (e: KeyboardEvent) => {
    if (e.key !== 'Escape' || !this._press?.moved) return;
    e.preventDefault();
    e.stopPropagation();
    this._endPress();
    this._moving = null;
  };

  private _endPress() {
    if (this._press?.timer) clearTimeout(this._press.timer);
    this._press = null;
    window.removeEventListener('pointermove', this._onPointerMove);
    window.removeEventListener('pointerup', this._onPointerUp);
    window.removeEventListener('pointercancel', this._onPointerCancel);
    window.removeEventListener('keydown', this._onDragKey, true);
  }

  private _onClickCapture = (e: Event) => {
    if (!this._swallowClick) return;
    this._swallowClick = false;
    e.stopPropagation();
    e.preventDefault();
  };

  /** Ask for the new position to be written (`item-move`); shown here until the model has it. */
  private _commitMove(item: Placed, at: Units) {
    if (at.x === item.at.x && at.y === item.at.y) {
      this._moving = null;
      return;
    }
    this._moving = { ...item, at, active: false };
    this.dispatchEvent(new CustomEvent('item-move', { bubbles: true, detail: { ref: item.ref, x: at.x, y: at.y } }));
  }

  /** The move was not written: the item goes back to where the model has it. */
  cancelMove() {
    this._moving = null;
  }

  /** Arrow keys on a focused item: one unit, with Shift a larger step. Enter / Space: as a click. */
  private _onKeyDown = (e: KeyboardEvent) => {
    const hit = this._itemEl(e.target);
    if (!hit || e.altKey || e.ctrlKey || e.metaKey) return;
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      hit.el.click();
      return;
    }
    const dir: Record<string, [number, number]> = {
      ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1],
    };
    const d = dir[e.key];
    if (!d) return;
    e.preventDefault();
    if (!this.editable) {
      this.dispatchEvent(new CustomEvent('edit-blocked', { bubbles: true }));
      return;
    }
    const item = this._placed(hit.ref, hit.el);
    if (!item) return;
    // A nudge that is still on its way to config.json: go on from there.
    const from = this._moving && sameConfigRef(this._moving.ref, item.ref) ? this._moving.at : item.at;
    const step = e.shiftKey ? BIG_STEP[item.kind] : 1;
    const at = clampUnits({ x: from.x + d[0] * step, y: from.y + d[1] * step }, unitRange(item.kind, item.align, item.size));
    this._commitMove(item, at);
  };

  /** The x and y (%) of a point of the screen, for a new display item there. */
  unitsAtPoint(clientX: number, clientY: number): Units {
    const r = this._screen()?.getBoundingClientRect();
    const scale = this._scale();
    return unitsAt('displayitem', { x: (clientX - (r?.left ?? 0)) / scale.x, y: (clientY - (r?.top ?? 0)) / scale.y });
  }

  /** The x and y that keep a display item where it is drawn when its align becomes `align`. */
  keepInPlace(ref: ConfigRef, align: number): Units | null {
    const el = this.querySelector<HTMLElement>(`[data-ref="${ref.list}:${ref.index}"]`);
    const item = this._placed(ref, el);
    return item && item.kind === 'displayitem' ? realign(item.align, align, item.at, item.size) : null;
  }

  /** x / y of an entry: the dragged position while it moves. */
  private _at(ref: ConfigRef | undefined, x: number, y: number): Units {
    return this._moving && sameConfigRef(this._moving.ref, ref) ? this._moving.at : { x, y };
  }

  /**
   * CSS position of a display item: "align" picks the screen corner, edge or
   * centre the box is put against, x and y (%) are the offset from there
   * (display-geometry.ts).
   */
  private _itemPosition(item: DisplayItem): string {
    const { x, y } = this._at(item.ref, item.x, item.y);
    const a = alignAnchor(item.align);
    const h = a.h === 0 ? `left: ${x}%;` : a.h === 1 ? `right: ${-x}%;` : `left: calc(50% + ${x}%);`;
    const v = a.v === 0 ? `top: ${y}%;` : a.v === 1 ? `bottom: ${-y}%;` : `top: calc(50% + ${y}%);`;
    const shift = a.h === 0.5 || a.v === 0.5
      ? ` transform: translate(${a.h === 0.5 ? '-50%' : '0'}, ${a.v === 0.5 ? '-50%' : '0'});` : '';
    return `${h} ${v}${shift}`;
  }

  /** The position of the item that moves, next to it (inside the screen). */
  private _renderMoveBadge() {
    const m = this._moving;
    if (!m) return '';
    const o = boxOrigin(m.kind, m.align, m.at, m.size);
    const top = o.top * SCREEN_SCALE;
    const below = (o.top + m.size.height) * SCREEN_SCALE + 3;
    return html`<div class="absolute z-10 px-1.5 py-[3px] rounded bg-purple-90 text-white text-[10px] leading-none font-mono whitespace-nowrap pointer-events-none"
      data-testid="move-position" role="status"
      style="left: ${px(Math.max(0, Math.min(o.left * SCREEN_SCALE, 283 - 78)))}; top: ${px(top >= 18 ? top - 18 : Math.min(below, 212.25 - 16))};"
      >x ${m.at.x} · y ${m.at.y}${m.kind === 'image' ? ' px' : ' %'}</div>`;
  }

  private _ariaLabel(what: string, name: string | undefined, x: number, y: number): string {
    return `${what}${name ? ' ' + name : ''}, x ${x}, y ${y}`;
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
    const at = this._at(img.ref, img.x, img.y);
    const box = `left: ${px(at.x * SCREEN_SCALE)}; top: ${px(at.y * SCREEN_SCALE)}; width: ${px(w * SCREEN_SCALE)}; height: ${px(h * SCREEN_SCALE)};`;
    if (img.icon) {
      // Like the device: an icon without width/height shows nothing.
      if (!img.width || !img.height) return '';
      return html`<div class="fb-item absolute flex cursor-pointer" data-image=${img.name}
        data-ref=${this._refAttr(img.ref)} data-ref-kind="image" ?data-selected=${sameConfigRef(img.ref, this.selected)}
        tabindex=${img.ref ? 0 : nothing} role="button" aria-label=${this._ariaLabel('Image', img.name, at.x, at.y)}
        title=${ITEM_HINT} style="${box} ${this._outline(img.ref)}">
        ${iconView(img.icon, img.width, img.height, img.color, SCREEN_SCALE)}
      </div>`;
    }
    return html`<div class="fb-item absolute flex items-center justify-center border border-dashed border-white/40 text-[9px] text-gray-200 overflow-hidden cursor-pointer"
      data-image=${img.name} data-ref=${this._refAttr(img.ref)} data-ref-kind="image" ?data-selected=${sameConfigRef(img.ref, this.selected)}
      tabindex=${img.ref ? 0 : nothing} role="button" aria-label=${this._ariaLabel('Image', img.name, at.x, at.y)}
      title=${img.src ? `images/${img.src}` : img.name} style="${box} ${this._outline(img.ref)}">${img.src ?? img.name}</div>`;
  }

  /** Outline of the selected entry (drawn outside the box: no layout change). */
  private _outline(ref: ConfigRef | undefined): string {
    return sameConfigRef(ref, this.selected) || sameConfigRef(ref, this._moving?.ref) ? 'outline: 2px solid #AF8BFF; outline-offset: 1px; z-index: 5;' : '';
  }

  private _refAttr(ref: ConfigRef | undefined) {
    return ref ? `${ref.list}:${ref.index}` : nothing;
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
    const at = this._at(list.ref, list.x, list.y);
    return html`
      <div class="fb-item absolute overflow-hidden cursor-pointer" data-list=${list.name}
        data-ref=${this._refAttr(list.ref)} data-ref-kind="list" ?data-selected=${sameConfigRef(list.ref, this.selected)}
        tabindex=${list.ref ? 0 : nothing} role="button" aria-label=${this._ariaLabel('List', list.name, at.x, at.y)}
        title=${ITEM_HINT}
        style="left: ${at.x}%; top: ${at.y}%; width: ${list.width}%; height: ${px(list.rows * rowH)}; ${this._outline(list.ref)}
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
          <div class="text-white flex items-center" aria-hidden="true">
            ${this._dotsIcon(24)}
          </div>
        </div>

        <!-- TFT Screen -->
        <div class="bg-[rgb(30,30,30)] w-[283px] h-[212.25px] relative overflow-hidden text-[11.2px] font-[Montserrat,sans-serif] leading-[11.2px] ${this._moving?.active ? 'cursor-grabbing [&_.fb-item]:cursor-grabbing' : ''}"
          data-screen @pointerdown=${this._onPointerDown} @keydown=${this._onKeyDown}>
          ${this.displayItems.map(item => {
            // Caption, value and unit in the item's fonts and sizes; markup
            // (\f \s \c \a) as on the device (text-spans.ts).
            const valueFont = this._valueFont(item, styles);
            const value = this._isTimeItem(item) ? this.timeStr : (item.value ?? '');
            return html`
            <div class="fb-item absolute cursor-pointer" data-display-item=${item.name ?? item.id ?? ''}
              data-ref=${this._refAttr(item.ref)} data-ref-kind="displayitem" ?data-selected=${sameConfigRef(item.ref, this.selected)}
              tabindex=${item.ref ? 0 : nothing} role="button"
              aria-label=${this._ariaLabel('Display item', item.name ?? item.label, this._at(item.ref, item.x, item.y).x, this._at(item.ref, item.x, item.y).y)}
              title=${item.ref ? ITEM_HINT : 'Sample data: no config.json is loaded'}
              style="${this._itemPosition(item)} width: ${item.width}%; border-bottom: 1px solid rgb(170, 170, 170); ${this._outline(item.ref)}">
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
          ${this._renderMoveBadge()}

          <!-- Status bar -->
          <div class="absolute text-[11.2px] font-normal font-[Montserrat,sans-serif] text-white text-left leading-[11.2px]"
            data-testid="tft-device-name" style="top: 201px; left: 0;">${this.deviceName}</div>
        </div>

        <!-- Right config dots column -->
        <div class="flex items-end w-[29.5px] h-[212.25px] pt-[12.32px] pb-[3.7px] font-fira">
          <div class="text-white flex items-center" aria-hidden="true">
            ${this._dotsIcon(24)}
          </div>
        </div>
      </div>
    `;
  }
}
