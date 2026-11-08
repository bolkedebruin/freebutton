import { LitElement, html, svg } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on, emit, type WorkspaceSnapshot } from '../js/app-state.js';
import { examples } from '../js/examples.js';
import { workspace } from '../js/workspace.js';

import './fb-toolbar.js';
import './fb-code-editor.js';
import './fb-status-bar.js';
import './fb-file-drawer.js';

type SidebarPanel = 'assets' | 'examples' | null;

@customElement('fb-editor-pane')
export class FbEditorPane extends LitElement {
  createRenderRoot() { return this; }

  @state() private _activePanel: SidebarPanel = 'assets';
  @state() private _ws: WorkspaceSnapshot = workspace.snapshot();
  // Set after config.json was saved to the device: offer a reboot.
  @state() private _rebootOffer = false;
  @state() private _rebooting = false;

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
      }),
      on('workspace-change', (e) => { this._ws = e.detail; }),
      on('reboot-suggested', () => { this._rebootOffer = true; })
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

  private async _reboot() {
    this._rebooting = true;
    const ok = await workspace.reboot();
    this._rebooting = false;
    if (ok) this._rebootOffer = false;
  }

  private _renderFileHeader() {
    const ws = this._ws;
    const where = ws.mode === 'device' ? 'device' : ws.mode === 'local' ? 'browser only' : '…';
    return html`
      <div class="flex items-center gap-2 px-4 h-8 shrink-0 bg-bg-default border-b border-border-default text-xs font-mono" data-testid="file-header">
        <span class="text-text-default truncate" data-testid="current-file">/${ws.currentKey}</span>
        ${ws.dirty ? html`<span class="text-status-orange" title="Unsaved changes">● unsaved</span>` : ''}
        ${!ws.dirty && ws.isNew ? html`<span class="text-status-orange">new (not saved yet)</span>` : ''}
        <span class="flex-1"></span>
        <span class="text-gray-200">${where}</span>
      </div>
      ${this._rebootOffer ? html`
        <div class="flex items-center gap-3 px-4 py-2 shrink-0 bg-purple-200 border-b border-border-default text-xs" data-testid="reboot-offer">
          <span class="flex-1 text-text-default">config.json saved. The config is applied at boot.</span>
          <button class="px-3 py-1 rounded-md bg-purple-90 hover:bg-purple-active text-white disabled:opacity-50"
            ?disabled=${this._rebooting}
            data-testid="reboot-now"
            @click=${() => this._reboot()}>${this._rebooting ? 'Rebooting…' : 'Reboot now'}</button>
          <button class="px-2 py-1 rounded-md text-gray-100 hover:bg-white/10" @click=${() => { this._rebootOffer = false; }}>Later</button>
        </div>
      ` : ''}
    `;
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
            ${this._renderFileHeader()}
            <fb-code-editor class="flex-1 min-h-0 overflow-hidden"></fb-code-editor>
            <fb-status-bar></fb-status-bar>
          </div>
        </div>
      </div>
    `;
  }
}
