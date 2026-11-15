import { LitElement, html, svg, type TemplateResult } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { XIcon, PlusIcon, RefreshIcon, TypeIcon } from '../icons/index.js';
import { emit, on, type WorkspaceFile, type WorkspaceSnapshot } from '../js/app-state.js';
import { formatFileSize } from '../js/virtual-fs.js';
import { workspace, LOCAL_MESSAGE } from '../js/workspace.js';
import { uploadFonts, uploadImages } from '../js/uploads.js';
import { canDeleteKey, fileKind, isEditableKey, MAIN_SCRIPT_KEY } from '../js/fs-rules.js';
import { ICON_MANIFEST_KEY } from '../js/icon-sync.js';

/**
 * The sidebar panels for what is stored on the device (or in the in-browser
 * fallback store when no device is reachable). `panel` chooses one:
 *
 *   app     the app's files in bucket `app`: .js / .json / .txt open in the
 *           editor; anything else (a file a tool left behind) is listed
 *           under "Other", cannot be opened and can be deleted.
 *   images  icons/*.svg, uploaded from disk and referenced from config.json
 *           "icons": open in the image view. The device draws rasters of
 *           them; the IDE makes those on every save of config.json and after
 *           an upload or delete ("Push images" in the toolbar does it by hand).
 *   fonts   the device's `fonts` bucket: the list, upload. The Fonts view in
 *           the main pane renders them for comparison.
 *
 * Examples have a panel of their own, and the IDE's bundle (bucket `web`) is
 * not listed.
 */
@customElement('fb-file-drawer')
export class FbFileDrawer extends LitElement {
  createRenderRoot() { return this; }

  @property({ attribute: false }) panel: 'app' | 'images' | 'fonts' = 'app';

  @state() private _ws: WorkspaceSnapshot = workspace.snapshot();
  @state() private _creating = false;
  @state() private _newName = '';
  /** The asset shown in the main pane's image view (highlighted). */
  @state() private _viewKey = '';
  @state() private _fontsOpen = false;

  private _unsubs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._ws = workspace.snapshot();
    this._unsubs.push(
      on('workspace-change', (e) => {
        const wasDevice = this._ws.mode === 'device';
        this._ws = e.detail;
        // The Fonts row shows a count: list the bucket once a device is there.
        if (!wasDevice && e.detail.mode === 'device') void workspace.loadReadOnly(['fonts']);
      }),
      on('layout-change', (e) => {
        this._fontsOpen = e.detail.view === 'fonts';
        if (e.detail.view !== 'image') this._viewKey = '';
      }),
    );
    if (this._ws.mode === 'device') void workspace.loadReadOnly(['fonts']);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  private async _open(file: WorkspaceFile) {
    if (isEditableKey(file.key)) {
      if (await workspace.open(file.key)) emit('main-view', { view: 'editor' });
      return;
    }
    // Not text: the image view shows it (an SVG) or says why it cannot.
    this._viewKey = file.key;
    emit('main-view', { view: 'image', key: file.key });
  }

  private async _delete(e: Event, file: WorkspaceFile) {
    e.stopPropagation();
    if (await workspace.remove(file.key) && file.key === this._viewKey) emit('main-view', { view: this.panel === 'images' ? 'image' : 'editor' });
  }

  private _pickSvg() {
    this.querySelector<HTMLInputElement>('[data-testid="svg-upload-input"]')?.click();
  }

  private async _onSvgPicked(e: Event) {
    const input = e.target as HTMLInputElement;
    const files = [...(input.files ?? [])];
    input.value = '';
    await uploadImages(files);
  }

  private _pickFont() {
    this.querySelector<HTMLInputElement>('[data-testid="font-upload-input"]')?.click();
  }

  private async _onFontPicked(e: Event) {
    const input = e.target as HTMLInputElement;
    const files = [...(input.files ?? [])];
    input.value = '';
    // The Fonts view shows the result.
    emit('main-view', { view: 'fonts' });
    await uploadFonts(files);
  }

  private _startCreate() {
    this._creating = true;
    this._newName = '';
    this.updateComplete.then(() => this.querySelector<HTMLInputElement>('[data-testid="new-file-input"]')?.focus());
  }

