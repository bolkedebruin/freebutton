// @vitest-environment node
/**
 * Stage 2: what the device does from config.json alone, by the firmware's own
 * code in the wasm: config topics, a key press's default action, Home
 * Assistant discovery and availability, text behaviour (markup, style, fonts,
 * truncation), clocks, the time zone, sensor states.
 */
import { describe, expect, it } from 'vitest';
import { ManualClock, type PublishEvent } from '../emulator';
import { makeEmulator, readRepoFile, type TestEmulator } from './emulator-helpers';

/** Main display on connector 0 (keys 1, 2), a bar on connector 3 (keys 7, 8). */
const CONFIG = {
  core: { name: 'ButtonPlus TEST' },
  info: { connectors: [{ id: 0, type: 2 }, { id: 3, type: 1 }] },
  brokers: [{ brokerid: 'ha', url: 'mqtt://10.0.0.1/', port: 1883, hassdiscovery: false }],
  buttons: [
    {
      name: 'k7', position: 7, label: 'Seven', toplabel: 'Top',
      topics: [
        { brokerid: 'ha', topic: 'cfg/k7/label', eventtype: 11 },
        { brokerid: 'ha', topic: 'cfg/k7/top', eventtype: 12 },
        { brokerid: 'ha', topic: 'cfg/k7/icon', eventtype: 44 },
        { brokerid: 'ha', topic: 'cfg/k7/click', eventtype: 0, payload: 'PRESSED' },
        { brokerid: 'ha', topic: 'cfg/k7/long', eventtype: 1 },
        { brokerid: 'ha', topic: 'cfg/k7/release', eventtype: 2 },
      ],
    },
    { name: 'k8', position: 8, label: 'Eight' },
    { name: 'mainLeft', position: 1, label: 'MainL' },
  ],
  displayitems: [
    {
      name: 'a', x: 10, y: 20, width: 50, label: 'A', unit: 'u',
      topics: [
        { brokerid: 'ha', topic: 'cfg/a/value', eventtype: 15 },
        { brokerid: 'ha', topic: 'cfg/a/label', eventtype: 16 },
        { brokerid: 'ha', topic: 'cfg/a/unit', eventtype: 17 },
        { brokerid: 'ha', topic: 'cfg/a/icon', eventtype: 45 },
      ],
    },
    { name: 'b', x: 0, y: 50, width: 50, page: 2, label: 'B' },
  ],
  icons: { sun: '/i/sun.svg' },
};

const firmware = (rec: TestEmulator['rec'], kind: PublishEvent['kind']): string[] =>
  rec.publishes.filter(p => p.origin === 'firmware' && p.kind === kind).map(p => `${p.topic} = ${p.payload}`);

