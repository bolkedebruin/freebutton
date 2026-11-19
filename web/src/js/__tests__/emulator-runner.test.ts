// @vitest-environment node
/**
 * The runner: compile, run, stop, restart, timers on the injected clock,
 * keys with the firmware's long-press timing, errors, runaway scripts.
 */
import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  DEFAULT_WASM_URL, DEVICE_HEAP_BYTES, Emulator, ManualClock, MockBroker, loadEmulatorModule, type MqttMessage, type PublishAbout,
} from '../emulator';
import { readFileSync } from 'node:fs';
import { emulatorModule, makeEmulator, readRepoFile, BASIC_CONFIG, WASM_PATH } from './emulator-helpers';

describe('identity', () => {
  it('is a virtual one by default, never a device id', async () => {
    const emu = await Emulator.create({ config: BASIC_CONFIG, wasm: emulatorModule(), clock: new ManualClock() });
    expect(emu.identity).toMatch(/^virt_btn_[0-9a-f]{6}$/);
    const lines: string[] = [];
    emu.on('console', line => lines.push(line));
    emu.run('print(system.id()); print(system.name());');
    expect(lines).toEqual([emu.identity, 'Test Button']);
  });

  it('refuses a real device id', async () => {
    await expect(Emulator.create({ config: {}, wasm: emulatorModule(), identity: 'btn_a00001' }))
      .rejects.toThrow(/virt_btn_/);
  });

  it('system.name() is the id when the config has no core.name', async () => {
    const { emu, rec } = await makeEmulator({ config: {} });
    emu.run('print(system.name());');
    expect(rec.console).toEqual(['virt_btn_0a1b2c']);
  });
});

describe('compile', () => {
  it('gives bytecode the engine runs', async () => {
    const { emu, rec } = await makeEmulator();
    const compiled = emu.compile('print("from bytecode");');
    expect(compiled.ok).toBe(true);
    if (!compiled.ok) return;
    expect(emu.run(compiled.bytecode)).toEqual({ ok: true });
    expect(rec.console).toEqual(['from bytecode']);
  });

  it('reports a syntax error with its line and column, and runs nothing', async () => {
    const { emu, rec } = await makeEmulator();
    const result = emu.run('var a = 1;\nvar b = ;\nprint("never");');
    expect(result.ok).toBe(false);
    if (result.ok) return;
    expect(result.phase).toBe('compile');
    expect(result.error).toMatchObject({ where: 'compile', line: 2, column: 9 });
    expect(result.error.message).toMatch(/^SyntaxError: /);
    expect(rec.errors).toEqual([result.error]);
    expect(rec.console).toEqual([]);
    expect(emu.running).toBe(false);
  });

  it('compile() alone emits nothing', async () => {
    const { emu, rec } = await makeEmulator();
    expect(emu.compile('foo(;').ok).toBe(false);
    expect(rec.errors).toEqual([]);
  });
});

describe('run', () => {
  it('reports an exception of the top level; what the script registered before it still runs (as on the device)', async () => {
    const { emu, clock, rec } = await makeEmulator();
    const result = emu.run('setTimeout(function () { print("still alive"); }, 10);\nnotDefined();');
    expect(result).toMatchObject({ ok: false, phase: 'run', error: { where: 'run', line: 2 } });
    expect(rec.errors[0]!.message).toBe("ReferenceError: variable 'notDefined' is not defined");
    expect(emu.running).toBe(true);
    clock.advance(10);
    expect(rec.console).toEqual(['still alive']);
  });

  it('runs on the device heap size by default and reports out of memory like the device', async () => {
    const { emu, rec } = await makeEmulator();
    const result = emu.run('var a = []; for (;;) a.push("0123456789abcdef" + a.length);');
    expect(result.ok).toBe(false);
    expect(rec.errors[0]!.message).toMatch(/out of memory/i);
    expect(emu.stats().jsHeapSize).toBe(DEVICE_HEAP_BYTES);
  });

  it('a larger heap is an option', async () => {
    const { emu } = await makeEmulator({ heapBytes: 64 * 1024 });
    emu.run('var a = []; for (var i = 0; i < 1500; i++) a.push("0123456789abcdef" + i);');
    expect(emu.stats()).toMatchObject({ jsHeapSize: 65536, running: true });
    expect(emu.stats().jsHeapUsed).toBeGreaterThan(DEVICE_HEAP_BYTES);
  });

  it('print() arrives as console lines, and log lines of the firmware as log events', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run('print("a", 1, true); console.log("b\\nc");');
    expect(rec.console).toEqual(['a 1 true', 'b', 'c']);
    expect(rec.logs.some(line => line.includes('fb object tree built in C (1 bars, main=0)'))).toBe(true);
  });

  it('load() reads the files given to the emulator', async () => {
    const { emu, rec } = await makeEmulator({ files: { '/lib.js': 'var fromLib = 41;' } });
    expect(emu.run('load("/lib.js"); print(fromLib + 1);\nload("/missing.js");').ok).toBe(false);
    expect(rec.console).toEqual(['42']);
    expect(rec.errors[0]!.message).toBe("Error: failed to load file '/missing.js'");
  });
});

