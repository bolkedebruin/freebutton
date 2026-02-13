import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on, type ConsoleLine, type LogLevel } from '../js/app-state';

const colorMap: Record<LogLevel, string> = {
  input: 'text-[#4fc3f7]',
  result: 'text-status-green',
  error: 'text-status-red',
  warn: 'text-status-orange',
  log: 'text-gray-100',
  info: 'text-[#4fc3f7]',
};

@customElement('fb-console-output')
export class FbConsoleOutput extends LitElement {
  createRenderRoot() { return this; }

  @state() private _lines: ConsoleLine[] = [];
  private _unsubs: (() => void)[] = [];

  connectedCallback() {
    super.connectedCallback();
    this._unsubs.push(
      on('console-line', (e) => {
        this._lines = [...this._lines, e.detail];
      }),
      on('console-clear', () => {
        this._lines = [];
      })
    );
    // Welcome message
    this._lines = [{ text: 'Console ready. Type JavaScript to evaluate on device.', type: 'info' }];
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  updated() {
    this.scrollTop = this.scrollHeight;
  }

  render() {
    return html`
      <div class="p-3 font-mono text-xs leading-5 bg-bg-default">
        ${this._lines.map(line => html`
          <div class="${colorMap[line.type] || 'text-gray-100'}">${line.type === 'input' ? '> ' : ''}${line.text}</div>
        `)}
      </div>
    `;
  }
}
