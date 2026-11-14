import { LitElement, html, svg } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import {
  on, emit, type ConfigRef, type MainView, type SidebarPanel, type WorkspaceSnapshot,
} from '../js/app-state.js';
import { examples, type Example } from '../js/examples.js';
import { workspace } from '../js/workspace.js';
import { revealConfigItem } from '../js/editor.js';
import { CONFIG_KEY, MAIN_SCRIPT_KEY } from '../js/fs-rules.js';
import { uploadDropped } from '../js/uploads.js';

import './fb-toolbar.js';
import './fb-code-editor.js';
import './fb-status-bar.js';
import './fb-file-drawer.js';
import './fb-font-view.js';
import './fb-image-view.js';
import './fb-device-view.js';
import './fb-example-view.js';

const SIDEBAR_MIN = 180;
const SIDEBAR_MAX = 420;

const VIEW_TITLES: Record<Exclude<MainView, 'editor'>, string> = {
  fonts: 'Fonts',
  image: 'Images',
  example: 'Example',
  device: 'Device',
  docs: 'Docs',
};

/**
 * The main pane: toolbar, the sidebar (App, Images, Fonts or Examples) and one main
 * view. The main view is the code editor, or (event `main-view`) the Fonts
 * view, the images or one asset that is not text, an example (read-only),
 * the Device view or Docs. The editor stays
 * alive behind a view (hidden, not removed): its text, undo history and
 * unsaved changes are kept. `layout-change` tells the nav bar what is shown.
 */
@customElement('fb-editor-pane')
export class FbEditorPane extends LitElement {
  createRenderRoot() { return this; }

  @state() private _activePanel: SidebarPanel | null = 'app';
  @state() private _view: MainView = 'editor';
  @state() private _viewKey = '';
  @state() private _sidebarWidth = 220;
  @state() private _ws: WorkspaceSnapshot = workspace.snapshot();
  // Set after config.json was saved to the device: offer a reboot.
  @state() private _rebootOffer = false;
  @state() private _rebooting = false;
  /** Files are being dragged over the Images or Fonts view. */
  @state() private _dropping = false;
  private _dragDepth = 0;

  private _unsubs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._unsubs.push(
      on('sidebar-panel', (e) => {
        const panel = e.detail.panel;
        // open: true shows it (no toggle), e.g. from a link in a view.
        this._activePanel = this._activePanel === panel && !e.detail.open ? null : panel;
        this._announce();
      }),
      on('main-view', (e) => {
        this._activePanel = this._panelFor(e.detail.view);
        this._view = e.detail.view;
        this._viewKey = e.detail.key ?? '';
        this._announce();
      }),
      on('workspace-change', (e) => { this._ws = e.detail; }),
      on('reboot-suggested', () => { this._rebootOffer = true; }),
      on('config-reveal', (e) => { void this._revealConfig(e.detail.ref); }),
    );
    this._announce();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  /**
   * The sidebar that belongs to a main view: one selection in the left bar
   * decides both. Device and Docs have none; the editor has the App list (which
   * can be folded while the editor is shown); a view with a list of its own shows that list.
   */
  private _panelFor(view: MainView): SidebarPanel | null {
    const now = this._activePanel;
    switch (view) {
      case 'device': case 'docs': return null;
      case 'fonts': return 'fonts';
      case 'example': return 'examples';
      // One file that is not text: from the App list ("Other") or the Images list.
      case 'image': return now === 'app' ? 'app' : 'images';
      // The editor: its list, unless the editor was already shown with the list folded.
      default: return this._view === 'editor' ? now : 'app';
    }
  }

  private _announce() {
    emit('layout-change', { view: this._view, panel: this._activePanel });
  }

  private _showEditor() {
    if (this._view === 'editor') return;
    this._view = 'editor';
    this._viewKey = '';
    this._activePanel = 'app';
    this._announce();
  }

