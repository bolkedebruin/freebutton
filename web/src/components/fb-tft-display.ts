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
import { attachCanvas } from '../js/emulator/canvas.js';
import type { Emulator } from '../js/emulator/index.js';
import type { LayoutRect } from '../js/emulator/device-state.js';
import {
  alignAnchor, boxOrigin, clampUnits, dragTo, realign, unitRange, unitsAt, SCREEN_H, SCREEN_W,
  type PlacedKind, type Size, type UnitRange, type Units,
} from '../js/display-geometry.js';

/**
 * CSS px per device px of the screen as this component lays it out: one.
 * (The virtual device scales the whole face down when its pane is narrower
 * than the face; the positions here are measured, so they follow.)
 */
const SCREEN_SCALE = 1;
/** A press that moves less than this (CSS px) is a click, not a drag. */
export const DRAG_THRESHOLD_PX = 4;
/** A touch held this long without moving opens the item's menu. */
const LONG_PRESS_MS = 550;
/** Shift + arrow: this many units (display items and lists: %; images: px). */
const BIG_STEP: Record<PlacedKind, number> = { displayitem: 5, list: 5, image: 10 };
const ITEM_HINT = 'Click: show in config.json. Drag or arrow keys: move. Right-click: properties.';
const RUNNING_HINT = 'The script is running: a press on the left or right half of the display is its left or right key.';

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
  /** The x and y the drawn box (the layout the firmware gave) stands for. */
  base: Units;
}

