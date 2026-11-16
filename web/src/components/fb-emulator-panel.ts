import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on } from '../js/app-state';
import { emulatorSession, type BrokerStatus, type MqttLogEntry, type MqttStatus } from '../js/emulator-session';

const INPUT = 'min-w-0 bg-white/5 border border-white/10 rounded-md px-2 py-1 text-xs text-text-default font-mono outline-none focus:border-purple-80';
const BUTTON = 'shrink-0 px-2 py-1 rounded-md text-[11px] bg-white/5 border border-white/10 text-text-default hover:bg-white/10 transition-colors '
  + 'outline-none focus-visible:ring-2 focus-visible:ring-purple-80 disabled:opacity-40';
const HEADING = 'text-[10px] uppercase tracking-wider text-gray-100';

const KIND: Record<MqttLogEntry['kind'], { label: string; title: string; color: string }> = {
  injected: { label: 'IN', title: 'Injected here: handed to the script as if the broker sent it (local)', color: 'text-[#4fc3f7]' },
  delivered: { label: '→JS', title: 'Delivered to the script: a subscription matches', color: 'text-status-green' },
  published: { label: 'OUT', title: 'Published by the script to the in-page broker (offline): not sent', color: 'text-gray-100' },
  sent: { label: 'SENT', title: 'Published to the real broker', color: 'text-status-orange' },
  'would-publish': { label: 'WOULD', title: 'Read-only: this would be published, and was not', color: 'text-gray-100' },
  blocked: { label: 'HELD', title: 'Not sent: the note says why', color: 'text-status-red' },
};

function clock(time: number): string {
  const d = new Date(time);
  const two = (n: number) => String(n).padStart(2, '0');
  return `${two(d.getHours())}:${two(d.getMinutes())}:${two(d.getSeconds())}`;
}

/**
 * The running virtual device's inputs from outside: its sensors and its
 * MQTT brokers. The brokers are in this page ("offline"): what the script
 * publishes is recorded here and sent nowhere; a message is given to the
 * script by injecting it. A session (what came in, what went out, the
 * retained messages) can be saved as a file and replayed.
 */
@customElement('fb-emulator-panel')
export class FbEmulatorPanel extends LitElement {
  createRenderRoot() { return this; }

  @state() private _broker = 0;
  @state() private _topic = '';
  @state() private _payload = '';
  @state() private _retain = false;
  @state() private _message = '';
  @state() private _sensorsOpen = false;
  /** Log filters: who published, and the direction. */
  @state() private _source: 'all' | 'script' | 'firmware' = 'all';
  @state() private _dir: 'all' | 'in' | 'out' = 'all';
  private _offs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._offs.push(
      on('emulator-mqtt', () => this.requestUpdate()),
      on('emulator-state', () => this.requestUpdate()),
      on('emulator-change', () => this.requestUpdate()),
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._offs.forEach(off => off());
    this._offs = [];
  }

