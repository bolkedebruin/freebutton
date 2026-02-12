import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';
import './fb-console-output.js';
import './fb-console-input.js';

@customElement('fb-console-pane')
export class FbConsolePane extends LitElement {
  createRenderRoot() { return this; }

  render() {
    return html`
      <div class="flex flex-col h-full overflow-hidden rounded-2xl">
        <!-- Header -->
        <div class="flex items-center justify-between px-4 py-2 bg-glass-dark backdrop-blur-md shrink-0">
          <span class="font-dm font-semibold text-sm text-text-default">Console</span>
          <button class="bg-white/5 hover:bg-white/10 border border-white/10 text-text-default rounded-full px-3 py-1 text-xs transition-colors duration-200">Clear</button>
        </div>
        <!-- Content -->
        <fb-console-output class="flex-1 min-h-0 overflow-y-auto custom-scrollbar"></fb-console-output>
        <fb-console-input class="shrink-0"></fb-console-input>
      </div>
    `;
  }
}
