import { LitElement, html, svg } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { XIcon, PlusIcon, ChevronDownIcon } from '../icons/index.js';
import { on, type WorkspaceFile, type WorkspaceSnapshot } from '../js/app-state.js';
import { formatFileSize } from '../js/virtual-fs.js';
import { workspace, LOCAL_MESSAGE } from '../js/workspace.js';
import { canDeleteKey, isEditableKey, MAIN_SCRIPT_KEY } from '../js/fs-rules.js';
import type { ReadOnlyBucket } from '../js/device-storage.js';

interface ReadOnlyGroup {
  id: 'examples' | 'system';
  title: string;
  tooltip: string;
  buckets: readonly ReadOnlyBucket[];
}

const READ_ONLY_GROUPS: readonly ReadOnlyGroup[] = [
  {
    id: 'examples',
    title: 'Examples',
    tooltip: 'Device bucket "examples" (/scripts/examples, read-only). Load one into the editor from the Examples panel.',
    buckets: ['examples'],
  },
  {
    id: 'system',
    title: 'System',
    tooltip: 'Device buckets "web" (the IDE bundle, read-only) and "fonts" (the display fonts; upload with an S3 client)',
    buckets: ['web', 'fonts'],
  },
];

/**
 * Assets panel: the files really on the device (bucket `app` of the S3
 * storage API), or the in-browser fallback store when no device is
 * reachable. Every .js/.json/.txt file there can be opened, saved and deleted
 * (config.json cannot be deleted). The read-only buckets are two collapsed
 * groups, Examples (`examples`) and System (`web` + `fonts`); a group's
 * buckets are listed when it is first expanded, never downloaded.
 */
@customElement('fb-file-drawer')
export class FbFileDrawer extends LitElement {
  createRenderRoot() { return this; }

  @state() private _ws: WorkspaceSnapshot = workspace.snapshot();
  @state() private _openGroups: Record<string, boolean> = {};
  @state() private _creating = false;
  @state() private _newName = '';

  private _unsub?: () => void;

  connectedCallback() {
    super.connectedCallback();
    this._ws = workspace.snapshot();
    this._unsub = on('workspace-change', (e) => { this._ws = e.detail; });
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsub?.();
  }

  private _open(file: WorkspaceFile) {
    if (!isEditableKey(file.key)) return;
    void workspace.open(file.key);
  }

  private _delete(e: Event, file: WorkspaceFile) {
    e.stopPropagation();
    void workspace.remove(file.key);
  }

  private _startCreate() {
    this._creating = true;
    this._newName = '';
    this.updateComplete.then(() => this.querySelector<HTMLInputElement>('[data-testid="new-file-input"]')?.focus());
  }

  private async _commitCreate() {
    const name = this._newName.trim();
    this._creating = false;
    if (name) await workspace.create(name);
  }

  private _onNewKey(e: KeyboardEvent) {
    if (e.key === 'Enter') { e.preventDefault(); void this._commitCreate(); }
    if (e.key === 'Escape') { this._creating = false; }
  }

  private _fileIcon() {
    return svg`<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
      <polyline points="14 2 14 8 20 8" />
    </svg>`;
  }

  private _refreshIcon() {
    return svg`<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <polyline points="23 4 23 10 17 10" />
      <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10" />
    </svg>`;
  }

  private _renderFile(file: WorkspaceFile) {
    const ws = this._ws;
    const active = file.key === ws.currentKey;
    const meta = [formatFileSize(file.size)];
    if (file.lastModified) meta.push(file.lastModified.toLocaleString());
    return html`
      <div class="flex items-center gap-2 px-2 py-1.5 rounded-lg cursor-pointer transition-colors duration-200 group/item
          ${active ? 'bg-white/10' : 'hover:bg-white/5'}"
        data-key=${file.key}
        title="/${file.key}"
        @click=${() => this._open(file)}>
        <span class="shrink-0 text-gray-200">${this._fileIcon()}</span>
        <div class="flex-1 min-w-0">
          <div class="text-xs text-text-default truncate">
            ${file.key}${active && ws.dirty ? html`<span class="text-status-orange ml-1" title="Unsaved changes">●</span>` : ''}
          </div>
          <div class="text-[10px] text-gray-200 truncate">${meta.join(' · ')}</div>
        </div>
        ${canDeleteKey(file.key) ? html`
          <button class="shrink-0 w-5 h-5 flex items-center justify-center rounded text-gray-200 hover:text-status-red hover:bg-white/10 opacity-0 group-hover/item:opacity-100 transition-all duration-200"
            title="Delete" data-testid="delete-file"
            @click=${(e: Event) => this._delete(e, file)}>
            <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5">${XIcon()}</span>
          </button>
        ` : ''}
      </div>
    `;
  }

  /** The open file when it is not in storage yet (template main.js). */
  private _renderUnsavedCurrent() {
    const ws = this._ws;
    if (!ws.isNew || ws.files.some(f => f.key === ws.currentKey)) return '';
    return html`
      <div class="flex items-center gap-2 px-2 py-1.5 rounded-lg bg-white/10" data-key=${ws.currentKey} data-testid="unsaved-file">
        <span class="shrink-0 text-gray-200">${this._fileIcon()}</span>
        <div class="flex-1 min-w-0">
          <div class="text-xs text-text-default truncate">${ws.currentKey}</div>
          <div class="text-[10px] text-status-orange">not saved yet</div>
        </div>
      </div>
    `;
  }

