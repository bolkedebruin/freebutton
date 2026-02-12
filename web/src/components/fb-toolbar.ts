import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';
import { PlayIcon, CheckIcon, UploadIcon, DownloadIcon, TrashIcon, PlugIcon } from '../icons/index.js';

@customElement('fb-toolbar')
export class FbToolbar extends LitElement {
  createRenderRoot() { return this; }

  private _iconBtn(icon: ReturnType<typeof PlayIcon>, label: string, primary = false) {
    const base = primary
      ? 'bg-purple-90 hover:bg-purple-active text-white'
      : 'bg-white/5 hover:bg-white/10 border border-white/10 text-text-default';
    return html`
      <button class="${base} rounded-full w-10 h-10 transition-colors duration-200 flex items-center justify-center" title="${label}">
        <span class="w-6 h-6 flex items-center justify-center [&>svg]:w-6 [&>svg]:h-6">${icon}</span>
      </button>
    `;
  }

  render() {
    return html`
      <div class="flex items-center px-8 py-5 bg-glass-dark backdrop-blur-md border-b border-border-default">
        <div class="flex items-center gap-4">
          ${this._iconBtn(PlayIcon(), 'Compile', true)}
          ${this._iconBtn(CheckIcon(), 'Validate')}
        </div>
        <div class="w-px h-6 bg-border-default mx-4"></div>
        <div class="flex items-center gap-4">
          ${this._iconBtn(UploadIcon(), 'Upload')}
          ${this._iconBtn(DownloadIcon(), 'Download')}
        </div>
        <div class="w-px h-6 bg-border-default mx-4"></div>
        ${this._iconBtn(TrashIcon(), 'Clear')}
        <div class="flex-1"></div>
        <div class="flex items-center rounded-xl h-14 bg-purple-300/30 border border-purple-100/10 w-72 focus-within:ring-3 focus-within:ring-purple-100/20 focus-within:ring-offset-0 transition-all duration-200">
          <span class="shrink-0 w-2.5 h-2.5 rounded-full bg-status-green ml-4"></span>
          <input
            type="text"
            placeholder="ws://device.local"
            class="flex-1 h-full px-3 bg-transparent text-sm text-text-default placeholder:text-sm placeholder:text-gray-200 outline-none"
          />
          <button class="shrink-0 mr-2 w-8 h-8 rounded-lg flex items-center justify-center text-gray-100 hover:text-text-default hover:bg-white/10 transition-colors duration-200" title="Connect">
            <span class="w-5 h-5 flex items-center justify-center [&>svg]:w-5 [&>svg]:h-5">${PlugIcon()}</span>
          </button>
        </div>
        <div class="flex-1"></div>
      </div>
    `;
  }
}
