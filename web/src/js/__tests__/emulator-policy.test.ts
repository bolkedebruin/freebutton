// @vitest-environment node
/**
 * The publish-safety policy (pure), and the emulator applying it. Stage 1
 * only talks to the in-page mock; the live modes are tested against it.
 */
import { describe, expect, it } from 'vitest';
import {
  decidePublish, isAnnouncement, isPeriodicState, realDeviceIn, isDiscoveryOrAvailability, isOwnTopic, isVirtualIdentity, rewriteIdentity, virtualIdentity,
  type PublishPolicy, type PublishRequest,
} from '../emulator/publish-policy';
import { makeEmulator } from './emulator-helpers';

const request = (topic: string, extra: Partial<PublishRequest> = {}): PublishRequest =>
  ({ broker: 0, topic, payload: 'ON', qos: 0, retain: false, origin: 'script', ...extra });

const live: PublishPolicy = { mode: 'live-publish', virtualId: 'virt_btn_0a1b2c', realId: 'btn_a00001' };

describe('virtual identity', () => {
  it('is virt_btn_ + 6 hex digits, never the shape of a device id', () => {
    for (let i = 0; i < 50; i++) {
      const id = virtualIdentity();
      expect(id).toMatch(/^virt_btn_[0-9a-f]{6}$/);
      expect(isVirtualIdentity(id)).toBe(true);
    }
    expect(virtualIdentity(() => 0.999)).toBe('virt_btn_ffffff');
    expect(isVirtualIdentity('btn_a00001')).toBe(false);
    expect(isVirtualIdentity('virt_btn_A00001')).toBe(false);
  });

  it('rewrites the real device id where it is a whole token', () => {
    expect(rewriteIdentity('buttonplus/btn_a00001/button_3/state', live)).toBe('buttonplus/virt_btn_0a1b2c/button_3/state');
    expect(rewriteIdentity('homeassistant/event/btn_a00001_key3/config', live)).toBe('homeassistant/event/virt_btn_0a1b2c_key3/config');
    expect(rewriteIdentity('btn_a00001', live)).toBe('virt_btn_0a1b2c');
    // Another device, a longer id, an id inside a word: untouched.
    expect(rewriteIdentity('buttonplus/btn_a00002/x', live)).toBe('buttonplus/btn_a00002/x');
    expect(rewriteIdentity('buttonplus/btn_a000011/x', live)).toBe('buttonplus/btn_a000011/x');
    expect(rewriteIdentity('xbtn_a00001/x', live)).toBe('xbtn_a00001/x');
    // Already virtual: not rewritten twice.
    expect(rewriteIdentity('buttonplus/virt_btn_0a1b2c/x', live)).toBe('buttonplus/virt_btn_0a1b2c/x');
    expect(rewriteIdentity('a/btn_a00001', { virtualId: 'virt_btn_0a1b2c' })).toBe('a/btn_a00001');
  });

  it('knows its own topics and the firmware\'s discovery / availability topics', () => {
    expect(isOwnTopic('buttonplus/virt_btn_0a1b2c/x', 'virt_btn_0a1b2c')).toBe(true);
    expect(isOwnTopic('buttonplus/virt_btn_0a1b2cd/x', 'virt_btn_0a1b2c')).toBe(false);
    expect(isDiscoveryOrAvailability('homeassistant/sensor/x/config')).toBe(true);
    expect(isDiscoveryOrAvailability('buttonplus/virt_btn_0a1b2c/status')).toBe(true);
    expect(isDiscoveryOrAvailability('buttonplus/virt_btn_0a1b2c/sensor/status')).toBe(false);
    expect(isDiscoveryOrAvailability('home/status')).toBe(false);
  });
});

