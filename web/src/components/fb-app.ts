import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { emit } from '../js/app-state.js';
import { getCode } from '../js/editor.js';
import { compile } from '../js/compiler.js';
import { validateCode, formatValidationErrors } from '../js/validator.js';

@customElement('fb-app')
export class FbApp extends LitElement {
  createRenderRoot() { return this; }

  @state() private rightPaneWidth = 400;

  connectedCallback() {
    super.connectedCallback();
    window.addEventListener('keydown', this._handleGlobalKeydown);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    window.removeEventListener('keydown', this._handleGlobalKeydown);
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
      </div>
    `;
  }
}
