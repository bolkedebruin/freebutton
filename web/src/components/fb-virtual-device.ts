import { LitElement, html, nothing } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import {
  emit, on, sameConfigRef, type ConfigRef, type PageConfig, type ConnectorConfig, type PreviewState,
} from '../js/app-state.js';
import './fb-device-info.js';
import { getConfig, getConnectors, getFontTable, getPreviewState, loadDefaultConfig } from '../js/device-config.js';
import { positionsForConnector } from '../js/config-model.js';
import { setDeviceInfoVisible } from '../js/device-info.js';
import { addItem, listKeyFor, removeItem, setItemProps, type PropValue } from '../js/config-edit.js';
import { applyConfigEdit, ensureConfigOpen, readConfigItem } from '../js/config-session.js';
import { fieldText, fieldValue, itemFields, KIND_LABEL, type Field } from '../js/item-props.js';
import { openPopover, type PopoverHandle } from '../js/popover.js';
import { InfoIcon, PlusIcon, TrashIcon, XIcon } from '../icons/index.js';
import { askConfirm } from './fb-dialog.js';
import type { FbTftDisplay } from './fb-tft-display.js';

// Front-face geometry (px) used to size the flip container per layout.
const TFT_H = 219;
const KEY_ROW_H = 57.1 + 6.72;
const MIN_FACE_H = 420;

// Kept for callers that import it from here.
export { formatFirmware } from './fb-device-info.js';

/** Who polls the device info for the flip side (device-info.ts). */
const INFO_WATCHER = 'flip';

const INVALID_HINT = 'config.json is not valid JSON: correct it to move or change items here';
const NOTICE_MS = 6000;
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
    this._popover?.close();
    if (this._noticeTimer) clearTimeout(this._noticeTimer);
    setDeviceInfoVisible(false, INFO_WATCHER);
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
    const pages = this._pages.length > 0 ? this._pages : getConfig();
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
    if (hit) emit('config-reveal', { ref: hit.ref });
  }

  private _selectPage(page: number) {
    this.currentPage = page;
  }

  private _toggleFlip() {
    this._flipped = !this._flipped;
    this._popover?.close();
    // Don't poll the device for the back face while the front is showing.
    setDeviceInfoVisible(this._flipped, INFO_WATCHER);
  }

  private _say(text: string) {
    if (this._noticeTimer) clearTimeout(this._noticeTimer);
    this._notice = text;
    this._noticeTimer = setTimeout(() => { this._notice = ''; }, NOTICE_MS);
  }

  /** Items can be moved and changed: not while config.json in the editor is not valid JSON. */
  private get _editable(): boolean {
    return this._preview !== 'stale';
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
    if (!this._editable) { this._say(INVALID_HINT); return; }
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
    if (!this._editable) { this._say(INVALID_HINT); return; }
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
        ${this._editable ? 'This entry is no longer in config.json.' : INVALID_HINT}</p>`;
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
    if (!this._editable) { this._say(INVALID_HINT); return; }
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
    await this.updateComplete;
    if (result.ref && this._itemElement(result.ref)) void this._openProperties(result.ref);
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
          .lists=${lists} .images=${images} .selected=${this._selected} .editable=${this._editable}></fb-tft-display>
        <fb-button-grid .currentPage=${this.currentPage} .buttons=${buttons} .selected=${this._selected}></fb-button-grid>
      `;
    }
    return slots.map(c => c.type === 'display'
      ? html`<fb-tft-display data-connector=${c.id} .currentPage=${this.currentPage} .displayItems=${items}
          .deviceName=${this._deviceName} .lists=${lists} .images=${images} .selected=${this._selected}
          .editable=${this._editable}></fb-tft-display>`
      : html`<fb-button-grid data-connector=${c.id} .currentPage=${this.currentPage} .buttons=${buttons}
          .positions=${positionsForConnector(c.id)} .selected=${this._selected}></fb-button-grid>`);
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
        <!-- Page stepper, preview note and the info toggle. -->
        <div class="relative flex items-center justify-between w-[342px] mb-4">
          ${this._renderStepper(pages)}
          ${this._flipped ? '' : this._renderPreviewHint()}
          <button
            class="shrink-0 p-2 rounded-lg hover:bg-white/10 cursor-pointer transition-colors ${this._flipped ? 'text-white bg-white/10' : 'text-gray-200 hover:text-white'}"
            title=${this._flipped ? 'Back to the device' : 'Device info'}
            aria-label="Device info" aria-pressed=${this._flipped ? 'true' : 'false'}
            @click=${() => this._toggleFlip()}
            data-testid="gear-icon"
          >
            ${InfoIcon('w-5 h-5')}
          </button>
          <!-- Why a move or a change was not made; in the gap under the row (no layout shift). -->
          <div class="absolute left-0 right-0 top-full h-4 flex items-center text-[10px] leading-none text-status-orange truncate"
            role="status" data-testid="device-notice" title=${this._notice}>${this._flipped ? '' : this._notice}</div>
        </div>

        <!-- Flip container -->
        <div class="device-flip-container w-[342px]" style="height: ${this._faceHeight()}px;">
          <div class="device-flip-inner ${this._flipped ? 'flipped' : ''} relative h-full">
            <!-- Front face -->
            <div class="device-flip-front w-full font-fira text-[11.2px]" @click=${this._onFrontClick}
              @contextmenu=${this._onContextMenu} @item-menu=${this._onContextMenu} @keydown=${this._onFrontKeyDown}
              @item-move=${this._onItemMove} @edit-blocked=${() => this._say(INVALID_HINT)}>
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
