// @vitest-environment node
/**
 * The seam between fb-emulator.wasm and the TypeScript host: the import list
 * and the limits, each checked against what the wasm (built from the
 * firmware's sources) says, so a firmware change fails here.
 */
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { EngineInstance } from '../emulator';
import { emulatorModule, makeEmulator, readRepoFile, repoPath } from './emulator-helpers';

describe('what crosses the seam', () => {
  it('a display item goes to the firmware as config.json and comes back through its own binding', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run('print(JSON.stringify(fb.main.display.get("power")));');
    // displayitem_get_info is the firmware's (src/scripting/displayitem_binding.cpp) since stage 2.
    expect(JSON.parse(rec.console[0]!)).toEqual({
      id: 9, x: 50, y: 20, width: 40, boxType: 2, fontSize: 0, alignment: 0, label: 'Power', unit: 'W', round: 1, page: 2,
    });
  });

  it('the display comes back as records the host only has to keep', async () => {
    const { emu } = await makeEmulator();
    expect(emu.state.item('temp')!.box).toMatchObject({
      x: 32, y: 48, width: 128, caption: { text: 'Temperature', runs: [{ text: 'Temperature' }] },
    });
  });
});

describe('imports', () => {
  const wanted = WebAssembly.Module.imports(emulatorModule());

  it('the loader provides exactly what the wasm imports', () => {
    const env = wanted.filter(i => i.module === 'env').map(i => i.name).sort();
    const wasi = wanted.filter(i => i.module === 'wasi_snapshot_preview1').map(i => i.name).sort();
    expect(EngineInstance.envImportNames()).toEqual(env);
    expect(EngineInstance.wasiImportNames()).toEqual(wasi);
    expect(wanted.every(i => i.kind === 'function')).toBe(true);
  });

  it('tools/js/emulator/imports.txt is that list (the build checks the wasm against it)', () => {
    const listed = readFileSync(repoPath('tools/js/emulator/imports.txt'), 'utf8').split('\n').filter(Boolean).sort();
    expect(listed).toEqual(wanted.map(i => `${i.module}.${i.name}`).sort());
  });
});

describe('limits come from the firmware sources', () => {
  const define = (file: string, name: string): number => {
    const match = new RegExp(`${name}\\s*=?\\s*\\(?(\\d+)`).exec(readRepoFile(file) ?? '');
    if (!match) throw new Error(`${name} not found in ${file}`);
    return Number(match[1]);
  };

  it('reports the values the firmware files have', async () => {
    const { emu } = await makeEmulator();
    expect(emu.limits.subscriptionsPerBroker).toBe(define('src/scripting/js/freebutton_mqtt.c', '#define MAX_SUBSCRIPTIONS_PER_BROKER'));
    expect(emu.limits.brokersWithSubscriptions).toBe(define('src/scripting/js/freebutton_mqtt.c', '#define MAX_BROKERS'));
    expect(emu.limits.timers).toBe(define('src/scripting/js/stdlib_export.c', '#define MAX_TIMERS'));
    expect(emu.limits.mqttQueueBytes).toBe(define('src/scripting/mqtt_binding.cpp', 'kQueueBytes'));
    expect(emu.limits.keyScanMs).toBe(define('src/drivers/ButtonManager.h', 'DEFAULT_UPDATE_INTERVAL'));
    expect(emu.limits.mqttInBufferBytes).toBe(define('sdkconfig.esp32', 'CONFIG_MQTT_BUFFER_SIZE'));
    expect(emu.limits.leds).toBe(define('src/drivers/NeoPixel.h', 'NUMBER_OF_LEDS'));
    expect(emu.limits.keys).toBe(define('include/config.h', '#define BUTTON_COUNT'));
    expect(emu.state.leds).toHaveLength(emu.limits.leds);
  });

  it('system.version() and system.model() are include/config.h', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run('print(system.version()); print(system.model());');
    const config = readRepoFile('include/config.h')!;
    expect(rec.console[0]).toBe(/#define FIRMWARE "([^"]+)"/.exec(config)![1]);
    expect(rec.console[1]).toBe(/#define MODEL "([^"]+)"/.exec(config)![1]);
  });
});
