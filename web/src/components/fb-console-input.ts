import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';

@customElement('fb-console-input')
export class FbConsoleInput extends LitElement {
  createRenderRoot() { return this; }

  render() {
    return html`
      <div class="flex items-center px-3 py-1 font-mono text-xs leading-5">
        <span class="text-[#4fc3f7] select-none">&gt;&nbsp;</span>
        <input
          type="text"
          class="flex-1 bg-transparent text-[#4fc3f7] font-mono text-xs leading-5 outline-none placeholder:text-gray-200 caret-[#4fc3f7]"
          placeholder="Type JavaScript..."
        />
      </div>
    `;
  }
}