  private async _commitCreate() {
    const name = this._newName.trim();
    this._creating = false;
    if (name && await workspace.create(name)) emit('main-view', { view: 'editor' });
  }

  private _onNewKey(e: KeyboardEvent) {
    if (e.key === 'Enter') { e.preventDefault(); void this._commitCreate(); }
    if (e.key === 'Escape') { this._creating = false; }
  }

  private _fileIcon() {
    return svg`<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
      <polyline points="14 2 14 8 20 8" />
    </svg>`;
  }

  private _imageIcon() {
    return svg`<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <rect x="3" y="3" width="18" height="18" rx="2" />
      <circle cx="8.5" cy="8.5" r="1.5" />
      <polyline points="21 15 16 10 5 21" />
    </svg>`;
  }

  private _unknownIcon() {
    return svg`<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
      <line x1="9" y1="13" x2="15" y2="13" />
    </svg>`;
  }

  /** Section title with its count and, where it has one, its "add" button. */
  private _sectionHeader(title: string, count: number | null, action?: TemplateResult) {
    return html`
      <div class="flex items-center justify-between h-5 px-2">
        <span class="text-[10px] text-gray-200 uppercase tracking-wider">${title}${count !== null ? html` · ${count}` : ''}</span>
        ${action ?? ''}
      </div>
    `;
  }

  private _addButton(title: string, testid: string, handler: () => void, disabled = false) {
    return html`
      <button class="w-5 h-5 flex items-center justify-center rounded text-gray-200 hover:text-text-default hover:bg-white/10 transition-colors duration-200 disabled:opacity-40"
        title=${title} aria-label=${title} data-testid=${testid} ?disabled=${disabled} @click=${handler}>
        <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5">${PlusIcon()}</span>
      </button>
    `;
  }

  private _renderFile(file: WorkspaceFile) {
    const ws = this._ws;
    const kind = fileKind(file.key);
    const editable = isEditableKey(file.key);
    const active = editable ? file.key === ws.currentKey && !this._viewKey && !this._fontsOpen : file.key === this._viewKey;
    const meta = [formatFileSize(file.size)];
    if (file.lastModified) meta.push(file.lastModified.toLocaleString());
    const title = editable ? `Open /${file.key}`
      : kind === 'svg' ? `View /${file.key} (an icon source: reference it in config.json "icons")`
      : `/${file.key}: the IDE cannot open this type; it can be deleted`;
    return html`
      <div class="flex items-center rounded-lg transition-colors duration-200 group/item ${active ? 'bg-white/10' : 'hover:bg-white/5'}"
        data-key=${file.key}>
        <button class="flex-1 min-w-0 flex items-center gap-2 px-2 py-1.5 text-left rounded-lg" title=${title}
          data-testid="open-file" @click=${() => this._open(file)}>
          <span class="shrink-0 text-gray-200">${editable ? this._fileIcon() : kind === 'svg' ? this._imageIcon() : this._unknownIcon()}</span>
          <span class="flex-1 min-w-0">
            <span class="block text-xs text-text-default truncate">
              ${file.key}${editable && file.key === ws.currentKey && ws.dirty ? html`<span class="text-status-orange ml-1" title="Unsaved changes">●</span>` : ''}
            </span>
            <span class="block text-[10px] text-gray-200 truncate">${meta.join(' · ')}</span>
          </span>
        </button>
        ${canDeleteKey(file.key) ? html`
          <button class="shrink-0 w-6 h-6 mr-1 flex items-center justify-center rounded text-gray-200 opacity-40 hover:opacity-100 focus-visible:opacity-100 group-hover/item:opacity-100 hover:text-status-red hover:bg-white/10 transition-all duration-200"
            title="Delete /${file.key}" aria-label="Delete /${file.key}" data-testid="delete-file"
            @click=${(e: Event) => this._delete(e, file)}>
            <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5">${XIcon()}</span>
          </button>
        ` : ''}
      </div>
    `;
  }

