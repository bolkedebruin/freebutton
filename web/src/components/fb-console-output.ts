import { LitElement, html } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { on, type ConsoleId, type ConsoleLine, type LogLevel } from '../js/app-state';

const MAX_LINES = 1000;

const colorMap: Record<LogLevel, string> = {
  input: 'text-[#4fc3f7]',
  result: 'text-status-green',
  error: 'text-status-red',
  warn: 'text-status-orange',
  log: 'text-gray-100',
  info: 'text-[#4fc3f7]',
};

const READY: Record<ConsoleId, string> = {
  device: 'Console ready. Type JavaScript to evaluate on device.',
  virtual: 'Console of the virtual device: what the script running in this page prints, and its errors.',
};

/**
 * The output of one console (`channel`): the lines sent to it and no others.
 * It stays at the last line while it is there; scrolled up, it stays where
 * the reader put it.
 */
@customElement('fb-console-output')
export class FbConsoleOutput extends LitElement {
  createRenderRoot() { return this; }

  /** The console whose lines these are. */
  @property() channel: ConsoleId = 'device';
  @state() private _lines: ConsoleLine[] = [];
  private _unsubs: (() => void)[] = [];
  private _atEnd = true;
  private _resize?: ResizeObserver;

  connectedCallback() {
    super.connectedCallback();
    this._unsubs.push(
      on('console-line', (e) => {
        if (e.detail.to !== this.channel) return;
        const next = [...this._lines, e.detail];
        this._lines = next.length > MAX_LINES ? next.slice(next.length - MAX_LINES) : next;
      }),
      on('console-clear', (e) => {
        if (e.detail?.console === this.channel) this._lines = [];
      })
    );
    this.addEventListener('scroll', this._onScroll);
    if (typeof ResizeObserver === 'function') {
      this._resize = new ResizeObserver(this._stick);
      this._resize.observe(this);
    }
    // After the first update the channel attribute is known.
    void this.updateComplete.then(() => {
      if (this._lines.length === 0) this._lines = [{ text: READY[this.channel], type: 'info', to: this.channel }];
    });
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeEventListener('scroll', this._onScroll);
    this._resize?.disconnect();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  /**
   * Whether the reader is at the last line: decided when the reader scrolls,
   * not when the pane changes size (a smaller pane must not stop the output
   * from following).
   */
  private _onScroll = () => {
    this._atEnd = this.scrollHeight - this.scrollTop - this.clientHeight < 8;
  };

  private _stick = () => {
    if (this._atEnd) this.scrollTop = this.scrollHeight;
  };

  updated() {
    this._stick();
  }

  render() {
    return html`
      <div class="p-3 font-mono text-xs leading-5 bg-bg-default min-h-full" role="log" aria-label=${this.channel === 'virtual' ? 'Virtual device console output' : 'Device console output'}>
        ${this._lines.map(line => html`
          <div class="${colorMap[line.type] || 'text-gray-100'} whitespace-pre-wrap break-words" data-line=${line.type}>${line.type === 'input' ? '> ' : ''}${line.text}</div>
        `)}
      </div>
    `;
  }
}
