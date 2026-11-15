// @vitest-environment node
/**
 * The publish-safety policy (pure), and the emulator applying it. Stage 1
 * only talks to the in-page mock; the live modes are tested against it.
 */
import { describe, expect, it } from 'vitest';
import {
  decidePublish, isDiscoveryOrAvailability, isOwnTopic, isVirtualIdentity, rewriteIdentity, virtualIdentity,
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

  it('live-publish: a literal topic is blocked unless the allow list has it', () => {
    const blocked = decidePublish(request('home/light/set'), live);
    expect(blocked).toMatchObject({ action: 'block', reason: 'not-allowed' });
    expect(blocked.log).toContain('would publish to broker 0: home/light/set = "ON"');

    expect(decidePublish(request('home/light/set'), { ...live, allowTopics: ['home/light/set'] }))
      .toMatchObject({ action: 'send', reason: 'allowed', topic: 'home/light/set' });
    // Filters need a matcher; without one only exact topics match.
    expect(decidePublish(request('home/light/set'), { ...live, allowTopics: ['home/+/set'] }).action).toBe('block');
    const plus = (filter: string, topic: string): boolean => new RegExp('^' + filter.replace('+', '[^/]+') + '$').test(topic);
    expect(decidePublish(request('home/light/set'), { ...live, allowTopics: ['home/+/set'] }, plus).action).toBe('send');
    // Another device's topic is a literal topic too.
    expect(decidePublish(request('buttonplus/btn_a00002/display/x'), live).action).toBe('block');
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