  private _toggleGroup(group: ReadOnlyGroup) {
    const open = !this._openGroups[group.id];
    this._openGroups = { ...this._openGroups, [group.id]: open };
    if (open) void workspace.loadReadOnly(group.buckets);
  }

  private _renderGroup(group: ReadOnlyGroup) {
    const ws = this._ws;
    if (ws.mode !== 'device') return '';
    const listed = group.buckets.map(b => ({ bucket: b, files: ws.readOnly?.[b] }));
    const loaded = listed.every(l => l.files !== undefined);
    // Several buckets in one group: show each key with its bucket name.
    const entries = listed.flatMap(l => (l.files ?? []).map(f => ({
      label: group.buckets.length > 1 ? `${l.bucket}/${f.key}` : f.key,
      title: `${l.bucket}: ${f.key} (read-only)`,
      size: f.size,
    })));
    const total = entries.reduce((n, f) => n + f.size, 0);
    const open = !!this._openGroups[group.id];
    return html`
      <div class="flex flex-col gap-0.5" data-testid="${group.id}-files">
        <button class="flex items-center gap-1 px-2 py-1 text-[10px] text-gray-200 hover:text-text-default uppercase tracking-wider"
          title=${group.tooltip} data-testid="${group.id}-toggle"
          @click=${() => this._toggleGroup(group)}>
          <span class="w-3 h-3 flex items-center justify-center [&>svg]:w-3 [&>svg]:h-3 transition-transform ${open ? '' : '-rotate-90'}">${ChevronDownIcon()}</span>
          ${group.title}${loaded ? ` · ${entries.length} files · ${formatFileSize(total)}` : ' · read-only'}
        </button>
        ${open ? entries.map(f => html`
          <div class="flex items-center gap-2 px-2 py-1 text-[10px] text-gray-200" title=${f.title}>
            <span class="flex-1 min-w-0 truncate">${f.label}</span>
            <span class="shrink-0">${formatFileSize(f.size)}</span>
          </div>
        `) : ''}
      </div>
    `;
  }

  render() {
    const ws = this._ws;
    const userFiles = ws.files;
    const usedBytes = ws.files.reduce((n, f) => n + f.size, 0);
    const source = ws.mode === 'device' ? 'Device' : ws.mode === 'local' ? 'Browser (no device)' : 'Connecting…';

    return html`
      <div class="flex flex-col gap-3">
        <!-- Header -->
        <div class="flex items-center justify-between">
          <span class="text-xs font-medium text-gray-100 uppercase tracking-wider">Assets</span>
          <div class="flex items-center gap-1">
            <button class="w-5 h-5 flex items-center justify-center rounded text-gray-200 hover:text-text-default hover:bg-white/10 transition-colors duration-200 disabled:opacity-40"
              title="Refresh file list" data-testid="refresh-files"
              ?disabled=${ws.busy}
              @click=${() => workspace.refresh()}>
              <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5 ${ws.busy ? 'animate-spin' : ''}">${this._refreshIcon()}</span>
            </button>
            <button class="w-5 h-5 flex items-center justify-center rounded text-gray-200 hover:text-text-default hover:bg-white/10 transition-colors duration-200"
              title="New file (.js, .json, .txt)" data-testid="new-file"
              @click=${this._startCreate}>
              <span class="w-4 h-4 flex items-center justify-center [&>svg]:w-4 [&>svg]:h-4">${PlusIcon()}</span>
            </button>
          </div>
        </div>

        <div class="text-[10px] text-gray-200 -mt-2" data-testid="storage-source">${source}</div>

        ${ws.message ? html`
          <div class="text-[10px] leading-snug rounded-md px-2 py-1.5 ${ws.message === LOCAL_MESSAGE ? 'bg-white/5 text-gray-100' : 'bg-status-red/10 text-status-red'}"
            data-testid="workspace-message">${ws.message}</div>
        ` : ''}

        ${this._creating ? html`
          <input type="text" data-testid="new-file-input"
            class="bg-white/5 border border-white/10 rounded-lg px-2 py-1 text-xs text-white outline-none focus:border-purple-80"
            placeholder="name.js / data.json / notes.txt"
            .value=${this._newName}
            @input=${(e: Event) => { this._newName = (e.target as HTMLInputElement).value; }}
            @keydown=${this._onNewKey}
            @blur=${() => { if (!this._newName.trim()) this._creating = false; }} />
        ` : ''}

        <!-- File list -->
        <div class="flex flex-col gap-0.5" data-testid="user-files">
          ${this._renderUnsavedCurrent()}
          ${userFiles.map(f => this._renderFile(f))}
          ${userFiles.length === 0 && !ws.isNew && ws.mode !== 'probing'
            ? html`<div class="text-[10px] text-gray-200 px-2">No files yet. ${MAIN_SCRIPT_KEY} is created on first save.</div>`
            : ''}
        </div>

        ${READ_ONLY_GROUPS.map(g => this._renderGroup(g))}

        <!-- Storage summary -->
        <div class="flex items-center justify-between pt-2 border-t border-border-default text-[10px] text-gray-200">
          <span>${ws.files.length} file${ws.files.length === 1 ? '' : 's'}</span>
          <span>${formatFileSize(usedBytes)} used</span>
        </div>
      </div>
    `;
  }
}
