import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { UploadIcon, DownloadIcon, TrashIcon, SaveIcon, MoreIcon, RefreshIcon, PlayIcon } from '../icons/index.js';
import { getCode, clearEditor as clearEditorContent } from '../js/editor.js';
import { compile } from '../js/compiler.js';
import { validateCode, formatValidationErrors } from '../js/validator.js';
import { emit, on, type EmulatorSnapshot, type WorkspaceSnapshot } from '../js/app-state.js';
import { emulatorSession } from '../js/emulator-session.js';
import { workspace } from '../js/workspace.js';
import { MAIN_BYTECODE_NAME, MAIN_SCRIPT_KEY } from '../js/fs-rules.js';
import { askConfirm } from './fb-dialog.js';
import { openPopover, type PopoverHandle } from '../js/popover.js';

const MAC = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.platform ?? '');
const MOD = MAC ? '⌘' : 'Ctrl+';
const SHIFT = MAC ? '⇧' : 'Shift+';
/** Run in the virtual device / stop it (fb-app handles the keys). */
export const RUN_SHORTCUT = `${MOD}${SHIFT}Enter`;
export const STOP_SHORTCUT = `${MOD}${SHIFT}.`;

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
 * After a divider, in the virtual device's colour:
 *   Run   run scripts/main.js with config.json in the virtual device in
 *         this page (Run again and Stop while it runs). Nothing is sent to
 *         the device: Save and Deploy are for the device, Run is not.
 * Centred in the space that is left: the device connection. In a narrow
 * toolbar the labels of Save, Run again and Stop go and their icons stay
 * (the toolbar's own width decides, not the window's: the pane beside it
 * can be resized).
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
  /** The emulator of the virtual device. */
  @state() private _emu: EmulatorSnapshot = emulatorSession.snapshot();
  private _unsubs: Array<() => void> = [];
  private _resultTimer?: ReturnType<typeof setTimeout>;
  private _menu: PopoverHandle | null = null;

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
      on('workspace-change', (e) => { this._ws = e.detail; this._menu?.update(); }),
      on('layout-change', (e) => {
        this._editing = e.detail.view === 'editor';
        if (!this._editing) this._menu?.close();
        this._images = e.detail.panel === 'images' || e.detail.view === 'image';
      }),
      on('emulator-change', (e) => { this._emu = e.detail; }),
      on('deploy-request', () => { this._deploying = true; this._result = null; }),
      on('deploy-result', (e) => {
        this._deploying = false;
        this._showResult(e.detail.success, e.detail.success
          ? (e.detail.size !== undefined ? `Deployed · ${e.detail.size} bytes${e.detail.note ? ' · ' + e.detail.note : ''}` : 'Saved')
          : e.detail.message);
      }),
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
    this._menu?.close();
    if (this._resultTimer) clearTimeout(this._resultTimer);
  }

  /**
   * The "more" menu, in the popover layer (popover.ts): inside the toolbar it
   * was painted under the file header and the editor below it.
   */
  private _toggleMenu(button: HTMLElement) {
    if (this._menu?.isOpen) { this._menu.close(); return; }
    this._menuOpen = true;
    this._menu = openPopover({
      anchor: button,
      placement: 'bottom-start',
      role: 'menu',
      label: 'More actions',
      testid: 'toolbar-menu',
      className: 'w-64 p-1 flex flex-col',
      content: () => this._renderMenu(),
      onClose: () => { this._menuOpen = false; this._menu = null; },
    });
  }

  private _renderMenu() {
    const isJs = this._ws.currentKey.endsWith('.js');
    return html`
      <button role="menuitem" class="flex items-center gap-2 px-3 py-2 rounded-lg text-xs text-text-default text-left hover:bg-white/10 focus-visible:bg-white/10 outline-none disabled:opacity-40 disabled:hover:bg-transparent"
        ?disabled=${!isJs} data-testid="menu-download"
        title="Compile the open script and save the bytecode on this computer (nothing is sent to the device)"
        @click=${() => this._downloadBytecode()}>
        <span class="w-4 h-4 flex items-center justify-center [&>svg]:w-4 [&>svg]:h-4">${DownloadIcon()}</span>
        Download bytecode (.jsc)
      </button>
      <button role="menuitem" class="flex items-center gap-2 px-3 py-2 rounded-lg text-xs text-status-red text-left hover:bg-white/10 focus-visible:bg-white/10 outline-none"
        data-testid="menu-clear" @click=${() => this._clearFile()}>
        <span class="w-4 h-4 flex items-center justify-center [&>svg]:w-4 [&>svg]:h-4">${TrashIcon()}</span>
        Clear file text…
      </button>
    `;
  }

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
    this._menu?.close();
    const code = getCode();
    if (!code.trim()) { this._showResult(false, 'Nothing to compile'); return; }
    const validation = validateCode(code);
    if (!validation.valid) {
      emit('console-line', { to: 'device', text: 'Not compiled — the script has errors:\n' + formatValidationErrors(validation.errors), type: 'error' });
      this._showResult(false, 'The script has errors');
      return;
    }
    try {
      const bytecode = await compile(code, { use32Bit: true });
      const url = URL.createObjectURL(new Blob([bytecode as BlobPart], { type: 'application/octet-stream' }));
      const a = document.createElement('a');
      a.href = url;
      a.download = MAIN_BYTECODE_NAME;
      a.click();
      URL.revokeObjectURL(url);
      this._showResult(true, `Downloaded ${MAIN_BYTECODE_NAME} · ${bytecode.length} bytes`);
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      emit('console-line', { to: 'device', text: 'Compile failed: ' + message, type: 'error' });
      this._showResult(false, 'Compile failed (see console)');
    }
  }

  private async _clearFile() {
    this._menu?.close();
    if (!getCode()) return;
    const ok = await askConfirm(`Clear the text of /${this._ws.currentKey}?\nThe stored file changes only when you save.`, 'Clear');
    if (ok) clearEditorContent();
  }

  private _connect() {
    if (this._connected) { emit('disconnect-request'); return; }
    const url = this._url.trim();
    if (url) emit('connection-request', { url });
  }

  /** There is a script to run: scripts/main.js is the open file or a stored one. */
  private get _hasMain(): boolean {
    return this._ws.currentKey === MAIN_SCRIPT_KEY || this._ws.files.some(f => f.key === MAIN_SCRIPT_KEY);
  }

  /** Run / Run again / Stop for the virtual device, set apart from Save and Deploy. */
  private _renderVirtual() {
    const status = this._emu.status;
    const accent = 'border border-purple-80/60 bg-purple-80/10 hover:bg-purple-80/20 text-purple-80';
    const button = 'rounded-full h-10 text-sm flex items-center justify-center gap-2 transition-colors duration-200 disabled:opacity-40 disabled:cursor-not-allowed';
    const icon = 'w-5 h-5 shrink-0 flex items-center justify-center [&>svg]:w-5 [&>svg]:h-5';
    const active = status === 'running' || status === 'starting';
    return html`
      <div class="flex items-center gap-2 pl-3 border-l border-white/15" role="group" aria-label="Virtual device" data-testid="toolbar-virtual">
        ${active ? html`
          <button class="${button} ${accent} min-w-10 px-2.5 @[860px]:pl-3 @[860px]:pr-4" data-testid="toolbar-run-again"
            title=${this._emu.stale
              ? `${this._emu.stale === 'config' ? 'config.json' : 'The script'} changed since Run: Run again (${RUN_SHORTCUT}) to use it`
              : `Run again in the virtual device (${RUN_SHORTCUT}): start the script and config.json as they are now`}
            aria-label="Run again in the virtual device" ?disabled=${status === 'starting'}
            @click=${() => { void emulatorSession.run(); }}>
            <span class="${icon} ${status === 'starting' ? 'animate-spin' : ''}">${RefreshIcon()}</span>
            <span class="hidden @[860px]:inline whitespace-nowrap">${status === 'starting' ? 'Starting…' : 'Run again'}</span>
            ${status === 'running' && this._emu.stale ? html`<span class="w-1.5 h-1.5 rounded-full bg-status-orange" data-testid="toolbar-run-stale" role="img"
              aria-label=${this._emu.stale === 'config' ? 'config.json changed since Run' : 'the script changed since Run'}></span>` : ''}
          </button>
          <button class="${button} min-w-10 px-2.5 @[640px]:pl-3 @[640px]:pr-4 border border-status-red/50 bg-status-red/10 hover:bg-status-red/20 text-status-red" data-testid="toolbar-stop"
            title="Stop the virtual device (${STOP_SHORTCUT})" aria-label="Stop the virtual device"
            @click=${() => emulatorSession.stop()}>
            <span class="${icon}"><span class="w-3 h-3 rounded-[2px] bg-current"></span></span>
            <span class="hidden @[640px]:inline">Stop</span>
          </button>
        ` : html`
          <button class="${button} ${accent} pl-3 pr-4" data-testid="toolbar-run" ?disabled=${!this._hasMain}
            title=${!this._hasMain
              ? `There is no /${MAIN_SCRIPT_KEY} to run in the virtual device`
              : status === 'error'
                ? `Run in the virtual device (${RUN_SHORTCUT}). It stopped: ${this._emu.error}`
                : `Run /${MAIN_SCRIPT_KEY} in the virtual device (${RUN_SHORTCUT}), with config.json as it is in the editor. Nothing is sent to the device.`}
            @click=${() => { void emulatorSession.run(); }}>
            <span class="${icon}">${PlayIcon()}</span>
            Run${status === 'error' ? html`<span class="w-1.5 h-1.5 rounded-full bg-status-red" data-testid="toolbar-run-error" role="img" aria-label="stopped with an error"></span>` : ''}
          </button>
        `}
      </div>
    `;
  }

  /**
   * What the last action came to, next to the buttons. It takes the room
   * that is left and is cut short before it pushes anything; in a narrow
   * toolbar it is a mark only (the text is its tooltip, and the Device
   * console has the full lines).
   */
  private _renderResult() {
    const r = this._result;
    if (!r) return '';
    return html`
      <span class="text-xs -ml-2 shrink-0 @[640px]:shrink @[640px]:min-w-0 max-w-[260px] truncate ${r.ok ? 'text-status-green' : 'text-status-red'}"
        role="status" title=${r.text} data-testid="toolbar-result"><span class="@[640px]:hidden" aria-hidden="true">${r.ok ? '✓' : '✕'}</span><span
          class="sr-only @[640px]:not-sr-only">${r.text}</span></span>
    `;
  }

  private _renderActions() {
    const ws = this._ws;
    const deployTitle = this._isMain
      ? `Deploy (${MOD}Enter): save, compile and upload as ${MAIN_BYTECODE_NAME}; the device runs it`
      : `Only /${MAIN_SCRIPT_KEY} is compiled and run by the device. Save stores this file.`;
    const plain = 'bg-white/5 hover:bg-white/10 border border-white/10 text-text-default';
    return html`
      <div class="flex items-center gap-2 min-h-10 shrink-0">
        ${this._editing ? html`
        <button class="${plain} rounded-full h-10 min-w-10 px-2.5 @[640px]:pl-3 @[640px]:pr-4 text-sm flex items-center justify-center gap-2 transition-colors duration-200"
          title="Save /${ws.currentKey} (${MOD}S)" aria-label="Save" data-testid="toolbar-save"
          @click=${() => emit('save-request')}>
          <span class="w-5 h-5 shrink-0 flex items-center justify-center [&>svg]:w-5 [&>svg]:h-5">${SaveIcon()}</span>
          <span class="hidden @[640px]:inline">Save</span>${ws.dirty || ws.isNew ? html`<span class="w-1.5 h-1.5 rounded-full bg-purple-80" title="Unsaved changes" data-testid="dirty-save"></span>` : ''}
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
            @click=${(e: Event) => this._toggleMenu(e.currentTarget as HTMLElement)}>
            <span class="w-5 h-5 flex items-center justify-center [&>svg]:w-5 [&>svg]:h-5">${MoreIcon()}</span>
          </button>
        </div>
        ` : ''}
        ${this._renderVirtual()}
        ${this._editing && !this._isMain ? html`
          <span class="hidden @[860px]:inline text-xs text-gray-200 ml-1 max-w-[220px] leading-tight" data-testid="deploy-hint">Deploy is for /${MAIN_SCRIPT_KEY}</span>
        ` : ''}

      </div>
    `;
  }

  render() {
    const dotColor = this._connected ? 'bg-status-green' : 'bg-status-red';
    return html`
      <div class="@container flex items-center gap-4 px-8 py-5 bg-glass-dark backdrop-blur-md border-b border-border-default">
        ${this._renderActions()}
        ${this._renderResult()}
        <div class="flex-1"></div>
        <label class="flex items-center rounded-xl h-10 bg-purple-300/30 border border-white/10 w-[340px] max-w-[40%] min-w-[150px] shrink focus-within:border-purple-80 transition-all duration-200">
          <span class="shrink-0 w-2.5 h-2.5 rounded-full ${dotColor} ml-3" aria-hidden="true"></span>
          <span class="hidden @[640px]:inline shrink-0 ml-2 text-xs text-gray-200" data-testid="connection-state">${this._connected ? 'Connected' : 'Device'}</span>
          <input
            type="text"
            placeholder="ws://device.local"
            aria-label="Device address"
            title=${this._connected ? 'Connected to this device' : 'The device to connect to'}
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
