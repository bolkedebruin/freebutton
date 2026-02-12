import { LitElement, html, svg } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';

@customElement('fb-tft-display')
export class FbTftDisplay extends LitElement {
  createRenderRoot() { return this; }

  @property({ type: Number }) currentPage = 1;
  @state() private timeStr = '';
  private _timer?: number;

  private _displayItems = [
    { label: 'Amsterdam', x: 0, y: 5, width: 100, type: 'time' as const },
    { label: 'Button+ Likes', x: 0, y: 50, width: 100, type: 'likes' as const },
  ];

  connectedCallback() {
    super.connectedCallback();
    this._updateTime();
    this._timer = window.setInterval(() => this._updateTime(), 1000);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    if (this._timer) clearInterval(this._timer);
  }

  private _updateTime() {
    const now = new Date();
    this.timeStr = `${now.getHours()}:${String(now.getMinutes()).padStart(2, '0')}`;
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

  render() {
    return html`
      <!-- LargeDisplay outer -->
      <div class="bg-[rgb(20,20,20)] w-[342px] h-[219px] flex py-[3.36px]">
        <!-- Left config dots column -->
        <div class="flex items-end w-[29.5px] h-[212.25px] pt-[12.32px] pb-[3.7px] font-fira">
          <div class="text-white cursor-pointer flex items-center">
            ${this._dotsIcon(24)}
          </div>
        </div>

        <!-- TFT Screen -->
        <div class="bg-[rgb(30,30,30)] w-[283px] h-[212.25px] relative overflow-hidden text-[11.2px] font-[Montserrat,sans-serif] leading-[11.2px]">
          ${this._displayItems.map(item => html`
            <div class="absolute text-white font-[Montserrat,sans-serif]"
              style="left: ${item.x}%; top: ${item.y}%; width: ${item.width}%; border-bottom: 1px solid rgb(170, 170, 170);">
              <div class="text-[1.106em] font-semibold my-[2px] text-[rgb(255,211,95)] leading-[1em]">
                ${item.label}
              </div>
              <div class="font-semibold text-white flex items-end
                ${item.type === 'time' ? 'text-[3.792em] pb-[3.36px]' : 'text-[1.58em] pb-[1.4px]'}">
                <div class="font-normal leading-[1.3em]">
                  ${item.type === 'time' ? this.timeStr : '67005'}
                </div>
              </div>
            </div>
          `)}

          <!-- Status bar -->
          <div class="absolute text-[11.2px] font-normal font-[Montserrat,sans-serif] text-white text-left leading-[11.2px]"
            style="top: 201px; left: 0;">
            btn_fab028
          </div>
        </div>

        <!-- Right config dots column -->
        <div class="flex items-end w-[29.5px] h-[212.25px] pt-[12.32px] pb-[3.7px] font-fira">
          <div class="text-white cursor-pointer flex items-center">
            ${this._dotsIcon(24)}
          </div>
        </div>
      </div>
    `;
  }
}
