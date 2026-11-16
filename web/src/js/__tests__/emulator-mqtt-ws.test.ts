// @vitest-environment node
import { describe, it, expect, afterEach } from 'vitest';
import { MqttLiveClient, connackError, type LinkError, type LinkState, type MqttLiveOptions, type WireMessage } from '../emulator/mqtt-live';
import { TestBroker, nodeSocket, topicMatches, until } from './mqtt-test-broker';

/**
 * The live client (MQTT.js under mqtt-live.ts) against a broker in the test
 * process: what this project decides about the connection, not the wire
 * format (that is the library's).
 */
type TestClient = MqttLiveClient & { messages: WireMessage[]; states: Array<[LinkState, LinkError['kind'] | null]> };

let broker: TestBroker | null = null;
const clients: MqttLiveClient[] = [];

function client(url: string, extra: Partial<MqttLiveOptions> = {}): TestClient {
  const c = new MqttLiveClient({ url, clientId: 'virt_btn_0a1b2c', socket: nodeSocket, backoffMs: 20, securePage: false, ...extra }) as TestClient;
  c.messages = [];
  c.states = [];
  c.onMessage(m => c.messages.push(m));
  c.onState((s, e) => c.states.push([s, e?.kind ?? null]));
  clients.push(c);
  return c;
}

afterEach(async () => {
  for (const c of clients.splice(0)) c.close();
  await broker?.stop();
  broker = null;
});

describe('helpers', () => {
  it('CONNACK return codes in words', () => {
    expect(connackError(5).kind).toBe('auth');
    expect(connackError(4).kind).toBe('auth');
    expect(connackError(2).kind).toBe('client-id');
    expect(connackError(3)).toMatchObject({ kind: 'refused' });
  });

  it('the test broker\'s filter match (the wildcards)', () => {
    expect(topicMatches('a/+/c', 'a/b/c')).toBe(true);
    expect(topicMatches('a/#', 'a/b/c')).toBe(true);
    expect(topicMatches('a/+', 'a/b/c')).toBe(false);
    expect(topicMatches('a/b', 'a/b')).toBe(true);
  });
});

