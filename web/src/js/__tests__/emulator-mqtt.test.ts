// @vitest-environment node
/**
 * MQTT in the emulator: the firmware's subscription table, filter and event
 * ring (inside the wasm) against the in-page MockBroker.
 */
import { describe, expect, it } from 'vitest';
import type { MockSession } from '../emulator';
import { BASIC_CONFIG, makeEmulator } from './emulator-helpers';

const echo = 'mqtt.subscribe(0, "home/+/temp", function (topic, payload) { print(topic + "=" + payload); });';

describe('brokers', () => {
  it('come from config.json "brokers", in order', async () => {
    const config = { ...BASIC_CONFIG, brokers: [{ brokerid: 'first', url: 'mqtt://a' }, { brokerid: 'second', url: 'mqtt://b' }] };
    const { emu, rec } = await makeEmulator({ config });
    emu.run('print(mqtt.getBrokerCount(), mqtt.getBrokerName(0), mqtt.getBrokerName(1), mqtt.isConnected(1), mqtt.isConnected(2));');
    expect(rec.console).toEqual(['2 first second true false']);
    expect(emu.broker(1).connectedAs).toBe('virt_btn_0a1b2c');
    expect(emu.transport(2)).toBeUndefined();
  });

  it('a config without brokers has none: publish fails as on the device', async () => {
    const { emu, rec } = await makeEmulator({ config: {} });
    expect(emu.run('print(mqtt.getBrokerCount()); mqtt.publish(0, "a", "b");').ok).toBe(false);
    expect(rec.console).toEqual(['0']);
    expect(rec.errors[0]!.message).toBe('InternalError: failed to publish MQTT message');
  });
});

describe('subscriptions', () => {
  it('are listed by the mock, and messages go through the firmware\'s topic filter', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(echo);
    expect(rec.subscribes).toEqual(['0:home/+/temp']);
    expect(emu.broker(0).subscriptions).toEqual([{ topic: 'home/+/temp', qos: 0 }]);

    expect(emu.deliver(0, 'home/kitchen/temp', '21.5')).toBe('queued');
    expect(emu.deliver(0, 'home/kitchen/humidity', '40')).toBe('unwanted');
    expect(emu.deliver(0, 'home/a/b/temp', '1')).toBe('unwanted');
    expect(emu.deliver(1, 'home/kitchen/temp', '1')).toBe('unwanted');
    // Delivered by the next loop pass, not inside deliver().
    expect(rec.console).toEqual([]);
    clock.advance(0);
    expect(rec.console).toEqual(['home/kitchen/temp=21.5']);
  });

  it('the firmware matcher is the one the mock uses (+, #, $ topics)', async () => {
    const { emu } = await makeEmulator();
    expect(emu.topicMatches('a/+/c', 'a/b/c')).toBe(true);
    expect(emu.topicMatches('a/+/c', 'a/c')).toBe(false);
    expect(emu.topicMatches('a/#', 'a')).toBe(true);
    expect(emu.topicMatches('a/#', 'ab')).toBe(false);
    expect(emu.topicMatches('#', '$SYS/x')).toBe(false);
    expect(emu.topicMatches('a/b+', 'a/bc')).toBe(false);
  });

  it('the firmware allows 8 per broker: the 9th throws its error', async () => {
    const { emu, rec } = await makeEmulator();
    const result = emu.run('for (var i = 0; i < 9; i++) mqtt.subscribe(0, "t/" + i, function () {});');
    expect(result.ok).toBe(false);
    expect(rec.errors[0]!.message).toBe('InternalError: too many MQTT subscriptions for broker 0');
    expect(emu.broker(0).subscriptions).toHaveLength(emu.limits.subscriptionsPerBroker);
    expect(emu.stats().subscriptions).toBe(8);
  });

  it('only the first two brokers take script subscriptions (the firmware\'s table)', async () => {
    // A broker entry without a url is skipped by the firmware.
    const brokers = ['a', 'b', 'c'].map(brokerid => ({ brokerid, url: `mqtt://${brokerid}.test/` }));
    const { emu, clock, rec } = await makeEmulator({ config: { ...BASIC_CONFIG, brokers } });
    emu.run('mqtt.subscribe(1, "x", function () {}); mqtt.subscribe(2, "x", function () {});');
    // A subscribe reaches the broker after the script's turn, as on the device.
    clock.advance(0);
    expect(rec.subscribes).toEqual(['1:x']);
    expect(rec.errors[0]!.message).toBe('RangeError: broker ID 2 out of range (0-1)');
  });

  it('unsubscribe reaches the broker', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(echo);
    clock.advance(0);
    expect(emu.broker(0).subscriptions).toEqual([{ topic: 'home/+/temp', qos: 0 }]);
    emu.eval('mqtt.unsubscribe(0, "home/+/temp")');
    clock.advance(0);
    expect(rec.unsubscribes).toEqual(['0:home/+/temp']);
    expect(emu.broker(0).subscriptions).toEqual([]);
    expect(emu.deliver(0, 'home/kitchen/temp', '1')).toBe('unwanted');
  });

  it('subscribe and unsubscribe within one turn of the script: nothing is sent to the broker', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(echo + ' mqtt.unsubscribe(0, "home/+/temp");');
    clock.advance(100);
    expect(rec.subscribes).toEqual([]);
    expect(rec.unsubscribes).toEqual([]);
    expect(emu.broker(0).subscriptions).toEqual([]);
  });

  it('reports an exception in a message callback', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run('mqtt.subscribe(0, "a", function (t, p) { JSON.parse(p).x.y; });');
    emu.broker(0).inject('a', '{}');
    clock.advance(0);
    expect(rec.errors).toMatchObject([{ where: 'callback' }]);
    expect(rec.errors[0]!.message).toMatch(/^TypeError/);
  });
});