  private _inject(e: Event) {
    e.preventDefault();
    const topic = this._topic.trim();
    if (!topic) { this._message = 'Give a topic'; return; }
    if (/[+#]/.test(topic)) { this._message = 'A message has one topic: replace + and # of the filter'; return; }
    this._message = emulatorSession.inject(this._broker, topic, this._payload, this._retain) ? '' : 'The virtual device is not running';
  }

  private _export() {
    const session = emulatorSession.exportSession();
    if (!session) return;
    const url = URL.createObjectURL(new Blob([JSON.stringify(session, null, 2)], { type: 'application/json' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = `mqtt-session-${session.identity}.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  private async _import(e: Event) {
    const input = e.target as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file) return;
    let parsed: unknown;
    try {
      parsed = JSON.parse(await file.text());
    } catch {
      this._message = `${file.name} is not a JSON file`;
      return;
    }
    this._message = emulatorSession.importSession(parsed);
  }

  private _renderSensors() {
    const sensors = emulatorSession.sensors();
    if (sensors.length === 0) return '';
    // Folded: the MQTT form below is what is used most, and the pane is small.
    return html`
      <details class="group" data-testid="emu-sensors" ?open=${this._sensorsOpen}
        @toggle=${(e: Event) => { this._sensorsOpen = (e.target as HTMLDetailsElement).open; }}>
        <summary class="${HEADING} cursor-pointer select-none outline-none focus-visible:ring-2 focus-visible:ring-purple-80 rounded"
          title="The script hears a new value at the next sample, once a second, when it differs by 0.1 or more"
          >Sensors · ${sensors.map(s => `${s.value} ${s.unit}`).join(' · ')}</summary>
        <div class="flex flex-wrap gap-x-3 gap-y-1.5 pt-1.5">
          ${sensors.map(s => html`
            <label class="flex items-center gap-1.5 text-[11px] text-gray-100" title="${s.name} (sensor id ${s.id})">
              <span class="capitalize">${s.type}</span>
              <input type="number" step="0.1" class="${INPUT} w-[72px]" data-sensor=${s.id} .value=${String(s.value)}
                aria-label="${s.type} in ${s.unit}"
                @change=${(e: Event) => { emulatorSession.setSensor(s.id, Number((e.target as HTMLInputElement).value)); }} />
              <span>${s.unit}</span>
            </label>`)}
        </div>
      </details>`;
  }

  private _setMode(mode: MqttStatus['mode']) {
    this._message = emulatorSession.setMqttMode(mode);
  }

  /** Why a broker is not connected, with what to do about it. */
  private _renderLinkError(b: BrokerStatus) {
    const e = b.error;
    if (!e) return '';
    return html`
      <div class="text-[11px] leading-snug text-status-red" role="alert" data-testid="mqtt-link-error" data-error=${e.kind}>
        ${e.message}
        ${e.kind === 'unreachable' || e.kind === 'not-mqtt' ? html`
          <span class="block mt-1 text-gray-100">Mosquitto needs a websockets listener (mosquitto.conf), and the address above its port as this computer reaches it:</span>
          <pre class="mt-1 px-2 py-1 rounded bg-white/5 font-mono text-[11px] text-text-default" data-testid="mqtt-listener-help">listener 9001
protocol websockets</pre>` : ''}
        ${e.kind === 'taken-over' ? html`<span class="block mt-1 text-gray-100">Stop the virtual device in the other tab or window.</span>` : ''}
      </div>`;
  }

  /**
   * Where MQTT goes: the in-page broker (offline), or the real broker of
   * config.json, read-only or publishing. Per broker: the WebSocket address
   * and the state of the connection. In live publish: what else may be sent.
   */
  private _renderMode(status: MqttStatus | null) {
    if (!status) return '';
    const mode = status.mode;
    const identity = emulatorSession.snapshot().identity;
    const option = (value: MqttStatus['mode'], label: string, title: string) => {
      const on = mode === value;
      const color = !on ? 'text-gray-100 hover:text-text-default'
        : value === 'live-publish' ? 'bg-status-orange text-black font-semibold' : 'bg-purple-90 text-white';
      return html`<button role="radio" aria-checked=${on ? 'true' : 'false'} data-mode=${value} title=${title}
        class="px-2 py-0.5 rounded-md text-[11px] whitespace-nowrap transition-colors outline-none focus-visible:ring-2 focus-visible:ring-purple-80 ${color}"
        @click=${() => this._setMode(value)}>${label}</button>`;
    };
    const words = mode === 'offline' ? html`In-page broker: nothing leaves this page.`
      : mode === 'live-readonly' ? html`Connected to the real broker as <span class="font-mono">${identity}</span>. It subscribes; nothing is published (publishes are listed as WOULD).`
        : html`<span class="text-status-orange">The virtual device publishes to the real broker</span> as <span class="font-mono">${identity}</span>.`;
    const chip = (b: BrokerStatus) => mode === 'offline' ? html`<span class="text-gray-100">not connected</span>`
      : b.state === 'connected' ? html`<span class="text-status-green">connected</span>`
        : b.error && b.state === 'disconnected' ? html`<span class="text-status-red">not connected</span>`
          : html`<span class="text-gray-100">connecting…</span>`;
    return html`
      <div class="flex flex-col gap-1.5" data-testid="mqtt-mode" data-mode=${mode}>
        <div class="flex items-center gap-2 flex-wrap">
          <h3 class=${HEADING}>MQTT</h3>
          <div class="flex rounded-lg p-0.5 bg-white/5 border border-white/10" role="radiogroup" aria-label="MQTT mode">
            ${option('offline', 'Offline', 'The broker in this page. The mode at every start.')}
            ${option('live-readonly', 'Live read-only', 'Connect to the real broker and listen. Nothing is published.')}
            ${option('live-publish', 'Live publish', 'Connect to the real broker and publish what the script publishes.')}
          </div>
        </div>
        <p class="text-[11px] leading-snug text-gray-100" data-testid="mqtt-mode-words">${words}</p>
        ${status.securePage ? html`<p class="text-[11px] leading-snug text-status-orange" data-testid="mqtt-secure-page">This page was loaded over https: the browser blocks ws:// addresses. Open the IDE over http, or use a wss:// address.</p>` : ''}
        ${status.brokers.map(b => html`
          <div class="flex flex-col gap-1" data-broker-status=${b.index}>
            <label class="flex items-center gap-1.5 text-[11px]">
              <span class="font-mono shrink-0 max-w-[96px] truncate" title="config.json broker ${b.name} (${b.host})">${b.name}</span>
              <input class="${INPUT} flex-1" spellcheck="false" autocomplete="off" data-testid="mqtt-ws-url" aria-label="WebSocket address of broker ${b.name}"
                title="The broker's WebSocket (websockets listener) address. Kept by this browser for ${b.host}."
                placeholder="ws://host:9001" .value=${b.wsUrl}
                @change=${(e: Event) => emulatorSession.setBrokerUrl(b.index, (e.target as HTMLInputElement).value)} />
              <span class="shrink-0" data-testid="mqtt-link-state" data-state=${mode === 'offline' ? 'offline' : b.state}>${chip(b)}</span>
            </label>
            ${mode !== 'offline' ? this._renderLinkError(b) : ''}
            ${b.configClientId ? html`<p class="text-[10px] leading-snug text-gray-100" data-testid="mqtt-client-id-note">config.json gives this broker the client id
              <span class="font-mono">${b.configClientId}</span>: not used here. The virtual device always connects as <span class="font-mono">${identity}</span>.</p>` : ''}
          </div>`)}
        ${mode === 'live-publish' ? html`
          <div class="flex flex-col gap-1 pl-2 border-l-2 border-status-orange/60" data-testid="mqtt-optins">
            <p class="text-[10px] leading-snug text-gray-100">Sent: what the script publishes and a key's own press topics. Not sent unless switched on here: the retain flag, announcements, the device's own states. A topic of a real device (btn_…) asks first.</p>
            <label class="flex items-start gap-1.5 text-[11px]">
              <input type="checkbox" class="mt-0.5" data-testid="mqtt-send-retained" .checked=${status.sendRetained}
                @change=${(e: Event) => emulatorSession.setSendRetained((e.target as HTMLInputElement).checked)} />
              <span>Send retained <span class="block text-[10px] leading-snug text-gray-100">A retained message stays on the broker, for a device that does not exist.</span></span>
            </label>
            <label class="flex items-start gap-1.5 text-[11px]">
              <input type="checkbox" class="mt-0.5" data-testid="mqtt-announce" .checked=${status.announce}
                @change=${(e: Event) => emulatorSession.setAnnounce((e.target as HTMLInputElement).checked)} />
              <span>Announce to Home Assistant <span class="block text-[10px] leading-snug text-gray-100">Discovery and "online" make entities for the virtual device in Home Assistant, with a last will. Reconnects. Remove them afterwards.</span></span>
            </label>
            <label class="flex items-start gap-1.5 text-[11px]">
              <input type="checkbox" class="mt-0.5" data-testid="mqtt-own-states" .checked=${status.ownStates}
                @change=${(e: Event) => emulatorSession.setOwnStates((e.target as HTMLInputElement).checked)} />
              <span>Publish the virtual device's own states <span class="block text-[10px] leading-snug text-gray-100">Its sensors report about eight messages a second under its own id: harmless, and noisy.</span></span>
            </label>
            ${status.announced > 0 ? html`
              <button class="${BUTTON} self-start" data-testid="mqtt-remove-announcement"
                title="Switch announcing off and publish an empty retained message to each announcement topic sent in this session: Home Assistant removes the entities"
                @click=${() => { this._message = emulatorSession.removeAnnouncement(); }}>Remove announcement (${status.announced})</button>` : ''}
          </div>` : ''}
      </div>`;
  }

  private _visible(entry: MqttLogEntry): boolean {
    const incoming = entry.kind === 'injected' || entry.kind === 'delivered';
    if (this._dir !== 'all' && (this._dir === 'in') !== incoming) return false;
    // Who published: a question about what goes out only.
    return this._source === 'all' || incoming || (entry.origin ?? 'script') === this._source;
  }

  private _renderFilters(shown: number, total: number) {
    const chip = (on: boolean, label: string, title: string, set: () => void, id: string) => html`
      <button role="radio" aria-checked=${on ? 'true' : 'false'} data-filter=${id} title=${title}
        class="px-1.5 py-0.5 rounded text-[10px] transition-colors outline-none focus-visible:ring-2 focus-visible:ring-purple-80 ${on ? 'bg-white/15 text-text-default' : 'text-gray-100 hover:text-text-default'}"
        @click=${set}>${label}</button>`;
    return html`
      <div class="flex items-center gap-2 flex-wrap text-[10px] text-gray-100" data-testid="mqtt-log-filters">
        <div class="flex rounded-md bg-white/5 border border-white/10" role="radiogroup" aria-label="Published by">
          ${chip(this._source === 'all', 'All', 'Publishes of the script and of the firmware', () => { this._source = 'all'; }, 'source-all')}
          ${chip(this._source === 'script', 'Script', 'What the script publishes (mqtt.publish)', () => { this._source = 'script'; }, 'source-script')}
          ${chip(this._source === 'firmware', 'Firmware', 'What the firmware publishes by itself: key presses, sensor states, discovery, availability', () => { this._source = 'firmware'; }, 'source-firmware')}
        </div>
        <div class="flex rounded-md bg-white/5 border border-white/10" role="radiogroup" aria-label="Direction">
          ${chip(this._dir === 'all', 'In + out', 'Both directions', () => { this._dir = 'all'; }, 'dir-all')}
          ${chip(this._dir === 'in', 'In', 'What the script received (and what was injected)', () => { this._dir = 'in'; }, 'dir-in')}
          ${chip(this._dir === 'out', 'Out', 'What was published', () => { this._dir = 'out'; }, 'dir-out')}
        </div>
        ${shown < total ? html`<span data-testid="mqtt-log-hidden">${shown} of ${total} shown</span>` : ''}
      </div>`;
  }

  /**
   * The log, newest first. The firmware's periodic and bulk publishes
   * (sensor, state, discovery) are one row per topic with a count and the
   * latest payload: the row is updated where it stands, so nothing moves
   * while it counts. Its earlier payloads open on demand.
   */
  private _renderLog() {
    const all = emulatorSession.mqttLog();
    if (all.length === 0) {
      return html`<p class="text-[11px] text-gray-100" data-testid="mqtt-log-empty">Nothing yet. Messages you inject, what the script receives and what is published are listed here, newest first.</p>`;
    }
    const log = all.filter(e => this._visible(e));
    const brokers = emulatorSession.brokers();
    const row = (entry: MqttLogEntry) => html`
      <span class="text-gray-100">${clock(entry.time)}</span>
      <span class="${KIND[entry.kind].color} font-semibold" title=${KIND[entry.kind].title}>${KIND[entry.kind].label}</span>
      <span class="min-w-0 break-words">
        <span class="text-text-default" data-topic>${entry.topic}</span>
        <span class="text-gray-100"> = </span><span class="text-status-green" data-payload>${entry.payload === '' ? '(empty)' : entry.payload}</span>
        ${entry.retain ? html`<span class="ml-1 px-1 rounded bg-white/10 text-gray-100" title="Retained">R</span>` : ''}
        ${entry.count > 1 ? html`<span class="ml-1 px-1 rounded bg-white/10 text-text-default tabular-nums" data-count title="${entry.count} messages on this topic; the latest is shown">×${entry.count}</span>` : ''}
        ${brokers.length > 1 ? html`<span class="ml-1 text-gray-100">[${brokers[entry.broker]?.name ?? entry.broker}]</span>` : ''}
        <span class="block font-sans text-[10px] text-gray-100"><span data-note>${entry.note}</span>${entry.what
          ? html`<span data-what> · firmware: ${entry.what}</span>` : entry.origin === 'script' ? html`<span data-what> · script</span>` : ''}</span>
      </span>`;
    return html`
      ${this._renderFilters(log.length, all.length)}
      <ol class="flex flex-col font-mono text-[11px] leading-[1.35]" data-testid="mqtt-log">
        ${log.map(entry => entry.history?.length ? html`
          <li class="border-b border-white/5" data-kind=${entry.kind} data-origin=${entry.origin ?? 'in'}>
            <details>
              <summary class="grid grid-cols-[52px_44px_1fr] gap-x-1.5 py-0.5 cursor-pointer list-none outline-none focus-visible:ring-2 focus-visible:ring-purple-80 rounded"
                title="Show the payloads before this one">${row(entry)}</summary>
              <ol class="pl-[104px] pb-1 text-gray-100" data-history>
                ${entry.history.map(h => html`<li>${clock(h.time)} <span class="text-status-green/80">${h.payload === '' ? '(empty)' : h.payload}</span></li>`)}
                ${entry.count > entry.history.length + 1 ? html`<li>… ${entry.count - entry.history.length - 1} earlier</li>` : ''}
              </ol>
            </details>
          </li>` : html`
          <li class="grid grid-cols-[52px_44px_1fr] gap-x-1.5 py-0.5 border-b border-white/5" data-kind=${entry.kind} data-origin=${entry.origin ?? 'in'}>${row(entry)}</li>`)}
      </ol>`;
  }

  render() {
    if (!emulatorSession.running) {
      // Stopped: what was logged can still be read; nothing can be sent.
      return html`
        <div class="p-3 flex flex-col gap-3 bg-bg-default min-h-full text-text-default" data-testid="emulator-panel">
          <p class="text-[11px] text-status-orange" data-testid="mqtt-stopped">The virtual device is not running: Run to use its MQTT and sensors. The log below is of the last run.</p>
          ${this._renderLog()}
        </div>`;
    }
    const brokers = emulatorSession.brokers();
    const status = emulatorSession.mqttStatus();
    const live = !!status && status.mode !== 'offline';
    const topics = [...new Set(brokers.flatMap(b => b.subscriptions.map(s => s.topic)))];
    return html`
      <div class="p-3 flex flex-col gap-3 bg-bg-default min-h-full text-text-default" data-testid="emulator-panel">
        ${this._renderSensors()}

        <section class="flex flex-col gap-2" aria-label="MQTT">
          ${this._renderMode(status)}

          ${brokers.length === 0 ? html`<p class="text-[11px] text-gray-100" data-testid="mqtt-no-broker">config.json has no "brokers": the script cannot use MQTT.</p>` : html`
            ${brokers.map(b => html`
              <div class="flex flex-col gap-1" data-broker=${b.index}>
                <div class="text-[11px]"><span class="font-mono">${b.name || `broker ${b.index}`}</span>
                  <span class="text-gray-100"> · ${b.subscriptions.length} subscription${b.subscriptions.length === 1 ? '' : 's'}</span></div>
                <div class="flex flex-wrap gap-1">
                  ${b.subscriptions.map(s => html`
                    <button class="px-1.5 py-0.5 rounded bg-white/5 border border-white/10 font-mono text-[11px] text-text-default hover:bg-white/10 outline-none focus-visible:ring-2 focus-visible:ring-purple-80"
                      data-subscription=${s.topic} title="Use as the topic to inject (qos ${s.qos})"
                      @click=${() => { this._broker = b.index; this._topic = s.topic; this.querySelector<HTMLElement>('[data-testid="mqtt-payload"]')?.focus(); }}>${s.topic}</button>`)}
                </div>
              </div>`)}

            <form class="flex flex-col gap-1.5" @submit=${this._inject} data-testid="mqtt-inject">
              <div class="flex gap-1.5">
                ${brokers.length > 1 ? html`
                  <select class="${INPUT} w-[96px] font-sans" aria-label="Broker" data-testid="mqtt-broker"
                    @change=${(e: Event) => { this._broker = Number((e.target as HTMLSelectElement).value); }}>
                    ${brokers.map(b => html`<option value=${b.index} ?selected=${b.index === this._broker}>${b.name || b.index}</option>`)}
                  </select>` : ''}
                <input class="${INPUT} flex-1" placeholder="topic" aria-label="Topic" list="fb-mqtt-topics" spellcheck="false" autocomplete="off"
                  data-testid="mqtt-topic" .value=${this._topic} @input=${(e: Event) => { this._topic = (e.target as HTMLInputElement).value; }} />
                <datalist id="fb-mqtt-topics">${topics.map(t => html`<option value=${t}></option>`)}</datalist>
              </div>
              <div class="flex items-center gap-1.5">
                <input class="${INPUT} flex-1" placeholder="payload" aria-label="Payload" spellcheck="false" autocomplete="off"
                  data-testid="mqtt-payload" .value=${this._payload} @input=${(e: Event) => { this._payload = (e.target as HTMLInputElement).value; }} />
                <label class="flex items-center gap-1 text-[11px] text-gray-100" title="The broker keeps it and gives it to every new subscription that matches">
                  <input type="checkbox" data-testid="mqtt-retain" .checked=${this._retain}
                    @change=${(e: Event) => { this._retain = (e.target as HTMLInputElement).checked; }} />retain</label>
                <button type="submit" class="${BUTTON} bg-purple-90 border-transparent hover:bg-purple-active text-white" data-testid="mqtt-inject-button"
                  title=${live
                    ? 'Hand this message to the script here, as if the broker sent it. Local: the real broker does not get it.'
                    : 'Send this message to the script as if another client published it'}>Inject${live ? ' (local)' : ''}</button>
              </div>
            </form>
            ${this._message ? html`<p class="text-[11px] text-status-orange" role="status" data-testid="mqtt-message">${this._message}</p>` : ''}

            <div class="flex items-center gap-1.5 flex-wrap">
              ${live ? html`<span class="mr-auto"></span>` : html`
              <label class="flex items-center gap-1 text-[11px] text-gray-100 mr-auto"
                title="As a real broker does: a message the script publishes comes back to its own matching subscriptions">
                <input type="checkbox" data-testid="mqtt-loopback" .checked=${emulatorSession.loopback}
                  @change=${(e: Event) => emulatorSession.setLoopback((e.target as HTMLInputElement).checked)} />loopback</label>`}
              <button class=${BUTTON} data-testid="mqtt-export" title="Save what came in and went out, and the retained messages, as a JSON file"
                @click=${() => this._export()}>Export</button>
              <label class="${BUTTON} cursor-pointer focus-within:ring-2 focus-within:ring-purple-80" title="Load a saved session: its retained messages are set and its incoming messages are injected again at their recorded times">
                Import &amp; replay
                <input type="file" accept=".json,application/json" class="sr-only" data-testid="mqtt-import" @change=${this._import} />
              </label>
              <button class=${BUTTON} data-testid="mqtt-clear" ?disabled=${emulatorSession.mqttLog().length === 0}
                @click=${() => emulatorSession.clearLog()}>Clear log</button>
            </div>
            ${this._renderLog()}
          `}
        </section>
      </div>
    `;
  }
}
