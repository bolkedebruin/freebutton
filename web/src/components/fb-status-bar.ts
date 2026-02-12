import { LitElement, html, svg } from 'lit';
import { customElement } from 'lit/decorators.js';

@customElement('fb-status-bar')
export class FbStatusBar extends LitElement {
  createRenderRoot() { return this; }

  private _checkCircle() {
    return svg`<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14" />
      <polyline points="22 4 12 14.01 9 11.01" />
    </svg>`;
  }

  render() {
    return html`
      <div class="flex items-center justify-between px-4 h-7 bg-bg-default border-t border-border-default text-xs font-mono">
        <span class="text-gray-200">Ln 7, Col 24</span>
        <span class="flex items-center gap-1.5 text-status-green">${this._checkCircle()} Valid</span>
      </div>
    `;
  }
}