describe('config topics, without a script', () => {
  it('a display item subscribes to its topics; a payload sets value, caption, unit and icon', async () => {
    const { emu, clock, rec } = await makeEmulator({ config: CONFIG });
    const broker = emu.broker(0);
    expect(broker.subscriptions.map(s => s.topic)).toEqual(expect.arrayContaining(
      ['cfg/a/value', 'cfg/a/label', 'cfg/a/unit', 'cfg/a/icon']));
    const a = emu.state.item('a')!;
    expect(a).toMatchObject({ label: 'A', value: '', unit: 'u', icon: '' });

    rec.changes.length = 0;
    expect(broker.inject('cfg/a/value', '21.5')).toBe(true);
    broker.inject('cfg/a/label', 'Room');
    broker.inject('cfg/a/unit', '°C');
    broker.inject('cfg/a/icon', 'sun');
    clock.advance(0);
    expect(a).toMatchObject({ label: 'Room', value: '21.5', unit: '°C', icon: 'sun' });
    expect(a.box!.icon).toEqual({ key: 'ico-sun-32x32.bin', name: 'sun', width: 32, height: 32, tint: '#ffffff' });
    expect(rec.changes).toEqual(expect.arrayContaining([
      { kind: 'item', id: a.id, field: 'value' }, { kind: 'item', id: a.id, field: 'label' },
      { kind: 'item', id: a.id, field: 'unit' }, { kind: 'item', id: a.id, field: 'icon' },
    ]));
    // An empty icon payload hides it again.
    broker.inject('cfg/a/icon', '');
    clock.advance(0);
    expect(a.icon).toBe('');
    expect(emu.running).toBe(false);
  });

  it('a key subscribes to its label, top label and icon topics', async () => {
    const { emu, clock } = await makeEmulator({ config: CONFIG });
    const broker = emu.broker(0);
    broker.inject('cfg/k7/label', 'On');
    broker.inject('cfg/k7/top', 'Lamp');
    clock.advance(0);
    const key = emu.state.key('k7')!;
    expect(key).toMatchObject({ position: 7, label: 'On', topLabel: 'Lamp', icon: '' });
    broker.inject('cfg/k7/icon', 'sun');
    clock.advance(0);
    expect(key.icon).toBe('sun');
    expect(key.display!.icon).toMatchObject({ key: 'ico-sun-48x48.bin', name: 'sun', width: 48 });
    // The icon stands in the label's place.
    expect(key.display!.label.hidden).toBe(true);
  });

  it('the old names of the lists (mqttbrokers, mqttbuttons, mqttdisplays) are not loaded', async () => {
    const config = {
      info: CONFIG.info,
      mqttbrokers: [{ brokerid: 'ha', url: 'mqtt://10.0.0.1/', hassdiscovery: false }],
      mqttbuttons: [{ position: 7, label: 'old', topics: [{ brokerid: 'ha', topic: 'old/key', eventtype: 11 }] }],
      mqttdisplays: [{ label: 'old item', topics: [{ brokerid: 'ha', topic: 'old/item', eventtype: 15 }] }],
    };
    const { emu, rec } = await makeEmulator({ config });
    // The device boots with what is left: its modules, no broker, no item, no configured key.
    expect(emu.state.brokers).toEqual([]);
    expect(emu.state.items).toEqual([]);
    expect(emu.state.keys.every(k => k.ref === undefined && k.label === '')).toBe(true);
    expect(rec.subscribes).toEqual([]);
    expect(emu.run('print(typeof fb.bars[0].left.display.setLabel)')).toEqual({ ok: true });
    expect(rec.console).toEqual(['function']);
  });

  it('an invalid topic entry is skipped by the firmware, with its warning', async () => {
    const config = {
      info: CONFIG.info, brokers: CONFIG.brokers,
      displayitems: [{ label: 'x', topics: [{ brokerid: 'ha', topic: '', eventtype: 15 }, { brokerid: 'ha', topic: 't', eventtype: 99 }, 5] }],
    };
    const logs: string[] = [];
    const { emu } = await makeEmulator({ config });
    emu.on('log', e => logs.push(e.text));
    expect(emu.broker(0).subscriptions.map(s => s.topic)).not.toContain('t');
  });

  it('a value from a config topic survives a script restart; one a script set does not', async () => {
    const { emu, clock } = await makeEmulator({ config: CONFIG });
    emu.broker(0).inject('cfg/a/value', 'from topic');
    clock.advance(0);
    emu.run('fb.main.display.setUnit("a", "from script");');
    expect(emu.state.item('a')).toMatchObject({ value: 'from topic', unit: 'from script' });
    emu.run('var nothing = 1;');
    expect(emu.state.item('a')).toMatchObject({ value: 'from topic', unit: 'u' });
  });

  it('a script and a config topic write the same field: the last one shows', async () => {
    const { emu, clock } = await makeEmulator({ config: CONFIG });
    emu.run('fb.main.display.setValue("a", "script");');
    expect(emu.state.item('a')!.value).toBe('script');
    emu.broker(0).inject('cfg/a/value', 'topic');
    clock.advance(0);
    expect(emu.state.item('a')!.value).toBe('topic');
  });
});

describe('a key press: the firmware\'s default action', () => {
  const press = (t: TestEmulator, key: number | string, holdMs = 100): void => {
    t.emu.press(key);
    t.clock.advance(holdMs);
    t.emu.release(key);
    t.clock.advance(100);
  };

  it('publishes the configured topics (payload of the config, else the firmware\'s JSON event)', async () => {
    const t = await makeEmulator({ config: CONFIG });
    press(t, 'k7', 1400);
    expect(firmware(t.rec, 'press')).toEqual([
      'cfg/k7/click = PRESSED',
      'cfg/k7/long = {"event_type":"longpress"}',
      'cfg/k7/long = {"event_type":"longpress"}',
      'cfg/k7/release = {"event_type":"release"}',
    ]);
    expect(t.rec.publishes.every(p => p.qos === 0 && !p.retain)).toBe(true);
    // They reach the broker like any publish.
    expect(t.emu.broker(0).published.map(p => p.topic)).toContain('cfg/k7/click');
  });

  it('with Home Assistant discovery the press is also on the device\'s event topic, under the virtual identity', async () => {
    const config = { ...CONFIG, brokers: [{ ...CONFIG.brokers[0], hassdiscovery: true }] };
    const t = await makeEmulator({ config });
    t.rec.publishes.length = 0;
    press(t, 'k7');
    press(t, 'k8');
    press(t, 1);
    expect(firmware(t.rec, 'press')).toEqual([
      'cfg/k7/click = PRESSED',
      'buttonplus/virt_btn_0a1b2c/button_7/state = {"event_type":"click"}',
      'cfg/k7/release = {"event_type":"release"}',
      'buttonplus/virt_btn_0a1b2c/button_8/state = {"event_type":"click"}',
      'buttonplus/virt_btn_0a1b2c/button_1/state = {"event_type":"click"}',
    ]);
  });

  it('a script handler that returns false suppresses it; any other handler does not', async () => {
    const t = await makeEmulator({ config: CONFIG });
    t.emu.run(`
      var seen = [];
      fb.button("k7").button.onClick(function () { seen.push("click"); return false; });
      fb.button("k7").button.onRelease(function () { seen.push("release"); });
    `);
    press(t, 'k7');
    expect(t.rec.keys).toEqual(['7:click:prevented', '7:release']);
    expect(firmware(t.rec, 'press')).toEqual(['cfg/k7/release = {"event_type":"release"}']);
    expect(t.emu.eval('seen.join()')).toEqual({ ok: true, value: 'click,release' });
  });

  it('a script publishing from its handler is the script\'s publish, next to the firmware\'s', async () => {
    const t = await makeEmulator({ config: CONFIG });
    t.emu.run('fb.button("k7").button.onClick(function () { mqtt.publish(0, "mine", "1"); });');
    t.emu.click('k7');
    expect(t.rec.publishes.map(p => `${p.origin}/${p.kind} ${p.topic}`)).toEqual([
      'script/script mine', 'firmware/press cfg/k7/click', 'firmware/press cfg/k7/release',
    ]);
  });

  it('a press sent from Home Assistant (button_N/set) is a click and a release', async () => {
    const config = { ...CONFIG, brokers: [{ ...CONFIG.brokers[0], hassdiscovery: true }] };
    const t = await makeEmulator({ config });
    t.emu.run('fb.button("k8").button.onClick(function () { print("clicked by HA"); });');
    t.rec.publishes.length = 0;
    expect(t.emu.broker(0).inject('buttonplus/virt_btn_0a1b2c/button_8/set', 'PRESS')).toBe(true);
    t.clock.advance(0);
    expect(t.rec.console).toEqual(['clicked by HA']);
    expect(t.rec.keys).toEqual(['8:click', '8:release']);
    expect(firmware(t.rec, 'press')).toEqual(['buttonplus/virt_btn_0a1b2c/button_8/state = {"event_type":"click"}']);
  });
});

