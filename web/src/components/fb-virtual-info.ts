import { LitElement, html } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { on } from '../js/app-state';
import { emulatorSession, type BrokerSettings, type BrokerStatus, type MqttStatus, type VirtualInfo } from '../js/emulator-session';
import { CONFIG_KEY, MAIN_SCRIPT_KEY } from '../js/fs-rules';
import { workspace } from '../js/workspace';

/** The values are measured this often while the info side is shown. */
export const INFO_INTERVAL_MS = 1000;

const MQTT_WORDS = { offline: 'offline: in-page broker', 'live-readonly': 'live, read-only', 'live-publish': 'live, publishing' } as const;
const LINK_WORDS = { none: 'not connected', connecting: 'connecting', connected: 'connected', error: 'not connected' } as const;
const CONNECTOR = ['none', 'bar', 'display'] as const;

const HEADING = 'text-[10px] uppercase tracking-wider text-gray-100';
const INPUT = 'w-full min-w-0 bg-white/5 border border-white/10 rounded-md px-2 py-1 text-xs text-text-default font-mono outline-none focus:border-purple-80 disabled:opacity-40';
const BUTTON = 'shrink-0 px-2 py-1 rounded-md text-[11px] bg-white/5 border border-white/10 text-text-default hover:bg-white/10 transition-colors '
  + 'outline-none focus-visible:ring-2 focus-visible:ring-purple-80 disabled:opacity-40 disabled:hover:bg-white/5 cursor-pointer disabled:cursor-default';
const NO_SETTINGS: BrokerSettings = { url: '', username: '', password: '' };

/** The port of a WebSocket address, for the listener example; 9001 when it has none. */
function portOf(url: string): string {
  try { return new URL(url).port || '9001'; } catch { return '9001'; }
}
const CONNECTOR_LONG = ['none', 'bar (two keys)', 'main display'] as const;

/** "42 s", "3 min 05 s", "2 h 07 min": the seconds show that it runs. */
export function formatRunTime(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  const two = (n: number) => String(n).padStart(2, '0');
  if (s < 60) return `${s} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min ${two(s % 60)} s`;
  return `${Math.floor(s / 3600)} h ${two(Math.floor((s % 3600) / 60))} min`;
}

function kbNumber(bytes: number): string {
  const k = (bytes / 1024).toFixed(1);
  return k.endsWith('.0') ? k.slice(0, -2) : k;
}

function kb(bytes: number): string {
  return kbNumber(bytes) + ' KB';
}

/**
 * The info side of the virtual device: what the emulator in this page is
 * and does (emulatorSession.info()). Nothing here is the real device's and
 * nothing is asked of it; the real device has the Device view.
 *
 * Below the values: the MQTT settings of the virtual device. The mode (live
 * publish / live read-only / offline), per broker of config.json the
 * WebSocket address, user name and password (config.json's unless this
 * browser keeps others for the broker's host) with the state of its
 * connection, and what live publish may send besides the script's own
 * publishes. All of it can be set while nothing runs: it holds for the
 * next Run. The brokers listed are the running config's, else config.json's
 * as the workspace has it.
 *
 * Measured when `active` (the side is shown), then once a second.
 */
@customElement('fb-virtual-info')
export class FbVirtualInfo extends LitElement {
  createRenderRoot() { return this; }

  /** The side is shown: measure. */
  @property({ type: Boolean }) active = false;
  @state() private _info: VirtualInfo = emulatorSession.info();
  /** What is typed in a broker's form and not saved yet, per broker host. */
  @state() private _drafts = new Map<string, BrokerSettings>();
  /** Broker hosts whose password is shown as text. */
  @state() private _shown = new Set<string>();
  /** Why something asked for in the MQTT settings was not done. */
  @state() private _mqttMessage = '';
  private _timer?: ReturnType<typeof setInterval>;
  private _offs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._offs.push(
      on('emulator-change', () => { if (this.active) this._measure(); }),
      on('emulator-face', () => { if (this.active) this._measure(); }),
      on('workspace-change', () => { if (this.active) this.requestUpdate(); }),
      // The brokers listed are config.json's; their settings are the browser's.
      on('device-config', () => { if (this.active) this.requestUpdate(); }),
      on('virtual-broker-change', () => this.requestUpdate()),
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._offs.forEach(off => off());
    this._offs = [];
    this._stop();
  }

