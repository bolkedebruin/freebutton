import { LitElement, html } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { EditorState } from '@codemirror/state';
import { EditorView, lineNumbers } from '@codemirror/view';
import { javascript } from '@codemirror/lang-javascript';
import { githubDark } from '@fsegurai/codemirror-theme-github-dark';
import { examples, type Example } from '../js/examples.js';

/**
 * Main-pane view of an example script: its title, what it shows, and its
 * code, read-only. Looking at an example creates no file and changes none;
 * the code can be selected and copied from here.
 */
@customElement('fb-example-view')
export class FbExampleView extends LitElement {
  createRenderRoot() { return this; }

  /** Example id; empty shows the first example. */
  @property() exampleId = '';
  private _view: EditorView | null = null;
  private _shown = '';

  private get _example(): Example | undefined {
    return examples.find(e => e.id === this.exampleId) ?? examples[0];
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._view?.destroy();
    this._view = null;
    this._shown = '';
  }

  protected updated() {
    const ex = this._example;
    const host = this.querySelector<HTMLElement>('[data-testid="example-code"]');
    if (!ex || !host || this._shown === ex.id) return;
    this._shown = ex.id;
    this._view?.destroy();
    this._view = new EditorView({
      parent: host,
      state: EditorState.create({
        doc: ex.code,
        extensions: [
          lineNumbers(), javascript(), githubDark,
          EditorState.readOnly.of(true), EditorView.editable.of(false),
          EditorView.theme({ '&': { height: '100%' }, '.cm-scroller': { overflow: 'auto' } }),
        ],
      }),
    });
  }

  render() {
    const ex = this._example;
    if (!ex) return html`<div class="p-6 text-xs text-gray-200" data-testid="example-view">No examples.</div>`;
    return html`
      <div class="h-full flex flex-col" data-testid="example-view" data-example-id=${ex.id}>
        <div class="px-6 py-4 border-b border-border-default">
          <h2 class="text-sm font-semibold text-text-default">${ex.title}</h2>
          <div class="text-[11px] text-gray-200 mt-0.5">${ex.description} · read-only</div>
        </div>
        <div class="flex-1 min-h-0 overflow-hidden" data-testid="example-code"></div>
      </div>
    `;
  }
}