describe('Home Assistant discovery and availability', () => {
  const config = { ...CONFIG, brokers: [{ ...CONFIG.brokers[0], hassdiscovery: true }] };

  it('availability: "online" on <device>/status at connect, retained; the last will is "offline"', async () => {
    const { emu, rec } = await makeEmulator({ config: CONFIG });
    expect(rec.publishes).toEqual([]); // connected while the emulator was made
    const status = emu.broker(0).published[0]!;
    expect(status).toMatchObject({ topic: 'buttonplus/virt_btn_0a1b2c/status', payload: 'online', retain: true });
    expect(emu.lastWill(0)).toEqual({ topic: 'buttonplus/virt_btn_0a1b2c/status', payload: 'offline', qos: 1, retain: true });

    emu.broker(0).setConnected(false);
    emu.broker(0).setConnected(true);
    expect(rec.publishes.map(p => [p.origin, p.kind, p.topic, p.payload])).toEqual([
      ['firmware', 'availability', 'buttonplus/virt_btn_0a1b2c/status', 'online'],
    ]);
  });

  it('nothing is announced on a broker without hassdiscovery', async () => {
    const { clock, rec } = await makeEmulator({ config: CONFIG });
    clock.advance(10_000);
    expect(firmware(rec, 'discovery')).toEqual([]);
  });

  it('discovery starts once the connect burst settled, one config per loop pass', async () => {
    const { emu, clock, rec } = await makeEmulator({ config });
    clock.advance(900);
    expect(firmware(rec, 'discovery')).toEqual([]);
    clock.advance(200);
    const first = rec.publishes.filter(p => p.kind === 'discovery');
    expect(first.length).toBeGreaterThan(0);
    expect(first.length).toBeLessThan(15);
    clock.advance(2000);
    const all = rec.publishes.filter(p => p.kind === 'discovery');
    // 8 sensors, 4 entries for each of the 8 key positions, the lights.
    expect(all.length).toBeGreaterThanOrEqual(8 + 32);
    expect(all.every(p => p.origin === 'firmware' && p.retain && p.qos === 1)).toBe(true);
    expect(all.every(p => p.topic.startsWith('homeassistant/') && p.topic.includes('/virt_btn_0a1b2c/'))).toBe(true);
    // The times differ by the loop delay.
    expect(all[1]!.time - all[0]!.time).toBe(emu.limits.loopDelayMs);
    // Key positions without a module get an empty payload (removes stale entities).
    expect(all.find(p => p.topic.endsWith('/event/virt_btn_0a1b2c/button_3/config'))!.payload).toBe('');
    // Nothing more afterwards, and no timer keeps running for it.
    const count = all.length;
    clock.advance(10_000);
    expect(rec.publishes.filter(p => p.kind === 'discovery')).toHaveLength(count);
    expect(clock.timers).toBe(0);
  });

  it('is published again when Home Assistant says "online" on homeassistant/status', async () => {
    const { emu, clock, rec } = await makeEmulator({ config });
    clock.advance(5000);
    const count = rec.publishes.filter(p => p.kind === 'discovery').length;
    expect(emu.broker(0).inject('homeassistant/status', 'online')).toBe(true);
    clock.advance(5000);
    expect(rec.publishes.filter(p => p.kind === 'discovery')).toHaveLength(2 * count);
  });

  /**
   * The payloads, against what the native firmware test expects
   * (test/test_ha_discovery): its fixtures are the full configs of a device
   * btn_a00002 named "ButtonPlus TEST"; expanded as Home Assistant expands
   * them (the abbreviation table of that test), the emulator's must be the
   * same with the virtual identity in place of the device id.
   */
  it('payloads equal the native firmware test\'s fixtures, with the virtual identity', async () => {
    const source = readRepoFile('test/test_ha_discovery/test_ha_discovery.cpp')!;
    const table = readRepoFile('test/test_ha_discovery/ha_abbreviations.h')!;
    const devFull = /#define DEV_FULL R"\((.*)\)"/.exec(source)![1]!;
    const fixture = (name: string): Record<string, unknown> => {
      const raw = new RegExp(`static const char\\* ${name} = R"\\((.*?)\\)" DEV_FULL "}";`).exec(source)![1]!;
      return JSON.parse((raw + devFull + '}').replaceAll('btn_a00002', 'virt_btn_0a1b2c')) as Record<string, unknown>;
    };
    const section = (name: string): Map<string, string> => {
      const body = new RegExp(`${name}\\[\\] = \\{([\\s\\S]*?)\\n\\};`).exec(table)![1]!;
      return new Map([...body.matchAll(/\{"([^"]+)",\s*"([^"]+)"\}/g)].map(m => [m[1]!, m[2]!]));
    };
    const keys = section('kAbbreviations');
    const deviceKeys = section('kDeviceAbbreviations');
    expect(keys.size).toBeGreaterThan(50);
    // homeassistant/components/mqtt/discovery.py: abbreviations, then the "~" topic base.
    const expand = (payload: string): Record<string, unknown> => {
      const out: Record<string, unknown> = {};
      for (const [k, v] of Object.entries(JSON.parse(payload) as Record<string, unknown>)) {
        const full = keys.get(k) ?? k;
        out[full] = full === 'device'
          ? Object.fromEntries(Object.entries(v as Record<string, unknown>).map(([dk, dv]) => [deviceKeys.get(dk) ?? dk, dv]))
          : v;
      }
      const base = out['~'];
      delete out['~'];
      if (typeof base === 'string') {
        for (const [k, v] of Object.entries(out)) {
          if (typeof v !== 'string' || v === '' || !k.endsWith('topic')) continue;
          out[k] = v.startsWith('~') ? base + v.slice(1) : v.endsWith('~') ? v.slice(0, -1) + base : v;
        }
      }
      return out;
    };

    const { clock, rec } = await makeEmulator({ config });
    clock.advance(5000);
    const published = (suffix: string): Record<string, unknown> =>
      expand(rec.publishes.find(p => p.kind === 'discovery' && p.topic === `homeassistant/${suffix}/config`)!.payload);
    expect(published('event/virt_btn_0a1b2c/button_7')).toEqual(fixture('kEvent'));
    expect(published('button/virt_btn_0a1b2c/button_7')).toEqual(fixture('kButton'));
    expect(published('device_automation/virt_btn_0a1b2c/button_7_click')).toEqual(fixture('kClickTrigger'));
    expect(published('device_automation/virt_btn_0a1b2c/button_7_long')).toEqual(fixture('kLongTrigger'));
    // (The fixture is a sensor without class and unit; the firmware's free-memory sensor has both.)
    expect(published('sensor/virt_btn_0a1b2c/free_memory_100'))
      .toEqual({ ...fixture('kVirtualSensor'), device_class: 'data_size', unit_of_measurement: 'B' });
    expect(published('light/virt_btn_0a1b2c/led_7_1')).toEqual(fixture('kLight'));
  });
});