  /**
   * A click on an item of the virtual device: show its config.json entry.
   * Opens config.json when another file is open (the workspace asks before
   * it drops unsaved changes).
   */
  private async _revealConfig(ref: ConfigRef) {
    if (workspace.snapshot().currentKey !== CONFIG_KEY && !(await workspace.open(CONFIG_KEY))) return;
    this._showEditor();
    await this.updateComplete;
    revealConfigItem(ref);
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
        ${this._view === 'editor' ? html`
          <span class="text-text-default truncate" data-testid="current-file">/${ws.currentKey}</span>
          ${ws.dirty ? html`<span class="text-status-orange" title="Unsaved changes">● unsaved</span>` : ''}
          ${!ws.dirty && ws.isNew ? html`<span class="text-status-orange">new (not saved yet)</span>` : ''}
          <span class="flex-1"></span>
          <span class="text-gray-200">${where}</span>
        ` : html`
          <span class="text-text-default font-sans" data-testid="view-title">${VIEW_TITLES[this._view]}</span>
          <span class="flex-1"></span>
          <button class="flex items-center gap-1.5 px-2 py-0.5 rounded-md text-gray-100 hover:text-text-default hover:bg-white/10"
            title="Back to the open file" data-testid="back-to-editor"
            @click=${() => this._showEditor()}>
            ← /${ws.currentKey}${ws.dirty ? html`<span class="text-status-orange" title="Unsaved changes">●</span>` : ''}
          </button>
        `}
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

  /** Show an example in the main pane, read-only: no file is made or changed. */
  private _loadExample(ex: Example) {
    emit('main-view', { view: 'example', key: ex.id });
  }

  private _renderExamplesPanel() {
    return html`
      <div class="flex flex-col gap-3">
        <div class="flex items-center justify-between">
          <span class="text-xs font-medium text-gray-100 uppercase tracking-wider">Examples</span>
        </div>
        <div class="flex flex-col gap-0.5">
          ${examples.map(ex => html`
            <button class="flex items-start gap-2 px-2 py-1.5 rounded-lg transition-colors duration-200 text-left ${this._view === 'example' && (this._viewKey || examples[0]?.id) === ex.id ? 'bg-white/10' : 'hover:bg-white/5'}"
              data-example=${ex.id}
              @click=${() => this._loadExample(ex)}>
              <span class="shrink-0 text-gray-200 mt-0.5">${this._fileIcon()}</span>
              <span class="flex-1 min-w-0">
                <span class="block text-xs text-text-default truncate">${ex.title}</span>
                <span class="block text-[10px] text-gray-200">${ex.description}</span>
              </span>
            </button>
          `)}
        </div>
      </div>
    `;
  }

  /** Docs: no documentation is linked yet; the shortcuts are what there is. */
  private _renderDocs() {
    const keys: Array<[string, string]> = [
      ['Ctrl/Cmd + S', 'Save the open file'],
      ['Ctrl/Cmd + Enter', `Deploy /${MAIN_SCRIPT_KEY} to the device`],
      ['Ctrl/Cmd + Space', 'Completions for the fb API'],
      ['Ctrl/Cmd + F', 'Find in the open file'],
    ];
    return html`
      <div class="flex-1 min-h-0 overflow-y-auto custom-scrollbar pane-scroll p-6 text-sm text-text-default" data-testid="docs-view">
        <p class="text-gray-100 max-w-prose">The documentation is not linked from the IDE yet. Hover a function of the <code>fb</code> API in the editor for its description; the Examples panel has a script for each part of it.</p>
        <h3 class="mt-6 mb-2 text-xs uppercase tracking-wider text-gray-200">Keyboard shortcuts</h3>
        <table class="text-xs">
          ${keys.map(([k, what]) => html`<tr><td class="pr-6 py-1 font-mono text-gray-100">${k}</td><td class="py-1">${what}</td></tr>`)}
        </table>
      </div>
    `;
  }

  /** Images and Fonts take files dropped anywhere on the middle pane. */
  private get _dropTarget(): 'images' | 'fonts' | null {
    return this._activePanel === 'images' ? 'images' : this._activePanel === 'fonts' ? 'fonts' : null;
  }

  private _hasFiles(e: DragEvent): boolean {
    return !!this._dropTarget && Array.from(e.dataTransfer?.types ?? []).includes('Files');
  }

  private _onDragEnter = (e: DragEvent) => {
    if (!this._hasFiles(e)) return;
    e.preventDefault();
    this._dragDepth++;
    this._dropping = true;
  };

  private _onDragOver = (e: DragEvent) => {
    if (!this._hasFiles(e)) return;
    e.preventDefault();   // without this the browser opens the file instead
    if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy';
  };

  private _onDragLeave = (e: DragEvent) => {
    if (!this._hasFiles(e)) return;
    if (--this._dragDepth <= 0) { this._dragDepth = 0; this._dropping = false; }
  };

  private _onDrop = async (e: DragEvent) => {
    if (!this._hasFiles(e)) return;
    e.preventDefault();
    this._dragDepth = 0;
    this._dropping = false;
    const shown = this._dropTarget;
    const got = await uploadDropped(Array.from(e.dataTransfer?.files ?? []));
    // A font dropped on Images (or the other way round) went where it belongs: show that.
    if (shown === 'images' && got.images === 0 && got.fonts > 0) {
      emit('main-view', { view: 'fonts' });
    } else if (shown === 'fonts' && got.fonts === 0 && got.images > 0) {
      emit('main-view', { view: 'image' });
    }
  };

  private _onSidebarResize = (e: CustomEvent<{ delta: number }>) => {
    this._sidebarWidth = Math.min(SIDEBAR_MAX, Math.max(SIDEBAR_MIN, this._sidebarWidth + e.detail.delta));
  };

  render() {
    const isOpen = this._activePanel !== null;
    const editing = this._view === 'editor';

    return html`
      <div class="flex flex-col h-full">
        <fb-toolbar></fb-toolbar>
        <div class="flex flex-1 min-h-0 relative" data-testid="middle"
          @dragenter=${this._onDragEnter} @dragover=${this._onDragOver}
          @dragleave=${this._onDragLeave} @drop=${this._onDrop}>
          ${this._dropping ? html`
            <div class="absolute inset-2 z-20 rounded-xl border-2 border-dashed border-purple-80 bg-purple-90/15 flex items-center justify-center pointer-events-none"
              data-testid="drop-overlay">
              <span class="px-4 py-2 rounded-lg bg-bg-default text-sm text-text-default">
                ${this._dropTarget === 'fonts' ? 'Drop .ttf / .otf fonts to upload them to the device' : 'Drop .svg images to upload them to the device'}
              </span>
            </div>
          ` : ''}
          <!-- Sidebar -->
          <div class="shrink-0 border-r border-border-default bg-glass-dark backdrop-blur-md overflow-hidden ${isOpen ? '' : 'border-r-0'}"
            style="width: ${isOpen ? this._sidebarWidth : 0}px;" data-testid="sidebar">
            <div class="p-3 h-full overflow-y-auto custom-scrollbar pane-scroll" data-pane="sidebar" style="width: ${this._sidebarWidth}px;">
              ${this._activePanel && this._activePanel !== 'examples'
                ? html`<fb-file-drawer .panel=${this._activePanel}></fb-file-drawer>` : ''}
              ${this._activePanel === 'examples' ? this._renderExamplesPanel() : ''}
            </div>
          </div>
          ${isOpen ? html`
            <fb-resize-handle class="w-[6px] -ml-[3px] cursor-col-resize shrink-0 z-10" data-direction="horizontal"
              data-scroll-source='[data-pane="sidebar"]' @resize=${this._onSidebarResize}></fb-resize-handle>
          ` : ''}
          <div class="flex-1 min-w-0 flex flex-col overflow-hidden">
            ${this._renderFileHeader()}
            <!-- The editor is hidden, not removed, behind another view. -->
            <fb-code-editor class="flex-1 min-h-0 overflow-hidden ${editing ? '' : 'hidden'}"></fb-code-editor>
            <fb-status-bar class=${editing ? '' : 'hidden'}></fb-status-bar>
            ${this._view === 'fonts' ? html`<fb-font-view class="flex-1 min-h-0"></fb-font-view>` : ''}
            ${this._view === 'image' ? html`<fb-image-view class="flex-1 min-h-0" .fileKey=${this._viewKey}></fb-image-view>` : ''}
            ${this._view === 'device' ? html`<fb-device-view class="flex-1 min-h-0"></fb-device-view>` : ''}
            ${this._view === 'example' ? html`<fb-example-view class="flex-1 min-h-0" .exampleId=${this._viewKey}></fb-example-view>` : ''}
            ${this._view === 'docs' ? this._renderDocs() : ''}
          </div>
        </div>
      </div>
    `;
  }
}