describe('timers', () => {
  it('fire from the injected clock, one per loop pass, and Date.now() is that clock', async () => {
    const { emu, clock, rec } = await makeEmulator();
    const start = clock.now();
    emu.run(`
      setTimeout(function () { print("a", Date.now()); }, 100);
      setTimeout(function () { print("b", Date.now()); }, 100);
      var id = setTimeout(function () { print("cancelled"); }, 50);
      clearTimeout(id);
    `);
    expect(emu.stats().timers).toBe(2);
    clock.advance(99);
    expect(rec.console).toEqual([]);
    clock.advance(1);
    expect(rec.console).toEqual([`a ${start + 100}`]);
    // The second due timer runs on the next pass of loop(), one delay later.
    clock.advance(emu.limits.loopDelayMs);
    expect(rec.console).toEqual([`a ${start + 100}`, `b ${start + 100 + emu.limits.loopDelayMs}`]);
    expect(emu.stats().timers).toBe(0);
  });

  it('does not poll: no timer is pending when the script has nothing to wait for', async () => {
    const { emu, clock } = await makeEmulator();
    emu.run('var x = 1;');
    clock.advance(1000);
    expect(clock.timers).toBe(0);
  });

  it('a setTimeout chain keeps exactly one clock timer', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run('var n = 0; function tick() { n++; print(n); setTimeout(tick, 1000); } tick();');
    clock.advance(3000);
    expect(rec.console).toEqual(['1', '2', '3', '4']);
    expect(clock.timers).toBe(1);
  });

  it('reports an exception in a timer callback (the device swallows it)', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run('setTimeout(function () { missing(); }, 5);');
    clock.advance(5);
    expect(rec.errors).toMatchObject([{ where: 'callback', message: "ReferenceError: variable 'missing' is not defined", line: 1 }]);
    expect(emu.running).toBe(true);
  });

  it('the firmware has 16 timers: the 17th throws its error', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run('for (var i = 0; i < 17; i++) setTimeout(function () {}, 1000);');
    expect(rec.errors[0]!.message).toBe('InternalError: too many timers');
    expect(emu.stats()).toMatchObject({ timers: 16, timerSlots: 16 });
  });
});