  protected willUpdate(changed: Map<PropertyKey, unknown>) {
    if (!changed.has('active')) return;
    this._stop();
    if (this.active) {
      this._info = emulatorSession.info();
      this._timer = setInterval(() => this._measure(), INFO_INTERVAL_MS);
    }
  }

  private _measure() {
    this._info = emulatorSession.info();
  }

  private _stop() {
    if (this._timer) clearInterval(this._timer);
    this._timer = undefined;
  }

  /** What Run takes for a file: the editor's text when it is the open file, else the stored one. */
  private _source(key: string): string {
    const ws = workspace.snapshot();
    if (ws.currentKey === key) return 'the editor';
    return ws.files.some(f => f.key === key) ? 'the stored file' : 'missing';
  }

  /** One line: a label and its value; a long value is cut short, with the whole of it as tooltip. */
  private _row(label: string, value: unknown, title = '') {
    return html`
      <div class="flex justify-between gap-3 items-baseline min-w-0">
        <span class="text-[11px] text-gray-200 shrink-0">${label}</span>
        <span class="text-[11px] text-gray-100 font-mono text-right truncate min-w-0" title=${title}>${value}</span>
      </div>`;
  }

  /** used of limit: label and value on one line, a thin bar under it (red when nearly full). Two share a row. */
  private _meter(label: string, text: string, used: number, limit: number, field: string, title: string) {
    const part = limit > 0 ? Math.min(1, Math.max(0, used / limit)) : 0;
    const tight = part > 0.9;
    return html`
      <div class="flex flex-col gap-[3px] min-w-0" title=${title}>
        <div class="flex justify-between gap-2 items-baseline min-w-0">
          <span class="text-[11px] text-gray-200 truncate">${label}</span>
          <span class="text-[11px] font-mono shrink-0 ${tight ? 'text-status-red' : 'text-gray-100'}" data-field=${field}>${text}</span>
        </div>
        <div class="h-[3px] rounded-full bg-white/10 overflow-hidden" role="progressbar" aria-label=${label}
          aria-valuemin="0" aria-valuemax=${limit} aria-valuenow=${used}>
          <div class="h-full rounded-full ${tight ? 'bg-status-red' : 'bg-purple-80'}" style="width: ${(part * 100).toFixed(1)}%;"></div>
        </div>
      </div>`;
  }

  /**
   * Does what is shown fit the device: LVGL's memory pool (the device's
   * size) as the firmware in the page has it, and the texts it could not
   * draw in full because that pool was low. Shown whenever the firmware
   * draws the displays, running or not.
   */
  private _renderFit(i: VirtualInfo) {
    if (!i.lvgl) return '';
    const dropped = i.textSkipped + i.textCollapsed > 0;
    return html`
      ${this._meter('Display memory free', `${kbNumber(i.lvgl.free)} / ${kb(i.lvgl.total)} · lowest ${kb(i.lvgl.lowest)}`,
        i.lvgl.total - i.lvgl.free, i.lvgl.total, 'lvgl',
        'LVGL\'s memory pool, the size the device has: what the displays shown here leave free, and the least that was free. The bar is what is used.')}
      ${this._row('Texts not drawn in full', html`<span class=${dropped ? 'text-status-orange' : ''} data-field="text-dropped"
        >${i.textSkipped} skipped · ${i.textCollapsed} without formats</span>`,
        'Texts the firmware left out, or drew in one format, because the display memory was too low')}
      ${dropped ? html`<p class="text-[10px] leading-snug text-status-orange" data-field="text-dropped-note">The device would drop text here: its display memory is too low for this page.</p>` : ''}
    `;
  }

  // ---- MQTT settings --------------------------------------------------------

  private _setMode(mode: MqttStatus['mode']) {
    this._mqttMessage = emulatorSession.setMqttMode(mode);
    this._measure();
  }

  /** What the form of a broker host shows: what is typed, else what the browser keeps. */
  private _formValues(host: string): BrokerSettings {
    return this._drafts.get(host) ?? emulatorSession.brokerSettings(host) ?? NO_SETTINGS;
  }

  private _type(host: string, field: keyof BrokerSettings, value: string) {
    const next = new Map(this._drafts);
    next.set(host, { ...this._formValues(host), [field]: value });
    this._drafts = next;
  }

  private _forget(host: string) {
    const next = new Map(this._drafts);
    next.delete(host);
    this._drafts = next;
  }

  private _save(e: Event, b: BrokerStatus) {
    e.preventDefault();
    const values = this._formValues(b.host);
    this._mqttMessage = emulatorSession.saveBrokerSettings(b.host, { url: values.url.trim(), username: values.username.trim(), password: values.password });
    if (!this._mqttMessage) this._forget(b.host);
  }

