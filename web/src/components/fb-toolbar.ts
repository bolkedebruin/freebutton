import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { UploadIcon, DownloadIcon, TrashIcon, SaveIcon, MoreIcon, RefreshIcon } from '../icons/index.js';
import { getCode, clearEditor as clearEditorContent } from '../js/editor.js';
import { compile } from '../js/compiler.js';
import { validateCode, formatValidationErrors } from '../js/validator.js';
import { emit, on, type WorkspaceSnapshot } from '../js/app-state.js';
import { workspace } from '../js/workspace.js';
import { MAIN_BYTECODE_NAME, MAIN_SCRIPT_KEY } from '../js/fs-rules.js';
import { askConfirm } from './fb-dialog.js';

const MOD = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.platform ?? '') ? '⌘' : 'Ctrl+';

/**
 * The two things you do with the open file, by name:
 *   Save    store the file (device, or the browser without one).
 *   Deploy  save, compile scripts/main.js and upload it as main.jsc; the
 *           device runs it. Only for the boot script.
 * There is no Validate or Compile step: the editor checks the file as you
 * type (status bar) and Deploy refuses a script with errors. Seldom-used
 * actions (download the bytecode, clear the file) are in the "more" menu.
 * Save, Deploy and the menu act on the open file, so they are shown only
 * while the editor is (not in the Fonts, Image, Device or Docs view).
 * With the Images panel or an image open there is a third one:
 *   Push images  make the rasters the device draws from the SVGs that
 *                config.json uses, and upload them (also done automatically).
 * Centred in the space that is left: the device connection.
 */
@customElement('fb-toolbar')
export class FbToolbar extends LitElement {
  createRenderRoot() { return this; }

  @state() private _connected = false;
  @state() private _url = '';
  @state() private _ws: WorkspaceSnapshot = workspace.snapshot();
  @state() private _deploying = false;
  @state() private _result: { ok: boolean; text: string } | null = null;
  @state() private _menuOpen = false;
  /** The Images panel or an image is shown: offer "Push images". */
  @state() private _images = false;
  /** The editor is the main view: the file actions apply. */
  @state() private _editing = true;
  private _unsubs: Array<() => void> = [];
  private _resultTimer?: ReturnType<typeof setTimeout>;

  connectedCallback() {
    super.connectedCallback();
    this._ws = workspace.snapshot();
    this._unsubs.push(
      on('connection-change', (e) => {
        this._connected = e.detail.connected;
        // Reflect where we actually connected (e.g. the auto-connect target).
        if (e.detail.connected && e.detail.url) {
          this._url = e.detail.url;
        }
      }),
      on('workspace-change', (e) => { this._ws = e.detail; }),
      on('layout-change', (e) => {
        this._editing = e.detail.view === 'editor';
        this._images = e.detail.panel === 'images' || e.detail.view === 'image';
      }),
      on('deploy-request', () => { this._deploying = true; this._result = null; }),
      on('deploy-result', (e) => {
        this._deploying = false;
        this._showResult(e.detail.success, e.detail.success
          ? (e.detail.size !== undefined ? `Deployed · ${e.detail.size} bytes${e.detail.note ? ' · ' + e.detail.note : ''}` : 'Saved')
          : e.detail.message);
      }),
    );
    window.addEventListener('click', this._closeMenu);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
    window.removeEventListener('click', this._closeMenu);
    if (this._resultTimer) clearTimeout(this._resultTimer);
  }

  private _closeMenu = () => { this._menuOpen = false; };

  /** Result next to the buttons; a success fades, a failure stays until the next action. */
  private _showResult(ok: boolean, text: string) {
    if (this._resultTimer) clearTimeout(this._resultTimer);
    this._result = { ok, text };
    if (ok) this._resultTimer = setTimeout(() => { this._result = null; }, 6000);
  }

  private get _isMain(): boolean {
    return this._ws.currentKey === MAIN_SCRIPT_KEY;
  }

  private _deploy() {
    if (this._deploying || !this._isMain) return;
    // fb-app saves the source, then compiles scripts/main.js for the device
    // and uploads main.jsc (same origin, or the device the REPL connected to).
    emit('deploy-request');
  }

  private async _pushImages() {
    this._result = null;
    const r = await workspace.syncIcons();
    if (!r) { this._showResult(false, 'Images not pushed (see console)'); return; }
    if (!r.ok) { this._showResult(false, r.noBucket ? 'This firmware has no image storage' : `${r.failed.length} image raster(s) failed (see console)`); return; }
    this._showResult(true, r.uploaded.length || r.removed.length
      ? `Images pushed · ${r.uploaded.length} uploaded, ${r.removed.length} removed`
      : 'Images on the device are up to date');
  }