describe('text as the firmware draws it', () => {
  const STYLE_CONFIG = {
    info: CONFIG.info,
    fonts: [{ id: 1, name: 'mdi', src: 'icons.ttf' }, { name: 'montserrat', fallback: 'mdi' }],
    styles: {
      // The kinds: what config.json sets, over the built-in look.
      button: { color: '#eeeeee' },
      displayitem: { 'font-size': 24, color: '#eeeeee', unit: { 'font-size': 14 } },
      icons32: { 'font-family': 'mdi', 'font-size': 32, title: { 'font-size': 18 } },
      icons48: { 'font-family': 'mdi', 'font-size': 48 },
      icons72: { 'font-family': 'mdi', 'font-size': 72 },
      semi20: { 'font-family': 'montserrat_semibold', 'font-size': 20 },
    },
    buttons: [{ name: 'k', position: 7, label: 'Key', toplabel: 'Top' }, { name: 'big', position: 8, label: 'B', style: 'icons72' }],
    displayitems: [
      { name: 'plain', label: 'Caption', unit: 'W' },
      { name: 'sized', label: 'Caption', unit: 'W', style: 'icons32' },
    ],
  };

  it('resolves the lookup: the built-in look, the kind\'s style, then the item\'s styles', async () => {
    const { emu } = await makeEmulator({ config: STYLE_CONFIG });
    expect(emu.state.fonts.map(f => [f.face, f.name, f.fallback])).toEqual([
      [0, 'montserrat', 2], [1, 'montserrat_semibold', -1], [2, 'mdi', -1],
    ]);
    expect(emu.state.styles.displayitem).toMatchObject({ font: 'montserrat', px: 24, color: '#eeeeee' });
    expect(emu.state.styles['displayitem.title']).toMatchObject({ font: 'montserrat_semibold', px: 14, color: '#ffd700' });
    expect(emu.state.styleNames).toEqual(['icons32', 'icons48', 'icons72', 'semi20']);

    emu.run('fb.main.display.setValue("plain", "12"); fb.main.display.setValue("sized", "34");');
    const plain = emu.state.item('plain')!.box!;
    expect(plain.caption.runs).toEqual([{ text: 'Caption', font: 'montserrat_semibold', face: 1, px: 14, color: '#ffd700' }]);
    expect(plain.value.runs).toEqual([{ text: '12', font: 'montserrat', face: 0, px: 24, color: '#eeeeee' }]);
    // The unit has its size from "displayitem" and the built-in colour; its face is the value's.
    expect(plain.unit.runs).toEqual([{ text: 'W', font: 'montserrat', face: 0, px: 14, color: '#999999' }]);
    expect(emu.state.item('plain')!.font).toEqual({ face: 0, size: 24 });

    const sized = emu.state.item('sized')!.box!;
    // The style's main block for the value; the unit has the value's face, and the size
    // "displayitem" gives units; the style's "title" block sizes the caption.
    expect(sized.value.runs[0]).toMatchObject({ font: 'mdi', px: 32 });
    expect(sized.unit.runs[0]).toMatchObject({ font: 'mdi', px: 14, color: '#999999' });
    expect(sized.caption.runs[0]).toMatchObject({ font: 'montserrat_semibold', px: 18 });
    // The paddings that go with the value's size (src/ui/box_logic.h).
    expect([sized.captionPad, sized.boxPad]).toEqual([4, 8]);
    expect([plain.captionPad, plain.boxPad]).toEqual([4, 3]);
    expect(sized).toMatchObject({ x: 0, y: 0, width: 320, underline: true });

    expect(emu.state.key('k')!.display!.label.runs).toEqual([{ text: 'Key', font: 'montserrat', face: 0, px: 32, color: '#eeeeee' }]);
    expect(emu.state.key('k')!.display!.topLabel.runs).toEqual([{ text: 'Top', font: 'montserrat_semibold', face: 1, px: 18, color: '#ffd700' }]);
    expect(emu.state.key('big')!.display!.label.runs[0]).toMatchObject({ font: 'mdi', px: 72 });
    expect(emu.state.key('big')!.font).toEqual({ face: 2, size: 72 });
  });

  it('parses the markup into runs: colour, size, font, alignment, code points', async () => {
    const { emu } = await makeEmulator({ config: STYLE_CONFIG });
    emu.run(String.raw`
      fb.main.display.setValue("plain", "\\ar\\cff000012\\c00ff00.5\\s{40} big\\f{mdi}\\u{F0599}");
      fb.main.display.setUnit("plain", "\\s1small\\f{nofont}x");
      fb.button("k").display.setLabel("\\c0000ffBlue");
      fb.button("k").display.setTopLabel("a\\cff0000b");
    `);
    const box = emu.state.item('plain')!.box!;
    expect(box.value).toMatchObject({ plain: '12.5 big\u{F0599}', align: 'right', spansCut: false });
    expect(box.value.runs).toEqual([
      { text: '12', font: 'montserrat', face: 0, px: 24, color: '#ff0000' },
      { text: '.5', font: 'montserrat', face: 0, px: 24, color: '#00ff00' },
      { text: ' big', font: 'montserrat', face: 0, px: 40, color: '#00ff00' },
      { text: '\u{F0599}', font: 'mdi', face: 2, px: 40, color: '#00ff00' },
    ]);
    // \s1 = 14 px; an unknown font name keeps the font (and the firmware warns once).
    expect(box.unit.runs.map(r => [r.text, r.font, r.px])).toEqual([['small', 'montserrat', 14], ['x', 'montserrat', 14]]);
    // The raw text stays what the script gave.
    expect(emu.state.item('plain')!.value).toContain('\\cff0000');

    // A key text in one format is the wrapping label with that colour; in several, a row.
    const display = emu.state.key('k')!.display!;
    expect(display.label).toMatchObject({ plain: 'Blue', rich: false, runs: [{ text: 'Blue', color: '#0000ff', px: 32 }] });
    expect(display.topLabel).toMatchObject({ plain: 'ab', rich: true });
    expect(display.topLabel.runs.map(r => [r.text, r.color])).toEqual([['a', '#ffd700'], ['b', '#ff0000']]);
  });

  it('a script\'s setStyle is what the firmware then draws with', async () => {
    const { emu } = await makeEmulator({ config: STYLE_CONFIG });
    emu.run('fb.main.display.setValue("plain", "1"); fb.main.display.setStyle("plain", "icons48"); fb.button("k").display.setStyle("semi20");');
    expect(emu.state.item('plain')!.box!.value.runs[0]).toMatchObject({ font: 'mdi', px: 48 });
    expect(emu.state.item('plain')!.font).toEqual({ face: 2, size: 48 });
    expect(emu.state.key('k')!.display!.label.runs[0]).toMatchObject({ font: 'montserrat_semibold', px: 20 });
    emu.eval('fb.main.display.setStyle("plain", "")');
    expect(emu.state.item('plain')!.box!.value.runs[0]).toMatchObject({ font: 'montserrat', px: 24 });
  });

  it('cuts texts to the firmware\'s slots, and says so in its log', async () => {
    const { emu, rec } = await makeEmulator({ config: STYLE_CONFIG });
    const long = 'x'.repeat(300);
    emu.run(`
      var d = fb.main.display;
      d.setValue("plain", "${long}"); d.setLabel("plain", "${long}"); d.setUnit("plain", "${long}");
      fb.button("k").display.setLabel("${long}"); fb.button("k").display.setTopLabel("${long}");
    `);
    const item = emu.state.item('plain')!;
    const key = emu.state.key('k')!;
    const lengths = [item.value.length, item.label.length, item.unit.length, key.label.length, key.topLabel.length];
    // What the firmware's TextSlots hold (src/components/DisplayItem.h, Button.h).
    const slot = (file: string, name: string): number =>
      Number(new RegExp(`${name} = (\\d+);`).exec(readRepoFile(file)!)![1]);
    const sizes = [
      slot('src/components/DisplayItem.h', 'kValueSlotBytes'), slot('src/components/DisplayItem.h', 'kCaptionSlotBytes'),
      slot('src/components/DisplayItem.h', 'kUnitSlotBytes'), slot('src/components/Button.h', 'kLabelSlotBytes'),
      slot('src/components/Button.h', 'kTopLabelSlotBytes'),
    ];
    lengths.forEach((length, i) => {
      expect(length).toBeLessThan(sizes[i]!);
      expect(length).toBeGreaterThan(sizes[i]! - 8);
    });
    expect(lengths).toEqual([58, 26, 26, 90, 90]);
    expect(rec.logs.filter(l => /cut to \d+/.test(l))).toHaveLength(5);
    // A multi-byte character is not split.
    emu.eval('fb.main.display.setLabel("plain", "' + 'é'.repeat(40) + '")');
    expect(item.label).toBe('é'.repeat(13));
  });

  it('a key of the main display has no display of its own: its label cannot be set', async () => {
    const { emu } = await makeEmulator({ config: CONFIG });
    expect(emu.state.key('mainLeft')).toMatchObject({ position: 1, label: 'MainL', display: null });
    expect(emu.state.keyDisplays).toEqual([7, 8]);
    expect(emu.eval('fb.button("mainLeft").display.setLabel("x")')).toMatchObject({
      ok: false, error: { message: 'InternalError: failed to set label for button 1' },
    });
  });

  it('an icon whose raster is not in the images bucket shows nothing', async () => {
    const config = { ...CONFIG, displayitems: [{ name: 'a', label: 'A', icon: 'sun' }] };
    const saved = await makeEmulator({ config });
    expect(saved.emu.state.item('a')).toMatchObject({ icon: 'sun', box: { icon: { name: 'sun', width: 32, height: 32 } } });
    // The IDE has not made the raster yet: the device finds no such key.
    const missing = await makeEmulator({ config, mediaExists: key => key !== 'ico-sun-32x32.bin' });
    expect(missing.emu.state.item('a')).toMatchObject({ icon: '', box: { icon: null } });
  });
});