describe('decidePublish', () => {
  it('offline: everything goes to the mock unchanged', () => {
    expect(decidePublish(request('buttonplus/btn_a00001/x', { retain: true }), { ...live, mode: 'offline' })).toEqual({
      action: 'mock', topic: 'buttonplus/btn_a00001/x', retain: true, rewritten: false, retainCleared: false, reason: 'offline',
    });
  });

  it('live-readonly: everything is blocked, with a "would publish" line', () => {
    const decision = decidePublish(request('home/light/set', { qos: 1, retain: true }), { ...live, mode: 'live-readonly' });
    expect(decision).toMatchObject({ action: 'block', reason: 'read-only', topic: 'home/light/set' });
    expect(decision.log).toBe('would publish to broker 0: home/light/set = "ON" (qos 1, retain); blocked: the emulator is connected read-only');
  });

  it('live-publish: a topic under the device id goes out under the virtual identity, retain cleared', () => {
    expect(decidePublish(request('buttonplus/btn_a00001/key/3', { retain: true }), live)).toEqual({
      action: 'send', topic: 'buttonplus/virt_btn_0a1b2c/key/3', retain: false, rewritten: true, retainCleared: true,
      reason: 'own-topic',
    });
    expect(decidePublish(request('buttonplus/virt_btn_0a1b2c/key/3', { retain: true }), { ...live, allowRetain: true }))
      .toMatchObject({ action: 'send', retain: true, retainCleared: false, rewritten: false });
  });

  it('live-publish: a literal topic is sent; with an allow list only what it has', () => {
    expect(decidePublish(request('home/light/set'), live)).toMatchObject({ action: 'send', reason: 'allowed', topic: 'home/light/set' });

    const listed = { ...live, allowTopics: ['home/light/set'] };
    expect(decidePublish(request('home/light/set'), listed)).toMatchObject({ action: 'send', reason: 'allowed' });
    const blocked = decidePublish(request('home/other/set'), listed);
    expect(blocked).toMatchObject({ action: 'block', reason: 'not-allowed' });
    expect(blocked.log).toContain('would publish to broker 0: home/other/set = "ON"');
    // Filters need a matcher; without one only exact topics match.
    expect(decidePublish(request('home/light/set'), { ...live, allowTopics: ['home/+/set'] }).action).toBe('block');
    const plus = (filter: string, topic: string): boolean => new RegExp('^' + filter.replace('+', '[^/]+') + '$').test(topic);
    expect(decidePublish(request('home/light/set'), { ...live, allowTopics: ['home/+/set'] }, plus).action).toBe('send');
    // The virtual device's own topics need no entry.
    expect(decidePublish(request(`buttonplus/${live.virtualId}/key`), { ...live, allowTopics: [] }).action).toBe('send');
  });

  it('live-publish: a real device\'s topic is held when asked for, until that exact topic is allowed', () => {
    const asking = { ...live, realId: undefined, askRealDevice: true };
    const topic = 'buttonplus/btn_a00001/cover/raam/set';
    const held = decidePublish(request(topic), asking);
    expect(held).toMatchObject({ action: 'block', reason: 'real-device', realDeviceId: 'btn_a00001', topic });
    expect(held.log).toContain('belongs to the real device btn_a00001');
    // Allowed: that topic only, as a Set or a list.
    expect(decidePublish(request(topic), { ...asking, realTopics: new Set([topic]) }))
      .toMatchObject({ action: 'send', reason: 'allowed', realDeviceId: 'btn_a00001' });
    expect(decidePublish(request(topic), { ...asking, realTopics: [topic] }).action).toBe('send');
    expect(decidePublish(request('buttonplus/btn_a00001/other'), { ...asking, realTopics: [topic] }).action).toBe('block');
    // Not asked for: sent (the Emulator's own call; the broker link asks).
    expect(decidePublish(request(topic), { ...live, realId: undefined }).action).toBe('send');
    // The virtual device's own topics and plain topics are never held.
    expect(decidePublish(request(`buttonplus/${live.virtualId}/key`), asking)).toMatchObject({ action: 'send', reason: 'own-topic' });
    expect(decidePublish(request('home/light/set'), asking).action).toBe('send');
    // Read-only and offline never get as far as asking.
    expect(decidePublish(request(topic), { ...asking, mode: 'live-readonly' }).reason).toBe('read-only');
    expect(decidePublish(request(topic), { ...asking, mode: 'offline' }).action).toBe('mock');
  });

  it('the firmware\'s periodic reports: held only when asked for, until allowed; a script\'s publish is never one', () => {
    const base = { ...live, realId: undefined };
    const sensor = { ...request(`buttonplus/${live.virtualId}/sensor/temperature`), origin: 'firmware' as const, kind: 'sensor' };
    const state = { ...sensor, topic: `buttonplus/${live.virtualId}/light/led_1/state`, kind: 'state' };
    expect(isPeriodicState(sensor)).toBe(true);
    expect(isPeriodicState(state)).toBe(true);
    expect(isPeriodicState({ origin: 'firmware', kind: 'press' })).toBe(false);
    expect(isPeriodicState({ origin: 'script', kind: 'state' })).toBe(false);
    // Not asked for (the Emulator's own call): sent.
    expect(decidePublish(sensor, base).action).toBe('send');
    const held = decidePublish(sensor, { ...base, holdStates: true });
    expect(held).toMatchObject({ action: 'block', reason: 'own-state' });
    expect(decidePublish(state, { ...base, holdStates: true }).reason).toBe('own-state');
    expect(decidePublish(sensor, { ...base, holdStates: true, allowStates: true })).toMatchObject({ action: 'send', reason: 'own-topic' });
    // A key's default action is what is being tested: it goes out.
    expect(decidePublish({ ...sensor, topic: `buttonplus/${live.virtualId}/button_3/state`, kind: 'press' }, { ...base, holdStates: true }).action).toBe('send');
    // Read-only and offline do not get this far.
    expect(decidePublish(sensor, { ...base, holdStates: true, mode: 'live-readonly' }).reason).toBe('read-only');
    expect(decidePublish(sensor, { ...base, holdStates: true, mode: 'offline' }).action).toBe('mock');
  });

  it('a script that publishes to an announcement topic is announcing too (kind "script": by the topic)', () => {
    const base = { ...live, realId: undefined };
    const config = { ...request(`homeassistant/sensor/${live.virtualId}_t/config`), kind: 'script' };
    expect(isAnnouncement(config)).toBe(true);
    expect(decidePublish(config, base)).toMatchObject({ action: 'block', reason: 'discovery' });
    expect(isAnnouncement({ topic: 'home/light/set', kind: 'script' })).toBe(false);
  });

  it('realDeviceIn: a device id as a token, never the virtual identity', () => {
    expect(realDeviceIn('buttonplus/btn_a00001/cover/set')).toBe('btn_a00001');
    expect(realDeviceIn('homeassistant/event/btn_a00001_key1/config')).toBe('btn_a00001');
    expect(realDeviceIn('btn_a00002')).toBe('btn_a00002');
    expect(realDeviceIn('buttonplus/virt_btn_a00001/key')).toBeNull();
    expect(realDeviceIn('home/btn_light/set')).toBeNull();
    expect(realDeviceIn('x/abtn_a00001/y')).toBeNull();
    expect(realDeviceIn('x/btn_a000011/y')).toBeNull();
  });

  it('announcements: by kind when the sender gives one, else by topic; off unless allowed; retain needs its own switch', () => {
    const base = { ...live, realId: undefined };
    const discovery = { ...request(`homeassistant/sensor/${live.virtualId}_t/config`), retain: true, origin: 'firmware' as const, kind: 'discovery' };
    expect(isAnnouncement(discovery)).toBe(true);
    expect(decidePublish(discovery, base)).toMatchObject({ action: 'block', reason: 'discovery', announce: true });
    expect(decidePublish(discovery, { ...base, allowDiscovery: true })).toMatchObject({ action: 'send', announce: true, retain: false, retainCleared: true });
    expect(decidePublish(discovery, { ...base, allowDiscovery: true, allowRetain: true })).toMatchObject({ action: 'send', retain: true });
    // An availability publish on a topic that does not look like one.
    expect(decidePublish({ ...request('custom/state'), origin: 'firmware', kind: 'availability' }, base).reason).toBe('discovery');
    // A firmware publish of another kind on its own topic: ordinary traffic.
    const keyEvent = { ...request(`buttonplus/${live.virtualId}/button_1/state`), origin: 'firmware' as const, kind: 'press' };
    expect(isAnnouncement(keyEvent)).toBe(false);
    expect(decidePublish(keyEvent, base)).toMatchObject({ action: 'send', reason: 'own-topic' });
    // A kind that is not an announcement wins over the look of the topic.
    expect(isAnnouncement({ topic: `buttonplus/${live.virtualId}/status`, kind: 'state' })).toBe(false);
    // No kind (a script): by the topic.
    expect(isAnnouncement({ topic: 'homeassistant/light/x/config' })).toBe(true);
  });

  it('live-publish: discovery and availability are suppressed unless explicitly allowed', () => {
    for (const topic of ['homeassistant/event/btn_a00001_key3/config', 'buttonplus/btn_a00001/status']) {
      const decision = decidePublish(request(topic, { origin: 'firmware', retain: true }), live);
      expect(decision).toMatchObject({ action: 'block', reason: 'discovery', rewritten: true });
      expect(decision.log).toContain('virt_btn_0a1b2c');
      expect(decidePublish(request(topic, { origin: 'firmware', retain: true }), { ...live, allowDiscovery: true }))
        .toMatchObject({ action: 'send', retain: false, reason: 'own-topic' });
    }
    // A script cannot get around it by writing the topic itself.
    expect(decidePublish(request('homeassistant/switch/x/config'), { ...live, allowTopics: ['homeassistant/switch/x/config'] }).action)
      .toBe('block');
  });
});

