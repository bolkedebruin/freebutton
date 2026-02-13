import { LitElement, html, svg } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import type { ButtonConfig } from '../js/app-state.js';

@customElement('fb-button-grid')
export class FbButtonGrid extends LitElement {
  createRenderRoot() { return this; }

  @property({ type: Number }) currentPage = 1;
  @property({ attribute: false }) buttons: ButtonConfig[] = [];

  private _isLeftSide(position: number): boolean {
    return position === 5 || position === 7;
  }

  private _dotsIcon(size: number) {
    return svg`
      <svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24"
        fill="currentColor" stroke="none">
        <circle cx="12" cy="5" r="2"/>
        <circle cx="12" cy="12" r="2"/>
        <circle cx="12" cy="19" r="2"/>
      </svg>
    `;
  }

  private _renderButton(btn: ButtonConfig) {
    const leftSide = this._isLeftSide(btn.position);

    return html`
      <div class="bg-[rgb(20,20,20)] h-[57.1px] flex items-center justify-center relative font-fira text-white
        ${leftSide ? 'flex-row' : 'flex-row-reverse'}">

        <!-- Config dots -->
        <div class="absolute top-1/2 -translate-y-1/2 text-white cursor-pointer z-10
          ${leftSide ? 'left-1' : 'right-1'}"
          style="font-size: 16px;">
          ${this._dotsIcon(16)}
        </div>

        <!-- LED indicator -->
        <div class="absolute top-1/2 -translate-y-1/2
          ${leftSide ? 'left-6' : 'right-6'}">
          <div class="w-[11.75px] h-[11.75px] border-[3px] bg-transparent
            ${btn.ledOn ? 'border-[rgb(255,0,0)]' : 'border-black'}">
          </div>
        </div>

        <!-- Button label area -->
        <button class="bg-[rgb(30,30,30)] border-0 text-white cursor-pointer flex flex-col items-center justify-center
          px-1.5 py-px w-[113px] h-[42px] font-fira text-[16.8px] font-normal text-center leading-[25.2px]
          transition-colors duration-200 hover:bg-[rgba(255,255,255,0.1)]">
          ${btn.topLabel ? html`
            <div class="text-[13.44px] font-normal font-fira text-[rgb(255,211,95)] text-center leading-[20.16px]">
              ${btn.topLabel}
            </div>
          ` : ''}
          <div class="text-[16.8px] font-normal text-white">${btn.label}</div>
        </button>

        <!-- Spacer -->
        <div class="h-1"></div>
      </div>
    `;
  }

  render() {
    const pageButtons = this.buttons
      .filter(b => b.page === this.currentPage)
      .sort((a, b) => a.position - b.position);

    return html`
      <div class="grid grid-cols-2 gap-[6.72px] w-[342px] mt-[6.72px] font-fira">
        ${pageButtons.map(btn => this._renderButton(btn))}
      </div>
    `;
  }
}