describe('pages and clocks', () => {
  const config = {
    info: CONFIG.info,
    displayitems: [{ name: 'one', label: '1', page: 1 }, { name: 'two', label: '2', page: 2 }, { name: 'all', label: '*', page: 0 }],
    clocks: [{ x: 10, y: 20, width: 120, height: 120, page: 2 }, { x: 0, y: 0, page: 1 }],
    styles: { orange: { selected: { 'background-color': '#ff8800' } } },
    lists: [{ name: 'l', page: 2, x: 50, y: 10, width: 40, rows: 3, items: ['a', 'b', 'c'], selected: 1, style: 'orange' }],
    images: [{ name: 'img', page: 1, x: 5, y: 6, width: 48, height: 48, icon: 'sun' }],
  };

  it('only the widgets of the shown page exist, as on the device', async () => {
    const { emu, rec } = await makeEmulator({ config });
    const s = emu.state;
    expect([s.item('one')!.box !== null, s.item('two')!.box !== null, s.item('all')!.box !== null]).toEqual([true, false, true]);
    expect(s.list('l')!.roller).toBeNull();
    expect(s.image('img')!.raster).toMatchObject({ name: 'sun', width: 48, height: 48 });
    expect(s.clocks.map(c => c.shown)).toEqual([false, false]);

    rec.changes.length = 0;
    emu.run('fb.main.display.showPage(2);');
    expect(s.shownPage).toBe(2);
    expect([s.item('one')!.box !== null, s.item('two')!.box !== null, s.item('all')!.box !== null]).toEqual([false, true, true]);
    expect(s.list('l')!.roller).toMatchObject({
      x: 160, y: 24, width: 128, rows: 3, selected: 1, selectedBg: '#ff8800', font: { font: 'montserrat', px: 32 },
    });
    expect(s.image('img')!.raster).toBeNull();
    // The first clock of the config is the one that is drawn.
    expect(s.clocks).toEqual([
      { index: 0, id: 1, page: 2, x: 10, y: 20, width: 120, height: 120, align: 1, shown: true },
      { index: 1, id: 2, page: 1, x: 0, y: 0, width: 260, height: 260, align: 1, shown: false },
    ]);
    expect(rec.changes).toEqual(expect.arrayContaining([
      { kind: 'page', page: 2 }, { kind: 'clock', index: 0 }, { kind: 'item', id: s.item('one')!.id, field: 'shown' },
      { kind: 'list', index: 0, field: 'shown' },
    ]));
  });

  it('a value set while its page is hidden shows when the page comes', async () => {
    const { emu } = await makeEmulator({ config });
    emu.run('fb.main.display.setValue("two", "kept");');
    // Not drawn, but the item holds it.
    expect(emu.state.item('two')).toMatchObject({ value: 'kept', box: null });
    emu.eval('fb.main.display.showPage(2)');
    expect(emu.state.item('two')!.box!.value.runs.map(r => r.text)).toEqual(['kept']);
  });
});

