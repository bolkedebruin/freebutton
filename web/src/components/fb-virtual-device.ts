import { LitElement, html, nothing } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import {
  emit, on, sameConfigRef, type ConfigRef, type EmulatorSnapshot, type PageConfig, type ConnectorConfig, type PreviewState,
} from '../js/app-state.js';
import { emulatorSession, type FaceInfo } from '../js/emulator-session.js';
import type { Emulator } from '../js/emulator/index.js';
import { drawScreenshot, saveCanvasPng } from '../js/screenshot.js';
import './fb-virtual-info.js';
import { getConfig, getConnectors, getFontTable, getPreviewState, loadDefaultConfig } from '../js/device-config.js';
import { positionsForConnector } from '../js/config-model.js';
import { addItem, listKeyFor, removeItem, setItemProps, type PropValue } from '../js/config-edit.js';
import { applyConfigEdit, ensureConfigOpen, readConfigItem } from '../js/config-session.js';
import { fieldText, fieldValue, itemFields, KIND_LABEL, type Field } from '../js/item-props.js';
import { openPopover, type PopoverHandle } from '../js/popover.js';
import { CameraIcon, InfoIcon, PlusIcon, TrashIcon, XIcon } from '../icons/index.js';
import { askConfirm } from './fb-dialog.js';
import type { FbTftDisplay } from './fb-tft-display.js';

/**
 * Front-face geometry (CSS px). Every display is drawn a CSS px per device
 * pixel: the main display 320 x 240 between two 19 px columns, a key's
 * display 160 x 80. A pane narrower than the face scales the whole face down.
 */
const FACE_W = 358;
const TFT_H = 240 + 6.72;
const KEY_ROW_H = 86 + 6.72;
/**
 * The least the face is high: what the info side needs without scrolling
 * (its title, the rows of fb-virtual-info with all gauges, the display memory
 * and the line about dropped texts).
 * The front is usually higher; the flip container takes the higher of the two.
 */
const MIN_FACE_H = 392;

// Kept for callers that import it from here.
export { formatFirmware } from './fb-device-info.js';

const INVALID_HINT = 'config.json is not valid JSON: correct it to move or change items here';
const RUNNING_HINT = 'The script is running: Stop (in the toolbar) to move or change items';
const NOTICE_MS = 6000;
/** How long a new entry may take to be drawn by the firmware before its properties are not opened. */
const DRAWN_WAIT_MS = 1500;
/** A new text item (a caption line, about 28 px) starts no lower than this (%): it stays on the screen. */
const NEW_ITEM_MAX_Y = 88;

const FIELD_CLASS = 'w-full min-w-0 bg-white/5 border border-white/10 rounded-md px-2 py-1 text-xs text-text-default outline-none focus:border-purple-80';
const STEP_CLASS = 'w-7 h-7 shrink-0 rounded-lg flex items-center justify-center text-lg leading-none pb-0.5 text-gray-100 '
  + 'hover:text-text-default hover:bg-white/10 transition-colors outline-none focus-visible:ring-2 focus-visible:ring-purple-80 '
  + 'disabled:opacity-30 disabled:hover:bg-transparent disabled:hover:text-gray-100 disabled:cursor-default cursor-pointer';
const MENU_ITEM_CLASS = 'flex items-center gap-2 px-3 py-1.5 rounded-lg text-xs text-left text-text-default hover:bg-white/10 focus-visible:bg-white/10 outline-none';

