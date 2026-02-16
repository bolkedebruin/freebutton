import { LitElement, html, svg } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on } from '../js/app-state.js';

@customElement('fb-status-bar')
export class FbStatusBar extends LitElement {
  createRenderRoot() { return this; }

  @state() private _line = 1;
  @state() private _col = 1;
  @state() private _valid = true;
  @state() private _errorCount = 0;

  private _unsubs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._unsubs.push(
      on('cursor-position', (e) => {
        this._line = e.detail.line;
        this._col = e.detail.col;
      }),
      on('validation-result', (e) => {
        this._valid = e.detail.valid;
        this._errorCount = e.detail.errorCount;
      })
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  private _checkCircle() {
    return svg`<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14" />
      <polyline points="22 4 12 14.01 9 11.01" />
    </svg>`;
  }

  private _errorIcon() {
    return svg`<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <circle cx="12" cy="12" r="10" />
      <line x1="15" y1="9" x2="9" y2="15" />
      <line x1="9" y1="9" x2="15" y2="15" />
    </svg>`;
  }

  render() {
    const statusClass = this._valid ? 'text-status-green' : 'text-status-red';
    const statusText = this._valid ? 'Valid' : `${this._errorCount} error${this._errorCount !== 1 ? 's' : ''}`;
    const statusIcon = this._valid ? this._checkCircle() : this._errorIcon();

    return html`
      <div class="flex items-center justify-between px-4 h-7 bg-bg-default border-t border-border-default text-xs font-mono">
        <span class="text-gray-200">Ln ${this._line}, Col ${this._col}</span>
        <span class="status-indicator flex items-center gap-1.5 text-balance ${statusClass}">
          ${statusIcon} ${statusText}
        </span>
      </div>
    `;
  }
}