  /** The open file when it is not in storage yet (template main.js). */
  private _renderUnsavedCurrent() {
    const ws = this._ws;
    if (!ws.isNew || ws.files.some(f => f.key === ws.currentKey)) return '';
    return html`
      <div class="flex items-center gap-2 px-2 py-1.5 rounded-lg bg-white/10" data-key=${ws.currentKey} data-testid="unsaved-file">
        <span class="shrink-0 text-gray-200">${this._fileIcon()}</span>
        <div class="flex-1 min-w-0">
          <div class="text-xs text-text-default truncate">${ws.currentKey}</div>
          <div class="text-[10px] text-status-orange">not saved yet</div>
        </div>
      </div>
    `;
  }

  private _renderFonts() {
    const ws = this._ws;
    if (ws.mode !== 'device') {
      return html`<div class="text-[10px] text-gray-200 px-2" data-testid="fonts-files">Fonts are stored on the device: connect to one to see them.</div>`;
    }
    const fonts = ws.readOnly?.fonts;
    return html`
      <div class="flex flex-col gap-0.5" data-testid="fonts-files">
        ${this._sectionHeader('Fonts', fonts ? fonts.length : null,
          this._addButton('Upload fonts (.ttf / .otf, up to 2 MiB) to the device. You can also drop files here.', 'upload-font', () => this._pickFont(), ws.busy))}
        <input type="file" accept=".ttf,.otf,font/ttf,font/otf" multiple class="hidden"
          data-testid="font-upload-input" @change=${this._onFontPicked} />
        ${(fonts ?? []).map(f => html`
          <button class="flex items-center gap-2 px-2 py-1.5 rounded-lg text-left transition-colors duration-200 hover:bg-white/5"
            title="Show the fonts rendered, to compare them" data-testid="open-fonts"
            @click=${() => emit('main-view', { view: 'fonts' })}>
            <span class="shrink-0 text-gray-200 w-4 h-4 flex items-center justify-center [&>svg]:w-4 [&>svg]:h-4">${TypeIcon()}</span>
            <span class="flex-1 min-w-0">
              <span class="block text-xs text-text-default truncate">${f.key}</span>
              <span class="block text-[10px] text-gray-200 truncate">${formatFileSize(f.size)}</span>
            </span>
          </button>
        `)}
        ${fonts && fonts.length === 0 ? html`<div class="text-[10px] text-gray-200 px-2">No fonts on the device.</div>` : ''}
      </div>
    `;
  }

  /**
   * Bottom line: how full the store behind this panel is. App and Images are
   * files of the device's filesystem; fonts are in its media store. Without
   * the device's numbers (no device, older firmware) the App panel shows what
   * its files add up to.
   */
  private _renderSpace(listedBytes: number) {
    const ws = this._ws;
    const st = ws.mode === 'device' ? ws.storage : undefined;
    let left: string, right: string, title = '';
    if (this.panel === 'fonts' && st?.media) {
      left = `${formatFileSize(st.media.used)} used`;
      right = `${formatFileSize(st.media.free)} free`;
      title = `Media store (fonts and the rasters of images): ${formatFileSize(st.media.total)}`;
    } else if (this.panel !== 'fonts' && st?.fs) {
      left = `${formatFileSize(st.fs.used)} used`;
      right = `${formatFileSize(Math.max(0, st.fs.total - st.fs.used))} free`;
      title = `Device filesystem (your files, the IDE, examples): ${formatFileSize(st.fs.total)}`;
    } else if (this.panel === 'app') {
      left = `${ws.files.length} file${ws.files.length === 1 ? '' : 's'}`;
      right = `${formatFileSize(listedBytes)} used`;
    } else {
      return '';
    }
    return html`
      <div class="flex items-center justify-between pt-2 border-t border-border-default text-[10px] text-gray-200"
        title=${title} data-testid="storage-space">
        <span>${left}</span>
        <span>${right}</span>
      </div>
    `;
  }

