import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';

@customElement('fb-right-pane')
export class FbRightPane extends LitElement {
  createRenderRoot() { return this; }

  render() {
    return html`
      <div class="flex flex-col h-full pr-4">
        <div class="shrink-0 border border-border-default rounded-2xl p-6 mt-[95px]">
          <fb-virtual-device></fb-virtual-device>
        </div>
        <fb-resize-handle class="h-[16px] cursor-row-resize shrink-0" data-direction="vertical"></fb-resize-handle>
        <fb-console-pane class="flex-1 min-h-[200px] flex flex-col border border-border-default rounded-2xl mb-0"></fb-console-pane>
      </div>
    `;
  }
}
