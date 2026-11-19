import { describe, it, expect, afterEach, beforeEach, vi } from 'vitest';
import { emulatorModule, BASIC_CONFIG } from './emulator-helpers';
import { EmulatorSession, virtualDeviceId, type RunSource } from '../emulator-session';
import type { AskRequest } from '../emulator/live-session';
import {
  brokerAccess, brokerConfigs, brokerHost, defaultWsUrl, readBrokerSettings, setDefaultStartMode, writeBrokerSettings,
} from '../virtual-broker';
import type { BrokerLink } from '../emulator/broker-link';
import type { ConsoleLine } from '../app-state';
import { TestBroker, nodeSocket, until } from './mqtt-test-broker';

const SCRIPT = `
  mqtt.subscribe(0, "home/temp", function (t, p) { fb.main.display.setValue("temp", p); });
  mqtt.subscribe(0, "home/+/state", function (t, p) { print(t + "=" + p); });
  fb.button("light").button.onClick(function () {
    mqtt.publish(0, "home/light/set", "ON", 0, 1);
    mqtt.publish(0, "buttonplus/" + system.id() + "/key", "press", 1, 0);
    mqtt.publish(0, "buttonplus/btn_a00001/cover/raam/set", "stop", 0, 0);
    mqtt.publish(0, "homeassistant/sensor/" + system.id() + "_t/config", "{}", 0, 1);
  });
`;
const REAL_TOPIC = 'buttonplus/btn_a00001/cover/raam/set';

const wait = (ms: number) => new Promise(r => setTimeout(r, ms));

function memoryStorage(): Storage {
  const map = new Map<string, string>();
  return {
    getItem: (k: string) => map.get(k) ?? null, setItem: (k: string, v: string) => { map.set(k, String(v)); },
    removeItem: (k: string) => { map.delete(k); }, clear: () => map.clear(), key: (i: number) => [...map.keys()][i] ?? null,
    get length() { return map.size; },
  } as Storage;
}

let session: EmulatorSession;
let broker: TestBroker;
let lines: ConsoleLine[];
let asked: AskRequest[];
let answer: string | null;
const onLine = (e: Event) => lines.push((e as CustomEvent<ConsoleLine>).detail);

function start(script = SCRIPT, config: unknown = BASIC_CONFIG): Promise<void> {
  const source: RunSource = { script, config: JSON.stringify(config) };
  session.readSource = async () => source;
  return session.run();
}

async function click(position = 3): Promise<void> {
  session.press(position);
  await wait(90);
  session.release(position);
  await wait(120);
}

/** Go live and wait for the connection. */
async function live(mode: 'live-readonly' | 'live-publish'): Promise<void> {
  expect(session.setMqttMode(mode)).toBe('');
  await until(() => session.snapshot().mqttLink === 'connected', 3000, 'the live connection');
  await until(() => broker.subscriptionsOf(virtualDeviceId()).length >= 2, 3000, 'the subscriptions');
}

/** What the script published and the broker got (the firmware's own publishes aside: a key's press event, states). */
const scriptPublished = () => broker.published.filter(p => !/\/(button_\d+\/state|status|sensor)/.test(p.topic) && !p.topic.startsWith('homeassistant/'));
const sent = () => scriptPublished().map(p => [p.topic, p.payload, p.qos, p.retain]);
/** The log without the firmware's own publishes. */
const logged = () => session.mqttLog().filter(e => e.origin !== 'firmware').map(e => [e.kind, e.topic, e.note]);

beforeEach(async () => {
  vi.stubGlobal('localStorage', memoryStorage());
  broker = await TestBroker.start();
  // The broker of the test config is "mqtt://broker.test/": its WebSocket address, as the user would set it.
  localStorage.setItem('fb-mqtt-ws:broker.test', broker.url);
  session = new EmulatorSession();
  session.wasm = emulatorModule();
  session.socket = nodeSocket;
  session.securePage = false;
  session.liveTiming = { backoffMs: 20 };
  asked = [];
  answer = null;
  session.ask = async (request) => { asked.push(request); return answer; };
  lines = [];
  window.addEventListener('console-line', onLine);
});

