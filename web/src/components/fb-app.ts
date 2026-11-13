import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { emit, on } from '../js/app-state.js';
import { getCode, getEditorView, openDocument } from '../js/editor.js';
import { compileForDevice } from '../js/compiler.js';
import { validateCode, formatValidationErrors } from '../js/validator.js';
import { workspace } from '../js/workspace.js';
import { MAIN_BYTECODE_NAME, MAIN_SCRIPT_KEY } from '../js/fs-rules.js';
import { signedFetch } from '../js/auth.js';
import { askConfirm } from './fb-dialog.js';

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
    // Load the device's files and open scripts/main.js once the editor exists.
    void workspace.start({
      editor: {
        isReady: () => getEditorView() !== null,
        getCode,
        open: (content, kind, opts) => openDocument(content, kind, opts),
      },
      // POST /api/reboot is protected: sign it (SigV4, src/js/auth.ts).
      fetchImpl: signedFetch,
      // Questions (delete, discard, add to config.json) in the IDE's own dialog.
      confirm: (message) => askConfirm(message),
    });
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    window.removeEventListener('keydown', this._handleGlobalKeydown);
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  private _handleGlobalKeydown = (e: KeyboardEvent) => {
    const mod = e.metaKey || e.ctrlKey;

    // Ctrl/Cmd+S — save the open file (instead of the browser's save page)
    if (mod && e.key === 's') {
      e.preventDefault();
      emit('save-request');
    }

    // Ctrl/Cmd+Enter — deploy (the toolbar's primary action)
    if (mod && e.key === 'Enter') {
      e.preventDefault();
      emit('deploy-request');
    }

    // Ctrl/Cmd+L — clear console
    if (mod && e.key === 'l') {
      e.preventDefault();
      emit('console-clear');
    }
  };

  /**
   * Deploy = save the source, then (for the boot script only) compile it for
   * the device and upload the bytecode as main.jsc.
   *
   * - scripts/main.js: saved, validated, compiled relocated for the device
   *   (GET /api/bytecode/next_address) and uploaded to /api/bytecode/upload.
   * - any other file: only saved. The firmware runs main.jsc at boot, so
   *   compiling a helper file on its own would never run it.
   * Progress and result are reported through the console pane.
   */
  private async _deploy() {
    if (this._deploying) return;
    this._deploying = true;
    try {
      // 1. Never lose the source: save it first (JSON is validated on save).
      const saved = await workspace.save();
      const { currentKey, mode } = workspace.snapshot();
      if (!saved) {
        emit('deploy-result', { success: false, message: 'Save failed' });
        return;
      }
      if (currentKey !== MAIN_SCRIPT_KEY) {
        const msg = `Saved /${currentKey}. Only /${MAIN_SCRIPT_KEY} is compiled and run at boot; open it to deploy.`;
        emit('console-line', { text: msg, type: 'info' });
        emit('deploy-result', { success: true, message: msg });
        return;
      }

      // 2. Validate before touching the device's bytecode store.
      const code = getCode();
      const validation = validateCode(code);
      if (!validation.valid) {
        emit('console-line', {
          text: 'Deploy aborted (source saved) — validation failed:\n' + formatValidationErrors(validation.errors),
          type: 'error'
        });
        emit('deploy-result', { success: false, message: 'Validation failed' });
        return;
      }
      if (mode === 'local') {
        emit('console-line', { text: 'No device storage reachable — trying the bytecode upload anyway...', type: 'warn' });
      }

      // 3. Compile relocated for the device and upload as main.jsc.
      const base = workspace.deviceBase;
      emit('console-line', { text: 'Compiling for device...', type: 'info' });
      const bytecode = await compileForDevice(code, base);

      emit('console-line', {
        text: 'Compiled ' + bytecode.length + ' bytes — uploading ' + MAIN_BYTECODE_NAME + '...',
        type: 'info'
      });

      const formData = new FormData();
      const blob = new Blob([bytecode as BlobPart], { type: 'application/octet-stream' });
      formData.append('file', blob, MAIN_BYTECODE_NAME);

      // Multipart body: signed with x-amz-content-sha256 UNSIGNED-PAYLOAD.
      const response = await signedFetch(base + '/api/bytecode/upload', {
        method: 'POST',
        body: formData
      });

      if (response.status === 403 && response.headers.get('x-freebutton-auth') === 'required') {
        // signedFetch already opened Settings → Device access.
        throw new Error('Upload refused: the device requires valid access credentials (Settings → Device access)');
      }
      if (!response.ok) {
        throw new Error('Upload failed: ' + response.status + ' ' + response.statusText);
      }

      const result = await response.json();
      if (result.status !== 'success') {
        throw new Error(result.message || 'Upload rejected by device');
      }

      const okMsg = '✓ Uploaded ' + bytecode.length + ' bytes to device as ' + MAIN_BYTECODE_NAME;
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
        <fb-dialog></fb-dialog>
      </div>
    `;
  }
}