describe('stop and restart', () => {
  const script = `
    var made = fb.main.display.create(0, 0, 50, 0, "", 0, "Made", "", 0, 1);
    print("made", made);
    fb.main.display.setValue("temp", "21.5");
    fb.button("light").display.setLabel("On");
    fb.bars[0].left.led.setColor(255, 0, 0);
    fb.main.display.showPage(2);
    mqtt.subscribe(0, "home/#", function () {});
    setTimeout(function () { print("timer"); }, 100);
    fb.button("light").button.onClick(function () { print("click"); });
  `;

  it('stop: timers, handlers and subscriptions are gone; the device state stays', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(script);
    emu.stop();
    expect(rec.exits).toEqual(['stopped']);
    expect(emu.running).toBe(false);
    expect(rec.unsubscribes).toEqual(['0:home/#']);
    expect(emu.broker(0).subscriptions).toEqual([]);
    emu.click('light');
    clock.advance(1000);
    expect(rec.console).toEqual(['made 10']);
    expect(emu.state.key('light')!.label).toBe('On');
    expect(emu.state.shownPage).toBe(2);
    // The value was set while page 2 was shown: the device has it, and shows it with page 1.
    expect(emu.state.item('power')!.box).not.toBeNull();
    expect(emu.state.item('temp')!.box).toBeNull();
    // The device itself runs on: its keys still give the firmware's events, messages are for nobody.
    expect(rec.keys).toEqual(['3:click', '3:release']);
    expect(emu.deliver(0, 'home/x', '1')).toBe('unwanted');
    expect(clock.timers).toBe(0);
  });

  it('restart: what docs/SCRIPT-RESTART.md restores, then the script runs again in a new context', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(script);
    const freeAfterStart = emu.stats().jsHeapFree;
    clock.advance(50);
    rec.changes.length = 0;

    expect(emu.restart()).toEqual({ ok: true });
    expect(rec.exits).toEqual(['restart']);
    // The state went back to config.json before the new top level ran.
    expect(rec.changes[0]).toEqual({ kind: 'reset' });
    // create() returns the same id as after boot.
    expect(rec.console).toEqual(['made 10', 'made 10']);
    expect(emu.state.items.map(i => i.id)).toEqual([7, 9, 10]);
    expect(rec.unsubscribes).toEqual(['0:home/#']);
    expect(rec.subscribes).toEqual(['0:home/#', '0:home/#']);
    // The old script's timer is gone (it was due 50 ms from now); the new one fires 100 ms after the restart.
    // Same heap, new context: as much free as after the first start.
    expect(emu.stats().jsHeapFree).toBe(freeAfterStart);
    clock.advance(60);
    expect(rec.console).toEqual(['made 10', 'made 10']);
    clock.advance(40);
    expect(rec.console).toEqual(['made 10', 'made 10', 'timer']);
  });

  it('restoreConfigState: values empty, texts and LEDs as configured, page 1, removed config items back', async () => {
    const { emu } = await makeEmulator();
    emu.run(`
      fb.main.display.setValue("temp", "1"); fb.main.display.setLabel("temp", "T"); fb.main.display.setUnit("temp", "K");
      fb.main.display.setPosition("temp", 1, 2); fb.main.display.setPage("temp", 3);
      fb.main.display.remove("power");
      fb.button("light").display.setTopLabel("x");
      fb.main.list.setItems("scenes", ["a"]);
      fb.main.image.setIcon("weather", "moon");
      fb.bars[0].right.backLed.setColor(1, 2, 3);
      fb.main.display.showPage(2);
    `);
    expect(emu.state.item('power')).toBeUndefined();
    expect(emu.state.leds[3]).toEqual({ index: 3, r: 1, g: 2, b: 3 });
    emu.stop();
    // The firmware's ConfigManager::restoreConfigState(), at the next start.
    emu.run('var nothing = 0;');
    expect(emu.state.item('temp')).toMatchObject({ value: '', label: 'Temperature', unit: '°C', x: 10, y: 20, page: 1 });
    expect(emu.state.items.map(i => i.name)).toEqual(['temp', 'power']);
    expect(emu.state.key('light')!.topLabel).toBe('Room');
    expect(emu.state.list('scenes')).toMatchObject({ items: ['Morning', 'Evening', 'Night'], selected: 1 });
    expect(emu.state.image('weather')).toMatchObject({ kind: 'icon', value: 'sun', raster: { name: 'sun' } });
    expect(emu.state.leds.every(led => led.r + led.g + led.b === 0)).toBe(true);
    expect(emu.state.shownPage).toBe(1);
  });

  it('run() replaces the running script', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run('setTimeout(function () { print("old"); }, 10);');
    emu.run('setTimeout(function () { print("new"); }, 10);');
    clock.advance(20);
    expect(rec.console).toEqual(['new']);
    expect(rec.exits).toEqual(['restart']);
  });
});