  private _reset(b: BrokerStatus) {
    emulatorSession.resetBrokerSettings(b.host);
    this._forget(b.host);
    this._mqttMessage = '';
  }

  private _togglePassword(host: string) {
    const next = new Set(this._shown);
    if (!next.delete(host)) next.add(host);
    this._shown = next;
  }

  /** Why a broker is not connected, with what to do about it. */
  private _renderLinkError(b: BrokerStatus) {
    const e = b.error;
    if (!e) return '';
    return html`
      <div class="text-[11px] leading-snug text-status-red break-words" role="alert" data-testid="mqtt-link-error" data-error=${e.kind}>
        ${e.message}
        ${e.kind === 'unreachable' || e.kind === 'not-mqtt' ? html`
          <span class="block mt-1 text-gray-100">Mosquitto needs a websockets listener (mosquitto.conf), and the address above its port as this computer reaches it. It is tried again until it connects.</span>
          <pre class="mt-1 px-2 py-1 rounded bg-white/5 font-mono text-[11px] text-text-default" data-testid="mqtt-listener-help">listener ${portOf(b.wsUrl)}
protocol websockets</pre>` : ''}
        ${e.kind === 'auth' ? html`<span class="block mt-1 text-gray-100">Correct the user name or the password above and Save: it connects again.</span>` : ''}
        ${e.kind === 'taken-over' ? html`<span class="block mt-1 text-gray-100">Stop the virtual device in the other tab or window.</span>` : ''}
      </div>`;
  }

  /** One broker of config.json: its connection, and the address, user name and password to connect with. */
  private _renderBroker(b: BrokerStatus, status: MqttStatus, running: boolean, identity: string) {
    const values = this._formValues(b.host);
    const dirty = this._drafts.has(b.host);
    const shown = this._shown.has(b.host);
    const offline = status.mode === 'offline';
    const state = offline ? 'offline' : !running ? 'idle' : b.state;
    const chip = offline ? html`<span class="text-gray-100">offline</span>`
      : !running ? html`<span class="text-gray-100">connects at Run</span>`
        : b.state === 'connected' ? html`<span class="text-status-green">connected</span>`
          : b.error && b.state === 'disconnected' ? html`<span class="text-status-red">not connected</span>`
            : html`<span class="text-gray-100">connecting…</span>`;
    const field = (label: string, control: unknown) => html`
      <label class="grid grid-cols-[104px_1fr] items-center gap-2 text-[11px] text-gray-200">${label}${control}</label>`;
    return html`
      <form class="flex flex-col gap-1.5 p-2 rounded-lg bg-white/[0.03] border border-white/10" autocomplete="off" data-testid="mqtt-broker-form"
        data-broker=${b.index} data-host=${b.host} @submit=${(e: Event) => this._save(e, b)}>
        <div class="flex items-baseline gap-2 min-w-0 text-[11px]">
          <span class="font-mono text-text-default truncate" title="config.json broker ${b.name}" data-testid="mqtt-broker-name">${b.name}</span>
          <span class="text-gray-200 truncate min-w-0" title="Host of the broker in config.json">${b.host || 'no url in config.json'}</span>
          <span class="ml-auto shrink-0" data-testid="mqtt-link-state" data-state=${state}>${chip}</span>
        </div>
        ${field('WebSocket address', html`<input class=${INPUT} name="mqtt-ws" type="text" spellcheck="false" autocomplete="off" data-testid="mqtt-ws-url"
          aria-label="WebSocket address of broker ${b.name}" ?disabled=${!b.host}
          title="The broker's websockets listener, as this computer reaches it. Empty: ${b.defaultWsUrl || 'none'} (from config.json: its &quot;wsport&quot;, else 9001)"
          placeholder=${b.defaultWsUrl} .value=${values.url}
          @input=${(e: Event) => this._type(b.host, 'url', (e.target as HTMLInputElement).value)} />`)}
        ${field('User name', html`<input class=${INPUT} name="mqtt-user" type="text" spellcheck="false" autocomplete="off" data-testid="mqtt-user"
          aria-label="User name for broker ${b.name}" ?disabled=${!b.host}
          title="Empty: config.json's" placeholder=${b.defaultUsername || 'none'} .value=${values.username}
          @input=${(e: Event) => this._type(b.host, 'username', (e.target as HTMLInputElement).value)} />`)}
        ${field('Password', html`<span class="flex items-center gap-1 min-w-0">
          <input class=${INPUT} name="mqtt-pass" type=${shown ? 'text' : 'password'} spellcheck="false" autocomplete="new-password" data-testid="mqtt-pass"
            aria-label="Password for broker ${b.name}" ?disabled=${!b.host}
            title="Empty: config.json's" placeholder=${b.configPassword ? 'from config.json' : 'none'} .value=${values.password}
            @input=${(e: Event) => this._type(b.host, 'password', (e.target as HTMLInputElement).value)} />
          <button type="button" class=${BUTTON} data-testid="mqtt-pass-show" aria-pressed=${shown ? 'true' : 'false'}
            @click=${() => this._togglePassword(b.host)}>${shown ? 'Hide' : 'Show'}</button></span>`)}
        <div class="flex items-center gap-1.5">
          <button type="submit" class="${BUTTON} ${dirty ? 'bg-purple-90 border-transparent hover:bg-purple-active text-white' : ''}" data-testid="mqtt-save"
            ?disabled=${!dirty || !b.host} title="Keep these for ${b.host} in this browser${running ? ' and connect with them' : ''}">Save</button>
          <button type="button" class=${BUTTON} data-testid="mqtt-reset" ?disabled=${!b.overridden && !dirty}
            title="Forget what this browser keeps for ${b.host}: config.json's values again" @click=${() => this._reset(b)}>Reset to default</button>
          <span class="ml-auto text-[10px] text-gray-200" data-testid="mqtt-broker-source">${b.overridden ? 'kept by this browser' : 'from config.json'}</span>
        </div>
        ${running && !offline ? this._renderLinkError(b) : ''}
        ${b.configClientId ? html`<p class="text-[10px] leading-snug text-gray-100" data-testid="mqtt-client-id-note">config.json gives this broker the client id
          <span class="font-mono">${b.configClientId}</span>: not used here. The virtual device always connects as <span class="font-mono">${identity}</span>.</p>` : ''}
      </form>`;
  }

