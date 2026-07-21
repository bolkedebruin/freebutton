import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { emit, on } from '../js/app-state.js';
import { getCode } from '../js/editor.js';
import { compile, compileForDevice } from '../js/compiler.js';
import { validateCode, formatValidationErrors } from '../js/validator.js';

@customElement('fb-app')
export class FbApp extends LitElement {
  createRenderRoot() { return this; }

  @state() private rightPaneWidth = 400;
  private _deploying = false;
  private _unsubs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    window.addEventListener('keydown', this._handleGlobalKeydown);
    this._unsubs.push(on('deploy-request', () => this._deploy()));
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    window.removeEventListener('keydown', this._handleGlobalKeydown);
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  private _handleGlobalKeydown = (e: KeyboardEvent) => {
    const mod = e.metaKey || e.ctrlKey;

    // Ctrl/Cmd+S — prevent browser save
    if (mod && e.key === 's') {
      e.preventDefault();
    }

    // Ctrl/Cmd+Enter — compile
    if (mod && e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      this._compileShortcut();
    }

    // Ctrl/Cmd+Shift+Enter — validate
    if (mod && e.key === 'Enter' && e.shiftKey) {
      e.preventDefault();
      this._validateShortcut();
    }

    // Ctrl/Cmd+L — clear console
    if (mod && e.key === 'l') {
      e.preventDefault();
      emit('console-clear');
    }
  };

  private async _compileShortcut() {
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

  private _validateShortcut() {
    const code = getCode();
    if (!code.trim()) return;

    const result = validateCode(code);
    emit('validation-result', {
      valid: result.valid,
      errors: result.errors.map(e => e.message),
      errorCount: result.errors.length
    });
  }

  /**
   * Compile the current code for the serving device and upload the bytecode.
   * Talks to the same origin that served the IDE (no device URL needed), which
   * is the device itself when the firmware serves this app. Progress and result
   * are reported through the console pane.
   */
  private async _deploy() {
    if (this._deploying) return;

    const code = getCode();
    if (!code.trim()) return;

    // Pre-validate before touching the device
    const validation = validateCode(code);
    if (!validation.valid) {
      emit('console-line', {
        text: 'Deploy aborted — validation failed:\n' + formatValidationErrors(validation.errors),
        type: 'error'
      });
      emit('deploy-result', { success: false, message: 'Validation failed' });
      return;
    }

    this._deploying = true;
    emit('console-line', { text: 'Compiling for device...', type: 'info' });

    try {
      // Same-origin: query the device for its virtual address, compile relocated,
      // then POST the bytecode blob to the upload endpoint.
      const bytecode = await compileForDevice(code);

      emit('console-line', {
        text: 'Compiled ' + bytecode.length + ' bytes — uploading...',
        type: 'info'
      });

      const formData = new FormData();
      const blob = new Blob([bytecode as BlobPart], { type: 'application/octet-stream' });
      formData.append('file', blob, 'script.jsc');

      const response = await fetch('/api/bytecode/upload', {
        method: 'POST',
        body: formData
      });

      if (!response.ok) {
        throw new Error('Upload failed: ' + response.status + ' ' + response.statusText);
      }

      const result = await response.json();
      if (result.status !== 'success') {
        throw new Error(result.message || 'Upload rejected by device');
      }

      const okMsg = '✓ Uploaded ' + bytecode.length + ' bytes to device';
      emit('console-line', { text: okMsg, type: 'result' });
      emit('deploy-result', { success: true, message: okMsg, size: bytecode.length });
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      emit('console-line', { text: 'Deploy failed: ' + message, type: 'error' });
      emit('deploy-result', { success: false, message });
    } finally {
      this._deploying = false;
    }
  }

  private _onHorizontalResize = (e: CustomEvent<{ delta: number }>) => {
    this.rightPaneWidth = Math.min(600, Math.max(380, this.rightPaneWidth - e.detail.delta));
  };

  render() {
    return html`
      <div class="flex h-screen w-screen overflow-hidden">
        <fb-navbar></fb-navbar>
        <div class="flex flex-1 min-w-0">
          <fb-editor-pane class="flex flex-col flex-1 min-w-0"></fb-editor-pane>
          <fb-resize-handle
            class="w-[16px] cursor-col-resize shrink-0"
            data-direction="horizontal"
            @resize=${this._onHorizontalResize}
          ></fb-resize-handle>
          <fb-right-pane
            class="flex flex-col shrink-0"
            style="width: ${this.rightPaneWidth}px; min-width: 380px; max-width: 600px;"
          ></fb-right-pane>
        </div>
        <fb-settings-modal></fb-settings-modal>
      </div>
    `;
  }
}