const PLACED_KINDS: readonly string[] = ['displayitem', 'list', 'image'];
/** The least height (device px) of the box over an entry: a thin one (an underline only) can still be pointed at. */
const MIN_BOX_H = 12;
/** The height of the place holder of an entry the firmware draws nothing of yet (no caption, no value). */
const EMPTY_BOX_H = 14;
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
   * The emulator that draws this display (the firmware's LVGL in the page):
   * its main display is shown on a canvas, device pixel for device pixel,
   * and every entry it laid out gets a transparent box over it for the
   * pointer, the keyboard and the selection outline. Null: the screen is
   * drawn here from the config model (an approximation, same layout).
   */
  @property({ attribute: false }) engine: Emulator | null = null;
  /** The face is drawn at a scale that is not a whole number of screen pixels: smooth instead of square pixels. */
  @property({ type: Boolean }) smooth = false;
  /**
   * A script runs in the virtual device: the screen is the device's, not the
   * editor's. A press on its left or right half is the main display's left or
   * right key (`main-key-press` / `main-key-release` with side 0 or 1), held
   * as long as the pointer is down; nothing is selected or moved.
   */
  @property({ type: Boolean }) running = false;
  /** Where the firmware laid out the entries of the shown page (device px). */
  @state() private _layout: readonly LayoutRect[] = [];
  private _attached: { engine: Emulator; canvas: HTMLCanvasElement; detach: () => void; offLayout: () => void } | null = null;
  /** The engine's display could not be shown on a canvas: this component draws the screen itself. */
  @state() private _canvasFailed: Emulator | null = null;
  /** The half held down now (0 left, 1 right); null: none. */
  @state() private _mainDown: 0 | 1 | null = null;
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
    this._releaseMain();
    this._detach();
    if (this._timer) clearInterval(this._timer);
    this._offIcons?.();
    this._offFonts?.();
  }

  protected willUpdate(changed: Map<PropertyKey, unknown>) {
    // The edited config.json came back: as the model, or (the firmware draws) as a new layout.
    if (this._moving && !this._moving.active && (this._drawsEngine()
      ? changed.has('engine') || changed.has('_layout')
      : changed.has('displayItems') || changed.has('lists') || changed.has('images'))) {
      this._moving = null;
    }
    if (changed.has('engine')) {
      this._canvasFailed = null;
      this._layout = this.engine ? this._mainLayout(this.engine) : [];
    }
    if (changed.has('running')) {
      this._endPress();
      this._moving = null;
      if (!this.running) this._releaseMain();
    }
  }

  protected updated() {
    this._attach();
    this._drawGhost();
  }

  // ---- the firmware's pixels ----------------------------------------------

  private _drawsEngine(): boolean {
    return !!this.engine && this._canvasFailed !== this.engine;
  }

  private _mainLayout(engine: Emulator): readonly LayoutRect[] {
    try { return engine.layout('main'); } catch { return []; }
  }

  /** Keep the canvas on the engine's main display. */
  private _attach() {
    const engine = this._drawsEngine() ? this.engine : null;
    const canvas = engine ? this.querySelector<HTMLCanvasElement>('canvas[data-display="main"]') : null;
    if (this._attached && this._attached.engine === engine && this._attached.canvas === canvas) return;
    this._detach();
    if (!engine || !canvas) return;
    try {
      const detach = attachCanvas(engine, 'main', canvas);
      const offLayout = engine.on('layout', (event) => {
        if (event.display === 'main') this._layout = event.rects;
      });
      this._attached = { engine, canvas, detach, offLayout };
    } catch {
      // No main display, or no 2D canvas: the config model is drawn instead.
      this._canvasFailed = engine;
    }
  }

  private _detach() {
    this._attached?.detach();
    this._attached?.offLayout();
    this._attached = null;
  }

  /** The dragged entry's pixels inside its moving box, copied once when the drag starts. */
  private _drawGhost() {
    const ghost = this.querySelector<HTMLCanvasElement>('canvas[data-ghost]:not([data-drawn])');
    const main = this._attached?.canvas;
    const rect = this._moving ? this._layout.find(r => sameConfigRef(r.ref, this._moving!.ref)) : undefined;
    if (!ghost || !main || !rect) return;
    ghost.setAttribute('data-drawn', '');
    ghost.getContext('2d')?.drawImage(main, rect.x, rect.y, rect.width, rect.height, 0, 0, rect.width, rect.height);
  }

  // ---- moving items -------------------------------------------------------

  private _screen(): HTMLElement | null {
    return this.querySelector<HTMLElement>('[data-screen]');
  }

  /** CSS px per device px of the drawn screen (the constant where nothing is laid out). */
  private _scale(): Units {
    const r = this._screen()?.getBoundingClientRect();
    return r && r.width > 0 && r.height > 0
      ? { x: r.width / SCREEN_W, y: r.height / SCREEN_H }
      : { x: SCREEN_SCALE, y: SCREEN_SCALE };
  }

  /** The entry `ref` with its box: as the firmware laid it out; else the width its rule gives and the height as drawn. */
  private _placed(ref: ConfigRef, el: HTMLElement | null): Placed | null {
    const laid = this._drawsEngine() ? this._layout.find(r => sameConfigRef(r.ref, ref)) : undefined;
    const size = (width: number, height: number): Size => (laid ? { width: laid.width, height: laid.height } : { width, height });
    const heightPx = (el?.offsetHeight ?? 0) / SCREEN_SCALE;
    const item = this.displayItems.find(d => sameConfigRef(d.ref, ref));
    if (item) {
      return {
        ref, kind: 'displayitem', align: item.align, at: { x: item.x, y: item.y },
        size: size(Math.trunc(item.width * SCREEN_W / 100), heightPx),
      };
    }
    const list = this.lists.find(l => sameConfigRef(l.ref, ref));
    if (list) {
      return {
        ref, kind: 'list', align: undefined, at: { x: list.x, y: list.y },
        size: size(Math.trunc(list.width * SCREEN_W / 100), heightPx),
      };
    }
    const img = this.images.find(m => sameConfigRef(m.ref, ref));
    if (img) {
      return {
        ref, kind: 'image', align: undefined, at: { x: img.x, y: img.y },
        size: size(img.width ?? 40, img.height ?? 40),
      };
    }
    return null;
  }

  private _itemEl(target: EventTarget | null): { el: HTMLElement; ref: ConfigRef } | null {
    const el = (target as Element | null)?.closest?.<HTMLElement>('[data-ref]');
    const [list, index] = (el?.getAttribute('data-ref') ?? '').split(':');
    const kind = el?.getAttribute('data-ref-kind') as ConfigRef['kind'] | null;
    if (!el || !list || !kind || index === undefined) return null;
    return { el, ref: { kind, list, index: Number(index) } };
  }

  private _releaseMain = () => {
    window.removeEventListener('pointerup', this._releaseMain);
    window.removeEventListener('pointercancel', this._releaseMain);
    if (this._mainDown === null) return;
    const side = this._mainDown;
    this._mainDown = null;
    this.dispatchEvent(new CustomEvent('main-key-release', { bubbles: true, detail: { side } }));
    // Released outside the screen: no click came to swallow.
    setTimeout(() => { this._swallowClick = false; }, 0);
  };

  private _onPointerDown = (e: PointerEvent) => {
    if (e.button !== 0 || e.isPrimary === false) return;
    if (this.running) {
      const r = this._screen()?.getBoundingClientRect();
      if (!r || this._mainDown !== null) return;
      this._mainDown = e.clientX < r.left + r.width / 2 ? 0 : 1;
      // The release may happen anywhere; the click that follows selects nothing.
      this._swallowClick = true;
      window.addEventListener('pointerup', this._releaseMain);
      window.addEventListener('pointercancel', this._releaseMain);
      this.dispatchEvent(new CustomEvent('main-key-press', { bubbles: true, detail: { side: this._mainDown } }));
      return;
    }
    const hit = this._itemEl(e.target);
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
    this._moving = { ...p.item, at, active: true, base: p.item.at };
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
    // A move still on its way: the drawn box stands for the position before it.
    const base = this._moving && sameConfigRef(this._moving.ref, item.ref) ? this._moving.base : item.at;
    if (at.x === item.at.x && at.y === item.at.y) {
      this._moving = null;
      return;
    }
    this._moving = { ...item, at, active: false, base };
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
    if (this.running && e.key.startsWith('Arrow')) {
      e.preventDefault();
      this.dispatchEvent(new CustomEvent('edit-blocked', { bubbles: true }));
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

  /** Top-left corner (device px) of the entry that moves: where the firmware drew it, shifted by the move. */
  private _movedOrigin(m: Moving): { left: number; top: number } {
    const to = boxOrigin(m.kind, m.align, m.at, m.size);
    const laid = this._drawsEngine() ? this._layout.find(r => sameConfigRef(r.ref, m.ref)) : undefined;
    if (!laid) return to;
    const from = boxOrigin(m.kind, m.align, m.base, m.size);
    return { left: laid.x + to.left - from.left, top: laid.y + to.top - from.top };
  }

  /** The position of the item that moves, next to it (inside the screen). */
  private _renderMoveBadge() {
    const m = this._moving;
    if (!m) return '';
    const o = this._movedOrigin(m);
    const top = o.top * SCREEN_SCALE;
    const below = (o.top + m.size.height) * SCREEN_SCALE + 3;
    return html`<div class="absolute z-10 px-1.5 py-[3px] rounded bg-purple-90 text-white text-[10px] leading-none font-mono whitespace-nowrap pointer-events-none"
      data-testid="move-position" role="status"
      style="left: ${px(Math.max(0, Math.min(o.left * SCREEN_SCALE, SCREEN_W - 78)))}; top: ${px(top >= 18 ? top - 18 : Math.min(below, SCREEN_H - 16))};"
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
        title=${this.running ? RUNNING_HINT : ITEM_HINT} style="${box} ${this._outline(img.ref)}">
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
    const rows = list.rows;
    const selected = list.selected;
    const colors = { color: list.color, focusColor: list.focusColor, focusTextColor: list.focusTextColor };
    const middle = (rows - 1) * rowH / 2;
    const shift = middle - Math.max(0, selected) * rowH;
    const at = this._at(list.ref, list.x, list.y);
    return html`
      <div class="fb-item absolute overflow-hidden cursor-pointer" data-list=${list.name}
        data-ref=${this._refAttr(list.ref)} data-ref-kind="list" ?data-selected=${sameConfigRef(list.ref, this.selected)}
        tabindex=${list.ref ? 0 : nothing} role="button" aria-label=${this._ariaLabel('List', list.name, at.x, at.y)}
        title=${this.running ? RUNNING_HINT : ITEM_HINT}
        style="left: ${at.x}%; top: ${at.y}%; width: ${list.width}%; height: ${px(rows * rowH)}; ${this._outline(list.ref)}
          ${fontStyle(font, SCREEN_SCALE)} line-height: ${px(rowH)};">
        <div class="absolute left-0 right-0 rounded-[6px]" data-list-focus
          style="top: ${px(middle)}; height: ${px(rowH)}; background: ${colors.focusColor};${sel.bgOpa !== undefined && sel.bgOpa < 255 ? ` opacity: ${sel.bgOpa / 255};` : ''}"></div>
        <div class="relative" style="transform: translateY(${px(shift)}); transition: transform 200ms ease-out;">
          ${list.items.map((text, i) => html`
            <div class="text-center whitespace-nowrap overflow-hidden text-ellipsis px-[4px]"
              data-list-item=${i} ?data-selected=${i === selected}
              style="height: ${px(rowH)}; ${i === selected
                ? `${selCss} ${textStyleCss(selText, SCREEN_SCALE)} color: ${styleColorCss(parseInt(colors.focusTextColor.slice(1), 16), sel.opa)};`
                : `${textStyleCss(listText, SCREEN_SCALE)} color: ${styleColorCss(parseInt(colors.color.slice(1), 16), ls.opa)};`}"
            >${text}</div>
          `)}
        </div>
      </div>
    `;
  }

  /** The entry of the config model with this ref: what a label needs. */
  private _entry(ref: ConfigRef): { what: string; name: string | undefined; x: number; y: number } | null {
    const item = this.displayItems.find(d => sameConfigRef(d.ref, ref));
    if (item) return { what: 'Display item', name: item.name ?? item.label, x: item.x, y: item.y };
    const list = this.lists.find(l => sameConfigRef(l.ref, ref));
    if (list) return { what: 'List', name: list.name, x: list.x, y: list.y };
    const img = this.images.find(m => sameConfigRef(m.ref, ref));
    return img ? { what: 'Image', name: img.name, x: img.x, y: img.y } : null;
  }

  /**
   * The box over an entry the firmware drew: nothing is drawn in it (the
   * pixels are the canvas's); it takes the pointer, the focus and the
   * outlines. While the entry is dragged it moves with the pointer and
   * carries a copy of the entry's pixels; config.json is written on drop.
   *
   * An entry with nothing to draw yet (its value comes from a script or
   * MQTT) has no pixels: a dashed place holder with its name stands where
   * it will be, so that it can be found and moved (not while a script runs).
   */
  private _renderOverlayBox(r: LayoutRect) {
    const ref = r.ref!;
    const entry = this._entry(ref);
    const moving = this._moving && sameConfigRef(this._moving.ref, ref) ? this._moving : null;
    const o = moving ? this._movedOrigin(moving) : { left: r.x, top: r.y };
    const at = moving ? moving.at : { x: entry?.x ?? 0, y: entry?.y ?? 0 };
    const idle = this.editable && !this.running;
    const empty = !r.visible || r.height === 0;
    const height = empty ? EMPTY_BOX_H : Math.max(r.height, MIN_BOX_H);
    return html`
      <div class="fb-item absolute cursor-pointer ${idle && !moving ? 'hover:outline hover:outline-1 hover:outline-white/50' : ''}
        ${empty ? 'border border-dashed border-white/25 text-[9px] leading-[12px] text-white/45 font-fira px-[3px] overflow-hidden whitespace-nowrap' : ''}"
        data-overlay=${r.kind} ?data-empty=${empty} data-ref=${this._refAttr(ref)} data-ref-kind=${r.kind}
        ?data-selected=${sameConfigRef(ref, this.selected)} tabindex="0" role="button"
        aria-label=${this._ariaLabel(entry?.what ?? 'Item', entry?.name, at.x, at.y)}
        title=${this.running ? RUNNING_HINT : ITEM_HINT}
        style="left: ${px(o.left)}; top: ${px(o.top)}; width: ${px(r.width)}; height: ${px(height)}; ${this._outline(ref)}">
        ${empty ? entry?.name ?? '' : ''}
        ${moving?.active && !empty ? html`<canvas data-ghost width=${r.width} height=${r.height} aria-hidden="true"
          class="block w-full h-full pointer-events-none" style="image-rendering: ${this.smooth ? 'auto' : 'pixelated'};"></canvas>` : ''}
      </div>`;
  }

  /** The firmware's main display on a canvas, and a box over each entry it laid out. */
  private _renderEngine() {
    // What the firmware drew; and, at design time, what it will draw once there is a value.
    const boxes = this._layout.filter(r => r.ref && PLACED_KINDS.includes(r.kind) && r.width > 0
      && ((r.visible && r.height > 0) || !this.running));
    const from = this._moving?.active ? this._layout.find(r => sameConfigRef(r.ref, this._moving!.ref)) : undefined;
    return html`
      <canvas data-display="main" width=${SCREEN_W} height=${SCREEN_H} class="absolute inset-0 w-full h-full block"
        role="img" aria-label="The main display, as the firmware draws it"
        style="image-rendering: ${this.smooth ? 'auto' : 'pixelated'};"></canvas>
      ${from ? html`<div class="absolute bg-black/60 pointer-events-none" data-move-origin
        style="left: ${px(from.x)}; top: ${px(from.y)}; width: ${px(from.width)}; height: ${px(from.height)};"></div>` : ''}
      ${boxes.map(r => this._renderOverlayBox(r))}`;
  }

  /** The screen drawn here from the config model: the same layout, approximate (the browser's text). */
  private _renderModel() {
    const styles = getStyles();
    const caption = styles[StyleElement.DisplayCaption];
    const valueStyle = styles[StyleElement.DisplayValue];
    const unitStyle = styles[StyleElement.DisplayUnit];
    return html`
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
              title=${this.running ? RUNNING_HINT : item.ref ? ITEM_HINT : 'Sample data: no config.json is loaded'}
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
          <!-- Status bar -->
          <div class="absolute text-[12.66px] font-normal font-[Montserrat,sans-serif] text-white text-left leading-[12.66px]"
            data-testid="tft-device-name" style="top: 227px; left: 0;">${this.deviceName}</div>`;
  }

  render() {
    const engine = this._drawsEngine();
    return html`
      <!-- LargeDisplay outer -->
      <div class="bg-[rgb(20,20,20)] w-[358px] h-[246.72px] flex py-[3.36px]">
        <!-- Left config dots column -->
        <div class="flex items-end justify-center w-[19px] h-[240px] pb-[4px] font-fira">
          <div class="text-white flex items-center" aria-hidden="true">
            ${this._dotsIcon(16)}
          </div>
        </div>

        <!-- TFT Screen: 320 x 240, a CSS px per device pixel -->
        <div class="bg-[rgb(30,30,30)] w-[320px] h-[240px] relative overflow-hidden text-[12.66px] font-[Montserrat,sans-serif] leading-[12.66px] ${this._moving?.active ? 'cursor-grabbing [&_.fb-item]:cursor-grabbing' : ''}"
          data-screen data-drawn-by=${engine ? 'firmware' : 'model'} @pointerdown=${this._onPointerDown} @keydown=${this._onKeyDown}>
          ${engine ? this._renderEngine() : this._renderModel()}
          ${this._renderMoveBadge()}
          ${this._mainDown === null ? '' : html`<div class="absolute top-0 bottom-0 w-1/2 bg-white/10 pointer-events-none ${this._mainDown === 0 ? 'left-0' : 'right-0'}"
            data-main-pressed=${this._mainDown}></div>`}
        </div>

        <!-- Right config dots column -->
        <div class="flex items-end justify-center w-[19px] h-[240px] pb-[4px] font-fira">
          <div class="text-white flex items-center" aria-hidden="true">
            ${this._dotsIcon(16)}
          </div>
        </div>
      </div>
    `;
  }
}
