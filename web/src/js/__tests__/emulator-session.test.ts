import { describe, it, expect, afterEach, beforeEach, vi } from 'vitest';
import { emulatorModule, BASIC_CONFIG } from './emulator-helpers';
import { EmulatorSession, MQTT_LOG_MAX, virtualDeviceId, type RunSource, type SessionFile } from '../emulator-session';
import type { ConsoleLine, EmulatorSnapshot } from '../app-state';
import { workspace } from '../workspace';

const CONFIG = JSON.stringify(BASIC_CONFIG);

const wait = (ms: number) => new Promise(r => setTimeout(r, ms));

let session: EmulatorSession;
let lines: ConsoleLine[];
let snapshots: EmulatorSnapshot[];
const onLine = (e: Event) => lines.push((e as CustomEvent<ConsoleLine>).detail);
const onChange = (e: Event) => snapshots.push((e as CustomEvent<EmulatorSnapshot>).detail);

function start(script: string, config = CONFIG): Promise<void> {
  const source: RunSource = { script, config };
  session.readSource = async () => source;
  return session.run();
}

/** In-memory Storage: recent Node versions shadow happy-dom's localStorage with their own (unusable) one. */
function memoryStorage(): Storage {
  const map = new Map<string, string>();
  return {
    getItem: (k: string) => map.get(k) ?? null,
    setItem: (k: string, v: string) => { map.set(k, String(v)); },
    removeItem: (k: string) => { map.delete(k); },
    clear: () => map.clear(),
    key: (i: number) => [...map.keys()][i] ?? null,
    get length() { return map.size; },
  } as Storage;
}

beforeEach(() => {
  vi.stubGlobal('localStorage', memoryStorage());
  session = new EmulatorSession();
  session.wasm = emulatorModule();
  lines = [];
  snapshots = [];
  window.addEventListener('console-line', onLine);
  window.addEventListener('emulator-change', onChange);
});

