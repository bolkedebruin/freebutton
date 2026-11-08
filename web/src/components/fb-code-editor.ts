import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';
import { initEditor, loadExample } from '../js/editor.js';
import { on, emit } from '../js/app-state.js';
import type { EditorView } from 'codemirror';

@customElement('fb-code-editor')
export class FbCodeEditor extends LitElement {
  createRenderRoot() { return this; }

  private _editor: EditorView | null = null;
  private _unsubs: Array<() => void> = [];

  render() {
    return html`<div id="cm-editor" class="h-full bg-black/70"></div>`;
  }

  protected firstUpdated() {
    const container = this.querySelector('#cm-editor');
    if (container) {
      this._editor = initEditor(container as HTMLElement);
      // The workspace waits for this to load scripts/main.js into the editor.
      emit('editor-ready');
    }

    this._unsubs.push(
      on('load-example', (e) => {
        loadExample(e.detail.name);
      })
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
    if (this._editor) {
      this._editor.destroy();
      this._editor = null;
    }
  }
}