describe('keys', () => {
  const handlers = `
    fb.button("light").button.onClick(function () { print("click"); });
    fb.button("light").button.onLongPress(function () { print("long"); });
    fb.button("light").button.onRelease(function () { print("release"); });
  `;
  // src/drivers/PushButton.h; the scan period comes from the wasm (ButtonManager.h).
  const header = readRepoFile('src/drivers/PushButton.h')!;
  const longDelay = Number(/DEFAULT_LONG_PRESS_DELAY = (\d+)/.exec(header)![1]);
  const longRepeat = Number(/DEFAULT_LONG_PRESS_REPEAT = (\d+)/.exec(header)![1]);

  it('a press is a click at the next key scan, and a release when the key goes up', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(handlers);
    const scan = emu.limits.keyScanMs;
    expect(emu.press('light')).toBe(true);
    expect(emu.pressedKeys).toEqual([3]);
    clock.advance(scan - 1);
    expect(rec.console).toEqual([]);
    clock.advance(1);
    expect(rec.console).toEqual(['click']);
    clock.advance(5 * scan);
    emu.release(3);
    clock.advance(scan);
    expect(rec.console).toEqual(['click', 'release']);
    expect(rec.keys).toEqual(['3:click', '3:release']);
    // The scan stops by itself once every key is up.
    clock.advance(10 * scan);
    expect(clock.timers).toBe(0);
  });

  it('long press and its repeats come from the firmware tick constants', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(handlers);
    const scan = emu.limits.keyScanMs;
    // PushButton::processEvents: the click scan already counts one tick.
    const firstLong = scan + (longDelay - 1) * scan;
    const repeat = longRepeat * scan;
    expect([longDelay, longRepeat, scan]).toEqual([40, 25, 20]);

    emu.press('light');
    clock.advance(firstLong - 1);
    expect(rec.console).toEqual(['click']);
    clock.advance(1);
    expect(rec.console).toEqual(['click', 'long']);
    clock.advance(repeat - 1);
    expect(rec.console).toEqual(['click', 'long']);
    clock.advance(1);
    expect(rec.console).toEqual(['click', 'long', 'long']);
    clock.advance(repeat);
    emu.release('light');
    clock.advance(scan);
    expect(rec.console).toEqual(['click', 'long', 'long', 'long', 'release']);
  });

  it('a handler returning false prevents the firmware\'s own press event; any other value does not', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(`
      fb.button("light").button.onClick(function () { return false; });
      fb.button("blind").button.onClick(function () { return true; });
      fb.button("blind").button.onRelease(function () { return 0; });
    `);
    emu.press('light');
    emu.press('blind');
    clock.advance(emu.limits.keyScanMs);
    emu.release('light');
    emu.release('blind');
    clock.advance(emu.limits.keyScanMs);
    expect(rec.keys).toEqual(['3:click:prevented', '4:click', '3:release', '4:release']);
  });

  it('a key without a handler still gives the firmware\'s event (also with no script running)', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.press(1);
    clock.advance(emu.limits.keyScanMs);
    expect(rec.keys).toEqual(['1:click']);
  });

  it('click() is a press sent without timing: click, then release', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run(handlers);
    expect(emu.click('light')).toBe(true);
    expect(rec.console).toEqual(['click', 'release']);
  });

  it('a key on a connector without a module does not exist', async () => {
    const { emu } = await makeEmulator();
    expect(emu.press(7)).toBe(false);
    expect(emu.press('nobody')).toBe(false);
    expect(emu.click(8)).toBe(false);
  });

  it('reports an exception in a key handler; the press event is not prevented', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run('fb.button("light").button.onClick(function () { broken(); return false; });');
    emu.click('light');
    expect(rec.errors).toMatchObject([{ where: 'key', message: "ReferenceError: variable 'broken' is not defined" }]);
    expect(rec.keys[0]).toBe('3:click');
  });
});

describe('runaway scripts and engine panics', () => {
  it('while(true){} at the top level is stopped after the time budget; the page lives on', async () => {
    const { emu, rec } = await makeEmulator({ interruptBudgetMs: 30 });
    const began = performance.now();
    const result = emu.run('print("before"); while (true) {}');
    expect(performance.now() - began).toBeLessThan(2000);
    expect(result.ok).toBe(false);
    expect(rec.console).toEqual(['before']);
    expect(rec.errors.map(e => e.where)).toEqual(['run', 'interrupt']);
    expect(rec.errors[0]!.message).toBe('InternalError: interrupted');
    expect(rec.exits).toEqual(['interrupted']);
    expect(emu.running).toBe(false);
    // And the emulator is still usable.
    expect(emu.run('print("after");')).toEqual({ ok: true });
    expect(rec.console).toEqual(['before', 'after']);
  });

  it('a runaway timer callback is stopped the same way', async () => {
    let now = 0;
    const { emu, clock, rec } = await makeEmulator({ interruptBudgetMs: 100, wallClock: () => (now += 60) });
    emu.run('setTimeout(function () { for (;;) {} }, 10);');
    expect(emu.running).toBe(true);
    clock.advance(10);
    expect(rec.errors.map(e => e.where)).toEqual(['callback', 'interrupt']);
    expect(rec.exits).toEqual(['interrupted']);
  });

  it('the interruption cannot be caught by the script', async () => {
    let now = 0;
    const { emu, rec } = await makeEmulator({ interruptBudgetMs: 100, wallClock: () => (now += 60) });
    const result = emu.run('for (;;) { try { for (;;) {} } catch (e) { print("caught"); } }');
    expect(result.ok).toBe(false);
    expect(rec.console).toEqual([]);
    expect(rec.exits).toEqual(['interrupted']);
  });

  it('an engine trap is reported, and the next run gets a new engine with the same device', async () => {
    // A trap inside the wasm reaches the host as a WebAssembly.RuntimeError.
    class TrappingBroker extends MockBroker {
      override publish(message: MqttMessage, about?: PublishAbout): void {
        if (about?.origin === 'script') throw new WebAssembly.RuntimeError('unreachable');
        super.publish(message);
      }
    }
    let trap = true;
    const { emu, rec } = await makeEmulator({
      transport: context => (trap ? new TrappingBroker(context) : new MockBroker(context)),
    });
    emu.run('fb.main.display.setValue("temp", "before"); mqtt.publish(0, "a", "b"); print("not reached");');
    expect(rec.errors[rec.errors.length - 1]).toMatchObject({ where: 'panic' });
    expect(rec.console).toEqual([]);
    expect(emu.running).toBe(false);
    expect(emu.stats().running).toBe(false);

    trap = false;
    expect(emu.run('print(fb.main.display.count());')).toEqual({ ok: true });
    expect(rec.console).toEqual(['2']);
    expect(emu.running).toBe(true);
  });

  it('engine calls from inside an event are refused, stop() is deferred', async () => {
    const { emu, rec } = await makeEmulator();
    let thrown = '';
    emu.on('console', () => {
      try { emu.run('1'); } catch (error) { thrown = String(error); }
      emu.stop();
    });
    emu.run('print("x"); setTimeout(function () { print("never"); }, 1);');
    expect(thrown).toMatch(/called from inside a script callback/);
    expect(rec.exits).toEqual(['stopped']);
    expect(emu.running).toBe(false);
  });
});