  /** Compile the open script (position-independent) and save the bytecode to disk. */
  private async _downloadBytecode() {
    this._menuOpen = false;
    const code = getCode();
    if (!code.trim()) { this._showResult(false, 'Nothing to compile'); return; }
    const validation = validateCode(code);
    if (!validation.valid) {
      emit('console-line', { text: 'Not compiled — the script has errors:\n' + formatValidationErrors(validation.errors), type: 'error' });
      this._showResult(false, 'The script has errors');
      return;
    }
    try {
      const bytecode = await compile(code, { targetAddr: 0, use32Bit: true });
      const url = URL.createObjectURL(new Blob([bytecode as BlobPart], { type: 'application/octet-stream' }));
      const a = document.createElement('a');
      a.href = url;
      a.download = MAIN_BYTECODE_NAME;
      a.click();
      URL.revokeObjectURL(url);
      this._showResult(true, `Downloaded ${MAIN_BYTECODE_NAME} · ${bytecode.length} bytes`);
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      emit('console-line', { text: 'Compile failed: ' + message, type: 'error' });
      this._showResult(false, 'Compile failed (see console)');
    }
  }

  private async _clearFile() {
    this._menuOpen = false;
    if (!getCode()) return;
    const ok = await askConfirm(`Clear the text of /${this._ws.currentKey}?\nThe stored file changes only when you save.`, 'Clear');
    if (ok) clearEditorContent();
  }

  private _connect() {
    if (this._connected) { emit('disconnect-request'); return; }
    const url = this._url.trim();
    if (url) emit('connection-request', { url });
  }

  private _renderActions() {
    const ws = this._ws;
    const isJs = ws.currentKey.endsWith('.js');
    const deployTitle = this._isMain
      ? `Deploy (${MOD}Enter): save, compile and upload as ${MAIN_BYTECODE_NAME}; the device runs it`
      : `Only /${MAIN_SCRIPT_KEY} is compiled and run by the device. Save stores this file.`;
    const plain = 'bg-white/5 hover:bg-white/10 border border-white/10 text-text-default';
    return html`
      <div class="flex items-center gap-2 min-h-10">
        ${this._editing ? html`
        <button class="${plain} rounded-full h-10 pl-3 pr-4 text-sm flex items-center gap-2 transition-colors duration-200"
          title="Save /${ws.currentKey} (${MOD}S)" data-testid="toolbar-save"
          @click=${() => emit('save-request')}>
          <span class="w-5 h-5 flex items-center justify-center [&>svg]:w-5 [&>svg]:h-5">${SaveIcon()}</span>
          Save${ws.dirty || ws.isNew ? html`<span class="w-1.5 h-1.5 rounded-full bg-status-orange" title="Unsaved changes"></span>` : ''}
        </button>
        <button class="bg-purple-90 hover:bg-purple-active text-white rounded-full h-10 pl-3 pr-4 text-sm font-medium flex items-center gap-2 transition-colors duration-200 disabled:opacity-40 disabled:hover:bg-purple-90 disabled:cursor-not-allowed"
          title=${deployTitle} data-testid="toolbar-deploy"
          ?disabled=${!this._isMain || this._deploying}
          @click=${() => this._deploy()}>
          <span class="w-5 h-5 flex items-center justify-center [&>svg]:w-5 [&>svg]:h-5 ${this._deploying ? 'animate-pulse' : ''}">${UploadIcon()}</span>
          ${this._deploying ? 'Deploying…' : 'Deploy'}
        </button>
        ` : ''}
        ${this._images ? html`
          <button class="${plain} rounded-full h-10 pl-3 pr-4 text-sm flex items-center gap-2 transition-colors duration-200 disabled:opacity-40 disabled:cursor-not-allowed"
            title=${ws.mode === 'device'
              ? 'Push images: make the rasters the device draws from the SVGs config.json uses, and upload them'
              : 'Connect to a device to push images'}
            data-testid="toolbar-push-images"
            ?disabled=${ws.mode !== 'device' || ws.iconSyncing}
            @click=${() => this._pushImages()}>
            <span class="w-5 h-5 flex items-center justify-center [&>svg]:w-5 [&>svg]:h-5 ${ws.iconSyncing ? 'animate-spin' : ''}">${RefreshIcon()}</span>
            ${ws.iconSyncing ? 'Pushing…' : 'Push images'}
          </button>
        ` : ''}
        ${this._editing ? html`
        <div class="relative">
          <button class="${plain} rounded-full w-10 h-10 flex items-center justify-center transition-colors duration-200"
            title="More actions" aria-label="More actions" aria-haspopup="menu" aria-expanded=${this._menuOpen ? 'true' : 'false'}
            data-testid="toolbar-more"
            @click=${(e: Event) => { e.stopPropagation(); this._menuOpen = !this._menuOpen; }}>
            <span class="w-5 h-5 flex items-center justify-center [&>svg]:w-5 [&>svg]:h-5">${MoreIcon()}</span>
          </button>
          ${this._menuOpen ? html`
            <div role="menu" class="absolute left-0 top-12 z-30 w-64 rounded-xl border border-border-default bg-bg-primary shadow-2xl p-1 flex flex-col"
              data-testid="toolbar-menu" @click=${(e: Event) => e.stopPropagation()}>
              <button role="menuitem" class="flex items-center gap-2 px-3 py-2 rounded-lg text-xs text-text-default text-left hover:bg-white/10 disabled:opacity-40 disabled:hover:bg-transparent"
                ?disabled=${!isJs} data-testid="menu-download"
                title="Compile the open script and save the bytecode on this computer (nothing is sent to the device)"
                @click=${() => this._downloadBytecode()}>
                <span class="w-4 h-4 flex items-center justify-center [&>svg]:w-4 [&>svg]:h-4">${DownloadIcon()}</span>
                Download bytecode (.jsc)
              </button>
              <button role="menuitem" class="flex items-center gap-2 px-3 py-2 rounded-lg text-xs text-status-red text-left hover:bg-white/10"
                data-testid="menu-clear" @click=${() => this._clearFile()}>
                <span class="w-4 h-4 flex items-center justify-center [&>svg]:w-4 [&>svg]:h-4">${TrashIcon()}</span>
                Clear file text…
              </button>
            </div>
          ` : ''}
        </div>
        ` : ''}
        ${this._editing && !this._isMain ? html`
          <span class="text-xs text-gray-200 ml-1 max-w-[220px] leading-tight" data-testid="deploy-hint">Deploy is for /${MAIN_SCRIPT_KEY}</span>
        ` : ''}
        ${this._result ? html`
          <span class="text-xs ml-1 max-w-[260px] truncate ${this._result.ok ? 'text-status-green' : 'text-status-red'}"
            role="status" title=${this._result.text} data-testid="toolbar-result">${this._result.text}</span>
        ` : ''}
      </div>
    `;
  }