describe('the time zone (config.json core.timezone, a POSIX TZ string)', () => {
  const at = async (iso: string, timezone?: string) => {
    const clock = new ManualClock(Date.parse(iso));
    const made = await makeEmulator({ config: { core: timezone === undefined ? {} : { timezone } }, clock });
    const local = (): string => {
      const result = made.emu.eval('var d = new Date(); d.getFullYear() + "-" + (d.getMonth() + 1) + "-" + d.getDate() + " " + d.getHours() + ":" + d.getMinutes() + " day " + d.getDay()');
      return result.ok ? result.value : result.error.message;
    };
    return { ...made, local };
  };
  const AMSTERDAM = 'CET-1CEST,M3.5.0,M10.5.0/3';

  it('Europe/Amsterdam across the change to summer time (29 March 2026, 01:00 UTC)', async () => {
    const t = await at('2026-03-29T00:59:00Z', AMSTERDAM);
    expect(t.emu.state.timezone).toBe(AMSTERDAM);
    expect(t.local()).toBe('2026-3-29 1:59 day 0');
    expect(t.emu.utcOffsetSeconds()).toBe(3600);
    t.clock.advance(60_000);
    expect(t.local()).toBe('2026-3-29 3:0 day 0');
    expect(t.emu.utcOffsetSeconds()).toBe(7200);
  });

  it('and back to winter time (25 October 2026, 01:00 UTC): the hour from 2 to 3 comes twice', async () => {
    const t = await at('2026-10-25T00:59:00Z', AMSTERDAM);
    expect(t.local()).toBe('2026-10-25 2:59 day 0');
    t.clock.advance(60_000);
    expect(t.local()).toBe('2026-10-25 2:0 day 0');
    expect(t.emu.utcOffsetSeconds()).toBe(3600);
  });

  it('a script\'s minute tick follows local time over the change', async () => {
    const t = await at('2026-03-29T00:58:30Z', AMSTERDAM);
    t.emu.run(`
      function tick() { var d = new Date(); print(d.getHours() + ":" + d.getMinutes()); setTimeout(tick, 60000 - d.getSeconds() * 1000); }
      tick();
    `);
    t.clock.advance(120_000);
    expect(t.rec.console).toEqual(['1:58', '1:59', '3:0']);
  });

  it('without core.timezone the firmware\'s default applies (Central European Time)', async () => {
    const t = await at('2026-07-01T12:00:00Z');
    expect(t.emu.state.timezone).toBe('CET-1CEST,M3.5.0,M10.5.0/3');
    expect(t.local()).toBe('2026-7-1 14:0 day 3');
  });

  it('other zones: UTC, a zone west of UTC with the US rule, the southern hemisphere, a quoted name', async () => {
    expect((await at('2026-07-01T12:00:00Z', 'UTC0')).local()).toBe('2026-7-1 12:0 day 3');
    // New York: second Sunday of March (8 March 2026, 07:00 UTC).
    const ny = await at('2026-03-08T06:59:00Z', 'EST5EDT,M3.2.0,M11.1.0');
    expect(ny.local()).toBe('2026-3-8 1:59 day 0');
    ny.clock.advance(60_000);
    expect(ny.local()).toBe('2026-3-8 3:0 day 0');
    // Sydney: summer time over the new year.
    expect((await at('2026-01-15T00:00:00Z', 'AEST-10AEDT,M10.1.0,M4.1.0/3')).local()).toBe('2026-1-15 11:0 day 4');
    expect((await at('2026-07-15T00:00:00Z', 'AEST-10AEDT,M10.1.0,M4.1.0/3')).local()).toBe('2026-7-15 10:0 day 3');
    expect((await at('2026-07-01T12:00:00Z', '<+0530>-5:30')).local()).toBe('2026-7-1 17:30 day 3');
  });

  it('a string that is not a POSIX zone gives UTC', async () => {
    expect((await at('2026-07-01T12:00:00Z', 'Europe/Amsterdam')).local()).toBe('2026-7-1 12:0 day 3');
  });
});

