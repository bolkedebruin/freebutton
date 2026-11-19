import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';

@customElement('fb-right-pane')
export class FbRightPane extends LitElement {
  createRenderRoot() { return this; }

  render() {
    return html`
      <div class="flex flex-col h-full pr-4">
        <!-- 12 px at the sides: the face (display-geometry.ts FACE_W, the main display a CSS px per device pixel) fits the pane at its default width (fb-app.ts). -->
        <!-- In a low window the space above the device goes first. -->
        <!-- In a window too low for the device and the console's least height, the device gives way and scrolls: the console stays on screen. -->
        <div class="shrink min-h-[160px] overflow-y-auto custom-scrollbar border border-border-default rounded-2xl px-3 py-6 mt-[95px] [@media(max-height:820px)]:mt-4 [@media(max-height:820px)]:py-4" data-testid="device-box">
          <fb-virtual-device></fb-virtual-device>
        </div>
        <fb-resize-handle class="h-[16px] cursor-row-resize shrink-0" data-direction="vertical"></fb-resize-handle>
        <fb-console-pane class="flex-1 min-h-[120px] flex flex-col border border-border-default rounded-2xl mb-0"></fb-console-pane>
      </div>
    `;
  }
}