describe('the live client against a broker', () => {
  it('connects as the virtual identity: MQTT 3.1.1, subprotocol mqtt, clean session, keepalive, no will', async () => {
    broker = await TestBroker.start();
    const c = client(broker.url, { keepAliveSec: 45 });
    c.connect();
    await until(() => c.state === 'connected');
    expect(broker.connects).toEqual([{
      clientId: 'virt_btn_0a1b2c', username: undefined, password: undefined, keepAlive: 45, cleanSession: true,
      protocol: 'MQTT', level: 4, subprotocol: 'mqtt',
    }]);
    expect(c.states).toEqual([['connecting', null], ['connected', null]]);
    expect(c.lastError).toBeNull();
  });

  it('a last will only when one is given; the broker publishes it when the connection is cut, not after a clean close', async () => {
    broker = await TestBroker.start();
    const will = { topic: 'buttonplus/virt_btn_0a1b2c/status', payload: 'offline', qos: 1, retain: true };
    const c = client(broker.url, { will });
    c.connect();
    await until(() => c.state === 'connected');
    expect(broker.connects[0]!.will).toEqual(will);
    broker.cut('virt_btn_0a1b2c');
    await until(() => broker!.wills.length === 1);
    expect(broker.wills[0]).toMatchObject({ topic: will.topic, payload: 'offline', retain: true });
    await until(() => c.state === 'connected');
    c.close();
    await until(() => broker!.disconnects === 1);
    await new Promise(r => setTimeout(r, 60));
    expect(broker.wills.length).toBe(1);
  });

  it('user name and password: accepted when right, "auth" (and no more attempts) when wrong or missing', async () => {
    broker = await TestBroker.start({ users: { ha: 'secret' } });
    const ok = client(broker.url, { username: 'ha', password: 'secret' });
    ok.connect();
    await until(() => ok.state === 'connected');
    expect(broker.connects[0]).toMatchObject({ username: 'ha', password: 'secret' });

    const wrong = client(broker.url, { clientId: 'virt_btn_ffffff', username: 'ha', password: 'nope' });
    wrong.connect();
    await until(() => wrong.lastError !== null);
    expect(wrong.lastError!.kind).toBe('auth');
    expect(wrong.state).toBe('disconnected');
    const none = client(broker.url, { clientId: 'virt_btn_eeeeee' });
    none.connect();
    await until(() => none.lastError !== null);
    expect(none.lastError!.kind).toBe('auth');
    const attempts = broker.connects.length;
    await new Promise(r => setTimeout(r, 150));
    expect(broker.connects.length).toBe(attempts);      // a refused login is not tried again
  });

  it('subscribe with wildcards; retained messages arrive flagged; QoS 1 deliveries are acknowledged', async () => {
    broker = await TestBroker.start();
    broker.publish('home/temp', '19', { retain: true, qos: 1 });
    const c = client(broker.url);
    c.connect();
    await until(() => c.state === 'connected');
    c.subscribe('home/+', 1);
    c.subscribe('other/#', 0);
    await until(() => c.messages.length === 1);
    expect(c.messages[0]).toEqual({ topic: 'home/temp', payload: '19', qos: 1, retain: true });
    expect(broker.subscribed.map(s => [s.topic, s.qos])).toEqual([['home/+', 1], ['other/#', 0]]);

    broker.publish('home/hum', '45', { qos: 1 });
    broker.publish('other/a/b', 'x', { qos: 1 });      // subscribed with QoS 0: delivered with 0
    broker.publish('nobody/wants', 'y');
    await until(() => c.messages.length === 3);
    expect(c.messages.slice(1)).toEqual([
      { topic: 'home/hum', payload: '45', qos: 1, retain: false },
      { topic: 'other/a/b', payload: 'x', qos: 0, retain: false },
    ]);
    await until(() => broker!.acked.length === 2);

    c.unsubscribe('home/+');
    await until(() => broker!.unsubscribed.length === 1);
    broker.publish('home/hum', '46');
    await new Promise(r => setTimeout(r, 40));
    expect(c.messages.length).toBe(3);
  });

  it('publish QoS 0 and 1, retain, UTF-8 payloads; QoS 2 goes out as 1; nothing is kept while disconnected', async () => {
    broker = await TestBroker.start();
    const c = client(broker.url);
    expect(c.publish({ topic: 'a', payload: 'early', qos: 0, retain: false })).toBe(false);
    c.connect();
    await until(() => c.state === 'connected');
    expect(c.publish({ topic: 'virt/a', payload: '21,5 °C \u{F050F}', qos: 0, retain: false })).toBe(true);
    c.publish({ topic: 'virt/b', payload: 'on', qos: 1, retain: true });
    c.publish({ topic: 'virt/c', payload: '', qos: 2, retain: false });
    await until(() => broker!.published.length === 3);
    expect(broker.published).toEqual([
      { clientId: 'virt_btn_0a1b2c', topic: 'virt/a', payload: '21,5 °C \u{F050F}', qos: 0, retain: false },
      { clientId: 'virt_btn_0a1b2c', topic: 'virt/b', payload: 'on', qos: 1, retain: true },
      { clientId: 'virt_btn_0a1b2c', topic: 'virt/c', payload: '', qos: 1, retain: false },
    ]);
    expect(broker.retained.get('virt/b')).toEqual({ payload: 'on', qos: 1 });
    // The early one was not queued for the connection.
    await new Promise(r => setTimeout(r, 40));
    expect(broker.published.some(p => p.payload === 'early')).toBe(false);
  });

  it('incoming payloads are decoded here as UTF-8, whatever the frames: a large message, packets split anywhere', async () => {
    broker = await TestBroker.start({ chunk: 3 });
    const c = client(broker.url);
    c.connect();
    await until(() => c.state === 'connected');
    c.subscribe('big/#', 1);
    await until(() => broker!.subscribed.length === 1);
    const big = 'é'.repeat(30000);
    broker.publish('big/one', big, { qos: 1 });
    broker.publish('big/two', 'small °C');
    await until(() => c.messages.length === 2, 5000);
    expect([c.messages[0]!.topic, c.messages[0]!.payload === big, c.messages[1]]).toEqual(
      ['big/one', true, { topic: 'big/two', payload: 'small °C', qos: 0, retain: false }]);
  });

  it('keepalive: pings when idle; a broker that stops answering is dropped and connected again', async () => {
    broker = await TestBroker.start();
    const c = client(broker.url, { keepAliveSec: 1 });
    c.connect();
    await until(() => c.state === 'connected');
    await until(() => broker!.pings >= 1, 4000, 'a ping');
    expect(c.state).toBe('connected');

    broker.options.ignorePing = true;
    await until(() => c.states.some(([s, e]) => s === 'disconnected' && e !== null), 6000, 'the dead connection to be dropped');
    broker.options.ignorePing = false;
    await until(() => c.state === 'connected', 4000, 'the reconnect');
    expect(broker.connects.length).toBeGreaterThanOrEqual(2);
  }, 15000);

  it('the broker goes away: connects again with a pause; it makes no subscription by itself (the firmware does)', async () => {
    broker = await TestBroker.start();
    const c = client(broker.url);
    c.connect();
    await until(() => c.state === 'connected');
    c.subscribe('home/#', 1);
    await until(() => broker!.subscribed.length === 1);
    broker.dropAll();
    await until(() => c.states.some(([s, e]) => s === 'disconnected' && e === 'closed'));
    await until(() => c.state === 'connected');
    await new Promise(r => setTimeout(r, 80));
    expect(broker.subscribed.length).toBe(1);          // not made again by the client
    expect(c.lastError).toBeNull();
    // The owner of the subscriptions makes them again, once.
    c.subscribe('home/#', 1);
    await until(() => broker!.subscribed.length === 2);
    broker.publish('home/a', '1');
    await until(() => c.messages.length === 1);
  });

  it('a subscribe while not connected is not kept', async () => {
    broker = await TestBroker.start();
    const c = client(broker.url);
    c.subscribe('early/topic', 0);
    c.connect();
    await until(() => c.state === 'connected');
    await new Promise(r => setTimeout(r, 60));
    expect(broker.subscribed).toEqual([]);
  });

  it('close(): DISCONNECT, then no new attempt', async () => {
    broker = await TestBroker.start();
    const c = client(broker.url);
    c.connect();
    await until(() => c.state === 'connected');
    c.close();
    await until(() => broker!.disconnects === 1 && broker!.clientIds.length === 0);
    expect(c.state).toBe('disconnected');
    expect(c.lastError).toBeNull();
    await new Promise(r => setTimeout(r, 80));
    expect(broker.connects.length).toBe(1);
  });

  it('nothing listens at the address: "unreachable", and it keeps trying', async () => {
    broker = await TestBroker.start();
    const url = broker.url;
    await broker.stop();
    broker = null;
    const c = client(url);
    c.connect();
    await until(() => c.lastError !== null);
    expect(c.lastError).toMatchObject({ kind: 'unreachable' });
    expect(c.lastError!.message).toContain('no websockets listener');
    await until(() => c.states.filter(([s]) => s === 'connecting').length >= 3, 3000, 'more attempts');
  });

  it('a WebSocket that is not MQTT: "not-mqtt"', async () => {
    broker = await TestBroker.start({ mute: true });
    const c = client(broker.url, { connectTimeoutMs: 80 });
    c.connect();
    await until(() => c.lastError?.kind === 'not-mqtt', 3000);
  });

  it('https page and a ws:// address: "mixed-content", nothing is tried', async () => {
    let made = 0;
    const c = client('ws://10.0.0.5:9001', { securePage: true, socket: (u, p, o) => { made++; return nodeSocket(u, p, o); } });
    c.connect();
    expect(c.lastError).toMatchObject({ kind: 'mixed-content' });
    expect(c.lastError!.message).toContain('https');
    expect(c.state).toBe('disconnected');
    expect(made).toBe(0);
    const bad = client('10.0.0.5:9001');
    bad.connect();
    expect(bad.lastError!.kind).toBe('url');
  });

  it('another client with the same id: the broker closes this one again and again -> "taken-over"', async () => {
    broker = await TestBroker.start();
    const a = client(broker.url);
    const b = client(broker.url);
    a.connect();
    await until(() => a.state === 'connected');
    b.connect();
    await until(() => a.lastError?.kind === 'taken-over' || b.lastError?.kind === 'taken-over', 5000, 'the take-over to be noticed');
    const error = (a.lastError?.kind === 'taken-over' ? a : b).lastError!;
    expect(error.message).toContain('virt_btn_0a1b2c');
  });

  it('a refusal of the client id is not tried again', async () => {
    broker = await TestBroker.start({ connack: 2 });
    const c = client(broker.url);
    c.connect();
    await until(() => c.lastError !== null);
    expect(c.lastError!.kind).toBe('client-id');
    await new Promise(r => setTimeout(r, 120));
    expect(broker.connects.length).toBe(1);
  });
});