describe('the emulator applies the policy', () => {
  const script = `
    mqtt.publish(0, "buttonplus/" + system.id() + "/key/3", "press", 0, 1);
    mqtt.publish(0, "buttonplus/btn_a00001/display/temp", "21", 0, 1);
    mqtt.publish(0, "home/light/set", "ON");
    mqtt.publish(0, "home/allowed/set", "ON");
  `;

  it('offline (the default): every publish reaches the mock as written', async () => {
    const { emu } = await makeEmulator();
    emu.run(script);
    expect(emu.broker(0).published.map(p => [p.topic, p.retain])).toEqual([
      // The firmware's birth message comes first, then what the script publishes.
      ['buttonplus/virt_btn_0a1b2c/status', true],
      ['buttonplus/virt_btn_0a1b2c/key/3', true],
      ['buttonplus/btn_a00001/display/temp', true],
      ['home/light/set', false],
      ['home/allowed/set', false],
    ]);
  });

  it('live-readonly: nothing is published, every attempt is logged', async () => {
    const { emu, rec } = await makeEmulator({ publishMode: 'live-readonly', transport: undefined });
    emu.run(script);
    expect(emu.broker(0).published).toEqual([]);
    expect(rec.publishes.map(p => p.action)).toEqual(['block', 'block', 'block', 'block']);
    expect(rec.publishes[2]!.log).toMatch(/^would publish to broker 0: home\/light\/set = "ON"/);
  });

  it('live-publish: identity rewritten, retain cleared, literal topics by allow list (matched with the firmware matcher)', async () => {
    const { emu, rec } = await makeEmulator({
      publishMode: 'live-publish', realId: 'btn_a00001', allowTopics: ['home/allowed/#'],
    });
    emu.run(script);
    expect(emu.broker(0).published.map(p => [p.topic, p.retain])).toEqual([
      ['buttonplus/virt_btn_0a1b2c/key/3', false],
      ['buttonplus/virt_btn_0a1b2c/display/temp', false],
      ['home/allowed/set', false],
    ]);
    expect(rec.publishes.map(p => [p.requestedTopic, p.action, p.reason])).toEqual([
      ['buttonplus/virt_btn_0a1b2c/key/3', 'send', 'own-topic'],
      ['buttonplus/btn_a00001/display/temp', 'send', 'own-topic'],
      ['home/light/set', 'block', 'not-allowed'],
      ['home/allowed/set', 'send', 'allowed'],
    ]);
  });

  it('offline takes no transport but the mock', async () => {
    const other = { state: 'disconnected' as const, connect() {}, disconnect() {}, subscribe() {}, unsubscribe() {}, publish() {}, onMessage: () => () => {}, onState: () => () => {} };
    await expect(makeEmulator({ transport: () => other })).rejects.toThrow(/only takes MockBroker/);
  });
});