  render() {
    const dotColor = this._connected ? 'bg-status-green' : 'bg-status-red';
    return html`
      <div class="flex items-center gap-4 px-8 py-5 bg-glass-dark backdrop-blur-md border-b border-border-default">
        ${this._renderActions()}
        <div class="flex-1"></div>
        <label class="flex items-center rounded-xl h-10 bg-purple-300/30 border border-white/10 w-[340px] max-w-[40%] focus-within:border-purple-80 transition-all duration-200">
          <span class="shrink-0 w-2.5 h-2.5 rounded-full ${dotColor} ml-3" aria-hidden="true"></span>
          <span class="shrink-0 ml-2 text-xs text-gray-200" data-testid="connection-state">${this._connected ? 'Connected' : 'Device'}</span>
          <input
            type="text"
            placeholder="ws://device.local"
            aria-label="Device address"
            .value=${this._url}
            ?readonly=${this._connected}
            @input=${(e: Event) => { this._url = (e.target as HTMLInputElement).value; }}
            @keydown=${(e: KeyboardEvent) => { if (e.key === 'Enter' && !this._connected) this._connect(); }}
            class="flex-1 min-w-0 h-full px-2 bg-transparent text-sm text-text-default placeholder:text-sm placeholder:text-gray-200 outline-none"
          />
          <button class="shrink-0 mr-1.5 px-2.5 h-7 rounded-lg text-xs text-gray-100 hover:text-text-default bg-white/5 hover:bg-white/10 transition-colors duration-200"
            data-testid="connect-button"
            @click=${(e: Event) => { e.preventDefault(); this._connect(); }}>${this._connected ? 'Disconnect' : 'Connect'}</button>
        </label>
        <!-- A spacer on both sides: the connection field sits in the middle of the free space. -->
        <div class="flex-1"></div>
      </div>
    `;
  }
}
