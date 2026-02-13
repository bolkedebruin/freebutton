import { LitElement, html } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { TemplateResult } from 'lit';

@customElement('fb-nav-item')
export class FbNavItem extends LitElement {
  createRenderRoot() { return this; }

  @property({ attribute: false })
  icon!: TemplateResult;

  @property()
  label = '';

  @property({ type: Boolean })
  active = false;

  render() {
    const activeStyles = this.active
      ? 'bg-white/10'
      : 'hover:bg-purple-100/10';

    const labelCls = '';

    return html`
      <div class="w-auto">
        <div class="inline-flex items-center rounded-lg p-2 transition-all duration-200 flex-col w-[52px] cursor-pointer ${activeStyles}">
          <span class="shrink-0 text-icon-default transition-colors duration-200">${this.icon}</span>
          <span class="text-text-default text-[11px] font-medium mt-1 text-center w-[52px]" style="-webkit-font-smoothing: subpixel-antialiased; -moz-osx-font-smoothing: auto;">${this.label}</span>
        </div>
      </div>
    `;
  }
}
