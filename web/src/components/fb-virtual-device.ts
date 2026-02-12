import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';

@customElement('fb-virtual-device')
export class FbVirtualDevice extends LitElement {
  createRenderRoot() { return this; }

  @state() private currentPage = 1;
  private pages = [1, 2];

  private _selectPage(page: number) {
    this.currentPage = page;
  }

  render() {
    return html`
      <div class="flex flex-col items-center">
        <!-- Page tabs -->
        <div class="flex w-max rounded-xl p-1 bg-white/5 backdrop-blur-md border border-white/10 mb-4">
          ${this.pages.map(page => html`
            <button
              class="px-5 py-2 text-sm rounded-lg font-medium cursor-pointer transition-all duration-200
                ${page === this.currentPage
                  ? 'bg-white/95 text-bg-default shadow-sm'
                  : 'bg-transparent text-gray-100 hover:text-text-default'}"
              @click=${() => this._selectPage(page)}
            >Page ${page}</button>
          `)}
        </div>

        <!-- Device container -->
        <div class="w-[342px] font-fira text-[11.2px]">
          <fb-tft-display .currentPage=${this.currentPage}></fb-tft-display>
          <fb-button-grid .currentPage=${this.currentPage}></fb-button-grid>
        </div>
      </div>
    `;
  }
}
