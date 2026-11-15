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

  for (const name of names) {
    const source = readRepoFile(`data/scripts/examples/${name}`)!;

    it.skipIf(name in KNOWN_BROKEN)(`${name} compiles and runs its top level without an error on the example config`, async () => {
      const { emu, clock, rec } = await makeEmulator({ config });
      const result = emu.run(source);
      expect(rec.errors).toEqual([]);
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
    const { emu, clock, rec } = await makeEmulator({ config });
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
    // 2026-01-01T12:00:00Z; the emulator's local time is UTC.
    const { emu, clock } = await load();
    clock.advance(0);
    expect(emu.state.item('time')!.value).toBe('12:00');
    clock.advance(61_000);
    expect(emu.state.item('time')!.value).toBe('12:01');
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