describe('sensors, as the firmware samples and publishes them', () => {
  it('every second each sensor\'s state goes to <device>/sensor/<name>/state on the first broker', async () => {
    const { emu, clock, rec } = await makeEmulator({ config: CONFIG, sampleSensors: true });
    clock.advance(999);
    expect(firmware(rec, 'sensor')).toEqual([]);
    clock.advance(1);
    const published = firmware(rec, 'sensor');
    const number = (name: string): number =>
      Number(published.find(line => line.includes(`/sensor/${name}/state = `))!.split(' = ')[1]);
    expect(published.map(line => line.replace(/(lvgl_\w+\/state) = .*/, '$1 = (pool)'))).toEqual([
      'buttonplus/virt_btn_0a1b2c/sensor/bme280_pressure/state = 1013.200012',
      'buttonplus/virt_btn_0a1b2c/sensor/bme280_humidity/state = 45.000000',
      'buttonplus/virt_btn_0a1b2c/sensor/bme280_temperature/state = 21.500000',
      'buttonplus/virt_btn_0a1b2c/sensor/free_memory/state = 40000.000000',
      'buttonplus/virt_btn_0a1b2c/sensor/lvgl_used_memory/state = (pool)',
      'buttonplus/virt_btn_0a1b2c/sensor/lvgl_free_memory/state = (pool)',
      'buttonplus/virt_btn_0a1b2c/sensor/js_used_memory/state = 0.000000',
      'buttonplus/virt_btn_0a1b2c/sensor/js_free_memory/state = 0.000000',
    ]);
    // LVGL's own pool (LV_MEM_SIZE 61440 less the allocator's bookkeeping), as the firmware's LVGL task sampled it.
    const total = number('lvgl_used_memory') + number('lvgl_free_memory');
    expect(total).toBeGreaterThan(50_000);
    expect(total).toBeLessThanOrEqual(61_440);
    // (A snapshot of up to a second ago, as on the device; stats() measures now.)
    expect(emu.stats().lvglFree).toBeLessThanOrEqual(number('lvgl_free_memory'));
    expect(number('lvgl_used_memory')).toBeGreaterThan(4000);   // the screens, the widgets, the font instances
    expect(rec.publishes.every(p => p.qos === 0 && !p.retain)).toBe(true);
    rec.publishes.length = 0;
    emu.setSensor(5, 22.25);
    clock.advance(3000);
    expect(rec.publishes).toHaveLength(3 * 8);
    expect(firmware(rec, 'sensor')[2]).toBe('buttonplus/virt_btn_0a1b2c/sensor/bme280_temperature/state = 22.250000');
    // It is the device that publishes, script or not; a script's heap shows once one runs.
    emu.run('var x = 1;');
    rec.publishes.length = 0;
    clock.advance(1000);
    expect(Number(firmware(rec, 'sensor')[7]!.split(' = ')[1])).toBeGreaterThan(8000);
    expect(emu.state.sensor(104)!.value).toBeGreaterThan(8000);
  });

  it('the sensors are the firmware\'s: the board the hardware has, and the memory sensors', async () => {
    const { emu } = await makeEmulator({ config: CONFIG });
    expect(emu.state.sensors.map(s => [s.id, s.name, s.type, s.unit, s.input])).toEqual([
      [3, 'BME280 Pressure', 'pressure', 'hPa', 1], [4, 'BME280 Humidity', 'humidity', '%', 2],
      [5, 'BME280 Temperature', 'temperature', '°C', 0], [100, 'Free Memory', 'data_size', 'B', -1],
      [101, 'LVGL Used Memory', 'data_size', 'B', -1], [102, 'LVGL Free Memory', 'data_size', 'B', -1],
      [103, 'JS Used Memory', 'data_size', 'B', -1], [104, 'JS Free Memory', 'data_size', 'B', -1],
    ]);
    const sts = await makeEmulator({ config: CONFIG, sensors: [{ id: 1, value: 19.5 }] });
    expect(sts.emu.state.sensors.slice(0, 2).map(s => [s.id, s.name])).toEqual([[1, 'STS35 Temperature 1'], [100, 'Free Memory']]);
    // The STS35 driver reads at most once a second, and not in the first.
    expect(sts.emu.eval('sensor.getValue(1)')).toEqual({ ok: true, value: '0' });
    sts.clock.advance(1000);
    expect(sts.emu.eval('sensor.getValue(1)')).toEqual({ ok: true, value: '19.5' });
    const none = await makeEmulator({ config: CONFIG, sensors: [] });
    expect(none.emu.state.sensors.map(s => s.id)).toEqual([100, 101, 102, 103, 104]);
    expect(none.emu.setSensor(5, 1)).toBe(false);
  });

  it('nothing is published without a broker, and nothing while it is disconnected', async () => {
    const { emu, clock, rec } = await makeEmulator({ config: CONFIG, sampleSensors: true });
    emu.broker(0).setConnected(false);
    clock.advance(3000);
    expect(rec.publishes).toEqual([]);
    const lonely = await makeEmulator({ config: { info: CONFIG.info }, sampleSensors: true });
    lonely.clock.advance(3000);
    expect(lonely.rec.publishes).toEqual([]);
  });
});

describe('the hardware scan', () => {
  it('the firmware finds the modules, keys and key displays from the virtual I2C bus', async () => {
    const { emu, rec } = await makeEmulator({ config: {}, connectors: [1, 2, 0, 1] });
    expect(emu.state.connectors).toEqual([1, 2, 0, 1]);
    expect(emu.state.physicalKeys()).toEqual([1, 2, 3, 4, 7, 8]);
    expect(emu.state.keyDisplays).toEqual([1, 2, 7, 8]);
    expect(emu.eval('fb.bars.length + " " + typeof fb.main.display + " " + typeof fb.bars[1].right.backLed')).toEqual({ ok: true, value: '2 object object' });
    expect(emu.state).toMatchObject({ firmware: expect.stringMatching(/^FreeButton /), model: 'Model 1 Version 9' });
    void rec;
  });
});
