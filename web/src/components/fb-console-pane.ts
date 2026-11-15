import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { emit, on, type ConsoleId, type EmulatorSnapshot } from '../js/app-state';
import { emulatorSession } from '../js/emulator-session';
import { initReplBridge } from '../js/repl-bridge';
import { initDeviceInfoPolling, stopDeviceInfoPolling } from '../js/device-info';
import './fb-console-output.js';
import './fb-console-input.js';

type Tab = ConsoleId | 'mqtt';
/** A tab got something while another one was shown; 'error': an error among it. */
type Unread = 'new' | 'error';

const TAB = 'relative px-2.5 py-1 rounded-lg text-xs whitespace-nowrap transition-colors outline-none focus-visible:ring-2 focus-visible:ring-purple-80';
const TAB_LABEL: Record<Tab, string> = { device: 'Device', virtual: 'Virtual', mqtt: 'MQTT' };
const TAB_TITLE: Record<Tab, string> = {
  device: 'The real device: its REPL, and what the IDE reports about it and its files',
  virtual: 'The script running in the virtual device in this page: its output, its errors, lines you evaluate in it',
  mqtt: "The virtual device's MQTT (a broker in this page) and its sensors",
};
const STOPPED_NOTE = 'The script is stopped: Run to type here';

/**
 * The consoles. Two of them, sharing nothing:
 *
 *   Device   the real device's REPL (over its WebSocket) and what the IDE
 *            reports about the device and its files, as before there was
 *            a virtual device
 *   Virtual  the script running in the virtual device: print() output,
 *            errors, and the lines typed here (evaluated in the page)
 *
 * Each has its own output, input, input history, scroll position and Clear.
 * A line says which console it is for (ConsoleLine.to); nothing is shown in
 * both. A third tab, MQTT, has the virtual device's brokers and sensors.
 *
 * Virtual and MQTT appear at the first Run and stay: after Stop the output
 * can still be read (the input is off until the next Run, which adds a
 * separator line and keeps what was there). A dot on a tab: it got lines
 * while another tab was shown, red when an error is among them. Run shows
 * the Virtual tab, Deploy the Device tab.
 */
@customElement('fb-console-pane')
export class FbConsolePane extends LitElement {
  createRenderRoot() { return this; }

  @state() private _emu: EmulatorSnapshot = emulatorSession.snapshot();
  @state() private _tab: Tab = 'device';
  @state() private _unread: Partial<Record<Tab, Unread>> = {};
  private _offs: Array<() => void> = [];
  private _mqttSeen = 0;

