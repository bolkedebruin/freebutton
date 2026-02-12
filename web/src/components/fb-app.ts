import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';

@customElement('fb-app')
export class FbApp extends LitElement {
  createRenderRoot() { return this; }

  @state() private rightPaneWidth = 400;

  private _onHorizontalResize = (e: CustomEvent<{ delta: number }>) => {
    this.rightPaneWidth = Math.min(600, Math.max(380, this.rightPaneWidth - e.detail.delta));
  };

  render() {
    return html`
      <div class="flex h-screen w-screen overflow-hidden">
        <fb-navbar></fb-navbar>
        <div class="flex flex-1 min-w-0">
          <fb-editor-pane class="flex flex-col flex-1 min-w-0"></fb-editor-pane>
          <fb-resize-handle
            class="w-[16px] cursor-col-resize shrink-0"
            data-direction="horizontal"
            @resize=${this._onHorizontalResize}
          ></fb-resize-handle>
          <fb-right-pane
            class="flex flex-col shrink-0"
            style="width: ${this.rightPaneWidth}px; min-width: 380px; max-width: 600px;"
          ></fb-right-pane>
        </div>
      </div>
    `;
  }
}