describe('the event ring and message sizes', () => {
  it('delivers at most 8 messages per loop pass', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run('mqtt.subscribe(0, "n", function (t, p) { print(p); });');
    for (let i = 0; i < 10; i++) expect(emu.deliver(0, 'n', String(i))).toBe('queued');
    clock.advance(0);
    expect(rec.console).toEqual(['0', '1', '2', '3', '4', '5', '6', '7']);
    clock.advance(emu.limits.loopDelayMs);
    expect(rec.console).toHaveLength(10);
  });

  it('drops messages when the 2 KB ring is full, and counts them', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run('var got = 0; mqtt.subscribe(0, "n", function () { got++; });');
    const payload = 'x'.repeat(100);
    // A record is 6 bytes + topic + payload (src/utils/MessageRing.h).
    const fits = Math.floor(emu.limits.mqttQueueBytes / (6 + 1 + payload.length));
    const results = Array.from({ length: fits + 3 }, () => emu.deliver(0, 'n', payload));
    expect(results.filter(r => r === 'queued')).toHaveLength(fits);
    expect(results.slice(fits)).toEqual(['dropped', 'dropped', 'dropped']);
    expect(emu.stats()).toMatchObject({ mqttDropped: 3, mqttQueueUsed: fits * 107 });
    clock.advance(1000);
    expect(emu.eval('got')).toEqual({ ok: true, value: String(fits) });
    expect(emu.stats().mqttQueueUsed).toBe(0);
    expect(rec.logs.some(line => line.includes('JS MQTT queue full or busy: dropped 3 event(s)'))).toBe(true);
  });

  it('a message larger than the MQTT in-buffer never reaches the script', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run('mqtt.subscribe(0, "big", function () { print("got"); });');
    const room = emu.limits.mqttInBufferBytes - (1 + 2 + 2 + 'big'.length);
    expect(emu.deliver(0, 'big', 'x'.repeat(room))).toBe('queued');
    expect(emu.deliver(0, 'big', 'x'.repeat(room + 1))).toBe('too-large');
    expect(rec.logs.some(line => line.includes('larger than the MQTT in-buffer'))).toBe(true);
  });
});