describe('console lines and stats', () => {
  it('eval() runs a line in the running context', async () => {
    const { emu } = await makeEmulator();
    emu.run('var counter = 41;');
    expect(emu.eval('counter + 1')).toEqual({ ok: true, value: '42' });
    expect(emu.eval('nope()')).toMatchObject({ ok: false, error: { where: 'eval', message: "ReferenceError: variable 'nope' is not defined" } });
  });

  it('eval() without a script gives a context with fb, like a device without a boot script', async () => {
    const { emu } = await makeEmulator();
    expect(emu.eval('typeof fb.main.display.setValue')).toEqual({ ok: true, value: 'function' });
  });

  it('stats() reports what /api/device reports for the JS heap', async () => {
    const { emu } = await makeEmulator();
    emu.run('var keep = "x"; mqtt.subscribe(0, "a", function () {}); setTimeout(function () {}, 1000);');
    const stats = emu.stats();
    expect(stats.jsHeapUsed + stats.jsHeapFree).toBeLessThanOrEqual(stats.jsHeapSize);
    expect(stats.jsHeapUsed).toBeGreaterThan(3000);
    expect(stats).toMatchObject({ jsHeapSize: 16384, timers: 1, subscriptions: 1, mqttDropped: 0, running: true });
  });
});

describe('in the page', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.useRealTimers();
  });

  it('the wasm is fetched on first use only, once, from the assets folder', async () => {
    const bytes = readFileSync(WASM_PATH);
    const fetchMock = vi.fn(async (_url: string) => new Response(new Uint8Array(bytes)));
    vi.stubGlobal('fetch', fetchMock);
    // Importing the module fetched nothing.
    expect(fetchMock).not.toHaveBeenCalled();
    const first = await Emulator.create({ config: BASIC_CONFIG, clock: new ManualClock() });
    const second = await Emulator.create({ config: BASIC_CONFIG, clock: new ManualClock() });
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0]![0]).toBe(DEFAULT_WASM_URL);
    expect(first.run('print(1);')).toEqual({ ok: true });
    expect(second.identity).not.toBe(first.identity);
  });

  it('a failed download is reported and tried again next time', async () => {
    const url = 'assets/missing-emulator.wasm';
    const fetchMock = vi.fn(async (_url: string) => new Response('', { status: 404 }));
    vi.stubGlobal('fetch', fetchMock);
    await expect(loadEmulatorModule(url)).rejects.toThrow(/HTTP 404/);
    await expect(loadEmulatorModule(url)).rejects.toThrow(/HTTP 404/);
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  it('with the real clock a waiting script is one browser timer to its next due time, not a poll', async () => {
    vi.useFakeTimers();
    const emu = await Emulator.create({ config: BASIC_CONFIG, wasm: emulatorModule(), sampleSensors: false });
    const lines: string[] = [];
    emu.on('console', line => lines.push(line));
    emu.run('setTimeout(function () { print("fired"); }, 60000);');
    await vi.advanceTimersByTimeAsync(20);
    // The first loop pass learned when the timer is due; nothing runs until then.
    expect(vi.getTimerCount()).toBe(1);
    await vi.advanceTimersByTimeAsync(59_000);
    expect(lines).toEqual([]);
    expect(vi.getTimerCount()).toBe(1);
    await vi.advanceTimersByTimeAsync(1000);
    expect(lines).toEqual(['fired']);
    expect(vi.getTimerCount()).toBe(0);
    emu.dispose();
  });
});