afterEach(async () => {
  session.destroy();
  window.removeEventListener('console-line', onLine);
  await broker.stop();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

describe('broker settings', () => {
  it('the host of a broker url; the brokers of config.json', () => {
    expect(['mqtt://192.168.1.10/', '192.168.1.10', 'mqtts://user:pw@broker.lan:8883/x', 'mqtt://[fe80::1]:1883'].map(brokerHost))
      .toEqual(['192.168.1.10', '192.168.1.10', 'broker.lan', '[fe80::1]']);
    expect(brokerConfigs(JSON.stringify({ brokers: [{ brokerid: 'ha', url: 'mqtt://10.0.0.5/', port: 1883, wsport: 9443, username: 'u', password: 'p', clientid: 'btn_a00001' }, 5] })))
      .toEqual([{ name: 'ha', url: 'mqtt://10.0.0.5/', host: '10.0.0.5', wsPort: 9443, username: 'u', password: 'p', clientId: 'btn_a00001' }]);
    expect(brokerConfigs('not json')).toEqual([]);
  });

  it('the WebSocket address config.json gives: its "wsport", else 9001; a ws:// or wss:// url as it is', () => {
    const of = (broker: Record<string, unknown>) => defaultWsUrl(brokerConfigs(JSON.stringify({ brokers: [broker] }))[0]!);
    expect(of({ url: 'mqtt://10.0.0.5/', port: 1883, wsport: 1884 })).toBe('ws://10.0.0.5:1884');
    expect(of({ url: 'mqtt://10.0.0.5/', port: 1883 })).toBe('ws://10.0.0.5:9001');
    expect(of({ url: 'mqtt://10.0.0.5/', wsport: 0 })).toBe('ws://10.0.0.5:9001');
    expect(of({ url: '10.0.0.5', wsport: '8083' })).toBe('ws://10.0.0.5:8083');
    expect(of({ url: 'ws://10.0.0.5:8080/mqtt', wsport: 1884 })).toBe('ws://10.0.0.5:8080/mqtt');
    expect(of({ url: 'wss://broker.lan/mqtt' })).toBe('wss://broker.lan/mqtt');
    expect(of({ url: '' })).toBe('');
  });

  it('what the browser keeps for a host wins, per field; empty fields are config.json\'s; the address set before the records is still read', () => {
    const [b] = brokerConfigs(JSON.stringify({ brokers: [{ url: 'mqtt://10.0.0.5/', wsport: 1884, username: 'u', password: 'p' }] }));
    expect(brokerAccess(b!)).toEqual({ url: 'ws://10.0.0.5:1884', username: 'u', password: 'p' });
    // The key of before: the address only.
    localStorage.setItem('fb-mqtt-ws:10.0.0.5', 'ws://10.0.0.5:32768');
    expect(readBrokerSettings('10.0.0.5')).toEqual({ url: 'ws://10.0.0.5:32768', username: '', password: '' });
    expect(brokerAccess(b!)).toEqual({ url: 'ws://10.0.0.5:32768', username: 'u', password: 'p' });
    // A record: its fields win; the old key goes.
    expect(writeBrokerSettings('10.0.0.5', { url: ' ws://10.0.0.5:9002 ', username: '', password: 'other' })).toBe(true);
    expect(JSON.parse(localStorage.getItem('freebutton.virtualBroker:10.0.0.5')!)).toEqual({ url: 'ws://10.0.0.5:9002', username: '', password: 'other' });
    expect(localStorage.getItem('fb-mqtt-ws:10.0.0.5')).toBeNull();
    expect(brokerAccess(b!)).toEqual({ url: 'ws://10.0.0.5:9002', username: 'u', password: 'other' });
    // Nothing in any field: the record goes.
    writeBrokerSettings('10.0.0.5', { url: '', username: '', password: '' });
    expect(localStorage.getItem('freebutton.virtualBroker:10.0.0.5')).toBeNull();
    expect(readBrokerSettings('10.0.0.5')).toBeNull();
    // Something unreadable in the store is no record.
    localStorage.setItem('freebutton.virtualBroker:10.0.0.5', '{nope');
    expect(brokerAccess(b!)).toEqual({ url: 'ws://10.0.0.5:1884', username: 'u', password: 'p' });
  });

  it('the status lists the brokers with what they connect with; saved settings are kept per host and announced; reset goes back to config.json', async () => {
    localStorage.clear();
    const changes: string[] = [];
    const onChange = (e: Event) => changes.push((e as CustomEvent<{ host: string }>).detail.host);
    window.addEventListener('virtual-broker-change', onChange);
    await start();
    expect(session.mqttStatus().brokers[0]).toMatchObject({
      name: 'local', host: 'broker.test', wsUrl: 'ws://broker.test:9001', defaultWsUrl: 'ws://broker.test:9001', username: '', overridden: false, state: 'disconnected',
    });
    expect(session.brokerSettings('broker.test')).toBeNull();
    expect(session.saveBrokerSettings('broker.test', { url: ' ws://broker.test:32768 ', username: 'me', password: 'pw' })).toBe('');
    expect(session.mqttStatus().brokers[0]).toMatchObject({
      wsUrl: 'ws://broker.test:32768', defaultWsUrl: 'ws://broker.test:9001', username: 'me', defaultUsername: '', overridden: true, hasCredentials: true,
    });
    expect(session.brokerSettings('broker.test')).toEqual({ url: 'ws://broker.test:32768', username: 'me', password: 'pw' });
    expect(JSON.parse(localStorage.getItem('freebutton.virtualBroker:broker.test')!)).toEqual({ url: 'ws://broker.test:32768', username: 'me', password: 'pw' });
    session.resetBrokerSettings('broker.test');
    expect(session.mqttStatus().brokers[0]).toMatchObject({ wsUrl: 'ws://broker.test:9001', username: '', overridden: false });
    expect(localStorage.getItem('freebutton.virtualBroker:broker.test')).toBeNull();
    expect(changes).toEqual(['broker.test', 'broker.test']);
    window.removeEventListener('virtual-broker-change', onChange);
  });

  it('while nothing runs the status is of config.json as the workspace has it: its brokers, the mode of the next Run', () => {
    localStorage.clear();
    session.readConfig = () => JSON.stringify({ brokers: [
      { brokerid: 'ha', url: 'mqtt://10.0.0.5/', wsport: 1884, username: 'u', password: 'p' },
      { brokerid: 'same-host', url: 'mqtts://10.0.0.5:8883/' },
      { brokerid: 'ws', url: 'wss://broker.lan/mqtt' },
    ] });
    expect(session.mqttStatus()).toMatchObject({ mode: 'offline', sendRetained: false, announce: false, ownStates: false, announced: 0 });
    expect(session.mqttStatus().brokers.map(b => [b.name, b.host, b.wsUrl, b.defaultUsername, b.configPassword, b.state])).toEqual([
      ['ha', '10.0.0.5', 'ws://10.0.0.5:1884', 'u', true, 'disconnected'],
      ['same-host', '10.0.0.5', 'ws://10.0.0.5:9001', '', false, 'disconnected'],
      ['ws', 'broker.lan', 'wss://broker.lan/mqtt', '', false, 'disconnected'],
    ]);
    // Two entries with one host share the record.
    session.saveBrokerSettings('10.0.0.5', { url: 'ws://10.0.0.5:8083', username: '', password: '' });
    expect(session.mqttStatus().brokers.map(b => [b.wsUrl, b.overridden])).toEqual([
      ['ws://10.0.0.5:8083', true], ['ws://10.0.0.5:8083', true], ['wss://broker.lan/mqtt', false],
    ]);
    session.readConfig = () => null;
    expect(session.mqttStatus().brokers).toEqual([]);
  });
});

describe('the mode a Run starts in', () => {
  const DISCOVERY = { ...BASIC_CONFIG, brokers: [{ ...BASIC_CONFIG.brokers[0], hassdiscovery: true }] };
  const own = () => `buttonplus/${virtualDeviceId()}`;

  it('by default a Run connects to the broker of config.json, live publish, without a last will; the firmware never talks to the in-page broker first', async () => {
    setDefaultStartMode('live-publish');
    expect(session.snapshot()).toMatchObject({ status: 'idle', mqtt: 'live-publish', mqttLink: 'none' });
    await start();
    expect(session.snapshot()).toMatchObject({ status: 'running', mqtt: 'live-publish' });
    expect(lines.some(l => l.text.includes(`started (MQTT live: connecting to ${broker.url})`))).toBe(true);
    await until(() => session.snapshot().mqttLink === 'connected', 3000, 'the live connection');
    await until(() => broker.subscriptionsOf(virtualDeviceId()).length >= 2, 3000, 'the subscriptions');
    expect(broker.connects.map(c => [c.clientId, c.cleanSession, c.will])).toEqual([[virtualDeviceId(), true, undefined]]);
    expect(session.mqttStatus()).toMatchObject({ mode: 'live-publish', sendRetained: false, announce: false, ownStates: false, announced: 0 });
    // The birth message was put to the real broker's rules (held: discovery is off), never to the in-page broker.
    expect(session.mqttLog().some(e => e.kind === 'published')).toBe(false);
    expect(session.mqttLog().filter(e => e.what === 'availability').map(e => [e.kind, e.payload, e.note])).toEqual([
      ['blocked', 'online', 'not sent: Home Assistant discovery is off'],
    ]);
    answer = null;
    await click();
    await until(() => scriptPublished().length === 2);
    expect(sent()).toEqual([['home/light/set', 'ON', 0, false], [`${own()}/key`, 'press', 1, false]]);
    // Nothing was stored as a choice: the default is not one.
    expect(localStorage.getItem('fb-mqtt-mode')).toBeNull();
  });

  it('"hassdiscovery": true in config.json sends no discovery by itself: no will, no birth message, no config topics', async () => {
    setDefaultStartMode('live-publish');
    await start(SCRIPT, DISCOVERY);
    await until(() => session.snapshot().mqttLink === 'connected', 3000, 'the live connection');
    await until(() => broker.subscriptionsOf(virtualDeviceId()).includes(`${own()}/button_3/set`));
    await wait(1600);                        // discovery starts a second after the connect
    expect(session.mqttStatus().announce).toBe(false);
    expect(broker.connects.map(c => c.will)).toEqual([undefined]);
    expect(broker.published).toEqual([]);
    expect(broker.retained.size).toBe(0);
    const log = session.mqttLog();
    expect(log.find(e => e.what === 'discovery')).toMatchObject({ kind: 'blocked', note: 'not sent: Home Assistant discovery is off' });
    expect(log.find(e => e.what === 'discovery')!.count).toBeGreaterThan(10);
    expect(log.find(e => e.what === 'availability')).toMatchObject({ kind: 'blocked', payload: 'online' });
    broker.cut(virtualDeviceId());
    await wait(80);
    expect(broker.wills).toEqual([]);
  });

  it('a config without brokers starts offline, whatever the mode', async () => {
    setDefaultStartMode('live-publish');
    await start('print("up");', { ...BASIC_CONFIG, brokers: [] });
    expect(session.snapshot()).toMatchObject({ status: 'running', mqtt: 'offline', mqttLink: 'none' });
    expect(lines.some(l => l.text.includes('started (MQTT offline: config.json has no brokers)'))).toBe(true);
    await wait(60);
    expect(broker.connects).toEqual([]);
  });

  it('the mode chosen is remembered: Offline holds over Stop, Run again and a new page', async () => {
    setDefaultStartMode('live-publish');
    expect(session.setMqttMode('offline')).toBe('');          // nothing runs: for the next Run
    expect(localStorage.getItem('fb-mqtt-mode')).toBe('offline');
    await start();
    expect(session.snapshot()).toMatchObject({ mqtt: 'offline', mqttLink: 'none' });
    expect(lines.some(l => l.text.includes('started (MQTT offline: in-page broker)'))).toBe(true);
    session.stop();
    await start();
    expect(session.snapshot()).toMatchObject({ mqtt: 'offline', mqttLink: 'none' });
    // A reload: another session object, the same browser storage.
    session.destroy();
    session = new EmulatorSession();
    session.wasm = emulatorModule();
    session.socket = nodeSocket;
    session.securePage = false;
    expect(session.snapshot().mqtt).toBe('offline');
    await start();
    expect(session.snapshot()).toMatchObject({ mqtt: 'offline', mqttLink: 'none' });
    await click();
    await wait(60);
    expect(broker.connects).toEqual([]);
    expect(session.mqttLog().filter(e => e.origin === 'script').every(e => e.kind === 'published')).toBe(true);
  });

  it('a remembered Read-only is the mode of the next Run: connected, nothing published', async () => {
    localStorage.setItem('fb-mqtt-mode', 'live-readonly');
    setDefaultStartMode('live-publish');
    await start();
    await until(() => session.snapshot().mqttLink === 'connected', 3000, 'the live connection');
    expect(session.snapshot().mqtt).toBe('live-readonly');
    await click();
    await wait(60);
    expect(broker.published).toEqual([]);
    expect(logged().slice(0, 4).map(l => l[0])).toEqual(Array(4).fill('would-publish'));
    // Something else in the store is not a mode: the default.
    localStorage.setItem('fb-mqtt-mode', 'everything');
    session.destroy();
    session = new EmulatorSession();
    expect(session.snapshot().mqtt).toBe('live-publish');
  });

  it('the switches can be set before Run and hold for it: discovery on connects with the firmware\'s will; Stop keeps them', async () => {
    setDefaultStartMode('live-publish');
    session.setAnnounce(true);
    session.setOwnStates(true);
    expect(session.mqttStatus()).toMatchObject({ mode: 'live-publish', announce: true, ownStates: true, sendRetained: false });
    await start(SCRIPT, DISCOVERY);
    await until(() => session.snapshot().mqttLink === 'connected', 3000, 'the live connection');
    expect(broker.connects.length).toBe(1);
    expect(broker.connects[0]!.will).toEqual({ topic: `${own()}/status`, payload: 'offline', qos: 1, retain: false });
    await until(() => broker.published.some(p => p.topic === `${own()}/status` && p.payload === 'online'), 3000, 'the birth message');
    expect(session.mqttStatus()).toMatchObject({ announce: true, ownStates: true });
    session.stop();
    expect(session.mqttStatus()).toMatchObject({ mode: 'live-publish', announce: true, ownStates: true });
    // Not in another mode: they belong to live publish.
    session.setMqttMode('live-readonly');
    expect(session.mqttStatus()).toMatchObject({ mode: 'live-readonly', announce: false, ownStates: false });
    session.setAnnounce(true);
    expect(session.mqttStatus().announce).toBe(false);
  });

  it('before the first Run too: a switch set in another mode than live publish is not set', () => {
    session.setAnnounce(true);                 // the tests' default: offline
    session.setSendRetained(true);
    expect(session.mqttStatus()).toMatchObject({ mode: 'offline', announce: false, sendRetained: false });
    session.setMqttMode('live-publish');
    session.setSendRetained(true);
    expect(session.mqttStatus()).toMatchObject({ mode: 'live-publish', sendRetained: true });
    session.setMqttMode('offline');
    expect(session.mqttStatus()).toMatchObject({ mode: 'offline', sendRetained: false });
  });
});

describe('modes', () => {
  it('offline: nothing connects, publishes go to the in-page broker; the mode can be changed while nothing runs', async () => {
    await start();
    expect(session.snapshot()).toMatchObject({ mqtt: 'offline', mqttLink: 'none' });
    expect(session.mqttStatus()).toMatchObject({ mode: 'offline', sendRetained: false, announce: false, announced: 0 });
    await click();
    expect(broker.connects).toEqual([]);
    expect(session.mqttLog().every(e => e.kind === 'published' && e.note === 'not sent: offline (in-page broker only)')).toBe(true);
    expect(session.mqttLog().filter(e => e.origin === 'script').length).toBe(4);
    // The firmware's own: its birth message and the key's press event, to the in-page broker like the rest.
    expect(session.mqttLog().filter(e => e.origin === 'firmware').map(e => [e.what, e.topic, e.payload])).toEqual([
      ['availability', `buttonplus/${virtualDeviceId()}/status`, 'online'],
    ]);
    session.stop();
    // Stopped: the choice is for the next Run; nothing connects until then.
    expect(session.setMqttMode('live-readonly')).toBe('');
    expect(session.snapshot()).toMatchObject({ status: 'idle', mqtt: 'live-readonly', mqttLink: 'none' });
    await wait(60);
    expect(broker.connects).toEqual([]);
    await start();
    await until(() => session.snapshot().mqttLink === 'connected', 3000, 'the live connection');
    expect(session.snapshot().mqtt).toBe('live-readonly');
  });

  it('live read-only: connected as the virtual identity, the script\'s subscriptions are made, messages arrive, nothing is published', async () => {
    await start();
    await live('live-readonly');
    expect(broker.connects.map(c => [c.clientId, c.cleanSession, c.will])).toEqual([[virtualDeviceId(), true, undefined]]);
    expect(broker.subscriptionsOf(virtualDeviceId()).sort()).toEqual(['home/+/state', 'home/temp']);
    expect(session.snapshot()).toMatchObject({ mqtt: 'live-readonly', mqttLink: 'connected' });

    broker.publish('home/temp', '21.5');
    broker.publish('home/lamp/state', 'on');
    await until(() => session.pages()![0]!.displayItems[0]!.value === '21.5');
    await until(() => lines.some(l => l.text === 'home/lamp/state=on'));
    expect(logged().filter(l => l[0] === 'delivered')).toEqual([
      ['delivered', 'home/lamp/state', 'from the broker'], ['delivered', 'home/temp', 'from the broker'],
    ]);

    await click();
    await wait(60);
    expect(broker.published).toEqual([]);
    expect(logged().slice(0, 4).map(l => [l[0], l[2]])).toEqual(Array(4).fill(['would-publish', 'not sent: read-only']));
    expect(asked).toEqual([]);
  });

  it('live publish, nothing opted in: plain and own topics are sent with retain cleared; announcements are not; a real device\'s topic asks', async () => {
    await start();
    await live('live-publish');
    answer = null;                        // "No"
    await click();
    await until(() => scriptPublished().length === 2);
    await wait(60);
    expect(sent()).toEqual([
      ['home/light/set', 'ON', 0, false],                                   // retain cleared
      [`buttonplus/${virtualDeviceId()}/key`, 'press', 1, false],
    ]);
    expect(asked.length).toBe(1);
    expect(asked[0]!.title).toBe('This topic belongs to the real device btn_a00001. Publish as it?');
    expect(asked[0]!.message).toContain(REAL_TOPIC);
    expect(asked[0]!.choices.map(c => c.label)).toEqual(['Once', 'For this session']);
    expect(asked[0]!.cancel).toBe('No');
    expect(logged().slice(0, 4)).toEqual([
      ['blocked', `homeassistant/sensor/${virtualDeviceId()}_t/config`, 'not sent: Home Assistant discovery is off'],
      ['blocked', REAL_TOPIC, 'not sent: you said no to publishing as the real device btn_a00001'],
      ['sent', `buttonplus/${virtualDeviceId()}/key`, 'sent to the broker'],
      ['sent', 'home/light/set', 'sent to the broker · retain cleared'],
    ]);

    // "No" holds for the session: not asked again, not sent.
    await click();
    await wait(60);
    expect(asked.length).toBe(1);
    expect(broker.published.filter(p => p.topic === REAL_TOPIC)).toEqual([]);
  });

  it('the real device\'s topic: "Once" sends this one and asks again; "For this session" stops asking', async () => {
    await start();
    await live('live-publish');
    answer = 'once';
    await click();
    await until(() => broker.published.some(p => p.topic === REAL_TOPIC));
    expect(session.mqttLog().find(e => e.topic === REAL_TOPIC)).toMatchObject({
      kind: 'sent', note: 'sent to the broker · as the real device btn_a00001 (this once)',
    });
    await click();
    await until(() => asked.length === 2);

    answer = 'session';
    await click();
    await until(() => asked.length === 3);
    await click();
    await until(() => broker.published.filter(p => p.topic === REAL_TOPIC).length === 4);
    await wait(60);
    expect(asked.length).toBe(3);         // the fourth went without a question
    expect(session.mqttLog().find(e => e.topic === REAL_TOPIC)!.note).toBe('sent to the broker · as the real device btn_a00001');
  });

  it('while the question is open, more publishes to that topic are not sent and not queued', async () => {
    await start();
    await live('live-publish');
    let release: (v: string | null) => void = () => {};
    session.ask = (request) => { asked.push(request); return new Promise((resolve) => { release = resolve; }); };
    await click();
    await click();
    expect(asked.length).toBe(1);
    expect(session.mqttLog().filter(e => e.topic === REAL_TOPIC).map(e => e.note)).toEqual([
      'not sent: waiting for your answer about the real device btn_a00001',
      'held: asking whether to publish as the real device btn_a00001',
    ]);
    release('once');
    await until(() => broker.published.filter(p => p.topic === REAL_TOPIC).length === 1);
    await wait(60);
    expect(broker.published.filter(p => p.topic === REAL_TOPIC).length).toBe(1);
  });

  it('opt-ins: "send retained" keeps the flag; "announce" lets discovery through; "remove announcement" clears it on the broker', async () => {
    await start();
    await live('live-publish');
    session.setSendRetained(true);
    expect(session.mqttStatus()).toMatchObject({ sendRetained: true, announce: false });
    await click();
    await until(() => scriptPublished().length === 2);
    expect(sent()[0]).toEqual(['home/light/set', 'ON', 0, true]);
    expect(broker.retained.has('home/light/set')).toBe(true);

    const config = `homeassistant/sensor/${virtualDeviceId()}_t/config`;
    session.setAnnounce(true);
    // Announcing makes the connection again; the firmware then sends its birth message.
    const status = `buttonplus/${virtualDeviceId()}/status`;
    await until(() => broker.retained.has(status), 3000, 'the birth message');
    await click();
    await until(() => broker.published.some(p => p.topic === config));
    expect(broker.retained.get(config)).toEqual({ payload: '{}', qos: 0 });
    // Two announcement topics to take back: the birth message and the script's config.
    expect(session.mqttStatus()!.announced).toBe(2);
    expect(session.mqttLog().find(e => e.topic === config)!.note).toBe('sent to the broker · announcement · retained');

    expect(session.removeAnnouncement()).toBe('');
    await until(() => !broker.retained.has(config) && !broker.retained.has(status));
    expect(broker.published.filter(p => p.payload === '' && p.retain).map(p => p.topic).sort()).toEqual([status, config].sort());
    expect(session.mqttStatus()!.announced).toBe(0);
    expect(session.mqttLog().filter(e => e.note === 'announcement removed (empty retained message)').map(e => e.topic).sort()).toEqual([status, config].sort());
    expect(session.removeAnnouncement()).toBe('Nothing was announced in this session');
  });

  it('announce without "send retained": discovery goes out unretained (nothing stays on the broker)', async () => {
    await start();
    await live('live-publish');
    session.setAnnounce(true);
    await click();
    const config = `homeassistant/sensor/${virtualDeviceId()}_t/config`;
    await until(() => broker.published.some(p => p.topic === config));
    expect(broker.published.find(p => p.topic === config)!.retain).toBe(false);
    expect(broker.retained.size).toBe(0);
  });

  it('the opt-ins belong to live publish: leaving it clears them', async () => {
    await start();
    await live('live-publish');
    session.setSendRetained(true);
    session.setAnnounce(true);
    answer = 'session';
    await click();
    await until(() => broker.published.some(p => p.topic === REAL_TOPIC));
    session.setOwnStates(true);
    expect(session.mqttStatus()).toMatchObject({ sendRetained: true, announce: true, ownStates: true });
    const connections = broker.connects.length;
    session.setMqttMode('live-readonly');
    expect(session.mqttStatus()).toMatchObject({ mode: 'live-readonly', sendRetained: false, announce: false, ownStates: false });
    await until(() => session.snapshot().mqttLink === 'connected');
    // Leaving "announce" behind makes the connection again, without the will.
    expect(broker.connects.length).toBe(connections + 1);
    expect(broker.connects[broker.connects.length - 1]!.will).toBeUndefined();
    session.setMqttMode('live-publish');
    const before = asked.length;
    await click();
    await until(() => asked.length === before + 1);  // asked again
    // And outside live publish they cannot be set.
    session.setMqttMode('live-readonly');
    session.setSendRetained(true);
    session.setAnnounce(true);
    session.setOwnStates(true);
    expect(session.mqttStatus()).toMatchObject({ sendRetained: false, announce: false, ownStates: false });
  });

  it('a publish the firmware makes says its kind: each kind has its rule', async () => {
    await start();
    await live('live-publish');
    const link = (session as unknown as { mqtt: { links: BrokerLink[] } }).mqtt.links[0]!;
    const own = `buttonplus/${virtualDeviceId()}`;
    const fw = (kind: string) => ({ origin: 'firmware' as const, kind });
    session.clearLog();
    link.publish({ topic: `${own}/status`, payload: 'online', qos: 1, retain: true }, fw('availability'));
    link.publish({ topic: `${own}/button_3/state`, payload: '{"event_type":"click"}', qos: 0, retain: false }, fw('press'));
    link.publish({ topic: `homeassistant/sensor/${virtualDeviceId()}/t/config`, payload: '{}', qos: 0, retain: true }, fw('discovery'));
    link.publish({ topic: `${own}/sensor/temperature`, payload: '21.5', qos: 0, retain: false }, fw('sensor'));
    link.publish({ topic: `${own}/light/led_1/state`, payload: 'OFF', qos: 0, retain: true }, fw('state'));
    // A key's press topic from config.json that names a real device: asked about, like a script's.
    link.publish({ topic: 'buttonplus/btn_a00001/button_3/state', payload: 'x', qos: 0, retain: false }, fw('press'));
    await until(() => broker.published.length === 1);
    await wait(60);
    // Only the press under the virtual identity went out.
    expect(broker.published.map(p => p.topic)).toEqual([`${own}/button_3/state`]);
    expect(asked.length).toBe(1);
    expect(session.mqttLog().slice().reverse().map(e => [e.what, e.kind, e.note])).toEqual([
      ['availability', 'blocked', 'not sent: Home Assistant discovery is off'],
      ['press', 'sent', 'sent to the broker'],
      ['discovery', 'blocked', 'not sent: Home Assistant discovery is off'],
      ['sensor', 'blocked', 'not sent: the virtual device\'s own states are off'],
      ['state', 'blocked', 'not sent: the virtual device\'s own states are off'],
      ['press', 'blocked', 'not sent: you said no to publishing as the real device btn_a00001'],
    ]);

    // Own states on: they go out (retain still needs its own switch).
    session.setOwnStates(true);
    link.publish({ topic: `${own}/sensor/temperature`, payload: '21.6', qos: 0, retain: false }, fw('sensor'));
    link.publish({ topic: `${own}/light/led_1/state`, payload: 'ON', qos: 0, retain: true }, fw('state'));
    await until(() => broker.published.length === 3);
    expect(broker.published.slice(1).map(p => [p.topic, p.payload, p.retain])).toEqual([
      [`${own}/sensor/temperature`, '21.6', false], [`${own}/light/led_1/state`, 'ON', false],
    ]);
  });

  it('the firmware\'s periodic and bulk publishes are one row each in the log, counted where they stand', async () => {
    await start();
    await live('live-readonly');
    const link = (session as unknown as { mqtt: { links: BrokerLink[] } }).mqtt.links[0]!;
    const own = `buttonplus/${virtualDeviceId()}`;
    session.clearLog();
    for (let i = 0; i < 12; i++) {
      link.publish({ topic: `${own}/sensor/temperature`, payload: `2${i}`, qos: 0, retain: false }, { origin: 'firmware', kind: 'sensor' });
      link.publish({ topic: `${own}/sensor/humidity`, payload: `4${i}`, qos: 0, retain: false }, { origin: 'firmware', kind: 'sensor' });
    }
    for (let i = 0; i < 52; i++) {
      link.publish({ topic: `homeassistant/sensor/${virtualDeviceId()}/s${i}/config`, payload: '{}', qos: 0, retain: true }, { origin: 'firmware', kind: 'discovery' });
    }
    link.publish({ topic: 'home/light/set', payload: 'ON', qos: 0, retain: false }, { origin: 'script', kind: 'script' });
    const log = session.mqttLog();
    expect(log.map(e => [e.what, e.kind, e.count, e.payload])).toEqual([
      ['script', 'would-publish', 1, 'ON'],
      ['discovery', 'would-publish', 52, '{}'],
      ['sensor', 'would-publish', 12, '411'],
      ['sensor', 'would-publish', 12, '211'],
    ]);
    // The row keeps the payloads before the latest (the newest first), up to a few; discovery keeps the topics.
    const temperature = log[3]!;
    expect(temperature.history!.map(h => h.payload)).toEqual(['210', '29', '28', '27', '26', '25', '24', '23']);
    expect(log[1]!.topic).toBe(`homeassistant/sensor/${virtualDeviceId()}/s51/config`);
    expect(log[1]!.history![0]!.payload).toBe(`homeassistant/sensor/${virtualDeviceId()}/s50/config`);
    // A row that counts stays where it is: the order did not change.
    link.publish({ topic: `${own}/sensor/temperature`, payload: '99', qos: 0, retain: false }, { origin: 'firmware', kind: 'sensor' });
    expect(session.mqttLog().map(e => e.what)).toEqual(['script', 'discovery', 'sensor', 'sensor']);
    expect(session.mqttLog()[3]).toMatchObject({ count: 13, payload: '99' });
  });
});

describe('live: discovery on in config.json', () => {
  const DISCOVERY = { ...BASIC_CONFIG, brokers: [{ ...BASIC_CONFIG.brokers[0], hassdiscovery: true }] };
  const own = () => `buttonplus/${virtualDeviceId()}`;

  it('live publish without opt-ins: no will, and nothing of kind availability, discovery or sensor reaches the broker; a key press does', async () => {
    await start(SCRIPT, DISCOVERY);
    expect(session.setMqttMode('live-publish')).toBe('');
    await until(() => session.snapshot().mqttLink === 'connected');
    // The firmware's own subscriptions (Home Assistant's status, the keys' command topics) are made too.
    await until(() => broker.subscriptionsOf(virtualDeviceId()).includes(`${own()}/button_3/set`));
    expect(broker.subscriptionsOf(virtualDeviceId())).toContain('homeassistant/status');
    answer = null;
    await click();
    await wait(1600);                        // discovery starts a second after the connect; sensors report every second
    expect(broker.connects.map(c => c.will)).toEqual([undefined]);
    expect(broker.published.map(p => p.topic).sort()).toEqual([
      `${own()}/button_3/state`, `${own()}/key`, 'home/light/set',
    ].sort());
    expect(broker.published.find(p => p.topic === `${own()}/button_3/state`)!.payload).toContain('click');
    const log = session.mqttLog();
    expect(log.find(e => e.what === 'discovery')).toMatchObject({ kind: 'blocked', note: 'not sent: Home Assistant discovery is off' });
    expect(log.find(e => e.what === 'discovery')!.count).toBeGreaterThan(10);
    expect(log.find(e => e.what === 'availability')).toMatchObject({ kind: 'blocked', payload: 'online' });
    expect(log.filter(e => e.what === 'sensor').every(e => e.kind === 'blocked')).toBe(true);
    // The connection is cut (the tab is closed): the broker has no will to publish.
    broker.cut(virtualDeviceId());
    await wait(80);
    expect(broker.wills).toEqual([]);
  });

  it('a command on the virtual device\'s own key topic presses the key, as on the device', async () => {
    await start('fb.button("light").button.onClick(function () { print("clicked"); });', DISCOVERY);
    session.setMqttMode('live-readonly');
    await until(() => broker.subscriptionsOf(virtualDeviceId()).includes(`${own()}/button_3/set`), 3000);
    broker.publish(`${own()}/button_3/set`, 'PRESS');
    await until(() => lines.some(l => l.text === 'clicked'), 3000, 'the key press from MQTT');
  });

  it('announce on: the connection is made again with the firmware\'s will; birth and discovery go out; removal clears them', async () => {
    await start(SCRIPT, DISCOVERY);
    session.setMqttMode('live-publish');
    await until(() => session.snapshot().mqttLink === 'connected');
    session.setSendRetained(true);
    session.setAnnounce(true);
    await until(() => broker.connects.length === 2 && session.snapshot().mqttLink === 'connected');
    expect(broker.connects[1]!.will).toEqual({ topic: `${own()}/status`, payload: 'offline', qos: 1, retain: true });
    await until(() => broker.retained.has(`${own()}/status`), 3000, 'the birth message');
    expect(broker.retained.get(`${own()}/status`)!.payload).toBe('online');
    await until(() => [...broker.retained.keys()].filter(t => t.startsWith('homeassistant/')).length > 10, 6000, 'discovery');
    // Every announcement topic that went out is on the list to take back.
    await until(() => session.mqttStatus()!.announced === [...broker.retained.keys()].filter(t => t.includes(virtualDeviceId())).length, 8000, 'discovery to finish');
    expect(session.mqttStatus()!.announced).toBeGreaterThan(10);
    // Sensor states still do not go out: that is the third switch.
    expect(broker.published.some(p => /\/sensor\//.test(p.topic) && !p.topic.startsWith('homeassistant/'))).toBe(false);

    expect(session.removeAnnouncement()).toBe('');
    await until(() => [...broker.retained.keys()].filter(t => t.includes(virtualDeviceId())).length === 0, 3000, 'the retained announcements to go');
    expect(session.mqttStatus()).toMatchObject({ announced: 0, announce: false });
    // And the connection that had the will is replaced by one without (ended cleanly: no "offline" was published).
    await until(() => broker.connects.length === 3 && session.snapshot().mqttLink === 'connected');
    expect(broker.connects[2]!.will).toBeUndefined();
    expect(broker.wills).toEqual([]);
    await wait(1500);
    expect([...broker.retained.keys()].filter(t => t.includes(virtualDeviceId()))).toEqual([]);
  }, 20000);

  it('announce without "send retained": the will and the birth message are not retained', async () => {
    await start(SCRIPT, DISCOVERY);
    session.setMqttMode('live-publish');
    await until(() => session.snapshot().mqttLink === 'connected');
    session.setAnnounce(true);
    await until(() => broker.connects.length === 2 && session.snapshot().mqttLink === 'connected');
    expect(broker.connects[1]!.will).toMatchObject({ topic: `${own()}/status`, payload: 'offline', retain: false });
    await until(() => broker.published.some(p => p.topic === `${own()}/status`), 3000);
    expect(broker.published.find(p => p.topic === `${own()}/status`)).toMatchObject({ payload: 'online', retain: false });
    expect(broker.retained.size).toBe(0);
  });

  it('own states on: the sensors\' reports go out, about one each per second', async () => {
    await start(SCRIPT, DISCOVERY);
    session.setMqttMode('live-publish');
    await until(() => session.snapshot().mqttLink === 'connected');
    session.setOwnStates(true);
    await until(() => broker.published.filter(p => p.topic.includes('/sensor/')).length >= 8, 4000, 'sensor reports');
    expect(broker.published.every(p => p.topic.includes(virtualDeviceId()))).toBe(true);
    expect(session.mqttLog().filter(e => e.what === 'sensor' && e.kind === 'sent').length).toBeGreaterThanOrEqual(3);
  });
});

describe('live: the rest of the session', () => {
  it('inject stays local in a live mode: the script gets it, the broker does not', async () => {
    await start();
    await live('live-publish');
    expect(session.inject(0, 'home/temp', '30')).toBe(true);
    await until(() => session.pages()![0]!.displayItems[0]!.value === '30');
    await wait(40);
    expect(broker.published).toEqual([]);
    expect(logged().slice(0, 2)).toEqual([
      ['delivered', 'home/temp', 'local, from the in-page broker'],
      ['injected', 'home/temp', 'local (not sent to the broker): a subscription matches'],
    ]);
  });

  it('Run again keeps the connection and the mode; the new script\'s subscriptions replace the old ones', async () => {
    await start();
    await live('live-publish');
    await start('mqtt.subscribe(0, "other/topic", function (t, p) { print("got " + p); });');
    expect(session.snapshot()).toMatchObject({ status: 'running', mqtt: 'live-publish', mqttLink: 'connected' });
    await until(() => broker.subscriptionsOf(virtualDeviceId()).join() === 'other/topic');
    expect(broker.connects.length).toBe(1);
    broker.publish('other/topic', 'x');
    await until(() => lines.some(l => l.text === 'got x'));
    broker.publish('home/temp', '1');
    await wait(60);
    expect(session.mqttLog().some(e => e.topic === 'home/temp')).toBe(false);
  });

  it('Stop ends the connection (a clean DISCONNECT); the next Run connects again in the same mode', async () => {
    await start();
    await live('live-publish');
    session.stop();
    await until(() => broker.disconnects === 1 && broker.clientIds.length === 0);
    expect(session.snapshot()).toMatchObject({ status: 'idle', mqtt: 'live-publish', mqttLink: 'none' });
    await start();
    expect(session.snapshot().mqtt).toBe('live-publish');
    await until(() => session.snapshot().mqttLink === 'connected' && broker.subscriptionsOf(virtualDeviceId()).length >= 2, 3000, 'the connection of the second Run');
    expect(broker.connects.length).toBe(2);
    expect(broker.wills).toEqual([]);
  });

  it('a config with other brokers: the connection ends and one to the new broker is made, in the same mode', async () => {
    const other = await TestBroker.start();
    localStorage.setItem('fb-mqtt-ws:elsewhere.test', other.url);
    try {
      await start();
      await live('live-readonly');
      await start(SCRIPT, { ...BASIC_CONFIG, brokers: [{ brokerid: 'other', url: 'mqtt://elsewhere.test/' }] });
      expect(session.snapshot()).toMatchObject({ status: 'running', mqtt: 'live-readonly' });
      await until(() => broker.clientIds.length === 0 && other.clientIds.length === 1, 3000, 'the connection to move');
      expect(session.mqttStatus().brokers.map(b => [b.name, b.wsUrl, b.state])).toEqual([['other', other.url, 'connected']]);
    } finally {
      session.stop();
      await other.stop();
    }
  });

  it('a script that does not start ends the connection too, and makes none', async () => {
    await start();
    await live('live-publish');
    await start('var x = ;');
    expect(session.snapshot()).toMatchObject({ status: 'error', mqtt: 'live-publish', mqttLink: 'none' });
    await until(() => broker.clientIds.length === 0);
    // From nothing: a script that does not compile never reaches the broker.
    session.stop();
    const before = broker.connects.length;
    await start('var y = ;');
    await wait(80);
    expect(broker.connects.length).toBe(before);
  });

  it('back to offline: disconnects; the in-page broker takes over', async () => {
    await start();
    await live('live-readonly');
    expect(session.setMqttMode('offline')).toBe('');
    await until(() => broker.clientIds.length === 0);
    expect(session.snapshot()).toMatchObject({ mqtt: 'offline', mqttLink: 'none' });
    await click();
    expect(session.mqttLog()[0]).toMatchObject({ kind: 'published' });
    expect(session.inject(0, 'home/temp', '7')).toBe(true);
    await until(() => session.pages()![0]!.displayItems[0]!.value === '7');
  });

  it('the client id of config.json is not used; its user name and password are', async () => {
    await broker.stop();
    broker = await TestBroker.start({ users: { ha: 'secret' } });
    localStorage.setItem('fb-mqtt-ws:broker.test', broker.url);
    await start(SCRIPT, { ...BASIC_CONFIG, brokers: [{ brokerid: 'local', url: 'mqtt://broker.test/', username: 'ha', password: 'secret', clientid: 'btn_a00001' }] });
    expect(session.mqttStatus()!.brokers[0]).toMatchObject({ configClientId: 'btn_a00001', hasCredentials: true });
    await live('live-readonly');
    expect(broker.connects[0]).toMatchObject({ clientId: virtualDeviceId(), username: 'ha', password: 'secret' });
  });

  it('a wrong password in config.json: refused; the password saved in the settings connects again, with it', async () => {
    await broker.stop();
    broker = await TestBroker.start({ users: { ha: 'secret' } });
    localStorage.setItem('fb-mqtt-ws:broker.test', broker.url);
    await start(SCRIPT, { ...BASIC_CONFIG, brokers: [{ brokerid: 'local', url: 'mqtt://broker.test/', username: 'ha', password: 'wrong' }] });
    session.setMqttMode('live-readonly');
    await until(() => session.snapshot().mqttLink === 'error');
    expect(session.mqttStatus().brokers[0]!.error).toMatchObject({ kind: 'auth' });
    expect(broker.clientIds).toEqual([]);
    // The password is given, no user name: config.json's user name stays.
    expect(session.saveBrokerSettings('broker.test', { url: broker.url, username: '', password: 'secret' })).toBe('');
    await until(() => session.snapshot().mqttLink === 'connected', 3000, 'the connection with the new password');
    expect(broker.connects[broker.connects.length - 1]).toMatchObject({ username: 'ha', password: 'secret' });
    expect(session.mqttStatus().brokers[0]).toMatchObject({ wsUrl: broker.url, username: 'ha', overridden: true, error: null });
    // Saved again unchanged: the connection stays.
    const connections = broker.connects.length;
    session.saveBrokerSettings('broker.test', { url: broker.url, username: '', password: 'secret' });
    await wait(80);
    expect(broker.connects.length).toBe(connections);
    // Another user name: connected again, and refused (the broker knows no such user).
    session.saveBrokerSettings('broker.test', { url: broker.url, username: 'nobody', password: 'secret' });
    await until(() => session.snapshot().mqttLink === 'error');
    expect(broker.connects[broker.connects.length - 1]).toMatchObject({ username: 'nobody' });
  });

  it('the firmware\'s limits hold in a live mode: 8 subscriptions per broker, the size of a message', async () => {
    await start(`
      var n = 0;
      for (var i = 0; i < 10; i++) {
        try { mqtt.subscribe(0, "t/" + i, function (t, p) { print("len " + p.length); }); n++; } catch (e) { print("refused " + i); }
      }
      print("subscribed " + n);
    `);
    expect(lines.some(l => l.text === 'subscribed 8')).toBe(true);
    expect(session.setMqttMode('live-readonly')).toBe('');
    await until(() => broker.subscriptionsOf(virtualDeviceId()).length === 8);
    await wait(40);
    expect(broker.subscribed.length).toBe(8);
    broker.publish('t/0', 'x'.repeat(100));
    broker.publish('t/0', 'x'.repeat(5000));      // over the firmware's MQTT buffer: the script never sees it
    broker.publish('t/1', 'x'.repeat(50));
    await until(() => lines.some(l => l.text === 'len 50'));
    expect(lines.filter(l => l.text.startsWith('len ')).map(l => l.text)).toEqual(['len 100', 'len 50']);
  });
});

describe('live: when it does not connect', () => {
  it('nothing listens there: an error line that names the likely cause, the state says error, it keeps trying', async () => {
    const url = broker.url;
    await broker.stop();
    broker = await TestBroker.start();
    localStorage.setItem('fb-mqtt-ws:broker.test', url);
    localStorage.setItem('fb-mqtt-mode', 'live-readonly');
    await start();
    await until(() => session.snapshot().mqttLink === 'error');
    const b = session.mqttStatus()!.brokers[0]!;
    expect(b.error).toMatchObject({ kind: 'unreachable' });
    expect(lines.some(l => l.type === 'error' && l.text.includes('no websockets listener'))).toBe(true);
    // Nothing falls back to the in-page broker: the mode stays, and it is tried again.
    await wait(120);
    expect(session.snapshot()).toMatchObject({ mqtt: 'live-readonly', mqttLink: expect.stringMatching(/error|connecting/) });
    expect(session.mqttLog().some(e => e.kind === 'published')).toBe(false);
    // The address is corrected: it connects.
    expect(session.saveBrokerSettings('broker.test', { url: broker.url, username: '', password: '' })).toBe('');
    await until(() => session.snapshot().mqttLink === 'connected');
    expect(session.mqttStatus()!.brokers[0]!.error).toBeNull();
  });

  it('"Use offline" is the user\'s choice: the in-page broker takes over, and the browser remembers it', async () => {
    const url = broker.url;
    await broker.stop();
    broker = await TestBroker.start();
    localStorage.setItem('fb-mqtt-ws:broker.test', url);
    setDefaultStartMode('live-publish');
    await start();
    await until(() => session.snapshot().mqttLink === 'error');
    expect(session.snapshot().mqtt).toBe('live-publish');
    expect(session.setMqttMode('offline')).toBe('');
    expect(session.snapshot()).toMatchObject({ mqtt: 'offline', mqttLink: 'none' });
    expect(localStorage.getItem('fb-mqtt-mode')).toBe('offline');
    await click();
    expect(session.mqttLog().filter(e => e.origin === 'script').every(e => e.kind === 'published')).toBe(true);
  });

  it('an https page and a ws:// address: said at once, nothing is tried', async () => {
    session.securePage = true;
    await start();
    session.setMqttMode('live-publish');
    expect(session.mqttStatus()).toMatchObject({ securePage: true });
    expect(session.mqttStatus()!.brokers[0]!.error).toMatchObject({ kind: 'mixed-content' });
    expect(session.snapshot().mqttLink).toBe('error');
    await wait(60);
    expect(broker.connects).toEqual([]);
  });

  it('the broker restarts: connected again, subscriptions made again, the script keeps getting messages', async () => {
    await start();
    await live('live-readonly');
    broker.dropAll();
    await until(() => broker.connects.length === 2 && broker.subscriptionsOf(virtualDeviceId()).length >= 2, 3000);
    broker.publish('home/temp', '5');
    await until(() => session.pages()![0]!.displayItems[0]!.value === '5');
  });

  it('a config without brokers cannot go live: the choice is kept for a config that has one', async () => {
    await start('1;', { ...BASIC_CONFIG, brokers: [] });
    expect(session.setMqttMode('live-readonly')).toBe('config.json has no "brokers" to connect to');
    expect(session.snapshot()).toMatchObject({ mqtt: 'offline', mqttLink: 'none' });
    await start();
    await until(() => session.snapshot().mqttLink === 'connected', 3000, 'the live connection');
    expect(session.snapshot().mqtt).toBe('live-readonly');
  });
});