describe('MockBroker', () => {
  it('records every publish with topic, payload, qos, retain and time', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run('mqtt.publish(0, "home/light/set", "ON"); setTimeout(function () { mqtt.publish(0, "home/state", "1", 1, 1); }, 500);');
    clock.advance(500);
    const start = clock.now() - 500;
    expect(emu.broker(0).published).toEqual([
      // The firmware's birth message, at connect.
      { topic: 'buttonplus/virt_btn_0a1b2c/status', payload: 'online', qos: 0, retain: true, time: start },
      { topic: 'home/light/set', payload: 'ON', qos: 0, retain: false, time: start },
      { topic: 'home/state', payload: '1', qos: 1, retain: true, time: start + 500 },
    ]);
    expect(rec.publishes.filter(p => p.origin === 'script').map(p => [p.broker, p.topic, p.payload, p.action, p.reason])).toEqual([
      [0, 'home/light/set', 'ON', 'mock', 'offline'],
      [0, 'home/state', '1', 'mock', 'offline'],
    ]);
  });

  it('inject() delivers only to matching subscriptions', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(echo);
    expect(emu.broker(0).inject('home/hall/temp', '19')).toBe(true);
    expect(emu.broker(0).inject('other', '1')).toBe(false);
    clock.advance(0);
    expect(rec.console).toEqual(['home/hall/temp=19']);
  });

  it('without loopback a script does not hear its own publish; with it, it does, as with a real broker', async () => {
    // The publish in a later turn: a subscribe reaches the broker after the turn it was made in.
    const script = 'mqtt.subscribe(0, "ping", function (t, p) { print("heard " + p); }); setTimeout(function () { mqtt.publish(0, "ping", "1"); }, 50);';
    const quiet = await makeEmulator();
    quiet.emu.run(script);
    quiet.clock.advance(200);
    expect(quiet.rec.console).toEqual([]);

    const loop = await makeEmulator();
    loop.emu.broker(0).loopback = true;
    loop.emu.run(script);
    loop.clock.advance(200);
    expect(loop.rec.console).toEqual(['heard 1']);

    // In the same turn as the subscribe the broker does not know the subscription yet: not heard.
    const early = await makeEmulator();
    early.emu.broker(0).loopback = true;
    early.emu.run('mqtt.subscribe(0, "ping", function (t, p) { print("heard " + p); }); mqtt.publish(0, "ping", "1");');
    early.clock.advance(200);
    expect(early.rec.console).toEqual([]);
  });

  it('a script that answers its own messages is paced by the loop, not an endless call', async () => {
    const { emu, clock } = await makeEmulator();
    emu.broker(0).loopback = true;
    emu.run('var n = 0; mqtt.subscribe(0, "p", function () { n++; mqtt.publish(0, "p", "x"); });');
    clock.advance(0);
    emu.eval('mqtt.publish(0, "p", "x")');
    clock.advance(5 * emu.limits.loopDelayMs);
    expect(emu.eval('n')).toEqual({ ok: true, value: '6' });
  });

  it('remembers retained messages and delivers them on subscribe', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.broker(0).inject('home/hall/temp', '18', { retain: true });
    emu.broker(0).inject('home/attic/temp', '12', { retain: true });
    emu.broker(0).inject('home/attic/temp', '', { retain: true });
    emu.run(echo);
    clock.advance(0);
    expect(rec.console).toEqual(['home/hall/temp=18']);
    expect(emu.broker(0).retainedMessages.filter(m => !m.topic.startsWith('buttonplus/virt_btn_'))).toEqual([{ topic: 'home/hall/temp', payload: '18', qos: 0, retain: true }]);
  });

  it('after a restart the new script subscribes again and gets the retained messages again', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.broker(0).inject('home/hall/temp', '18', { retain: true });
    emu.run(echo);
    clock.advance(0);
    emu.restart();
    clock.advance(0);
    expect(rec.console).toEqual(['home/hall/temp=18', 'home/hall/temp=18']);
    expect(rec.unsubscribes).toEqual(['0:home/+/temp']);
  });

  it('a session can be exported, imported and replayed', async () => {
    const first = await makeEmulator();
    first.emu.run(echo + ' mqtt.publish(0, "home/ready", "1");');
    first.emu.broker(0).inject('home/config', 'c', { retain: true });
    first.clock.advance(1000);
    first.emu.broker(0).inject('home/hall/temp', '18');
    first.clock.advance(250);
    first.emu.broker(0).inject('home/hall/temp', '19');
    const session = JSON.parse(JSON.stringify(first.emu.broker(0).exportSession())) as MockSession;
    expect(session.events.map(e => [e.t, e.dir, e.topic, e.payload])).toEqual([
      [0, 'out', 'buttonplus/virt_btn_0a1b2c/status', 'online'],
      [0, 'out', 'home/ready', '1'],
      [0, 'in', 'home/config', 'c'],
      [1000, 'in', 'home/hall/temp', '18'],
      [1250, 'in', 'home/hall/temp', '19'],
    ]);
    expect(session.retained.filter(m => m.topic.startsWith('home/'))).toEqual([{ topic: 'home/config', payload: 'c', qos: 0, retain: true }]);

    const second = await makeEmulator();
    second.emu.run(echo);
    second.emu.broker(0).importSession(session);
    second.emu.broker(0).replay(session);
    second.clock.advance(999);
    expect(second.rec.console).toEqual([]);
    second.clock.advance(1);
    expect(second.rec.console).toEqual(['home/hall/temp=18']);
    second.clock.advance(250);
    expect(second.rec.console).toEqual(['home/hall/temp=18', 'home/hall/temp=19']);
    expect(second.emu.broker(0).retainedMessages.filter(m => m.topic.startsWith('home/'))).toHaveLength(1);
    expect(second.clock.timers).toBe(0); // the replay left no timer behind
  });
});

describe('connect and disconnect', () => {
  const script = `
    mqtt.onConnect(0, function () { print("connected"); });
    mqtt.onDisconnect(0, function () { print("disconnected"); });
    print("top", mqtt.isConnected(0));
  `;

  it('after a deploy the broker is already connected: onConnect does not run', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(script);
    clock.advance(100);
    expect(rec.console).toEqual(['top true']);
  });

  it('boot "cold": the broker connects after the top level, and onConnect runs', async () => {
    const { emu, clock, rec } = await makeEmulator({ boot: 'cold' });
    emu.run(script);
    expect(rec.console).toEqual(['top false']);
    clock.advance(0);
    expect(rec.console).toEqual(['top false', 'connected']);
  });

  it('a connection that drops and comes back runs the handlers; publishing while it is down fails', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(script + echo);
    emu.broker(0).inject('home/hall/temp', '5', { retain: true });
    clock.advance(0);
    emu.broker(0).setConnected(false);
    clock.advance(0);
    expect(emu.eval('mqtt.publish(0, "a", "b")')).toMatchObject({ ok: false, error: { message: 'InternalError: failed to publish MQTT message' } });
    emu.broker(0).setConnected(true);
    clock.advance(emu.limits.loopDelayMs);
    expect(rec.console).toEqual(['top true', 'home/hall/temp=5', 'disconnected', 'connected', 'home/hall/temp=5']);
  });
});
