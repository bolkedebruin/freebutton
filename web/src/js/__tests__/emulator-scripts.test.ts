// @vitest-environment node
/**
 * Real scripts in the emulator: the IDE's examples (data/scripts/examples,
 * the files web/src/js/examples.ts imports) and, when the devices folder is
 * there (a separate repository), the two device scripts with their configs.
 */
import { existsSync, readdirSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { makeEmulator, readRepoFile, repoPath } from './emulator-helpers';

describe('the IDE example scripts', () => {
  const dir = repoPath('data/scripts/examples');
  const names = readdirSync(dir).filter(name => name.endsWith('.js')).sort();
  const config = JSON.parse(readRepoFile('data/config.json')!) as unknown;

  it('are the six the IDE offers', () => {
    expect(names).toEqual(['blink.js', 'button.js', 'displays.js', 'mqtt_demo.js', 'mqtt_simple.js', 'sensor.js']);
  });

  /**
   * Examples that do not run, with the engine's error. Not the emulator's
   * doing: the engine is the device's. Remove an entry when the example is
   * fixed (this test then fails). None now: displays.js declared
   * `function show()` inside an `else { }` block, which this engine does not
   * define; it is declared at the top level since.
   */
  const KNOWN_BROKEN: Record<string, string> = {};

  /**
   * Errors of an example that runs on, as the firmware reports them (the
   * device only logs these). None now: mqtt_demo.js called sensor.onChange()
   * for every sensor that sensor.forEach() gives, the firmware's memory
   * sensors (ids 100..104) included, and onChange takes ids 0..7
   * ("RangeError: sensor ID 100 out of range (0-7)"); it skips those since.
   */
  const KNOWN_ERRORS: Record<string, string[]> = {};

  for (const name of names) {
    const source = readRepoFile(`data/scripts/examples/${name}`)!;

    it.skipIf(name in KNOWN_BROKEN)(`${name} compiles and runs its top level without an error on the example config`, async () => {
      const { emu, clock, rec } = await makeEmulator({ config, sampleSensors: true });
      const result = emu.run(source);
      expect(rec.errors.map(e => e.message)).toEqual(KNOWN_ERRORS[name] ?? []);
      rec.errors.length = 0;
      expect(result).toEqual({ ok: true });
      // And keeps running: timers, keys and a few messages raise no error either.
      clock.advance(5000);
      for (const key of emu.state.keys) emu.click(key.position);
      for (const sub of emu.broker(0).subscriptions) {
        emu.broker(0).inject(sub.topic.replace(/[+#]/g, 'x'), '1');
      }
      clock.advance(5000);
      expect(rec.errors).toEqual([]);
      expect(emu.running).toBe(true);
    });

    it.skipIf(!(name in KNOWN_BROKEN))(`${name} compiles, but stops with the engine's error (known)`, async () => {
      const { emu, rec } = await makeEmulator({ config });
      expect(emu.compile(source).ok).toBe(true);
      const result = emu.run(source);
      expect(result.ok).toBe(false);
      expect(rec.errors.map(e => e.message)).toEqual([KNOWN_BROKEN[name]]);
    });
  }

  it('displays.js shows the temperature and follows the sensor (it stopped with "variable \'show\' is not defined")', async () => {
    const { emu, clock, rec } = await makeEmulator({ config, sampleSensors: true });
    expect(emu.run(readRepoFile('data/scripts/examples/displays.js')!)).toEqual({ ok: true });
    expect(rec.errors).toEqual([]);
    const item = emu.state.items.find(i => i.label === 'Temperature')!;
    expect(item.value).toBe('21.5');
    emu.setSensor(5, 23.44);
    clock.advance(2000);
    expect(item.value).toBe('23.4');
    expect(rec.console).toContain('Temperature display running — updates on sensor change');
    expect(rec.errors).toEqual([]);
  });

  it('displays.js runs to its end on a device without a temperature sensor', async () => {
    const { emu, rec } = await makeEmulator({ config, sensors: [] });
    expect(emu.run(readRepoFile('data/scripts/examples/displays.js')!)).toEqual({ ok: true });
    expect(rec.console).toEqual(['No temperature sensor found']);
  });
});

describe.skipIf(!existsSync(repoPath('devices/btn_a00002/btn_a00002.js')))('device script btn_a00002', () => {
  const load = async () => {
    const source = readRepoFile('devices/btn_a00002/btn_a00002.js')!;
    const config = JSON.parse(readRepoFile('devices/btn_a00002/config.json')!) as unknown;
    const made = await makeEmulator({ config });
    return { ...made, result: made.emu.run(source) };
  };

  it('starts without an error and subscribes to its topics', async () => {
    const { emu, rec, result } = await load();
    expect(rec.errors).toEqual([]);
    expect(result).toEqual({ ok: true });
    expect(emu.broker(0).subscriptions.map(s => s.topic)).toContain('buttonplus/btn_a00001/display/#');
  });

  it('shows the outside temperature it gets from the other device', async () => {
    const { emu, clock, rec } = await load();
    expect(emu.state.item('outsideTemp')).toBeDefined();
    emu.broker(0).inject('buttonplus/btn_a00001/display/outside_temperature', '6');
    clock.advance(0);
    expect(emu.state.item('outsideTemp')!.value).toBe('6°');
    expect(rec.changes).toContainEqual({ kind: 'item', id: emu.state.item('outsideTemp')!.id, field: 'value' });
    expect(rec.errors).toEqual([]);
  });

  it('shows the time of the injected clock, and again a minute later', async () => {
    // 2026-01-01T12:00:00Z; the config has no core.timezone: the firmware's default, CET.
    const { emu, clock } = await load();
    clock.advance(0);
    expect(emu.state.item('time')!.value).toBe('13:00');
    clock.advance(61_000);
    expect(emu.state.item('time')!.value).toBe('13:01');
  });

  it('takes other payloads on its topics without an error', async () => {
    const { emu, clock, rec } = await load();
    emu.broker(0).inject('buttonplus/btn_a00001/sensor/sts35_1/state', '21.4');
    emu.broker(0).inject('victron/N/0123456789ab/system/0/Ac/Grid/L1/Power', '{"value":412}');
    emu.broker(0).inject('home/weather/current/icon', 'weather-sunny');
    emu.broker(0).inject('buttonplus/btn_a00001/cover/raam/state', 'open');
    emu.broker(0).inject('buttonplus/btn_a00001/filter/state', 'ON');
    clock.advance(1000);
    for (const key of emu.state.keys) {
      emu.press(key.position);
      clock.advance(100);
      emu.release(key.position);
      clock.advance(100);
    }
    expect(rec.errors).toEqual([]);
  });
});

describe.skipIf(!existsSync(repoPath('devices/btn_a00001/btn_a00001.js')))('device script btn_a00001', () => {
  const load = async () => {
    const source = readRepoFile('devices/btn_a00001/btn_a00001.js')!;
    const config = JSON.parse(readRepoFile('devices/btn_a00001/config.json')!) as unknown;
    const made = await makeEmulator({ config });
    return { ...made, source, result: made.emu.run(source) };
  };

  it('without a script: what the firmware shows from config.json alone', async () => {
    const config = JSON.parse(readRepoFile('devices/btn_a00001/config.json')!) as unknown;
    const { emu, clock, rec } = await makeEmulator({ config, sampleSensors: true });
    clock.advance(5000);
    const s = emu.state;
    expect(s.connectors).toEqual([2, 1, 1, 1]);
    expect(s.fonts.map(f => f.name)).toEqual(['montserrat', 'montserrat_semibold', 'mdi', 'pacifico']);
    // The items of page 1 are drawn, empty: this config has no topics, the script fills them.
    const drawn = s.items.filter(i => i.box);
    expect(drawn.map(i => i.name)).toEqual(['outsideTemp', 'date', 'co2', 'humidity', 'filter', 'weatherNow', 'weatherToday', 'volume']);
    expect(drawn.every(i => i.value === '' && i.label === '')).toBe(true);
    expect(s.items).toHaveLength(34);
    expect(s.item('sceneTitle')).toMatchObject({ label: 'Scènes', page: 4, box: null });
    // The six keys of the three bars show their configured labels (icon glyphs of the "mdi" font).
    expect(s.keys.map(k => [k.position, k.name, k.topLabel, k.display !== null])).toEqual([
      [3, 'volumeDown', 'Volume', true], [4, 'volumeUp', 'Volume', true], [5, 'windowOpen', 'Raam', true],
      [6, 'windowClose', 'Raam', true], [7, 'screenOpen', 'Scherm', true], [8, 'screenClose', 'Scherm', true],
    ]);
    expect(s.key('windowOpen')!.display!.label.runs[0]).toMatchObject({ text: 'Open' });
    // The analog clock of page 1 runs; the list of scenes is on another page.
    expect(s.clocks).toMatchObject([{ page: 1, width: 180, height: 180, shown: true }]);
    expect(s.lists[0]).toMatchObject({ name: 'scenes', items: ['Ochtend', 'Vertrek', 'Diner', 'Avond', 'Bedtijd'], roller: null });
    // The firmware announces itself to Home Assistant and listens for its presses; nothing else is subscribed.
    expect(emu.broker(0).subscriptions.map(x => x.topic)).toEqual([
      'homeassistant/status', ...[1, 2, 3, 4, 5, 6, 7, 8].map(n => `buttonplus/virt_btn_0a1b2c/button_${n}/set`),
    ]);
    const kinds = new Map<string, number>();
    for (const p of rec.publishes) kinds.set(p.kind, (kinds.get(p.kind) ?? 0) + 1);
    expect(Object.fromEntries(kinds)).toEqual({ discovery: 52, sensor: 40 });
    expect(rec.publishes.every(p => p.origin === 'firmware' && !p.topic.includes('btn_a00001'))).toBe(true);
    expect(rec.errors).toEqual([]);
  });

  it('starts without an error, with all 8 subscriptions the firmware allows', async () => {
    const { emu, rec, result } = await load();
    expect(rec.errors).toEqual([]);
    expect(result).toEqual({ ok: true });
    expect(emu.stats().subscriptions).toBe(8);
  });

  it('heap fidelity: the device reports jsHeapFree 5256 with this script; the emulator is within a few hundred bytes', async () => {
    const { emu, source } = await load();
    const compiled = emu.compile(source);
    expect(compiled.ok && compiled.bytecode.length).toBeGreaterThan(20_000);
    const stats = emu.stats();
    // The bytecode runs from outside the 16 KB heap, as from flash on the device.
    expect(stats.jsHeapSize).toBe(16384);
    expect(Math.abs(stats.jsHeapFree - 5256)).toBeLessThan(400);
  });

  it('reacts to its topics: state changes, no errors, and the heap does not run out', async () => {
    const { emu, clock, rec } = await load();
    rec.changes.length = 0;
    const broker = emu.broker(0);
    broker.inject('buttonplus/btn_a00001/display/outside_temperature', '6');
    broker.inject('buttonplus/btn_a00001/display/inside_temperature', '21.5');
    broker.inject('home/weather/current/condition', 'sunny');
    broker.inject('home/weather/forecast/today/1/condition', 'rainy');
    broker.inject('victron/N/0123456789ab/system/0/Dc/Battery/Soc', '{"value":87}');
    broker.inject('victron/N/0123456789ab/system/0/Ac/Grid/L1/Power', '{"value":412}');
    broker.inject('buttonplus/btn_a00001/cover/raam/state', 'open');
    broker.inject('buttonplus/btn_a00001/filter/state', 'ON');
    clock.advance(2000);
    for (const key of emu.state.keys) {
      emu.press(key.position);
      clock.advance(100);
      emu.release(key.position);
      clock.advance(100);
    }
    clock.advance(120_000);
    expect(rec.errors).toEqual([]);
    expect(rec.changes.length).toBeGreaterThan(3);
    expect(emu.stats().jsHeapFree).toBeGreaterThan(1500);
  });
});
