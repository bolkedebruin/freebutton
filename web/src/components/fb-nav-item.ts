import { LitElement, html } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { TemplateResult } from 'lit';

/**
 * A nav bar entry: a real button (focus, Enter/Space), `active` shown and
 * announced (aria-pressed). The click is the host's: listen on <fb-nav-item>.
 */
@customElement('fb-nav-item')
export class FbNavItem extends LitElement {
  createRenderRoot() { return this; }

  @property({ attribute: false })
  icon!: TemplateResult;

  @property()
  label = '';

  @property({ type: Boolean })
  active = false;

  /** Tooltip: what the entry opens. */
  @property()
  hint = '';

  render() {
    const activeStyles = this.active
      ? 'bg-white/10'
      : 'hover:bg-purple-100/10';

    return html`
      <div class="w-auto">
        <button type="button" title=${this.hint || this.label} aria-pressed=${this.active ? 'true' : 'false'}
          class="inline-flex items-center rounded-lg p-2 transition-all duration-200 flex-col w-[52px] cursor-pointer ${activeStyles}">
          <span class="shrink-0 transition-colors duration-200 ${this.active ? 'text-white' : 'text-icon-default'}">${this.icon}</span>
          <span class="text-text-default text-[11px] font-medium mt-1 text-center w-[52px]" style="-webkit-font-smoothing: subpixel-antialiased; -moz-osx-font-smoothing: auto;">${this.label}</span>
        </button>
      </div>
    `;
  }
}