afterEach(() => {
  session.destroy();
  window.removeEventListener('console-line', onLine);
  window.removeEventListener('emulator-change', onChange);
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

describe('virtual device identity', () => {
  it('is virt_btn_ + 6 hex digits, made once per browser and kept', () => {
    const id = virtualDeviceId();
    expect(id).toMatch(/^virt_btn_[0-9a-f]{6}$/);
    expect(virtualDeviceId()).toBe(id);
    expect(localStorage.getItem('fb-virtual-device-id')).toBe(id);
    // Something else in the store (a real device's id) is never used.
    localStorage.setItem('fb-virtual-device-id', 'btn_a00001');
    expect(virtualDeviceId()).toMatch(/^virt_btn_[0-9a-f]{6}$/);
  });

  it('is what system.id() gives the script', async () => {
    await start('print(system.id());');
    expect(lines.find(l => l.type === 'log')).toEqual({ text: virtualDeviceId(), type: 'log', to: 'virtual' });
    expect(session.snapshot().identity).toBe(virtualDeviceId());
  });
});

describe('emulator session: run and stop', () => {
  it('idle -> starting -> running; print output goes to the virtual console', async () => {
    expect(session.snapshot()).toMatchObject({ status: 'idle', mqtt: 'offline', used: false });
    await start('print("hello"); fb.main.display.setValue("temp", "21.5");');
    expect(snapshots.map(s => s.status)).toContain('starting');
    expect(session.snapshot()).toMatchObject({ status: 'running', errors: 0, stale: null, used: true });
    expect(lines.filter(l => l.type === 'log')).toEqual([{ text: 'hello', type: 'log', to: 'virtual' }]);
    // Every line of the emulator is for the virtual console: none for the device's.
    expect(lines.every(l => l.to === 'virtual')).toBe(true);
    // The pages drawn: config.json with what the script changed.
    expect(session.pages()![0]!.displayItems[0]).toMatchObject({ name: 'temp', value: '21.5' });
    expect(session.shownPage()).toBe(1);
  });

  it('stop: nothing runs, the pages are config.json\'s again', async () => {
    await start('function tick() { print("tick"); setTimeout(tick, 10); } tick();');
    await wait(40);
    expect(lines.filter(l => l.text === 'tick').length).toBeGreaterThan(1);
    session.stop();
    expect(session.snapshot()).toMatchObject({ status: 'idle', used: true });
    expect(session.pages()).toBeNull();
    const before = lines.length;
    await wait(60);
    expect(lines.length).toBe(before);
    expect(session.press(3)).toBe(false);
  });

  it('Run while running starts again from config.json', async () => {
    await start('fb.main.display.setValue("temp", "1");');
    await start('print("second");');
    expect(session.snapshot().status).toBe('running');
    expect(session.pages()![0]!.displayItems[0]!.value).toBe('');
    expect(lines.some(l => l.text === 'second')).toBe(true);
  });

  it('a compile error: stopped with the error, an error line with the position, nothing runs', async () => {
    await start('var x = ;\n');
    const snap = session.snapshot();
    expect(snap.status).toBe('error');
    expect(snap.error).not.toBe('');
    expect(lines.filter(l => l.type === 'error').length).toBe(1);
    expect(lines.find(l => l.type === 'error')!.text).toMatch(/^compile: /);
    expect(session.pages()).toBeNull();
  });

  it('an error at the top level: reported with its line; the script context stays (handlers run), as on the device', async () => {
    await start('fb.key("light").button.onClick(function () { print("clicked"); });\nundefinedFunction();\n');
    const snap = session.snapshot();
    expect(snap.status).toBe('running');
    expect(snap.errors).toBe(1);
    expect(snap.error).toContain('undefinedFunction');
    expect(lines.find(l => l.type === 'error')!.text).toMatch(/line 2/);
    lines.length = 0;
    session.press(3);
    await wait(80);
    session.release(3);
    await wait(80);
    expect(lines.some(l => l.text === 'clicked')).toBe(true);
  });

  it('an error in a handler: an error line, it keeps running, the count goes up', async () => {
    await start('setTimeout(function () { throw new Error("boom"); }, 10);');
    await wait(80);
    expect(session.snapshot()).toMatchObject({ status: 'running', errors: 1 });
    expect(lines.find(l => l.type === 'error')!.text).toContain('boom');
  });

  it('invalid config.json: does not start, and says so', async () => {
    await start('print(1);', '{ "buttons": [ }');
    expect(session.snapshot().status).toBe('error');
    expect(session.snapshot().error).toContain('config.json is not valid JSON');
    expect(lines.some(l => l.type === 'error' && l.text.includes('did not start'))).toBe(true);
  });

  it('the script cannot be read: does not start', async () => {
    session.readSource = async () => null;
    await session.run();
    expect(session.snapshot()).toMatchObject({ status: 'error' });
    expect(session.snapshot().error).toContain('scripts/main.js could not be read');
  });

  it('a script that never returns is stopped with an error', async () => {
    const spy = vi.spyOn(performance, 'now');
    let t = 0;
    spy.mockImplementation(() => (t += 50));
    await start('while (true) {}');
    spy.mockRestore();
    await wait(0);
    expect(session.snapshot().status).toBe('error');
    expect(session.snapshot().error).toContain('without returning');
  });
});

describe('emulator session: info (the virtual device\'s info side)', () => {
  it('not run yet: the identity, idle, nothing measured', () => {
    expect(session.info()).toEqual({
      identity: virtualDeviceId(), status: 'idle', error: '', mqtt: 'offline', uptimeSeconds: null, heap: null, bytecode: null,
      timers: null, subscriptions: null, mqttDropped: null, brokers: null, sensors: null, firmware: null, model: null, connectors: null,
    });
  });

  it('running: heap, bytecode, timers and subscriptions against the firmware\'s limits, brokers, sensors, modules, version', async () => {
    await start('setTimeout(function () {}, 60000); mqtt.subscribe(0, "a/b", function () {}); mqtt.subscribe(0, "c/#", function () {});');
    expect(session.snapshot().errors).toBe(0);
    const info = session.info();
    expect(info).toMatchObject({
      identity: virtualDeviceId(), status: 'running', error: '', mqtt: 'offline', uptimeSeconds: 0, mqttDropped: 0,
      timers: { used: 1, limit: 16 }, subscriptions: { used: 2, limit: 8 },
      brokers: ['local'], sensors: 3, connectors: [2, 1, 0, 0],
      model: 'Model 1 Version 9',
    });
    expect(info.firmware).toMatch(/^FreeButton \d+\.\d+/);
    expect(info.heap!.size).toBe(16384);
    expect(info.heap!.free + info.heap!.used).toBeLessThanOrEqual(16384);
    expect(info.heap!.free).toBeGreaterThan(1000);
    expect(info.bytecode!.bytes).toBeGreaterThan(50);
    expect(info.bytecode!.limit).toBeGreaterThan(info.bytecode!.bytes);
    // Asking the version costs the script nothing visible: no console line, no error.
    expect(lines.filter(l => l.type === 'error' || l.type === 'result')).toEqual([]);
  });

  it('the heap is measured when asked: an allocation shows', async () => {
    await start('var keep = [];');
    const before = session.info().heap!.free;
    window.dispatchEvent(new CustomEvent('console-eval', { detail: { code: 'for (var i = 0; i < 40; i++) keep.push("line " + i); keep.length', target: 'virtual' } }));
    expect(session.info().heap!.free).toBeLessThan(before);
  });

  it('stopped with an error: the state and the error; a clean Stop: idle, no error', async () => {
    await start('var x = ;');
    expect(session.info()).toMatchObject({ status: 'error', heap: null });
    expect(session.info().error).toContain('SyntaxError');
    await start('1;');
    session.stop();
    expect(session.info()).toMatchObject({ status: 'idle', error: '', uptimeSeconds: null });
  });
});

describe('emulator session: keys, console, staleness', () => {
  const SCRIPT = `
    var n = 0;
    fb.key("light").button.onClick(function () {
      n++;
      fb.key("light").display.setLabel("On " + n);
      fb.bars[0].left.led.setColor(0, 255, 0);
    });
    fb.key("light").button.onLongPress(function () { print("long"); });
  `;

  it('a short press is a click: label and LED follow', async () => {
    await start(SCRIPT);
    expect(session.press(3)).toBe(true);
    await wait(80);
    session.release(3);
    await wait(80);
    const key = session.pages()![0]!.buttons.find(b => b.position === 3)!;
    expect(key).toMatchObject({ label: 'On 1', ledOn: true, ledColor: 'rgb(0,255,0)' });
    expect(lines.some(l => l.text === 'long')).toBe(false);
  });

  it('a tap shorter than the firmware\'s key scan is still a click', async () => {
    await start(SCRIPT);
    session.press(3);
    session.release(3);
    await wait(200);
    expect(session.pages()![0]!.buttons.find(b => b.position === 3)!.label).toBe('On 1');
    // Two quick taps: two clicks.
    session.press(3);
    session.release(3);
    await wait(200);
    expect(session.pages()![0]!.buttons.find(b => b.position === 3)!.label).toBe('On 2');
  });

  it('holding a key is a long press', async () => {
    await start(SCRIPT);
    session.press(3);
    await wait(1000);
    session.release(3);
    await wait(60);
    expect(lines.some(l => l.text === 'long')).toBe(true);
  });

  it('a key that does not exist is not pressed', async () => {
    await start(SCRIPT);
    expect(session.press(8)).toBe(false);
  });

  it('console lines for the virtual device are evaluated in the script; others are left alone', async () => {
    await start('var answer = 41;');
    window.dispatchEvent(new CustomEvent('console-eval', { detail: { code: 'answer + 1', target: 'virtual' } }));
    expect(lines[lines.length - 1]).toEqual({ text: '42', type: 'result', to: 'virtual' });
    const before = lines.length;
    window.dispatchEvent(new CustomEvent('console-eval', { detail: { code: 'answer + 2', target: 'device' } }));
    window.dispatchEvent(new CustomEvent('console-eval', { detail: { code: 'answer + 3' } }));
    expect(lines.length).toBe(before);
    window.dispatchEvent(new CustomEvent('console-eval', { detail: { code: 'nope(', target: 'virtual' } }));
    expect(lines[lines.length - 1]).toMatchObject({ type: 'error', to: 'virtual' });
  });

  it('a line for the virtual console while nothing runs is refused there, not sent anywhere else', async () => {
    await start('1;');
    session.stop();
    lines.length = 0;
    window.dispatchEvent(new CustomEvent('console-eval', { detail: { code: '1 + 1', target: 'virtual' } }));
    expect(lines).toEqual([{ text: 'The virtual device is not running', type: 'error', to: 'virtual' }]);
  });

  it('Run again keeps the console and marks where the new run starts', async () => {
    await start('print("one");');
    await start('print("two");');
    const texts = lines.map(l => l.text);
    expect(texts.indexOf('one')).toBeLessThan(texts.findIndex(t => t.startsWith('──── Run again')));
    expect(texts.findIndex(t => t.startsWith('──── Run again'))).toBeLessThan(texts.indexOf('two'));
  });

  it('an edit of config.json or the script while it runs marks it stale; it keeps running on the old text', async () => {
    await start('1;');
    const key = vi.spyOn(workspace, 'snapshot');
    key.mockReturnValue({ ...workspace.snapshot(), currentKey: 'scripts/other.js' });
    window.dispatchEvent(new CustomEvent('editor-doc-change'));
    expect(session.snapshot().stale).toBeNull();
    key.mockReturnValue({ ...workspace.snapshot(), currentKey: 'scripts/main.js' });
    window.dispatchEvent(new CustomEvent('editor-doc-change'));
    expect(session.snapshot().stale).toBe('script');
    key.mockReturnValue({ ...workspace.snapshot(), currentKey: 'config.json' });
    window.dispatchEvent(new CustomEvent('editor-doc-change'));
    expect(session.snapshot()).toMatchObject({ stale: 'config', status: 'running' });
    // Run again takes the new text: not stale any more.
    await start('1;');
    expect(session.snapshot().stale).toBeNull();
  });
});

describe('emulator session: MQTT (in-page broker) and sensors', () => {
  const SCRIPT = `
    mqtt.subscribe(0, "home/temp", function (topic, payload) {
      fb.main.display.setValue("temp", payload);
      mqtt.publish(0, "home/ack", "got " + payload);
    });
  `;

  it('lists the brokers of config.json with the script\'s subscriptions', async () => {
    await start(SCRIPT);
    expect(session.brokers()).toEqual([
      { index: 0, name: 'local', url: 'mqtt://broker.test/', subscriptions: [{ topic: 'home/temp', qos: 0 }] },
    ]);
  });

  it('an injected message reaches the script; the log has it injected, delivered and the answer published, newest first', async () => {
    await start(SCRIPT);
    expect(session.inject(0, 'home/temp', '21.5')).toBe(true);
    await wait(60);
    expect(session.pages()![0]!.displayItems[0]!.value).toBe('21.5');
    const log = session.mqttLog();
    expect(log.map(e => [e.kind, e.topic, e.payload])).toEqual([
      ['published', 'home/ack', 'got 21.5'],
      ['delivered', 'home/temp', '21.5'],
      ['injected', 'home/temp', '21.5'],
    ]);
    // What the publish policy decided: offline, nothing is sent.
    expect(log[0]!.note).toBe('not sent: offline (in-page broker only)');
    expect(log[2]!.note).toBe('a subscription matches');
    expect(log.every(e => e.time > 0)).toBe(true);
  });

  it('a message no subscription wants is logged as not delivered; a retained one is kept for a later subscription', async () => {
    await start(SCRIPT);
    session.inject(0, 'other/topic', 'x');
    expect(session.mqttLog()[0]).toMatchObject({ kind: 'injected', note: 'no subscription matches: not delivered' });
    session.inject(0, 'late/topic', 'kept', true);
    expect(session.mqttLog()[0]).toMatchObject({ retain: true, note: 'no subscription matches: kept as retained' });
    window.dispatchEvent(new CustomEvent('console-eval', {
      detail: { code: 'mqtt.subscribe(0, "late/topic", function (t, p) { print("late " + p); })', target: 'virtual' },
    }));
    await wait(60);
    expect(lines.some(l => l.text === 'late kept')).toBe(true);
    expect(session.mqttLog().some(e => e.kind === 'delivered' && e.retain && e.topic === 'late/topic')).toBe(true);
  });

  it('refuses an inject without a topic, for a broker that is not there, or when nothing runs', async () => {
    await start(SCRIPT);
    expect(session.inject(0, '', 'x')).toBe(false);
    expect(session.inject(4, 'a', 'x')).toBe(false);
    session.stop();
    expect(session.inject(0, 'home/temp', 'x')).toBe(false);
  });

  it('loopback: a publish comes back to the script\'s own subscription', async () => {
    await start(`
      mqtt.subscribe(0, "loop/#", function (t, p) { print("back " + p); });
      fb.key("light").button.onClick(function () { mqtt.publish(0, "loop/a", "1"); });
    `);
    const click = async () => { session.press(3); await wait(80); session.release(3); await wait(80); };
    await click();
    expect(lines.some(l => l.text === 'back 1')).toBe(false);
    expect(session.loopback).toBe(false);
    session.setLoopback(true);
    expect(session.loopback).toBe(true);
    await click();
    expect(lines.some(l => l.text === 'back 1')).toBe(true);
  });

  it('the log is capped and can be cleared', async () => {
    await start(SCRIPT);
    for (let i = 0; i < MQTT_LOG_MAX + 20; i++) session.inject(0, 'nobody/listens', String(i));
    expect(session.mqttLog().length).toBe(MQTT_LOG_MAX);
    expect(session.mqttLog()[0]!.payload).toBe(String(MQTT_LOG_MAX + 19));
    session.clearLog();
    expect(session.mqttLog()).toEqual([]);
  });

  it('export and import: a session file replays its incoming messages and sets its retained ones', async () => {
    await start(SCRIPT);
    session.inject(0, 'home/temp', '19');
    session.inject(0, 'home/mode', 'eco', true);
    await wait(40);
    const file = session.exportSession()!;
    expect(file).toMatchObject({ format: 'freebutton-mqtt-session', version: 1, identity: virtualDeviceId() });
    expect(file.brokers[0]!.name).toBe('local');
    expect(file.brokers[0]!.session.events.map(e => [e.dir, e.topic, e.payload])).toEqual([
      // The script answers at its next loop pass: after the second message came in.
      ['in', 'home/temp', '19'], ['in', 'home/mode', 'eco'], ['out', 'home/ack', 'got 19'],
    ]);
    expect(file.brokers[0]!.session.retained.map(m => m.topic)).toEqual(['home/mode']);
    // Through JSON, as the file on disk.
    const saved = JSON.parse(JSON.stringify(file)) as SessionFile;

    await start(SCRIPT);
    expect(session.pages()![0]!.displayItems[0]!.value).toBe('');
    expect(session.importSession(saved)).toBe('');
    await wait(120);
    expect(session.pages()![0]!.displayItems[0]!.value).toBe('19');
    expect(session.mqttLog().some(e => e.kind === 'injected' && e.topic === 'home/temp')).toBe(true);

    expect(session.importSession({ hello: 1 })).toBe('Not a FreeButton MQTT session file');
    session.stop();
    expect(session.importSession(saved)).toBe('Run the virtual device first');
    expect(session.exportSession()).toBeNull();
  });

  it('sensors: the default three; a new value reaches onChange at the next sample', async () => {
    await start('sensor.onChange(5, function (v) { print("t=" + v); });');
    expect(session.sensors().map(s => [s.id, s.type, s.unit])).toEqual([
      [3, 'pressure', 'hPa'], [4, 'humidity', '%'], [5, 'temperature', '°C'],
    ]);
    expect(session.setSensor(5, 25)).toBe(true);
    expect(session.setSensor(5, NaN)).toBe(false);
    expect(session.setSensor(99, 1)).toBe(false);
    await wait(1300);
    expect(lines.some(l => l.text === 't=25')).toBe(true);
    expect(session.sensors().find(s => s.id === 5)!.value).toBe(25);
  });
});
