import { LitElement, html, svg } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { XIcon, PlusIcon } from '../icons/index.js';
import { on, type FileEntry } from '../js/app-state.js';
import { virtualFS, initDefaultFiles, formatFileSize } from '../js/virtual-fs.js';

@customElement('fb-file-drawer')
export class FbFileDrawer extends LitElement {
  createRenderRoot() { return this; }

  @state() private _files: FileEntry[] = [];
  @state() private _usedBytes = 0;
  @state() private _totalBytes = 128 * 1024;

  private _unsub?: () => void;
  private _fileInput?: HTMLInputElement;

  connectedCallback() {
    super.connectedCallback();
    this._unsub = on('files-change', (e) => {
      this._files = e.detail.files;
      this._usedBytes = e.detail.used;
      this._totalBytes = e.detail.total;
    });
    // Seed default files if empty
    if (virtualFS.getFileList().length === 0) {
      initDefaultFiles();
    } else {
      // Sync state from existing FS
      this._files = virtualFS.getFileList();
      this._usedBytes = virtualFS.usedSpace;
      this._totalBytes = virtualFS.totalSpace;
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsub?.();
  }

  private _handleAddClick() {
    if (!this._fileInput) {
      this._fileInput = document.createElement('input');
      this._fileInput.type = 'file';
      this._fileInput.addEventListener('change', () => {
        const file = this._fileInput?.files?.[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = () => {
          const data = new Uint8Array(reader.result as ArrayBuffer);
          virtualFS.addFile(file.name, data);
        };
        reader.readAsArrayBuffer(file);
        // Reset so same file can be re-selected
        this._fileInput!.value = '';
      });
    }
    this._fileInput.click();
  }

  private _handleDelete(name: string) {
    virtualFS.deleteFile(name);
  }

  private _fileIcon(type: FileEntry['type']) {
    if (type === 'svg') {
      return svg`<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <polygon points="12 2 2 7 12 12 22 7 12 2" />
        <polyline points="2 17 12 22 22 17" />
        <polyline points="2 12 12 17 22 12" />
      </svg>`;
    }
    return svg`<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <rect x="3" y="3" width="18" height="18" rx="2" ry="2" />
      <circle cx="8.5" cy="8.5" r="1.5" />
      <polyline points="21 15 16 10 5 21" />
    </svg>`;
  }

  private _renderFile(file: FileEntry) {
    return html`
      <div class="flex items-center gap-2 px-2 py-1.5 rounded-lg hover:bg-white/5 transition-colors duration-200 group/item">
        <span class="shrink-0 text-gray-200">${this._fileIcon(file.type)}</span>
        <div class="flex-1 min-w-0">
          <div class="text-xs text-text-default truncate">${file.name}</div>
          <div class="text-[10px] text-gray-200">${formatFileSize(file.size)}</div>
        </div>
        <button class="shrink-0 w-5 h-5 flex items-center justify-center rounded text-gray-200 hover:text-status-red hover:bg-white/10 opacity-0 group-hover/item:opacity-100 transition-all duration-200"
          title="Remove"
          @click=${() => this._handleDelete(file.name)}>
          <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5">${XIcon()}</span>
        </button>
      </div>
    `;
  }

  render() {
    const usedKB = this._usedBytes / 1024;
    const totalKB = this._totalBytes / 1024;
    const usedPct = (usedKB / totalKB) * 100;

    return html`
      <div class="flex flex-col gap-3">
        <!-- Header -->
        <div class="flex items-center justify-between">
          <span class="text-xs font-medium text-gray-100 uppercase tracking-wider">Assets</span>
          <button class="w-5 h-5 flex items-center justify-center rounded text-gray-200 hover:text-text-default hover:bg-white/10 transition-colors duration-200"
            title="Add asset"
            @click=${this._handleAddClick}>
            <span class="w-4 h-4 flex items-center justify-center [&>svg]:w-4 [&>svg]:h-4">${PlusIcon()}</span>
          </button>
        </div>

        <!-- File list -->
        <div class="flex flex-col gap-0.5">
          ${this._files.map(f => this._renderFile(f))}
        </div>

        <!-- Storage indicator -->
        <div class="flex flex-col gap-1.5 pt-2 border-t border-border-default">
          <div class="flex items-center justify-between text-[10px] text-gray-200">
            <span>${usedKB % 1 === 0 ? usedKB.toFixed(0) : usedKB.toFixed(1)} KB used</span>
            <span>${totalKB} KB</span>
          </div>
          <div class="w-full h-1.5 rounded-full bg-white/5 overflow-hidden">
            <div class="h-full rounded-full bg-purple-90 transition-all duration-300" style="width: ${usedPct}%"></div>
          </div>
        </div>
      </div>
    `;
  }
}
