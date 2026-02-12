import { LitElement, html, svg } from 'lit';
import { customElement } from 'lit/decorators.js';
import { XIcon, PlusIcon } from '../icons/index.js';

interface AssetFile {
  name: string;
  size: string;
  type: 'image' | 'svg';
}

@customElement('fb-file-drawer')
export class FbFileDrawer extends LitElement {
  createRenderRoot() { return this; }

  private _files: AssetFile[] = [
    { name: 'logo.svg', size: '2.1 KB', type: 'svg' },
    { name: 'icon-home.svg', size: '1.4 KB', type: 'svg' },
    { name: 'banner.png', size: '24.3 KB', type: 'image' },
    { name: 'bg-pattern.svg', size: '3.8 KB', type: 'svg' },
  ];

  private _usedKB = 31.6;
  private _totalKB = 128;

  private _fileIcon(type: 'image' | 'svg') {
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

  private _renderFile(file: AssetFile) {
    return html`
      <div class="flex items-center gap-2 px-2 py-1.5 rounded-lg hover:bg-white/5 transition-colors duration-200 group/item">
        <span class="shrink-0 text-gray-200">${this._fileIcon(file.type)}</span>
        <div class="flex-1 min-w-0">
          <div class="text-xs text-text-default truncate">${file.name}</div>
          <div class="text-[10px] text-gray-200">${file.size}</div>
        </div>
        <button class="shrink-0 w-5 h-5 flex items-center justify-center rounded text-gray-200 hover:text-status-red hover:bg-white/10 opacity-0 group-hover/item:opacity-100 transition-all duration-200" title="Remove">
          <span class="w-3.5 h-3.5 flex items-center justify-center [&>svg]:w-3.5 [&>svg]:h-3.5">${XIcon()}</span>
        </button>
      </div>
    `;
  }

  render() {
    const usedPct = (this._usedKB / this._totalKB) * 100;

    return html`
      <div class="flex flex-col gap-3">
        <!-- Header -->
        <div class="flex items-center justify-between">
          <span class="text-xs font-medium text-gray-100 uppercase tracking-wider">Assets</span>
          <button class="w-5 h-5 flex items-center justify-center rounded text-gray-200 hover:text-text-default hover:bg-white/10 transition-colors duration-200" title="Add asset">
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
            <span>${this._usedKB} KB used</span>
            <span>${this._totalKB} KB</span>
          </div>
          <div class="w-full h-1.5 rounded-full bg-white/5 overflow-hidden">
            <div class="h-full rounded-full bg-purple-90 transition-all duration-300" style="width: ${usedPct}%"></div>
          </div>
        </div>
      </div>
    `;
  }
}