  /**
   * Where the virtual device's MQTT goes and what it may send. Settable
   * while nothing runs: it holds for the next Run.
   */
  private _renderMqtt(i: VirtualInfo) {
    const status = emulatorSession.mqttStatus();
    const mode = status.mode;
    const running = i.status === 'running';
    const option = (value: MqttStatus['mode'], label: string, title: string) => {
      const on = mode === value;
      const color = !on ? 'text-gray-100 hover:text-text-default'
        : 'bg-purple-90 text-white';
      return html`<button type="button" role="radio" aria-checked=${on ? 'true' : 'false'} data-mode=${value} title=${title}
        class="px-2 py-0.5 rounded-md text-[11px] whitespace-nowrap transition-colors cursor-pointer outline-none focus-visible:ring-2 focus-visible:ring-purple-80 ${color}"
        @click=${() => this._setMode(value)}>${label}</button>`;
    };
    const words = mode === 'offline' ? html`The broker in this page: nothing leaves it.`
      : mode === 'live-readonly' ? html`Connects to the broker of config.json as <span class="font-mono">${i.identity}</span>. It subscribes; nothing is published (publishes are listed as WOULD).`
        : html`The virtual device publishes to the broker of config.json as <span class="font-mono">${i.identity}</span>.`;
    const failed = running && mode !== 'offline' && status.brokers.some(b => b.error && b.state === 'disconnected');
    return html`
      <section class="flex flex-col gap-2 pt-1" aria-label="MQTT settings" data-testid="mqtt-settings-section">
        <div class="flex items-center gap-2 flex-wrap" data-testid="mqtt-mode" data-mode=${mode}>
          <h3 class=${HEADING}>MQTT</h3>
          <div class="flex rounded-lg p-0.5 bg-white/5 border border-white/10" role="radiogroup" aria-label="MQTT mode">
            ${option('live-publish', 'Live publish', 'Connect to the broker of config.json and publish what the script publishes. The mode of a Run unless you choose another.')}
            ${option('live-readonly', 'Live read-only', 'Connect to the broker of config.json and listen. Nothing is published.')}
            ${option('offline', 'Offline', 'The broker in this page: nothing leaves it.')}
          </div>
        </div>
        <p class="text-[11px] leading-snug text-gray-100" data-testid="mqtt-mode-words">${words}${running ? '' : html` <span class="text-gray-200">Used at the next Run; this browser remembers it.</span>`}</p>
        ${status.securePage && mode !== 'offline' ? html`<p class="text-[11px] leading-snug text-status-orange" data-testid="mqtt-secure-page">This page was loaded over https: the browser blocks ws:// addresses. Open the IDE over http, or use a wss:// address.</p>` : ''}
        ${status.brokers.length === 0 ? html`<p class="text-[11px] leading-snug text-gray-100" data-testid="mqtt-no-broker">config.json has no "brokers": there is nothing to connect to, and MQTT stays in this page.</p>` : ''}
        ${status.brokers.map(b => this._renderBroker(b, status, running, i.identity))}
        ${failed ? html`
          <div class="flex items-center gap-2 text-[11px] text-gray-100">
            <span class="min-w-0">The connection is tried again until it is there. Nothing falls back to the broker in this page by itself.</span>
            <button type="button" class=${BUTTON} data-testid="mqtt-use-offline" title="Switch to offline: the broker in this page. This browser remembers it."
              @click=${() => this._setMode('offline')}>Use offline</button>
          </div>` : ''}
        ${this._mqttMessage ? html`<p class="text-[11px] text-status-orange" role="status" data-testid="mqtt-settings-message">${this._mqttMessage}</p>` : ''}
        ${mode === 'live-publish' ? html`
          <div class="flex flex-col gap-1 pl-2 border-l-2 border-white/15" data-testid="mqtt-optins">
            <p class="text-[10px] leading-snug text-gray-100">Sent: what the script publishes and a key's own press topics. Not sent unless switched on here: Home Assistant discovery, the retain flag, the device's own states. A topic of a real device (btn_…) asks first.</p>
            <label class="flex items-start gap-1.5 text-[11px]">
              <input type="checkbox" class="mt-0.5" data-testid="mqtt-announce" .checked=${status.announce}
                @change=${(e: Event) => { emulatorSession.setAnnounce((e.target as HTMLInputElement).checked); this._measure(); }} />
              <span>Send Home Assistant discovery <span class="block text-[10px] leading-snug text-gray-100">Never sent by itself, also not with "hassdiscovery" in config.json. Discovery and "online" make entities for the virtual device in Home Assistant, with a last will. Reconnects. Remove them afterwards.</span></span>
            </label>
            <label class="flex items-start gap-1.5 text-[11px]">
              <input type="checkbox" class="mt-0.5" data-testid="mqtt-send-retained" .checked=${status.sendRetained}
                @change=${(e: Event) => { emulatorSession.setSendRetained((e.target as HTMLInputElement).checked); this._measure(); }} />
              <span>Send retained <span class="block text-[10px] leading-snug text-gray-100">A retained message stays on the broker, for a device that does not exist.</span></span>
            </label>
            <label class="flex items-start gap-1.5 text-[11px]">
              <input type="checkbox" class="mt-0.5" data-testid="mqtt-own-states" .checked=${status.ownStates}
                @change=${(e: Event) => { emulatorSession.setOwnStates((e.target as HTMLInputElement).checked); this._measure(); }} />
              <span>Publish the virtual device's own states <span class="block text-[10px] leading-snug text-gray-100">Its sensors report about eight messages a second under its own id: harmless, and noisy.</span></span>
            </label>
            ${status.announced > 0 ? html`
              <button type="button" class="${BUTTON} self-start" data-testid="mqtt-remove-announcement"
                title="Switch discovery off and publish an empty retained message to each announcement topic sent in this session: Home Assistant removes the entities"
                @click=${() => { this._mqttMessage = emulatorSession.removeAnnouncement(); this._measure(); }}>Remove announcement (${status.announced})</button>` : ''}
          </div>` : ''}
      </section>`;
  }

  render() {
    const i = this._info;
    const running = i.status === 'running';
    const state = i.status === 'running' ? 'running' : i.status === 'starting' ? 'starting' : i.status === 'error' ? 'stopped with an error' : 'not running';
    const stateColor = running ? 'text-status-green' : i.status === 'error' ? 'text-status-red' : 'text-gray-100';
    const modules = i.connectors ? i.connectors.map((kind, id) => (kind === 0 ? '' : `${id}: ${CONNECTOR[kind]}`)).filter(Boolean) : [];
    const brokers = i.brokers ? (i.brokers.length ? i.brokers.join(', ') : 'none in config.json') : '';
    return html`
      <div class="flex flex-col gap-1.5 min-w-0" data-testid="virtual-info" data-status=${i.status}>
        ${this._row('Device', html`<span data-field="identity">${i.identity}</span>`, 'system.id() in the script: this browser\'s virtual device, never the real one')}
        ${this._row('State', html`<span class=${stateColor} data-field="state">${state}</span>${running && i.uptimeSeconds !== null
          ? html`<span class="text-gray-200"> · </span><span data-field="uptime">${formatRunTime(i.uptimeSeconds)}</span>` : ''}`,
          running ? 'The script, and how long it has run' : '')}
        ${i.error ? html`<p class="text-[11px] leading-snug text-status-red break-words line-clamp-2" data-field="error" title=${i.error}>${i.error}</p>` : ''}

        ${running ? html`
          <div class="grid grid-cols-2 gap-x-4 gap-y-1.5 pt-0.5">
            ${i.heap ? this._meter('Heap free', `${kbNumber(i.heap.free)} / ${kb(i.heap.size)}`, i.heap.size - i.heap.free, i.heap.size, 'heap',
              'JavaScript heap of the script: free of the device\'s, after a garbage collection. The bar is what is used.') : ''}
            ${i.bytecode ? this._meter('Bytecode', `${kbNumber(i.bytecode.bytes)} / ${kb(i.bytecode.limit)}`, i.bytecode.bytes, i.bytecode.limit, 'bytecode',
              'Size of the running script\'s bytecode, of what the device stores') : ''}
            ${i.timers ? this._meter('Timers', `${i.timers.used} / ${i.timers.limit}`, i.timers.used, i.timers.limit, 'timers',
              'Pending setTimeout timers, of the firmware\'s slots') : ''}
            ${i.subscriptions && i.subscriptions.limit > 0
              ? this._meter('Subscriptions', `${i.subscriptions.used} / ${i.subscriptions.limit}`, i.subscriptions.used, i.subscriptions.limit, 'subscriptions',
                'MQTT subscriptions of the script, of the firmware\'s room for them') : ''}
          </div>
          ${i.mqttDropped ? this._row('MQTT events lost', html`<span class="text-status-red" data-field="dropped">${i.mqttDropped}</span>`,
            'Events lost because the script did not keep up') : ''}
        ` : html`
          <p class="text-[11px] leading-snug text-gray-100" data-field="hint">Run (in the toolbar) starts the script in this page, with:</p>
          ${this._row(`/${MAIN_SCRIPT_KEY}`, html`<span data-field="source-script">${this._source(MAIN_SCRIPT_KEY)}</span>`)}
          ${this._row(`/${CONFIG_KEY}`, html`<span data-field="source-config">${this._source(CONFIG_KEY)}</span>`)}
        `}

        ${this._renderFit(i)}
        <div class="border-t border-white/10"></div>
        ${this._row('MQTT', html`<span data-field="mqtt">${MQTT_WORDS[i.mqtt]}${i.mqtt === 'offline' ? '' : ` (${running ? LINK_WORDS[i.mqttLink] : 'connects at Run'})`}</span>${brokers
          ? html`<span class="text-gray-200"> · </span><span data-field="brokers">${brokers}</span>` : ''}`,
          `${i.mqtt === 'live-publish' ? 'The virtual device publishes to the real broker.' : 'Nothing is sent to a real broker.'}${brokers ? ' Brokers of config.json: ' + brokers : ''} The settings are below.`)}
        ${modules.length ? this._row('Modules', html`<span data-field="modules">${modules.join(' · ')}</span>`, `Module per connector: ${i.connectors!.map((kind, id) => (kind === 0 ? '' : `${id}: ${CONNECTOR_LONG[kind]}`)).filter(Boolean).join(', ')}`) : ''}
        ${i.sensors !== null ? this._row('Sensors', html`<span data-field="sensors">${i.sensors}</span>`) : ''}
        ${i.timezone ? this._row('Time zone', html`<span data-field="timezone">${i.timezone}</span>`, `The device's local time follows this POSIX TZ rule (config.json "core.timezone"): ${i.timezone}`) : ''}
        ${i.model ? this._row('Model', html`<span data-field="model">${i.model}</span>`, i.model) : ''}
        ${i.firmware ? this._row('Engine built from', html`<span data-field="firmware">${i.firmware}</span>`, `The firmware the script engine in this page was built from: ${i.firmware}`) : ''}
        <p class="text-[10px] leading-snug text-gray-200">The real device is in the Device view (left bar).</p>
        <div class="border-t border-white/10"></div>
        ${this._renderMqtt(i)}
      </div>
    `;
  }
}
