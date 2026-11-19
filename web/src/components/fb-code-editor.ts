import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';
import { gotoFirstDiagnostic, initEditor } from '../js/editor.js';
import { on, emit } from '../js/app-state.js';
import type { EditorView } from 'codemirror';
import { getFontTable } from '../js/device-config.js';
import { familyName, fontLoader } from '../js/font-loader.js';

/**
 * font-family list of the device fonts that are loaded in the browser (the
 * faces of config.json "fonts"), or '' when there are none. The editor
 * draws icon glyphs with them (class cm-fb-glyph).
 */
export function deviceFontFamilies(): string {
  return getFontTable().faces
    .filter(f => f && fontLoader.isLoaded(f.name, f.src))
    // The fonts config.json adds (icon fonts) before the system text fonts.
    .sort((a, b) => Number(a.system) - Number(b.system))
    .map(f => `"${familyName(f.name)}"`)
    .join(', ');
}

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
      on('goto-diagnostic', () => { gotoFirstDiagnostic(); }),
      // A device font was loaded or dropped: the editor draws the icon glyphs
      // in config.json with them.
      on('preview-fonts-change', () => this._applyDeviceFonts()),
    );
    this._applyDeviceFonts();
  }

  private _applyDeviceFonts() {
    const families = deviceFontFamilies();
    if (families) this.style.setProperty('--fb-device-fonts', families);
    else this.style.removeProperty('--fb-device-fonts');
    // The glyph widths changed: let CodeMirror measure again.
    this._editor?.requestMeasure();
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
