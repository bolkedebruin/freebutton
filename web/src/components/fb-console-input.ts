import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';
import { emit } from '../js/app-state';
import { isIncompleteCode } from '../js/console';
import { initConsoleAutocomplete, isAutocompleteVisible } from '../js/console-autocomplete';

@customElement('fb-console-input')
export class FbConsoleInput extends LitElement {
  createRenderRoot() { return this; }

  private _history: string[] = [];
  private _historyIndex = -1;
  private _autocompleteInitialized = false;

  private get _textarea(): HTMLTextAreaElement | null {
    return this.querySelector('textarea');
  }

  updated() {
    if (!this._autocompleteInitialized) {
      const textarea = this._textarea;
      if (textarea) {
        initConsoleAutocomplete(textarea);
        this._autocompleteInitialized = true;
      }
    }
  }

  private _handleKeydown = (e: KeyboardEvent) => {
    const textarea = this._textarea;
    if (!textarea) return;

    if (isAutocompleteVisible()) return;

    switch (e.key) {
      case 'Enter':
        if (e.shiftKey) return;
        e.preventDefault();
        {
          const code = textarea.value;
          if (!code.trim()) return;

          if (isIncompleteCode(code)) {
            const start = textarea.selectionStart || 0;
            const end = textarea.selectionEnd || 0;
            textarea.value = code.substring(0, start) + '\n' + code.substring(end);
            textarea.selectionStart = textarea.selectionEnd = start + 1;
          } else {
            this._execute(code);
          }
        }
        break;

      case 'ArrowUp':
        if (textarea.selectionStart === 0 && textarea.selectionEnd === 0) {
          e.preventDefault();
          this._navigateHistory(1);
        }
        break;

      case 'ArrowDown':
        if (textarea.selectionStart === textarea.value.length && textarea.selectionEnd === textarea.value.length) {
          e.preventDefault();
          this._navigateHistory(-1);
        }
        break;

      case 'Escape':
        textarea.value = '';
        this._historyIndex = -1;
        break;
    }
  };

  private _execute(code: string) {
    if (this._history[0] !== code) {
      this._history.unshift(code);
      if (this._history.length > 50) this._history.pop();
    }
    this._historyIndex = -1;

    emit('console-line', { text: code, type: 'input' });
    emit('console-eval', { code });

    const textarea = this._textarea;
    if (textarea) {
      textarea.value = '';
    }
  }

  private _navigateHistory(direction: number) {
    const textarea = this._textarea;
    if (!textarea || this._history.length === 0) return;

    this._historyIndex += direction;
    if (this._historyIndex < -1) this._historyIndex = -1;
    else if (this._historyIndex >= this._history.length) this._historyIndex = this._history.length - 1;

    textarea.value = this._historyIndex === -1 ? '' : this._history[this._historyIndex] || '';
    textarea.setSelectionRange(textarea.value.length, textarea.value.length);
  }

  render() {
    return html`
      <div class="flex items-start px-3 py-1 font-mono text-xs leading-5">
        <span class="text-[#4fc3f7] select-none">&gt;&nbsp;</span>
        <textarea
          rows="1"
          class="flex-1 bg-transparent text-[#4fc3f7] font-mono text-xs leading-5 outline-none placeholder:text-gray-200 caret-[#4fc3f7] resize-none max-h-[150px]"
          style="field-sizing: content"
          placeholder="Type JavaScript..."
          @keydown=${this._handleKeydown}
        ></textarea>
      </div>
    `;
  }
}
