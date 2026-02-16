import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on } from '../js/app-state.js';
import { XIcon } from '../icons/index.js';

@customElement('fb-settings-modal')
export class FbSettingsModal extends LitElement {
  createRenderRoot() { return this; }

  @state() private _open = false;
  @state() private _mcpEnabled = false;
  @state() private _rippleEnabled = true;

  private _unsubs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._rippleEnabled = document.body.style.animation !== 'none';
    this._unsubs.push(
      on('open-settings', () => { this._open = true; })
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  private _close() {
    this._open = false;
  }

  private _onBackdropClick(e: MouseEvent) {
    if (e.target === e.currentTarget) this._close();
  }

  private _toggleMcp() {
    this._mcpEnabled = !this._mcpEnabled;
  }

  private _toggleRipple() {
    this._rippleEnabled = !this._rippleEnabled;
    document.body.style.animation = this._rippleEnabled ? '' : 'none';
  }

  render() {
    if (!this._open) return html``;

    return html`
      <div
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm"
        @click=${this._onBackdropClick}
      >
        <div class="w-[420px] rounded-xl border border-border-default bg-bg-primary shadow-2xl">
          <!-- Header -->
          <div class="flex items-center justify-between px-5 py-4 border-b border-border-default">
            <h2 class="text-sm font-semibold text-text-default">Settings</h2>
            <button
              class="p-1 rounded-md text-gray-200 hover:text-text-default hover:bg-white/5 transition-colors"
              @click=${this._close}
            >${XIcon()}</button>
          </div>

          <!-- Body -->
          <div class="px-5 py-4 flex flex-col gap-4">
            <!-- MCP Server -->
            <label class="flex items-center justify-between cursor-pointer group">
              <div>
                <div class="text-sm text-text-default">MCP Server</div>
                <div class="text-xs text-gray-200 mt-0.5">Enable the Model Context Protocol server</div>
              </div>
              <div
                class="relative w-9 h-5 rounded-full transition-colors ${this._mcpEnabled ? 'bg-purple-90' : 'bg-gray-80'}"
                @click=${this._toggleMcp}
              >
                <div class="absolute top-0.5 left-0.5 w-4 h-4 rounded-full bg-white transition-transform ${this._mcpEnabled ? 'translate-x-4' : ''}"></div>
              </div>
            </label>

            <!-- Ripple Effect -->
            <label class="flex items-center justify-between cursor-pointer group">
              <div>
                <div class="text-sm text-text-default">Background ripple</div>
                <div class="text-xs text-gray-200 mt-0.5">Animated gradient on the background</div>
              </div>
              <div
                class="relative w-9 h-5 rounded-full transition-colors ${this._rippleEnabled ? 'bg-purple-90' : 'bg-gray-80'}"
                @click=${this._toggleRipple}
              >
                <div class="absolute top-0.5 left-0.5 w-4 h-4 rounded-full bg-white transition-transform ${this._rippleEnabled ? 'translate-x-4' : ''}"></div>
              </div>
            </label>
          </div>
        </div>
      </div>
    `;
  }
}