  connectedCallback() {
    super.connectedCallback();
    initReplBridge();
    initDeviceInfoPolling();
    this._offs.push(
      on('emulator-change', (e) => {
        const was = this._emu.status;
        this._emu = e.detail;
        if (e.detail.status === 'starting' && was !== 'starting') {
          // Run: its output is what the user wants to see. The MQTT panel is
          // only needed from now on: it is not in the IDE's first load.
          void import('./fb-emulator-panel.js');
          this._show('virtual');
        }
      }),
      on('deploy-request', () => this._show('device')),
      on('console-line', (e) => {
        if (e.detail.to !== this._tab) this._mark(e.detail.to, e.detail.type === 'error' ? 'error' : 'new');
      }),
      on('emulator-mqtt', () => {
        const count = emulatorSession.mqttLog().length;
        if (this._tab !== 'mqtt' && count > this._mqttSeen) this._mark('mqtt', 'new');
        this._mqttSeen = count;
      }),
      // Ctrl/Cmd+L and the like: no console named = the one that is shown.
      on('console-clear', (e) => {
        if (!e.detail?.console && this._tab !== 'mqtt') emit('console-clear', { console: this._tab });
      }),
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._offs.forEach(off => off());
    this._offs = [];
    // Matches the init above: without this the module-level poller keeps its
    // window listener (and any interval) alive after the pane is gone.
    stopDeviceInfoPolling();
  }

  private _mark(tab: Tab, unread: Unread) {
    if (this._unread[tab] === 'error' || this._unread[tab] === unread) return;
    this._unread = { ...this._unread, [tab]: unread };
  }

  private _show(tab: Tab) {
    this._tab = tab;
    if (this._unread[tab]) {
      const { [tab]: _seen, ...rest } = this._unread;
      this._unread = rest;
    }
  }

  private _tabs(): Tab[] {
    return this._emu.used ? ['device', 'virtual', 'mqtt'] : ['device'];
  }

  /** Left / right move between the tabs, as in a tab list. */
  private _onTabKey(e: KeyboardEvent) {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    e.preventDefault();
    const tabs = this._tabs();
    const at = tabs.indexOf(this._tab);
    this._show(tabs[(at + (e.key === 'ArrowRight' ? 1 : tabs.length - 1)) % tabs.length]);
    void this.updateComplete.then(() => this.querySelector<HTMLElement>('[role="tab"][aria-selected="true"]')?.focus());
  }

  private _renderTab(tab: Tab) {
    const on = tab === this._tab;
    const unread = this._unread[tab];
    return html`
      <button role="tab" id="fb-tab-${tab}" aria-selected=${on ? 'true' : 'false'} aria-controls="fb-tabpanel-${tab}"
        tabindex=${on ? 0 : -1} data-testid="tab-${tab}" title=${TAB_TITLE[tab]}
        class="${TAB} ${on ? 'bg-white/10 text-text-default font-semibold' : 'text-gray-100 hover:text-text-default hover:bg-white/5'}"
        @click=${() => this._show(tab)}>${TAB_LABEL[tab]}${unread ? html`
          <span class="absolute top-0.5 right-0.5 w-1.5 h-1.5 rounded-full ${unread === 'error' ? 'bg-status-red' : 'bg-purple-80'}"
            data-unread=${unread} role="img" aria-label=${unread === 'error' ? 'new lines, with an error' : 'new lines'}></span>` : ''}</button>`;
  }

  /** One console: its own output and input, alive (and scrolled where it was) behind the other tabs. */
  private _renderConsole(id: ConsoleId) {
    const shown = this._tab === id;
    const stopped = id === 'virtual' && this._emu.status !== 'running';
    return html`
      <div class="absolute inset-0 flex flex-col ${shown ? '' : 'invisible'}" id="fb-tabpanel-${id}" data-console=${id}
        role=${this._emu.used ? 'tabpanel' : 'presentation'} aria-labelledby="fb-tab-${id}" ?inert=${!shown}>
        <fb-console-output class="flex-1 min-h-0 overflow-y-auto custom-scrollbar" channel=${id}></fb-console-output>
        <fb-console-input class="shrink-0" .target=${id} .disabled=${stopped} .note=${stopped ? STOPPED_NOTE : ''}></fb-console-input>
      </div>`;
  }

  render() {
    const tabs = this._tabs();
    const tab = tabs.includes(this._tab) ? this._tab : 'device';
    return html`
      <div class="flex flex-col h-full overflow-hidden rounded-2xl">
        <!-- Header -->
        <div class="flex items-center gap-2 px-3 py-2 bg-glass-dark backdrop-blur-md shrink-0">
          ${tabs.length > 1 ? html`
            <div class="flex items-center gap-0.5" role="tablist" aria-label="Consoles" @keydown=${this._onTabKey}>
              ${tabs.map(t => this._renderTab(t))}
            </div>
          ` : html`<span class="font-dm font-semibold text-sm text-text-default px-1">Console</span>`}
          <span class="flex-1"></span>
          ${tab !== 'mqtt' ? html`
            <button @click=${() => emit('console-clear', { console: tab })} data-testid="console-clear"
              title=${tabs.length > 1 ? `Clear the ${TAB_LABEL[tab]} console` : 'Clear the console'}
              class="bg-white/5 hover:bg-white/10 border border-white/10 text-text-default rounded-full px-3 py-1 text-xs transition-colors duration-200">Clear</button>
          ` : ''}
        </div>
        <div class="relative flex-1 min-h-0">
          ${this._renderConsole('device')}
          ${this._emu.used ? this._renderConsole('virtual') : ''}
          ${tab === 'mqtt' ? html`
            <fb-emulator-panel class="absolute inset-0 overflow-y-auto custom-scrollbar" id="fb-tabpanel-mqtt"
              role="tabpanel" aria-labelledby="fb-tab-mqtt"></fb-emulator-panel>
          ` : ''}
        </div>
      </div>
    `;
  }
}
