import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on } from '../js/app-state';
import { emulatorSession, type MqttLogEntry } from '../js/emulator-session';

const INPUT = 'min-w-0 bg-white/5 border border-white/10 rounded-md px-2 py-1 text-xs text-text-default font-mono outline-none focus:border-purple-80';
const BUTTON = 'shrink-0 px-2 py-1 rounded-md text-[11px] bg-white/5 border border-white/10 text-text-default hover:bg-white/10 transition-colors '
  + 'outline-none focus-visible:ring-2 focus-visible:ring-purple-80 disabled:opacity-40';
const HEADING = 'text-[10px] uppercase tracking-wider text-gray-100';

const KIND: Record<MqttLogEntry['kind'], { label: string; title: string; color: string }> = {
  injected: { label: 'IN', title: 'Injected here: a message from another client of the broker', color: 'text-[#4fc3f7]' },
  delivered: { label: '→JS', title: 'Delivered to the script: a subscription matches', color: 'text-status-green' },
  published: { label: 'OUT', title: 'Published by the script', color: 'text-status-orange' },
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

  private _renderLog() {
    const log = emulatorSession.mqttLog();
    if (log.length === 0) {
      return html`<p class="text-[11px] text-gray-100" data-testid="mqtt-log-empty">Nothing yet. Messages you inject, what the script receives and what it publishes are listed here, newest first.</p>`;
    }
    const brokers = emulatorSession.brokers();
    return html`
      <ol class="flex flex-col font-mono text-[11px] leading-[1.35]" data-testid="mqtt-log">
        ${log.map(entry => html`
          <li class="grid grid-cols-[52px_30px_1fr] gap-x-1.5 py-0.5 border-b border-white/5" data-kind=${entry.kind}>
            <span class="text-gray-100">${clock(entry.time)}</span>
            <span class="${KIND[entry.kind].color} font-semibold" title=${KIND[entry.kind].title}>${KIND[entry.kind].label}</span>
            <span class="min-w-0 break-words">
              <span class="text-text-default" data-topic>${entry.topic}</span>
              <span class="text-gray-100"> = </span><span class="text-status-green" data-payload>${entry.payload === '' ? '(empty)' : entry.payload}</span>
              ${entry.retain ? html`<span class="ml-1 px-1 rounded bg-white/10 text-gray-100" title="Retained">R</span>` : ''}
              ${brokers.length > 1 ? html`<span class="ml-1 text-gray-100">[${brokers[entry.broker]?.name ?? entry.broker}]</span>` : ''}
              ${entry.note ? html`<span class="block font-sans text-[10px] text-gray-100" data-note>${entry.note}</span>` : ''}
            </span>
          </li>`)}
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
    const topics = [...new Set(brokers.flatMap(b => b.subscriptions.map(s => s.topic)))];
    return html`
      <div class="p-3 flex flex-col gap-3 bg-bg-default min-h-full text-text-default" data-testid="emulator-panel">
        ${this._renderSensors()}

        <section class="flex flex-col gap-2" aria-label="MQTT">
          <div class="flex items-center gap-2 flex-wrap">
            <h3 class=${HEADING}>MQTT</h3>
            <span class="text-[10px] text-gray-100" title="The brokers of config.json are replaced by brokers inside this page. Nothing is sent to a real broker.">offline: in-page broker, nothing leaves this page</span>
          </div>

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
                  title="Send this message to the script as if another client published it">Inject</button>
              </div>
            </form>
            ${this._message ? html`<p class="text-[11px] text-status-orange" role="status" data-testid="mqtt-message">${this._message}</p>` : ''}

            <div class="flex items-center gap-1.5 flex-wrap">
              <label class="flex items-center gap-1 text-[11px] text-gray-100 mr-auto"
                title="As a real broker does: a message the script publishes comes back to its own matching subscriptions">
                <input type="checkbox" data-testid="mqtt-loopback" .checked=${emulatorSession.loopback}
                  @change=${(e: Event) => emulatorSession.setLoopback((e.target as HTMLInputElement).checked)} />loopback</label>
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
