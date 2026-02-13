import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { PlayIcon, CheckIcon, UploadIcon, DownloadIcon, TrashIcon, PlugIcon } from '../icons/index.js';
import { getCode, clearEditor as clearEditorContent } from '../js/editor.js';
import { compile } from '../js/compiler.js';
import { validateCode, formatValidationErrors } from '../js/validator.js';
import { emit, on } from '../js/app-state.js';

@customElement('fb-toolbar')
export class FbToolbar extends LitElement {
  createRenderRoot() { return this; }

  @state() private _connected = false;
  private _unsubs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._unsubs.push(
      on('connection-change', (e) => {
        this._connected = e.detail.connected;
      })
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  private _iconBtn(icon: ReturnType<typeof PlayIcon>, label: string, handler: () => void, primary = false) {
    const base = primary
      ? 'bg-purple-90 hover:bg-purple-active text-white'
      : 'bg-white/5 hover:bg-white/10 border border-white/10 text-text-default';
    return html`
      <button class="${base} rounded-full w-10 h-10 transition-colors duration-200 flex items-center justify-center" title="${label}" @click=${handler}>
        <span class="w-6 h-6 flex items-center justify-center [&>svg]:w-6 [&>svg]:h-6">${icon}</span>
      </button>
    `;
  }

  private async _handleCompile() {
    const code = getCode();
    if (!code.trim()) return;

    const validation = validateCode(code);
    if (!validation.valid) {
      emit('compile-result', {
        success: false,
        error: 'Validation failed:\n' + formatValidationErrors(validation.errors)
      });
      return;
    }

    emit('compile-start');
    try {
      const bytecode = await compile(code, { targetAddr: 0, use32Bit: true });
      emit('compile-result', { success: true, bytecode, size: bytecode.length });
    } catch (err) {
      emit('compile-result', {
        success: false,
        error: err instanceof Error ? err.message : String(err)
      });
    }
  }

  private _handleValidate() {
    const code = getCode();
    if (!code.trim()) return;

    const result = validateCode(code);
    emit('validation-result', {
      valid: result.valid,
      errors: result.errors.map(e => e.message),
      errorCount: result.errors.length
    });
  }

  private _handleUpload() {
    const input = this.querySelector<HTMLInputElement>('input');
    const url = input?.value.trim();
    if (url) {
      emit('connection-request', { url });
    }
  }

  private _handleDownload() {
    const code = getCode();
    if (!code.trim()) return;

    // Compile then trigger download
    this._handleCompile().then(() => {
      // Download handled by compile-result listener elsewhere or inline
    });
  }

  private _handleClear() {
    clearEditorContent();
  }

  private _handleConnect() {
    const input = this.querySelector<HTMLInputElement>('input');
    const url = input?.value.trim();
    if (url) {
      emit('connection-request', { url });
    }
  }

  render() {
    const dotColor = this._connected ? 'bg-status-green' : 'bg-status-red';
    return html`
      <div class="flex items-center px-8 py-5 bg-glass-dark backdrop-blur-md border-b border-border-default">
        <div class="flex items-center gap-4">
          ${this._iconBtn(PlayIcon(), 'Compile', () => this._handleCompile(), true)}
          ${this._iconBtn(CheckIcon(), 'Validate', () => this._handleValidate())}
        </div>
        <div class="w-px h-6 bg-border-default mx-4"></div>
        <div class="flex items-center gap-4">
          ${this._iconBtn(UploadIcon(), 'Upload', () => this._handleUpload())}
          ${this._iconBtn(DownloadIcon(), 'Download', () => this._handleDownload())}
        </div>
        <div class="w-px h-6 bg-border-default mx-4"></div>
        ${this._iconBtn(TrashIcon(), 'Clear', () => this._handleClear())}
        <div class="flex-1"></div>
        <div class="flex items-center rounded-xl h-14 bg-purple-300/30 border border-purple-100/10 w-72 focus-within:ring-3 focus-within:ring-purple-100/20 focus-within:ring-offset-0 transition-all duration-200">
          <span class="shrink-0 w-2.5 h-2.5 rounded-full ${dotColor} ml-4"></span>
          <input
            type="text"
            placeholder="ws://device.local"
            class="flex-1 h-full px-3 bg-transparent text-sm text-text-default placeholder:text-sm placeholder:text-gray-200 outline-none"
          />
          <button class="shrink-0 mr-2 w-8 h-8 rounded-lg flex items-center justify-center text-gray-100 hover:text-text-default hover:bg-white/10 transition-colors duration-200" title="Connect" @click=${() => this._handleConnect()}>
            <span class="w-5 h-5 flex items-center justify-center [&>svg]:w-5 [&>svg]:h-5">${PlugIcon()}</span>
          </button>
        </div>
        <div class="flex-1"></div>
      </div>
    `;
  }
}
