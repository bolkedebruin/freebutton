import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { BookIcon, CpuIcon, FileTextIcon, SettingsIcon, FolderIcon, ImageIcon, TypeIcon, FbLogo } from '../icons/index.js';
import { emit, on, type MainView, type SidebarPanel } from '../js/app-state.js';
import './fb-nav-item.js';

/**
 * Left bar: one selection decides the whole middle pane (the main view and
 * the sidebar that belongs to it). App is the editor with the app's files;
 * Images, Fonts and Examples show their view with their list; Device and
 * Docs have no list. Settings opens the dialog. The selected entry is marked
 * (`layout-change` from the editor pane).
 */
@customElement('fb-navbar')
export class FbNavbar extends LitElement {
  createRenderRoot() { return this; }

  @state() private _view: MainView = 'editor';
  @state() private _panel: SidebarPanel | null = 'app';
  private _unsub?: () => void;

  connectedCallback() {
    super.connectedCallback();
    this._unsub = on('layout-change', (e) => {
      this._view = e.detail.view;
      this._panel = e.detail.panel;
    });
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsub?.();
  }

  /** App: the editor with the file list. Already there, it folds or unfolds the list. */
  private _app() {
    if (this._view === 'editor') {
      emit('sidebar-panel', { panel: 'app' });
    } else {
      emit('main-view', { view: 'editor' });
      emit('sidebar-panel', { panel: 'app', open: true });
    }
  }

  /** Images, Fonts, Examples: the main view and its list in the sidebar. */
  private _show(panel: SidebarPanel, view: MainView) {
    if (this._panel === panel) return;
    emit('main-view', { view });
    emit('sidebar-panel', { panel, open: true });
  }

  render() {
    return html`
      <nav class="h-screen w-[100px] bg-glass-dark backdrop-blur-md border-r border-border-default overflow-hidden flex flex-col pt-6 px-6 pb-0" aria-label="Main">
        <div class="flex h-full flex-col items-start justify-between gap-6 pt-6 pb-2">
          <!-- Top: Logo + Nav Items -->
          <div class="flex w-full flex-col items-start gap-8">
            <!-- Logo: same placement as Q logo -->
            <div class="flex w-[52px] justify-center">
              ${FbLogo()}
            </div>

            <!-- Main nav items -->
            <div class="flex w-full flex-col gap-3 items-center pl-1.5">
              <fb-nav-item .icon=${FolderIcon()} label="App" hint="The app's files on the device (scripts, config.json) and the editor"
                ?active=${this._view === 'editor' || this._panel === 'app'}
                @click=${() => this._app()}></fb-nav-item>
              <fb-nav-item .icon=${ImageIcon()} label="Images" hint="Images on the device (SVG sources of the icons)"
                ?active=${this._panel === 'images'}
                @click=${() => this._show('images', 'image')}></fb-nav-item>
              <fb-nav-item .icon=${TypeIcon()} label="Fonts" hint="Fonts on the device, rendered for comparison"
                ?active=${this._panel === 'fonts'}
                @click=${() => this._show('fonts', 'fonts')}></fb-nav-item>
              <fb-nav-item .icon=${FileTextIcon()} label="Examples" hint="Example scripts"
                ?active=${this._panel === 'examples'}
                @click=${() => this._show('examples', 'example')}></fb-nav-item>
              <fb-nav-item .icon=${CpuIcon()} label="Device" hint="Device status, screenshot, reboot"
                ?active=${this._view === 'device'}
                @click=${() => emit('main-view', { view: 'device' })}></fb-nav-item>
              <fb-nav-item .icon=${BookIcon()} label="Docs" hint="Documentation and keyboard shortcuts"
                ?active=${this._view === 'docs'}
                @click=${() => emit('main-view', { view: 'docs' })}></fb-nav-item>
            </div>
          </div>

          <!-- Bottom section — pinned to bottom -->
          <div class="flex w-full flex-col items-center pl-1.5 mt-auto">
            <fb-nav-item .icon=${SettingsIcon()} label="Settings" hint="Device access and IDE settings" @click=${() => emit('open-settings')}></fb-nav-item>
          </div>
        </div>
      </nav>
    `;
  }
}