/** "Page 2 of 5"; with page numbers that are not 1..n: "Page 7 (3/3)". */
export function pageLabel(pages: Array<{ number: number }>, current: number): string {
  const index = pages.findIndex(p => p.number === current);
  if (index < 0) return `Page ${current}`;
  const plain = pages.every((p, i) => p.number === i + 1);
  return plain ? `Page ${current} of ${pages.length}` : `Page ${current} (${index + 1}/${pages.length})`;
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
  // The device's own name, once it reported it ('' until then).
  @state() private _deviceName = '';
  // The config.json entry under the editor cursor (outlined on the device).
  @state() private _selected: ConfigRef | null = null;
  /** Why something asked for here was not done (shown for a moment). */
  @state() private _notice = '';
  /** The page menu is open. */
  @state() private _pageMenuOpen = false;
  /** The emulator: a script running in this virtual device. */
  @state() private _emu: EmulatorSnapshot = emulatorSession.snapshot();
  /** The pages as the running script shows them; null: config.json's. */
  @state() private _runPages: PageConfig[] | null = null;
  /** The page the script showed last (the stepper may look at another one). */
  private _shownPage: number | null = null;
  /** The emulator whose displays are shown (the firmware's pixels); null: the config model is drawn here. */
  @state() private _engine: Emulator | null = emulatorSession.face();
  @state() private _face: FaceInfo = emulatorSession.faceInfo();
  /** The face's scale: 1, or less in a pane narrower than the face. */
  @state() private _zoom = 1;
  private _resize?: ResizeObserver;
  private _dpr?: MediaQueryList;

  private _unsubs: Array<() => void> = [];
  private _noticeTimer?: ReturnType<typeof setTimeout>;
  private _popover: PopoverHandle | null = null;

  connectedCallback() {
    super.connectedCallback();
    this._unsubs.push(
      on('device-config', (e) => {
        this._pages = e.detail.pages;
        this._connectors = e.detail.connectors ?? [];
        this._preview = e.detail.preview ?? null;
        if (!this._runPages && !this._pages.some(p => p.number === this.currentPage)) {
          this.currentPage = this._pages[0]?.number ?? 1;
        }
      }),
      on('device-info', (e) => { this._deviceName = e.detail.data?.deviceName ?? ''; }),
      on('config-selection', (e) => this._select(e.detail.ref)),
      on('emulator-change', (e) => {
        const was = this._emu.status;
        const stale = this._emu.stale;
        this._emu = e.detail;
        // What runs is the older text: said once, when it becomes so (the toolbar's Run again keeps a mark).
        if (e.detail.status === 'running' && e.detail.stale && e.detail.stale !== stale) {
          this._say(e.detail.stale === 'config' ? 'config.json changed: Run again to use it' : 'The script changed: Run again to use it');
        }
        // Nothing can be changed here while it runs.
        if (e.detail.status !== 'idle' && was === 'idle') this._popover?.close();
      }),
      on('emulator-state', () => this._followEmulator()),
      on('emulator-face', () => this._followFace()),
    );
    // On screen: config.json is drawn by the firmware from here on (the engine and the media load now, not with the IDE).
    emulatorSession.showPage(this.currentPage);
    emulatorSession.showPreview();
    if (typeof ResizeObserver === 'function') {
      this._resize = new ResizeObserver(() => this._fit());
      this._resize.observe(this);
    }
    this._watchScreen();
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
    this._popover?.close();
    if (this._noticeTimer) clearTimeout(this._noticeTimer);
    this._resize?.disconnect();
    this._dpr?.removeEventListener('change', this._onScreen);
    // The emulator belongs to this element: without it nothing shows what runs.
    emulatorSession.stop();
    emulatorSession.hidePreview();
  }

  private _followFace() {
    this._engine = emulatorSession.face();
    this._face = emulatorSession.faceInfo();
  }

  /** Another screen, or the browser's zoom: a device pixel is another number of screen pixels (square pixels or smoothing). */
  private _onScreen = () => {
    this._watchScreen();
    this.requestUpdate();
  };

  private _watchScreen() {
    this._dpr?.removeEventListener('change', this._onScreen);
    if (typeof window.matchMedia !== 'function') return;
    this._dpr = window.matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`);
    this._dpr.addEventListener?.('change', this._onScreen);
  }

  /** The whole face at a smaller scale when the pane is narrower than it. */
  private _fit() {
    const width = this.clientWidth;
    this._zoom = width > 0 && width < FACE_W ? Math.max(0.5, Math.floor(width / FACE_W * 1000) / 1000) : 1;
  }

  /**
   * Square pixels only when a device pixel is a whole number of screen
   * pixels (the face at its own size on a 1x or 2x screen); at any other
   * scale the browser smooths, as text with uneven pixel widths reads worse.
   */
  private get _smooth(): boolean {
    const perPixel = this._zoom * (window.devicePixelRatio || 1);
    return Math.abs(perPixel - Math.round(perPixel)) > 0.01;
  }

  /** Draw what the running script made of the device; go to the page it shows. */
  private _followEmulator() {
    this._runPages = emulatorSession.pages();
    const shown = emulatorSession.shownPage();
    if (shown !== null && shown !== this._shownPage) this.currentPage = shown;
    this._shownPage = shown;
    if (!this._runPages && !this._pages.some(p => p.number === this.currentPage)) {
      this.currentPage = this._pages[0]?.number ?? 1;
    }
  }

  /** The pages drawn: the running device's, else config.json's (the unsaved draft included). */
  private _drawnPages(): PageConfig[] {
    return this._runPages ?? (this._pages.length > 0 ? this._pages : getConfig());
  }

  private get _running(): boolean {
    return this._emu.status === 'running';
  }

  protected willUpdate(changed: Map<PropertyKey, unknown>) {
    // The device that is shown goes to the page looked at (a page the running device went to itself is the same one).
    if (changed.has('currentPage')) emulatorSession.showPage(this.currentPage);
    if (changed.has('_emu')) this._followFace();
  }

  protected updated() {
    // The popover is drawn outside this element: it follows this element's state.
    this._popover?.update();
  }

  /** Outline the entry the editor cursor is in; show a page it is on. */
  private _select(ref: ConfigRef | null) {
    this._selected = ref;
    if (!ref) return;
    const has = (p: PageConfig) => [...p.buttons, ...p.displayItems, ...(p.lists ?? []), ...(p.images ?? [])]
      .some(item => sameConfigRef(item.ref, ref));
    const pages = this._drawnPages();
    if (pages.find(p => p.number === this.currentPage && has(p))) return;
    const page = pages.find(has);
    if (page) this.currentPage = page.number;
    // The front shows it: an outline on the back side would be invisible.
    if (page && this._flipped) this._toggleFlip();
  }

  private _refOf(target: EventTarget | null): { el: HTMLElement; ref: ConfigRef } | null {
    const el = (target as Element | null)?.closest?.<HTMLElement>('[data-ref]');
    const [list, index] = (el?.getAttribute('data-ref') ?? '').split(':');
    const kind = el?.getAttribute('data-ref-kind') as ConfigRef['kind'] | null;
    if (!el || !list || !kind || index === undefined) return null;
    return { el, ref: { kind, list, index: Number(index) } };
  }

  /** A click on a drawn entry (data-ref="list:index"): select it in config.json. */
  private _onFrontClick(e: Event) {
    const hit = this._refOf(e.target);
    // While a script runs a key is pressed, not looked up (the editor would take the focus at every press).
    if (!hit || (this._running && hit.ref.kind === 'button')) return;
    emit('config-reveal', { ref: hit.ref });
  }

  // ---- the emulator ---------------------------------------------------------

  private _onKeyPress(e: CustomEvent<{ position: number }>) {
    emulatorSession.press(e.detail.position);
  }

  private _onKeyRelease(e: CustomEvent<{ position: number }>) {
    emulatorSession.release(e.detail.position);
  }

  /** The main display's left / right half: its two keys. */
  private _onMainKey(e: CustomEvent<{ side: 0 | 1 }>, down: boolean) {
    const keys = emulatorSession.mainKeys();
    if (!keys) return;
    if (down) emulatorSession.press(keys[e.detail.side]); else emulatorSession.release(keys[e.detail.side]);
  }

  private _selectPage(page: number) {
    this.currentPage = page;
  }

  private _toggleFlip() {
    this._flipped = !this._flipped;
    this._popover?.close();
  }

  private _say(text: string) {
    if (this._noticeTimer) clearTimeout(this._noticeTimer);
    this._notice = text;
    this._noticeTimer = setTimeout(() => { this._notice = ''; }, NOTICE_MS);
  }

  /**
   * Items can be moved and changed: not while config.json in the editor is
   * not valid JSON, and not while a script runs (what is drawn then is the
   * script's runtime state, which is never written to config.json).
   */
  private get _editable(): boolean {
    return this._preview !== 'stale' && (this._emu.status === 'idle' || this._emu.status === 'error');
  }

  /** Why nothing can be moved or changed here now. */
  private get _blockedHint(): string {
    return this._emu.status === 'running' || this._emu.status === 'starting' ? RUNNING_HINT : INVALID_HINT;
  }

  // ---- edits of config.json made here -------------------------------------

  /** A drop or a nudge on the TFT: write the entry's new x and y. */
  private async _onItemMove(e: CustomEvent<{ ref: ConfigRef; x: number; y: number }>) {
    const tft = e.target as FbTftDisplay;
    const { ref, x, y } = e.detail;
    const ok = await ensureConfigOpen();
    const result = ok
      ? applyConfigEdit(s => setItemProps(s, ref, { x, y }), { reveal: ref, userEvent: 'input.device.move' })
      : null;
    if (result?.ok) return;
    tft.cancelMove?.();
    if (result) this._say(result.error);
  }

  private _itemElement(ref: ConfigRef): HTMLElement | null {
    return this.querySelector<HTMLElement>(`.device-flip-front [data-ref="${ref.list}:${ref.index}"]`);
  }

  /** What takes the focus for an entry: the display item itself, a key's button. */
  private _focusTarget(ref: ConfigRef): HTMLElement | null {
    const el = this._itemElement(ref);
    return el?.matches('[tabindex]') ? el : el?.querySelector<HTMLElement>('button') ?? el ?? null;
  }

  /** Right-click, the menu key, Shift+F10 or a long press on the front. */
  private _onContextMenu(e: Event) {
    const hit = this._refOf(e.target);
    if (hit) {
      e.preventDefault();
      void this._openProperties(hit.ref);
      return;
    }
    // Empty space of the main display: offer a new item there.
    const screen = (e.target as Element | null)?.closest?.('[data-screen]');
    const tft = screen?.closest('fb-tft-display') as FbTftDisplay | null;
    if (!tft || !(e instanceof MouseEvent)) return;
    e.preventDefault();
    if (!this._editable) { this._say(this._blockedHint); return; }
    this._openAddMenu(tft, e.clientX, e.clientY);
  }

  private _onFrontKeyDown(e: KeyboardEvent) {
    if (e.key !== 'ContextMenu' && !(e.key === 'F10' && e.shiftKey)) return;
    const hit = this._refOf(e.target);
    if (!hit) return;
    e.preventDefault();
    void this._openProperties(hit.ref);
  }

  /** The properties of one entry, next to it. */
  private async _openProperties(ref: ConfigRef) {
    if (!this._editable) { this._say(this._blockedHint); return; }
    if (!(await ensureConfigOpen())) return;
    if (!readConfigItem(ref)) {
      this._say(`${ref.list}[${ref.index}] cannot be read from config.json`);
      return;
    }
    this._selected = ref;
    this._popover = openPopover({
      anchor: () => this._itemElement(ref),
      placement: 'left-start',
      role: 'dialog',
      label: `${KIND_LABEL[ref.kind]} properties`,
      testid: 'item-popover',
      className: 'w-[276px] p-3 flex flex-col gap-2.5',
      content: () => this._renderProperties(ref),
      returnFocus: () => this._focusTarget(ref),
      onClose: () => { this._popover = null; },
    });
  }

  /** One change from the popover: one edit of config.json. */
  private _setProps(ref: ConfigRef, props: Record<string, PropValue>, group: string) {
    if (!this._editable) { this._say(this._blockedHint); return; }
    const result = applyConfigEdit(s => setItemProps(s, ref, props), { reveal: ref, userEvent: `input.device.${group}` });
    if (!result.ok) this._say(result.error);
    this._popover?.update();
  }

  /**
   * A field changed. `commit` = the user is done with it (change event): an
   * empty number then removes the key; while typing, only a value is written.
   */
  private _onField(ref: ConfigRef, field: Field, input: string, commit: boolean) {
    const value = fieldValue(field, input);
    if (value === null) return;
    if (value === undefined && !commit) return;
    if (field.key === 'align' && ref.kind === 'displayitem' && typeof value === 'number') {
      // The box stays where it is: x and y become the offset from the new corner.
      const tft = this._itemElement(ref)?.closest('fb-tft-display') as FbTftDisplay | null;
      const at = tft?.keepInPlace?.(ref, value);
      this._setProps(ref, { align: value, ...(at ? { x: at.x, y: at.y } : {}) }, 'align');
      return;
    }
    this._setProps(ref, { [field.key]: value }, field.key);
  }

  private async _deleteItem(ref: ConfigRef, name: string) {
    this._popover?.close();
    const what = `${KIND_LABEL[ref.kind].toLowerCase()}${name ? ` "${name}"` : ''}`;
    if (!(await askConfirm(`Delete ${what} from config.json?\nUndo in the editor brings it back.`, 'Delete'))) {
      this._focusTarget(ref)?.focus();
      return;
    }
    const result = applyConfigEdit(s => removeItem(s, ref), { userEvent: 'delete.device' });
    if (!result.ok) this._say(result.error);
  }

  /** Font names for a select: the style's fonts and the system fonts. */
  private _fontOptions(current: unknown): Array<{ value: string; label: string }> {
    const faces = getFontTable().faces;
    const options = faces.map(f => ({ value: f.name, label: f.system ? `${f.name} (system)` : f.name }));
    const byId = typeof current === 'number' ? faces.find(f => f.id === current) : undefined;
    const name = byId?.name ?? (typeof current === 'string' ? current : current === undefined ? '' : String(current));
    if (name && !options.some(o => o.value === name)) options.push({ value: name, label: `${name} (not in style.fonts)` });
    return options;
  }

  private _fontValue(current: unknown): string {
    const byId = typeof current === 'number' ? getFontTable().faces.find(f => f.id === current) : undefined;
    return byId?.name ?? (typeof current === 'string' ? current : current === undefined ? '' : String(current));
  }

  /** The colour drawn for an entry that sets none (the style's), for the swatch. */
  private _styleColor(ref: ConfigRef, key: string): string {
    for (const page of this._pages.length > 0 ? this._pages : getConfig()) {
      const list = page.lists?.find(l => sameConfigRef(l.ref, ref));
      if (list) return { color: list.color, focuscolor: list.focusColor, focustextcolor: list.focusTextColor }[key] ?? '#ffffff';
    }
    return '#ffffff';
  }

  private _renderField(ref: ConfigRef, field: Field, raw: Record<string, unknown>) {
    const value = raw[field.key] ?? (field.key === 'toplabel' ? raw.topLabel : undefined);
    const text = fieldText(field, value);
    const id = `fb-prop-${field.key}`;
    const onInput = (e: Event) => this._onField(ref, field, (e.target as HTMLInputElement).value, false);
    const onChange = (e: Event) => this._onField(ref, field, (e.target as HTMLInputElement).value, true);
    let control;
    switch (field.type) {
      case 'text':
        control = html`<input id=${id} type="text" class=${FIELD_CLASS} spellcheck="false" autocomplete="off"
          data-field=${field.key} .value=${text} @input=${onInput} />`;
        break;
      case 'lines':
        control = html`<textarea id=${id} class="${FIELD_CLASS} resize-y min-h-[64px] leading-snug" rows="4" spellcheck="false"
          data-field=${field.key} .value=${text} @change=${onChange}></textarea>`;
        break;
      case 'number':
        control = html`<input id=${id} type="number" class=${FIELD_CLASS} step="1"
          min=${field.min ?? nothing} max=${field.max ?? nothing} placeholder=${field.unset ?? ''}
          data-field=${field.key} .value=${text} @input=${onInput} @change=${onChange} />`;
        break;
      case 'font': {
        const current = this._fontValue(value);
        control = html`<select id=${id} class=${FIELD_CLASS} data-field=${field.key} @change=${onChange}>
          <option value="" ?selected=${current === ''}>Style's font</option>
          ${this._fontOptions(value).map(o => html`<option value=${o.value} ?selected=${o.value === current}>${o.label}</option>`)}
        </select>`;
        break;
      }
      case 'select':
        control = html`<select id=${id} class=${FIELD_CLASS} data-field=${field.key} @change=${onChange}>
          ${text === '' || !field.options?.some(o => String(o.value) === text)
            ? html`<option value="" selected disabled>${text === '' || text === '0' ? 'Top left (default)' : `${text} (other)`}</option>` : ''}
          ${field.options?.map(o => html`<option value=${o.value} ?selected=${String(o.value) === text}>${o.label}</option>`)}
        </select>`;
        break;
      case 'color':
        control = html`<div class="flex items-center gap-2">
          <input id=${id} type="color" class="w-8 h-6 shrink-0 p-0 bg-transparent border border-white/10 rounded cursor-pointer"
            data-field=${field.key} .value=${text || this._styleColor(ref, field.key)} @input=${onInput} />
          <span class="flex-1 text-[11px] font-mono text-gray-100">${text || 'style\'s colour'}</span>
          ${text ? html`<button class="px-1.5 py-0.5 rounded text-[11px] text-gray-100 hover:bg-white/10 hover:text-text-default outline-none focus-visible:ring-2 focus-visible:ring-purple-80"
            data-clear=${field.key} title="Remove the key: the style's colour applies" aria-label="Use the style's colour for ${field.label}"
            @click=${() => this._setProps(ref, { [field.key]: undefined }, field.key)}>Reset</button>` : ''}
        </div>`;
        break;
    }
    return html`
      <div class="flex flex-col gap-1 min-w-0 ${field.half ? '' : 'col-span-2'}">
        <label for=${id} class="text-[10px] uppercase tracking-wider text-gray-100">${field.label}</label>
        ${control}
      </div>
    `;
  }

  private _renderProperties(ref: ConfigRef) {
    const raw = readConfigItem(ref);
    if (!raw) {
      return html`<p class="text-xs text-status-orange" data-testid="item-popover-gone">
        ${this._editable ? 'This entry is no longer in config.json.' : this._blockedHint}</p>`;
    }
    const name = typeof raw.name === 'string' ? raw.name : '';
    const fields = itemFields(ref.kind, raw);
    const hints = [...new Set(fields.map(f => f.hint).filter((h): h is string => !!h))];
    return html`
      <div class="flex items-center gap-2">
        <span class="flex-1 min-w-0 truncate text-xs font-semibold text-text-default" data-testid="item-popover-title"
          >${KIND_LABEL[ref.kind]}${name ? html` <span class="font-mono font-normal text-gray-100">${name}</span>` : ''}</span>
        <button class="p-1 rounded-md text-gray-100 hover:text-text-default hover:bg-white/10 outline-none focus-visible:ring-2 focus-visible:ring-purple-80"
          aria-label="Close" title="Close (Esc)" data-testid="item-popover-close" @click=${() => this._popover?.close()}>
          <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5">${XIcon()}</span>
        </button>
      </div>
      <div class="grid grid-cols-2 gap-x-2 gap-y-2">
        ${fields.map(f => this._renderField(ref, f, raw))}
      </div>
      ${hints.map(h => html`<p class="text-[10px] leading-snug text-gray-100">Size: ${h}. Empty: the style's.</p>`)}
      <div class="flex items-center justify-between gap-2 pt-2 border-t border-white/10">
        <span class="min-w-0 truncate text-[10px] font-mono text-gray-100" title="This entry in config.json">${ref.list}[${ref.index}]</span>
        <button class="shrink-0 whitespace-nowrap flex items-center gap-1.5 px-2 py-1 rounded-md text-xs bg-status-red/20 text-status-red hover:bg-status-red/30 outline-none focus-visible:ring-2 focus-visible:ring-status-red"
          data-testid="item-delete" @click=${() => this._deleteItem(ref, name)}>
          <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5">${TrashIcon()}</span>
          Delete item…
        </button>
      </div>
    `;
  }

  /** Right-click on empty space of the main display. */
  private _openAddMenu(tft: FbTftDisplay, clientX: number, clientY: number) {
    const at = tft.unitsAtPoint(clientX, clientY);
    this._popover = openPopover({
      anchor: { left: clientX, top: clientY, width: 0, height: 0 },
      placement: 'bottom-start',
      role: 'menu',
      label: 'Main display',
      testid: 'display-menu',
      className: 'w-52 p-1 flex flex-col',
      returnFocus: null,
      content: () => html`
        <button role="menuitem" class=${MENU_ITEM_CLASS} data-testid="menu-add-text"
          @click=${() => { this._popover?.close(); void this._addTextItem(at.x, at.y); }}>
          <span class="w-4 h-4 flex items-center justify-center [&>svg]:w-4 [&>svg]:h-4">${PlusIcon()}</span>
          Add text item here
        </button>`,
      onClose: () => { this._popover = null; },
    });
  }

  /** A new display item at x, y (%) on the current page, then its properties. */
  private async _addTextItem(x: number, y: number) {
    if (!this._editable) { this._say(this._blockedHint); return; }
    if (!(await ensureConfigOpen())) return;
    const taken = new Set((this._pages.length > 0 ? this._pages : getConfig())
      .flatMap(p => p.displayItems.map(d => d.name)));
    let n = 1;
    while (taken.has(`text${n}`)) n++;
    const width = Math.max(10, Math.min(40, 100 - x));
    const result = applyConfigEdit(s => addItem(s, listKeyFor(s, 'displayitem'), {
      name: `text${n}`, page: this.currentPage, x: Math.min(x, 100 - width), y: Math.min(y, NEW_ITEM_MAX_Y), width, align: 1, boxtype: 1, label: 'Text',
    }), { userEvent: 'input.device.add' });
    if (!result.ok) { this._say(result.error); return; }
    if (result.ref && await this._whenDrawn(result.ref)) void this._openProperties(result.ref);
  }

  /** Resolves when the entry is on the front (the firmware draws a new config a moment after the edit); false: it did not come. */
  private async _whenDrawn(ref: ConfigRef): Promise<boolean> {
    const until = Date.now() + DRAWN_WAIT_MS;
    for (;;) {
      await this.updateComplete;
      if (this._itemElement(ref)) return true;
      if (Date.now() > until) return false;
      await new Promise(resolve => setTimeout(resolve, 20));
    }
  }

  /** The main display as a PNG file, at the device's resolution (nothing is written on a device for it). */
  private _saveScreenshot() {
    const shot = emulatorSession.screenshot();
    if (!shot) return;
    const canvas = document.createElement('canvas');
    drawScreenshot(canvas, shot);
    saveCanvasPng(canvas, `virtual-device-page${this.currentPage}.png`);
  }

  // ---- pages --------------------------------------------------------------

  /** The list of all pages, under the stepper's label. */
  private _togglePageMenu(button: HTMLElement, pages: PageConfig[]) {
    if (this._pageMenuOpen) { this._popover?.close(); return; }
    this._pageMenuOpen = true;
    this._popover = openPopover({
      anchor: button,
      placement: 'bottom-start',
      role: 'menu',
      label: 'Go to page',
      testid: 'page-menu',
      className: 'w-[132px] p-1',
      // Many pages: the list scrolls.
      content: () => html`<div class="flex flex-col max-h-[264px] overflow-y-auto custom-scrollbar">${pages.map(page => html`
        <button role="menuitemradio" aria-checked=${page.number === this.currentPage ? 'true' : 'false'}
          class="${MENU_ITEM_CLASS} justify-between ${page.number === this.currentPage ? 'bg-white/10' : ''}"
          ?data-autofocus=${page.number === this.currentPage} data-page=${page.number}
          @click=${() => { this._selectPage(page.number); this._popover?.close(); }}>
          Page ${page.number}${page.number === this.currentPage ? html`<span class="text-purple-80" aria-hidden="true">●</span>` : ''}
        </button>`)}</div>`,
      onClose: () => { this._popover = null; this._pageMenuOpen = false; },
    });
  }

  /**
   * ‹ Page 2 of 5 ›: the same width for any number of pages. The label opens
   * the list of pages when there are more than two. Belongs to the front
   * face: hidden (space kept) while the back is showing.
   */
  private _renderStepper(pages: PageConfig[]) {
    const index = pages.findIndex(p => p.number === this.currentPage);
    const off = this._flipped;
    const label = pageLabel(pages, this.currentPage);
    // From the page shown at the click (two quick clicks are two steps).
    const go = (step: number) => {
      const p = pages[pages.findIndex(pg => pg.number === this.currentPage) + step];
      if (p) this._selectPage(p.number);
    };
    const labelClass = 'w-[98px] h-7 shrink-0 rounded-lg text-xs font-medium tabular-nums text-text-default text-center whitespace-nowrap';
    return html`
      <div class="flex items-center shrink-0 rounded-xl p-0.5 bg-white/5 border border-white/10 ${off ? 'invisible' : ''}"
        role="group" aria-label="Pages" data-testid="page-stepper" aria-hidden=${off ? 'true' : 'false'}>
        <button class=${STEP_CLASS} aria-label="Previous page" title="Previous page" data-testid="page-prev"
          ?disabled=${off || index <= 0} @click=${() => go(-1)}>‹</button>
        ${pages.length > 2 ? html`
          <button class="${labelClass} cursor-pointer hover:bg-white/10 transition-colors outline-none focus-visible:ring-2 focus-visible:ring-purple-80"
            data-testid="page-label" title="Go to page…" aria-haspopup="menu" aria-expanded=${this._pageMenuOpen ? 'true' : 'false'}
            ?disabled=${off} @click=${(e: Event) => this._togglePageMenu(e.currentTarget as HTMLElement, pages)}>${label}</button>
        ` : html`
          <span class="${labelClass} leading-7" data-testid="page-label" aria-live="polite">${label}</span>
        `}
        <button class=${STEP_CLASS} aria-label="Next page" title="Next page" data-testid="page-next"
          ?disabled=${off || index < 0 || index >= pages.length - 1} @click=${() => go(1)}>›</button>
      </div>
    `;
  }

  /**
   * Back face: the virtual device itself (the emulator's identity, state,
   * heap, limits), measured while this side is shown. The real device's
   * information is in the Device view; nothing is asked of it for this.
   */
  private _renderInfoCard() {
    return html`
      <div class="w-full h-full bg-[rgb(20,20,20)] rounded-2xl p-3.5 flex flex-col gap-2 overflow-y-auto custom-scrollbar" data-testid="info-card">
        <div class="flex items-center justify-between">
          <span class="text-[13px] font-medium text-gray-100">Virtual device</span>
          <button
            class="ml-auto mr-1 flex items-center gap-1.5 px-2 py-1 rounded-lg text-[11px] text-gray-100 hover:bg-white/10 hover:text-white cursor-pointer transition-colors outline-none focus-visible:ring-2 focus-visible:ring-purple-80 disabled:opacity-40 disabled:cursor-default disabled:hover:bg-transparent"
            title=${this._engine ? 'Save the main display as a PNG, at the device\'s resolution (nothing is written on a device)' : 'No display drawn by the firmware to save'}
            ?disabled=${!this._engine} @click=${() => this._saveScreenshot()} data-testid="virtual-screenshot">
            <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5">${CameraIcon()}</span>
            Save screenshot
          </button>
          <button
            class="p-1 rounded-lg hover:bg-white/10 cursor-pointer transition-colors text-gray-200 hover:text-white outline-none focus-visible:ring-2 focus-visible:ring-purple-80"
            title="Back to the device" aria-label="Back to the device"
            @click=${() => this._toggleFlip()}
            data-testid="settings-back"
          >
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <line x1="18" y1="6" x2="6" y2="18" /><line x1="6" y1="6" x2="18" y2="18" />
            </svg>
          </button>
        </div>
        <fb-virtual-info .active=${this._flipped}></fb-virtual-info>
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
    const slots = this._slots();
    const smooth = this._smooth;
    if (slots.length === 0) {
      return html`
        <fb-tft-display .currentPage=${this.currentPage} .displayItems=${items} .deviceName=${this._deviceName}
          .lists=${lists} .images=${images} .selected=${this._selected} .editable=${this._editable} .running=${this._running}
          .engine=${this._engine} .smooth=${smooth}></fb-tft-display>
        <fb-button-grid .currentPage=${this.currentPage} .buttons=${buttons} .selected=${this._selected} .pressable=${this._running}
          .engine=${this._engine} .smooth=${smooth}></fb-button-grid>
      `;
    }
    return slots.map(c => c.type === 'display'
      ? html`<fb-tft-display data-connector=${c.id} .currentPage=${this.currentPage} .displayItems=${items}
          .deviceName=${this._deviceName} .lists=${lists} .images=${images} .selected=${this._selected}
          .editable=${this._editable} .running=${this._running} .engine=${this._engine} .smooth=${smooth}></fb-tft-display>`
      : html`<fb-button-grid data-connector=${c.id} .currentPage=${this.currentPage} .buttons=${buttons}
          .positions=${positionsForConnector(c.id)} .selected=${this._selected} .pressable=${this._running}
          .engine=${this._engine} .smooth=${smooth}></fb-button-grid>`);
  }

  /**
   * The modules drawn, top to bottom: the known layout (the device's
   * hardware scan, else config.json "info.connectors"); without one, the
   * modules the firmware in the page made of the config, so that every bar
   * shows its two key displays on every page. Empty: neither (legacy).
   */
  private _slots(): ConnectorConfig[] {
    const known = this._connectors.filter(c => c.type !== 'empty');
    if (known.length > 0 || !this._engine) return known;
    return this._engine.state.connectors
      .map((kind, id): ConnectorConfig => ({ id, type: kind === 2 ? 'display' : kind === 1 ? 'bar' : 'empty' }))
      .filter(c => c.type !== 'empty');
  }

  /** Subtle note while the preview shows unsaved config.json edits. */
  private _renderPreviewHint() {
    if (!this._preview) return '';
    const stale = this._preview === 'stale';
    return html`<span class="ml-auto mr-1 min-w-0 truncate text-[10px] font-mono ${stale ? 'text-status-orange' : 'text-gray-200'}"
      data-testid="preview-hint" data-state=${this._preview}
      title=${stale ? 'config.json is not valid JSON: showing the last valid preview; items cannot be moved or changed here until it is' : 'Preview of unsaved config.json edits'}
      >${stale ? 'unsaved · invalid JSON' : 'unsaved'}</span>`;
  }

  /**
   * Height of the flip container: the front as it is drawn (per module with
   * a known layout; else the TFT and the rows of configured keys of the page
   * that has most), and at least what the info side needs.
   */
  private _faceHeight(): number {
    const slots = this._slots();
    let h: number;
    if (slots.length === 0) {
      const mostKeys = Math.max(0, ...this._drawnPages().map(p => p.buttons.length));
      h = TFT_H + Math.ceil(mostKeys / 2) * KEY_ROW_H;
    } else {
      h = slots.reduce((sum, c) => sum + (c.type === 'display' ? TFT_H : KEY_ROW_H), 0);
    }
    return Math.max(MIN_FACE_H, Math.ceil(h));
  }

  render() {
    const pages = this._drawnPages();
    const currentPageConfig = pages.find(p => p.number === this.currentPage);

    return html`
      <div class="flex flex-col items-center" style=${this._zoom === 1 ? '' : `zoom: ${this._zoom};`} data-zoom=${this._zoom}>
        <!-- Page stepper, preview note and the info toggle. -->
        <div class="relative flex items-center justify-between gap-1 w-[358px] mb-4">
          ${this._renderStepper(pages)}
          ${this._flipped ? '' : this._renderPreviewHint()}
          <button
            class="shrink-0 p-2 rounded-lg hover:bg-white/10 cursor-pointer transition-colors ${this._flipped ? 'text-white bg-white/10' : 'text-gray-200 hover:text-white'}"
            title=${this._flipped ? 'Back to the device' : 'About this virtual device'}
            aria-label="Virtual device info" aria-pressed=${this._flipped ? 'true' : 'false'}
            @click=${() => this._toggleFlip()}
            data-testid="gear-icon"
          >
            ${InfoIcon('w-5 h-5')}
          </button>
          <!-- Why something asked for here was not done: shown for a moment, in the gap under the row (no layout shift). -->
          ${this._notice && !this._flipped ? html`<div class="absolute left-0 right-0 top-full h-4 leading-4 text-[10px] text-status-orange truncate"
            role="status" data-testid="device-notice" title=${this._notice}>${this._notice}</div>` : ''}
        </div>

        <!-- Flip container -->
        <div class="device-flip-container w-[358px] ${this._running ? 'fb-device-running' : ''} ${this._running && this._emu.mqtt === 'live-publish' ? 'fb-device-live' : ''}" data-running=${this._running ? 'true' : 'false'} style="height: ${this._faceHeight()}px;">
          <div class="device-flip-inner ${this._flipped ? 'flipped' : ''} relative h-full">
            ${this._emu.status === 'starting' && !this._flipped ? html`
              <div class="absolute inset-0 z-10 flex items-center justify-center bg-black/40 pointer-events-none" role="status" data-testid="emu-loading">
                <span class="px-3 py-1.5 rounded-lg bg-bg-primary border border-purple-80/50 text-xs text-text-default">Loading the virtual device…</span>
              </div>` : ''}
            ${this._face.state === 'loading' && this._emu.status !== 'starting' && !this._flipped ? html`
              <div class="absolute right-1 top-1 z-10 px-2 py-1 rounded-md bg-bg-primary/90 border border-white/10 text-[10px] text-gray-100 pointer-events-none"
                role="status" data-testid="face-loading" title="Until then the display is drawn by the page: the same layout, approximate">Loading the display engine…</div>` : ''}
            <!-- Front face -->
            <div class="device-flip-front w-full font-fira text-[11.2px]" @click=${this._onFrontClick}
              @contextmenu=${this._onContextMenu} @item-menu=${this._onContextMenu} @keydown=${this._onFrontKeyDown}
              @item-move=${this._onItemMove} @edit-blocked=${() => this._say(this._blockedHint)}
              @key-press=${this._onKeyPress} @key-release=${this._onKeyRelease}
              @main-key-press=${(e: CustomEvent<{ side: 0 | 1 }>) => this._onMainKey(e, true)}
              @main-key-release=${(e: CustomEvent<{ side: 0 | 1 }>) => this._onMainKey(e, false)}>
              ${this._renderFront(currentPageConfig)}
            </div>
            <!-- Back face -->
            <div class="device-flip-back">
              ${this._renderInfoCard()}
            </div>
          </div>
        </div>
        ${this._renderFaceNote()}
      </div>
    `;
  }

  /**
   * Under the face, only when there is something to know about how it is
   * drawn: the firmware could not draw it (the page's approximation shows),
   * or it draws without font files.
   */
  private _renderFaceNote() {
    const failed = this._face.state === 'failed';
    const text = failed ? `Approximate preview, drawn by the page: ${this._face.note}` : this._face.hint;
    if (!text || this._flipped) return '';
    return html`<p class="w-[358px] mt-2 text-[10px] leading-snug truncate ${failed ? 'text-status-orange' : 'text-gray-200'}"
      data-testid="face-note" data-state=${failed ? 'failed' : 'hint'} title=${text}>${text}</p>`;
  }
}
