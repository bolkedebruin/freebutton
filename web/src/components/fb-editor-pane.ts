import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';

import './fb-toolbar.js';
import './fb-code-editor.js';
import './fb-status-bar.js';
import './fb-file-drawer.js';

@customElement('fb-editor-pane')
export class FbEditorPane extends LitElement {
  createRenderRoot() { return this; }

  @state() private _assetsOpen = true;

  connectedCallback() {
    super.connectedCallback();
    window.addEventListener('toggle-assets', this._onToggleAssets);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    window.removeEventListener('toggle-assets', this._onToggleAssets);
  }

  private _onToggleAssets = () => {
    this._assetsOpen = !this._assetsOpen;
  };

  render() {
    return html`
      <div class="flex flex-col h-full">
        <fb-toolbar></fb-toolbar>
        <div class="flex flex-1 min-h-0">
          <!-- Assets sidebar -->
          <div class="shrink-0 border-r border-border-default bg-glass-dark backdrop-blur-md overflow-hidden transition-[width] duration-200 ${this._assetsOpen ? 'w-[200px]' : 'w-0 border-r-0'}">
            <div class="w-[200px] p-3 h-full overflow-y-auto custom-scrollbar">
              <fb-file-drawer></fb-file-drawer>
            </div>
          </div>
          <div class="flex-1 min-w-0 flex flex-col overflow-hidden">
            <fb-code-editor class="flex-1 min-h-0 overflow-hidden"></fb-code-editor>
            <fb-status-bar></fb-status-bar>
          </div>
        </div>
      </div>
    `;
  }
}
