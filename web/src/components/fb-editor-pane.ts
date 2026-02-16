import { LitElement, html, svg } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on, emit } from '../js/app-state.js';
import { examples } from '../js/examples.js';

import './fb-toolbar.js';
import './fb-code-editor.js';
import './fb-status-bar.js';
import './fb-file-drawer.js';

type SidebarPanel = 'assets' | 'examples' | null;

@customElement('fb-editor-pane')
export class FbEditorPane extends LitElement {
  createRenderRoot() { return this; }

  @state() private _activePanel: SidebarPanel = 'assets';

  private _unsubs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._unsubs.push(
      on('sidebar-panel', (e) => {
        const panel = e.detail.panel;
        this._activePanel = this._activePanel === panel ? null : panel;
      }),
      on('toggle-assets', () => {
        this._activePanel = this._activePanel === 'assets' ? null : 'assets';
      })
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  private _fileIcon() {
    return svg`<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
      <polyline points="14 2 14 8 20 8" />
      <line x1="16" y1="13" x2="8" y2="13" />
      <line x1="16" y1="17" x2="8" y2="17" />
      <line x1="10" y1="9" x2="8" y2="9" />
    </svg>`;
  }

  private _loadExample(name: string) {
    emit('load-example', { name });
  }

  private _renderExamplesPanel() {
    return html`
      <div class="flex flex-col gap-3">
        <div class="flex items-center justify-between">
          <span class="text-xs font-medium text-gray-100 uppercase tracking-wider">Examples</span>
        </div>
        <div class="flex flex-col gap-0.5">
          ${examples.map(ex => html`
            <div class="flex items-center gap-2 px-2 py-1.5 rounded-lg hover:bg-white/5 transition-colors duration-200 cursor-pointer group/example"
              @click=${() => this._loadExample(ex.id)}>
              <span class="shrink-0 text-gray-200">${this._fileIcon()}</span>
              <div class="flex-1 min-w-0">
                <div class="text-xs text-text-default truncate">${ex.title}</div>
                <div class="text-[10px] text-gray-200">${ex.description}</div>
              </div>
            </div>
          `)}
        </div>
      </div>
    `;
  }

  render() {
    const isOpen = this._activePanel !== null;

    return html`
      <div class="flex flex-col h-full">
        <fb-toolbar></fb-toolbar>
        <div class="flex flex-1 min-h-0">
          <!-- Sidebar -->
          <div class="shrink-0 border-r border-border-default bg-glass-dark backdrop-blur-md overflow-hidden transition-[width] duration-200 ${isOpen ? 'w-[200px]' : 'w-0 border-r-0'}">
            <div class="w-[200px] p-3 h-full overflow-y-auto custom-scrollbar">
              ${this._activePanel === 'assets' ? html`<fb-file-drawer></fb-file-drawer>` : ''}
              ${this._activePanel === 'examples' ? this._renderExamplesPanel() : ''}
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