  render() {
    const ws = this._ws;
    // The raster manifest is the IDE's own bookkeeping (icon-sync.ts): not listed.
    const texts = ws.files.filter(f => isEditableKey(f.key) && f.key !== ICON_MANIFEST_KEY);
    const icons = ws.files.filter(f => !isEditableKey(f.key) && fileKind(f.key) === 'svg');
    const others = ws.files.filter(f => !isEditableKey(f.key) && fileKind(f.key) !== 'svg');
    const usedBytes = ws.files.reduce((n, f) => n + f.size, 0);
    const source = ws.mode === 'device' ? 'On the device' : ws.mode === 'local' ? 'Browser (no device)' : 'Connecting…';

    return html`
      <div class="flex flex-col gap-3">
        <!-- Header -->
        <div class="flex items-center justify-between">
          <span class="text-xs font-medium text-gray-100 uppercase tracking-wider" data-testid="panel-title">${{ app: 'App', images: 'Images', fonts: 'Fonts' }[this.panel]}</span>
          <button class="w-5 h-5 flex items-center justify-center rounded text-gray-200 hover:text-text-default hover:bg-white/10 transition-colors duration-200 disabled:opacity-40"
            title="Read the file list again" aria-label="Read the file list again" data-testid="refresh-files"
            ?disabled=${ws.busy}
            @click=${() => workspace.refresh()}>
            <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5 ${ws.busy ? 'animate-spin' : ''}">${RefreshIcon()}</span>
          </button>
        </div>

        <div class="text-[10px] text-gray-200 -mt-2" data-testid="storage-source">${source}</div>

        ${ws.message ? html`
          <div class="text-[10px] leading-snug rounded-md px-2 py-1.5 ${ws.message === LOCAL_MESSAGE ? 'bg-white/5 text-gray-100' : 'bg-status-red/10 text-status-red'}"
            data-testid="workspace-message">${ws.message}</div>
        ` : ''}

        ${this.panel === 'app' ? html`
        <!-- Files -->
        <div class="flex flex-col gap-0.5" data-testid="user-files">
          ${this._sectionHeader('Files', texts.length,
            this._addButton('New file (.js, .json, .txt)', 'new-file', () => this._startCreate()))}
          ${this._creating ? html`
            <input type="text" data-testid="new-file-input" aria-label="New file name"
              class="bg-white/5 border border-white/10 rounded-lg px-2 py-1 text-xs text-white outline-none focus:border-purple-80"
              placeholder="name.js / data.json / notes.txt"
              .value=${this._newName}
              @input=${(e: Event) => { this._newName = (e.target as HTMLInputElement).value; }}
              @keydown=${this._onNewKey}
              @blur=${() => { if (!this._newName.trim()) this._creating = false; }} />
          ` : ''}
          ${this._renderUnsavedCurrent()}
          ${texts.map(f => this._renderFile(f))}
          ${texts.length === 0 && !ws.isNew && ws.mode !== 'probing'
            ? html`<div class="text-[10px] text-gray-200 px-2">No files yet. ${MAIN_SCRIPT_KEY} is created on first save.</div>`
            : ''}
        </div>
        ` : ''}

        ${this.panel === 'images' ? html`
        <!-- Icons -->
        <div class="flex flex-col gap-0.5" data-testid="icon-files">
          ${this._sectionHeader('Images', icons.length,
            this._addButton('Upload SVG images (stored as icons/<name>.svg). You can also drop files here.', 'upload-svg', () => this._pickSvg(), ws.busy))}
          <input type="file" accept=".svg,image/svg+xml" multiple class="hidden"
            data-testid="svg-upload-input" @change=${this._onSvgPicked} />
          ${icons.map(f => this._renderFile(f))}
          ${icons.length === 0 ? html`<div class="text-[10px] text-gray-200 px-2">No images. Upload SVG files, then name them in config.json "icons".</div>` : ''}
          ${ws.mode === 'device' && icons.length > 0 ? html`
            <div class="px-2 pt-1 text-[10px] leading-snug text-gray-200" data-testid="icon-sync-status">
              The device draws rasters of the images config.json uses. The IDE makes them on every save of config.json and after an upload or delete here; "Push images" in the toolbar does it by hand.
            </div>
          ` : ''}
        </div>
        ` : ''}

        ${this.panel === 'fonts' ? this._renderFonts() : ''}

        <!-- Other -->
        ${this.panel === 'app' && others.length > 0 ? html`
          <div class="flex flex-col gap-0.5" data-testid="other-files">
            ${this._sectionHeader('Other', others.length)}
            ${others.map(f => this._renderFile(f))}
          </div>
        ` : ''}

        ${this._renderSpace(usedBytes)}
      </div>
    `;
  }
}
