import { describe, it, expect, afterEach, beforeEach, vi } from 'vitest';
import '../fb-virtual-device.js';
import '../fb-tft-display.js';
import '../fb-button-grid.js';
import '../fb-console-pane.js';
import '../fb-toolbar.js';
import { RUN_SHORTCUT, STOP_SHORTCUT } from '../fb-toolbar.js';
import { emulatorModule, BASIC_CONFIG } from '../../js/__tests__/emulator-helpers';
import { emulatorSession, virtualDeviceId } from '../../js/emulator-session.js';
import { configToModel } from '../../js/config-model.js';
import { clearPreviewConfig, loadDefaultConfig, setPreviewConfig } from '../../js/device-config.js';
import { currentPopover } from '../../js/popover.js';
import { workspace } from '../../js/workspace.js';
import { TestBroker, nodeSocket, until } from '../../js/__tests__/mqtt-test-broker';
import '../fb-dialog.js';

const CONFIG = JSON.stringify(BASIC_CONFIG);

const SCRIPT = `
  var n = 0;
  fb.key("light").button.onClick(function () {
    n++;
    fb.key("light").display.setLabel("On " + n);
    fb.bars[0].left.led.setColor(0, 255, 0);
    fb.main.display.setLabel("temp", "Clicked");
  });
  fb.key("light").button.onLongPress(function () { fb.main.display.showPage(2); });
  fb.main.left.button.onClick(function () { fb.main.display.setValue("temp", "left"); });
  fb.main.right.button.onClick(function () { fb.main.display.setValue("temp", "right"); });
  mqtt.subscribe(0, "home/temp", function (topic, payload) {
    fb.main.display.setValue("temp", payload);
    mqtt.publish(0, "home/ack", "got " + payload);
  });
  print("started " + system.id());
`;

const wait = (ms: number) => new Promise(r => setTimeout(r, ms));
/** A frame of the page (the emulator's redraw is debounced to one) and Lit's update. */
const settle = () => wait(40);

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

async function mount(tag: string): Promise<HTMLElement> {
  const el = document.createElement(tag);
  document.body.appendChild(el);
  await settle();
  return el;
}

function use(script: string, config = CONFIG) {
  emulatorSession.readSource = async () => ({ script, config });
}

/** Run (as the toolbar's button does) and wait for the script's top level. */
async function run(_el?: HTMLElement) {
  void emulatorSession.run();
  for (let i = 0; i < 50 && emulatorSession.snapshot().status === 'starting'; i++) await wait(10);
  await settle();
}

function pointer(type: string, target: EventTarget, x = 0, y = 0) {
  target.dispatchEvent(new MouseEvent(type, { bubbles: true, cancelable: true, clientX: x, clientY: y, button: 0 }));
}

const key = (el: HTMLElement, position: number) => el.querySelector(`fb-button-grid [data-position="${position}"]`) as HTMLElement;
const item = (el: HTMLElement, name: string) => el.querySelector(`[data-display-item="${name}"]`) as HTMLElement;
const notice = (el: HTMLElement) => el.querySelector('[data-testid="device-notice"]')?.textContent?.trim() ?? '';

beforeEach(() => {
  vi.stubGlobal('localStorage', memoryStorage());
  emulatorSession.wasm = emulatorModule();
  setPreviewConfig(configToModel(BASIC_CONFIG));
  use(SCRIPT);
});

afterEach(() => {
  emulatorSession.destroy();
  currentPopover()?.close();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  clearPreviewConfig();
  loadDefaultConfig();
  document.body.innerHTML = '';
});

describe('toolbar: Run and Stop for the virtual device', () => {
  const withMain = () => vi.spyOn(workspace, 'snapshot').mockReturnValue({ ...workspace.snapshot(), currentKey: 'scripts/main.js' });

  it('idle: Run, in its own group after Save / Deploy / more; the shortcut is in its tooltip', async () => {
    withMain();
    const bar = await mount('fb-toolbar');
    const group = bar.querySelector('[data-testid="toolbar-virtual"]') as HTMLElement;
    expect(group.getAttribute('role')).toBe('group');
    expect(group.getAttribute('aria-label')).toBe('Virtual device');
    const runButton = group.querySelector('[data-testid="toolbar-run"]') as HTMLButtonElement;
    expect(runButton.textContent?.trim()).toBe('Run');
    expect(runButton.disabled).toBe(false);
    expect(runButton.title).toContain(RUN_SHORTCUT);
    expect(runButton.title).toContain('Nothing is sent to the device');
    expect(group.querySelector('[data-testid="toolbar-stop"]')).toBeNull();
    // Save, Deploy and the menu are not in the virtual device's group, and come before it.
    for (const id of ['toolbar-save', 'toolbar-deploy', 'toolbar-more']) {
      const el = bar.querySelector(`[data-testid="${id}"]`) as HTMLElement;
      expect(group.contains(el)).toBe(false);
      expect(el.compareDocumentPosition(group) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    }
  });

  it('no scripts/main.js (neither open nor stored): Run is off and says why', async () => {
    vi.spyOn(workspace, 'snapshot').mockReturnValue({ ...workspace.snapshot(), currentKey: 'config.json', files: [{ key: 'config.json', size: 1 }] });
    const bar = await mount('fb-toolbar');
    const runButton = bar.querySelector('[data-testid="toolbar-run"]') as HTMLButtonElement;
    expect(runButton.disabled).toBe(true);
    expect(runButton.title).toBe('There is no /scripts/main.js to run in the virtual device');
    // Stored, another file open: it can run.
    window.dispatchEvent(new CustomEvent('workspace-change', {
      detail: { ...workspace.snapshot(), currentKey: 'config.json', files: [{ key: 'scripts/main.js', size: 1 }] },
    }));
    await settle();
    expect((bar.querySelector('[data-testid="toolbar-run"]') as HTMLButtonElement).disabled).toBe(false);
  });

  it('Run starts it: Run again and Stop; Stop goes back to Run', async () => {
    withMain();
    const bar = await mount('fb-toolbar');
    (bar.querySelector('[data-testid="toolbar-run"]') as HTMLElement).click();
    for (let i = 0; i < 50 && emulatorSession.snapshot().status !== 'running'; i++) await wait(10);
    await settle();
    expect(bar.querySelector('[data-testid="toolbar-run"]')).toBeNull();
    const again = bar.querySelector('[data-testid="toolbar-run-again"]') as HTMLButtonElement;
    const stop = bar.querySelector('[data-testid="toolbar-stop"]') as HTMLButtonElement;
    expect(again.getAttribute('aria-label')).toBe('Run again in the virtual device');
    expect(again.title).toContain(RUN_SHORTCUT);
    expect(stop.getAttribute('aria-label')).toBe('Stop the virtual device');
    expect(stop.title).toContain(STOP_SHORTCUT);
    // Save and Deploy are still there: they are for the device.
    expect(bar.querySelector('[data-testid="toolbar-deploy"]')).not.toBeNull();

    again.click();
    for (let i = 0; i < 50 && emulatorSession.snapshot().status !== 'running'; i++) await wait(10);
    expect(emulatorSession.snapshot().status).toBe('running');

    stop.click();
    await settle();
    expect(emulatorSession.snapshot().status).toBe('idle');
    expect(bar.querySelector('[data-testid="toolbar-run"]')).not.toBeNull();
  });

  it('stopped with an error: Run again is offered, with a red dot and the error in the tooltip', async () => {
    withMain();
    use('var x = ;');
    const bar = await mount('fb-toolbar');
    await run();
    const runButton = bar.querySelector('[data-testid="toolbar-run"]') as HTMLButtonElement;
    expect(runButton.querySelector('[data-testid="toolbar-run-error"]')).not.toBeNull();
    expect(runButton.title).toContain('It stopped: ');
    expect(runButton.disabled).toBe(false);
  });

  it('the shortcuts: Ctrl/Cmd+Shift+Enter runs (and does not deploy), Ctrl/Cmd+Shift+. stops', async () => {
    await import('../fb-app.js');
    await mount('fb-app');
    const deploys: Event[] = [];
    const onDeploy = (e: Event) => deploys.push(e);
    window.addEventListener('deploy-request', onDeploy);
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', ctrlKey: true, shiftKey: true, bubbles: true, cancelable: true }));
    for (let i = 0; i < 50 && emulatorSession.snapshot().status !== 'running'; i++) await wait(10);
    window.removeEventListener('deploy-request', onDeploy);
    expect(emulatorSession.snapshot().status).toBe('running');
    expect(deploys).toEqual([]);
    window.dispatchEvent(new KeyboardEvent('keydown', { key: '>', code: 'Period', metaKey: true, shiftKey: true, bubbles: true, cancelable: true }));
    expect(emulatorSession.snapshot().status).toBe('idle');
  });
});

describe('virtual device: header, glow and info side', () => {
  const face = (el: HTMLElement) => el.querySelector('.device-flip-container') as HTMLElement;
  const field = (el: HTMLElement, name: string) => el.querySelector(`fb-virtual-info [data-field="${name}"]`)?.textContent?.replace(/\s+/g, ' ').trim();
  const bars0 = (el: HTMLElement) => Array.from(el.querySelectorAll('fb-virtual-info [role="progressbar"]')) as HTMLElement[];
  const flip = async (el: HTMLElement) => { (el.querySelector('[data-testid="gear-icon"]') as HTMLElement).click(); await settle(); };

  it('idle: the stepper and the info button only: no Run or Stop, no badge, no status line, no glow', async () => {
    const el = await mount('fb-virtual-device');
    expect(el.querySelector('[data-testid="page-stepper"]')).not.toBeNull();
    expect(el.querySelector('[data-testid="gear-icon"]')).not.toBeNull();
    expect(el.querySelector('[data-testid^="emu-"]')).toBeNull();
    expect(el.querySelector('[data-testid="device-notice"]')).toBeNull();
    expect(face(el).classList.contains('fb-device-running')).toBe(false);
    const presses: Event[] = [];
    el.addEventListener('key-press', e => presses.push(e));
    pointer('pointerdown', key(el, 3));
    expect(presses).toEqual([]);
  });

  it('running: the face glows; still no badge, heap readout or error line in the header', async () => {
    use('fb.key("light").button.onClick(function () { throw new Error("boom"); });');
    const el = await mount('fb-virtual-device');
    await run(el);
    expect(face(el).classList.contains('fb-device-running')).toBe(true);
    expect(face(el).getAttribute('data-running')).toBe('true');
    expect(el.querySelector('[data-testid^="emu-"]')).toBeNull();
    // An error of the script is for the Virtual console and the editor, not for this header.
    pointer('pointerdown', key(el, 3));
    await wait(60);
    pointer('pointerup', key(el, 3));
    await wait(120);
    expect(emulatorSession.snapshot().errors).toBe(1);
    expect(el.querySelector('[data-testid="device-notice"]')).toBeNull();
    expect(el.textContent).not.toContain('boom');
    expect(el.textContent).not.toContain('heap');

    emulatorSession.stop();
    await settle();
    expect(face(el).classList.contains('fb-device-running')).toBe(false);
  });

  it('stopped with an error: no glow, nothing in the header; items can be changed again', async () => {
    use('var x = ;');
    const el = await mount('fb-virtual-device');
    await run(el);
    expect(emulatorSession.snapshot().status).toBe('error');
    expect(face(el).classList.contains('fb-device-running')).toBe(false);
    expect(el.querySelector('[data-testid="device-notice"]')).toBeNull();
    expect((el.querySelector('fb-tft-display') as HTMLElement & { editable: boolean }).editable).toBe(true);
  });

  it('the info side, not run yet: the virtual device\'s id, "not running" and what Run will use; nothing of the real device', async () => {
    vi.spyOn(workspace, 'snapshot').mockReturnValue({
      ...workspace.snapshot(), currentKey: 'scripts/main.js', dirty: true, files: [{ key: 'config.json', size: 10 }],
    });
    const el = await mount('fb-virtual-device');
    await flip(el);
    expect(el.querySelector('fb-device-info')).toBeNull();
    expect(field(el, 'identity')).toBe(virtualDeviceId());
    expect(field(el, 'state')).toBe('not running');
    expect(field(el, 'source-script')).toBe('the editor (unsaved changes)');
    expect(field(el, 'source-config')).toBe('the stored file');
    expect(field(el, 'mqtt')).toBe('offline: in-page broker');
    for (const real of ['IPv4', 'MAC', 'SSID', 'Firmware', 'Uptime']) expect(el.querySelector(`[data-field="${real}"]`)).toBeNull();
    expect(el.querySelector('[data-testid="memory-graph"]')).toBeNull();
  });

  it('the info side while running: live values of the emulator', async () => {
    const el = await mount('fb-virtual-device');
    await run(el);
    // Run comes to the front (the outline is there); flip to the info.
    await flip(el);
    expect(field(el, 'identity')).toBe(virtualDeviceId());
    expect(field(el, 'state')).toBe('running');
    expect(field(el, 'uptime')).toMatch(/^\d s$/);
    expect(field(el, 'heap')).toMatch(/^\d+(\.\d)? \/ 16 KB$/);
    expect(field(el, 'bytecode')).toMatch(/^\d+(\.\d)? \/ \d+(\.\d)? KB$/);
    expect(field(el, 'timers')).toBe('0 / 16');
    expect(field(el, 'subscriptions')).toBe('1 / 8');
    expect(field(el, 'brokers')).toBe('local');
    expect(field(el, 'sensors')).toBe('8');     // a BME280 (three readings) and the firmware's five memory sensors
    expect(field(el, 'timezone')).toMatch(/^[A-Z]{3}/);
    expect(field(el, 'modules')).toBe('0: display · 1: bar');
    expect(field(el, 'model')).toBe('Model 1 Version 9');
    expect(field(el, 'firmware')).toMatch(/^FreeButton \d/);
    // A value too long for its line is cut short there; the whole of it is the tooltip.
    const modulesRow = el.querySelector('fb-virtual-info [data-field="modules"]')!.parentElement as HTMLElement;
    expect(modulesRow.classList.contains('truncate')).toBe(true);
    expect(modulesRow.title).toBe('Module per connector: 0: main display, 1: bar (two keys)');
    // The gauges are two per row: the card stays short.
    expect((bars0(el)[0].parentElement!.parentElement as HTMLElement).classList.contains('grid-cols-2')).toBe(true);
    const bars = Array.from(el.querySelectorAll('fb-virtual-info [role="progressbar"]'));
    expect(bars.map(b => b.getAttribute('aria-label'))).toEqual(['Heap free', 'Bytecode', 'Timers', 'Subscriptions']);

    // It follows the script: a timer set from the console shows at the next measurement.
    window.dispatchEvent(new CustomEvent('console-eval', { detail: { code: 'setTimeout(function () {}, 60000); 1', target: 'virtual' } }));
    await wait(1100);
    expect(field(el, 'timers')).toBe('1 / 16');

    // Stopped: the state follows at once.
    emulatorSession.stop();
    await settle();
    expect(field(el, 'state')).toBe('not running');
    expect(field(el, 'heap')).toBeUndefined();
  });

  it('formatRunTime: seconds show that it runs', async () => {
    const { formatRunTime } = await import('../fb-virtual-info.js');
    expect([0, 42, 185, 3600 * 2 + 7 * 60 + 9].map(formatRunTime)).toEqual(['0 s', '42 s', '3 min 05 s', '2 h 07 min']);
  });

  it('the info side measures only while it is shown, and the real device is not polled for it', async () => {
    const deviceInfo = await import('../../js/device-info.js');
    const visible = vi.spyOn(deviceInfo, 'setDeviceInfoVisible');
    const info = vi.spyOn(emulatorSession, 'info');
    const fetched = vi.fn(async (_url: unknown) => new Response('{}'));
    vi.stubGlobal('fetch', fetched);
    const el = await mount('fb-virtual-device');
    await run(el);
    info.mockClear();
    await wait(1200);
    expect(info).not.toHaveBeenCalled();           // the front is shown
    await flip(el);
    await wait(1200);
    expect(info.mock.calls.length).toBeGreaterThanOrEqual(2);
    expect(info.mock.calls.length).toBeLessThanOrEqual(4);   // once a second, not more
    await flip(el);
    info.mockClear();
    await wait(1200);
    expect(info).not.toHaveBeenCalled();
    expect(visible).not.toHaveBeenCalled();
    expect(fetched.mock.calls.filter(c => String(c[0]).includes('/api/device'))).toEqual([]);
  });

  it('a script that does not compile: the info side says stopped with an error, and the error', async () => {
    use('var x = ;');
    const el = await mount('fb-virtual-device');
    await run(el);
    await flip(el);
    expect(field(el, 'state')).toBe('stopped with an error');
    expect(field(el, 'error')).toContain('SyntaxError');
  });

  it('removing the virtual device stops the emulator', async () => {
    const el = await mount('fb-virtual-device');
    await run(el);
    expect(emulatorSession.running).toBe(true);
    el.remove();
    expect(emulatorSession.running).toBe(false);
    expect(emulatorSession.snapshot().status).toBe('idle');
  });
});

describe('virtual device: keys and drawing while a script runs', () => {
  it('a press and release of a key with the pointer is a click: its label, its LED and the display follow', async () => {
    const el = await mount('fb-virtual-device');
    await run(el);
    expect(key(el, 3).textContent).toContain('Light');
    pointer('pointerdown', key(el, 3));
    await settle();
    expect(key(el, 3).hasAttribute('data-pressed')).toBe(true);
    await wait(60);
    pointer('pointerup', key(el, 3));
    await wait(120);
    expect(key(el, 3).hasAttribute('data-pressed')).toBe(false);
    expect(key(el, 3).textContent).toContain('On 1');
    const led = key(el, 3).querySelector('[data-led]') as HTMLElement;
    expect(led.hasAttribute('data-led-on')).toBe(true);
    expect(led.style.borderColor.replace(/\s/g, '')).toBe('rgb(0,255,0)');
    expect(item(el, 'temp').textContent).toContain('Clicked');
    // The other key did not change.
    expect(key(el, 4).querySelector('[data-led]')!.hasAttribute('data-led-on')).toBe(false);
  });

  it('the pointer leaving a held key releases it', async () => {
    const el = await mount('fb-virtual-device');
    await run(el);
    pointer('pointerdown', key(el, 3));
    await wait(60);
    pointer('pointerleave', key(el, 3));
    await wait(120);
    expect(emulatorSession.pages()![0]!.buttons.find(b => b.position === 3)!.label).toBe('On 1');
  });

  it('holding a key is a long press: the script shows page 2 and the stepper follows', async () => {
    const el = await mount('fb-virtual-device');
    await run(el);
    expect(el.querySelector('[data-testid="page-label"]')?.textContent?.trim()).toBe('Page 1 of 2');
    pointer('pointerdown', key(el, 3));
    await wait(1000);
    pointer('pointerup', key(el, 3));
    await wait(80);
    expect(el.querySelector('[data-testid="page-label"]')?.textContent?.trim()).toBe('Page 2 of 2');
    expect(item(el, 'power')).not.toBeNull();
  });

  it('Space on a focused key presses it while it is held', async () => {
    const el = await mount('fb-virtual-device');
    await run(el);
    const button = key(el, 3).querySelector('button') as HTMLElement;
    expect(button.getAttribute('aria-pressed')).toBe('false');
    const down = new KeyboardEvent('keydown', { key: ' ', bubbles: true, cancelable: true });
    button.dispatchEvent(down);
    expect(down.defaultPrevented).toBe(true);
    await settle();
    expect(button.getAttribute('aria-pressed')).toBe('true');
    // Key repeat of the keyboard is not a second press.
    button.dispatchEvent(new KeyboardEvent('keydown', { key: ' ', repeat: true, bubbles: true, cancelable: true }));
    await wait(40);
    button.dispatchEvent(new KeyboardEvent('keyup', { key: ' ', bubbles: true, cancelable: true }));
    await wait(120);
    expect(key(el, 3).textContent).toContain('On 1');
  });

  it('a press of a key does not jump to config.json; without a script it still does', async () => {
    const el = await mount('fb-virtual-device');
    const reveals: Event[] = [];
    const onReveal = (e: Event) => reveals.push(e);
    window.addEventListener('config-reveal', onReveal);
    (key(el, 3).querySelector('button') as HTMLElement).click();
    expect(reveals.length).toBe(1);
    await run(el);
    (key(el, 3).querySelector('button') as HTMLElement).click();
    window.removeEventListener('config-reveal', onReveal);
    expect(reveals.length).toBe(1);
  });

  it('the left and right half of the main display are its two keys', async () => {
    const el = await mount('fb-virtual-device');
    await run(el);
    const screen = el.querySelector('[data-screen]') as HTMLElement;
    screen.getBoundingClientRect = () => ({ left: 0, top: 0, width: 283, height: 212, right: 283, bottom: 212, x: 0, y: 0, toJSON: () => '' });
    pointer('pointerdown', screen, 250, 100);
    await settle();
    expect(el.querySelector('[data-main-pressed]')?.getAttribute('data-main-pressed')).toBe('1');
    await wait(40);
    pointer('pointerup', window, 250, 100);
    await wait(120);
    expect(el.querySelector('[data-main-pressed]')).toBeNull();
    expect(item(el, 'temp').textContent).toContain('right');
    pointer('pointerdown', item(el, 'temp'), 20, 100);
    await wait(60);
    pointer('pointerup', window, 20, 100);
    await wait(120);
    expect(item(el, 'temp').textContent).toContain('left');
  });

  it('what is drawn while it runs is what the firmware resolved: runs with its fonts and colours, key displays, the roller', async () => {
    use(`
      fb.main.display.setValue("temp", "\\\\cff0000hot \\\\c00ff0021,5");
      fb.key("light").display.setLabel("\\\\cffd700On");
      fb.main.list.select("scenes", 2);
    `);
    const el = await mount('fb-virtual-device');
    // Design time: the IDE's own preview.
    expect(item(el, 'temp').hasAttribute('data-firmware-box')).toBe(false);
    await run(el);
    const temp = item(el, 'temp');
    expect(temp.hasAttribute('data-firmware-box')).toBe(true);
    const runs = Array.from(temp.querySelectorAll('[data-value] [data-run]')) as HTMLElement[];
    expect(runs.slice(0, 2).map(r => [r.textContent, r.style.color.replace(/\s/g, '')])).toEqual([
      ['hot ', '#ff0000'], ['21,5', '#00ff00'],
    ]);
    // Caption and unit are the firmware's runs too, each with a font size it chose.
    expect(temp.querySelector('[data-caption] [data-run]')?.textContent).toBe('Temperature');
    expect(runs.every(r => /px$/.test(r.style.fontSize))).toBe(true);
    // The key's display: its label as a run in the colour of the markup.
    const label = key(el, 3).querySelector('[data-label][data-firmware-text] [data-run]') as HTMLElement;
    expect([label.textContent, label.style.color.replace(/\s/g, '')]).toEqual(['On', '#ffd700']);
    expect(key(el, 3).querySelector('[data-top-label][data-firmware-text]')?.textContent).toBe('Room');
    // The list: the firmware's roller (pixels, selection).
    const list = el.querySelector('[data-list="scenes"]') as HTMLElement;
    expect(list.hasAttribute('data-firmware-roller')).toBe(true);
    expect(list.querySelector('[data-list-item][data-selected]')?.textContent?.trim()).toBe('Night');
    expect(list.style.left).toMatch(/px$/);

    emulatorSession.stop();
    await settle();
    expect(item(el, 'temp').hasAttribute('data-firmware-box')).toBe(false);
    expect(el.querySelector('[data-firmware-roller]')).toBeNull();
  });

  it('a text longer than the firmware keeps is drawn as the firmware cut it', async () => {
    use(`fb.main.display.setValue("temp", "${'x'.repeat(80)}");`);
    const el = await mount('fb-virtual-device');
    await run(el);
    const value = item(el, 'temp').querySelector('[data-value] [data-run]')!.textContent!;
    expect(value.length).toBeLessThan(80);
    expect(value.length).toBeGreaterThan(40);
  });

  it('Stop: the device shows config.json again', async () => {
    const el = await mount('fb-virtual-device');
    await run(el);
    pointer('pointerdown', key(el, 3));
    await wait(60);
    pointer('pointerup', key(el, 3));
    await wait(120);
    emulatorSession.stop();
    await settle();
    expect(key(el, 3).textContent).toContain('Light');
    expect(item(el, 'temp').textContent).toContain('Temperature');
    expect(key(el, 3).querySelector('[data-led]')!.hasAttribute('data-led-on')).toBe(false);
  });
});

describe('virtual device: editing while a script runs', () => {
  it('no drag, nudge, properties or add item; a hint says why', async () => {
    const el = await mount('fb-virtual-device');
    await run(el);
    const tft = el.querySelector('fb-tft-display') as HTMLElement & { editable: boolean; running: boolean };
    expect(tft.editable).toBe(false);
    expect(tft.running).toBe(true);
    const moves: Event[] = [];
    el.addEventListener('item-move', e => moves.push(e));

    const target = item(el, 'temp');
    pointer('pointerdown', target, 100, 100);
    pointer('pointermove', window, 160, 140);
    pointer('pointerup', window, 160, 140);
    await settle();
    expect(moves).toEqual([]);
    expect(el.querySelector('[data-testid="move-position"]')).toBeNull();

    target.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true, cancelable: true }));
    await settle();
    expect(notice(el)).toContain('The script is running: Stop (in the toolbar)');
    expect(moves).toEqual([]);

    const menu = new MouseEvent('contextmenu', { bubbles: true, cancelable: true });
    target.dispatchEvent(menu);
    (el.querySelector('[data-screen]') as HTMLElement).dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true }));
    await settle();
    expect(menu.defaultPrevented).toBe(true);
    expect(document.querySelector('[data-testid="item-popover"]')).toBeNull();
    expect(document.querySelector('[data-testid="display-menu"]')).toBeNull();
  });

  it('an item a script made has no config entry: it cannot be selected; config items keep their ref', async () => {
    use('fb.main.display.create(5, 60, 30, 0, 2, 1, "Made", "kW", 0, 1);');
    const el = await mount('fb-virtual-device');
    await run(el);
    const made = Array.from(el.querySelectorAll('[data-display-item]')).find(n => n.textContent?.includes('Made')) as HTMLElement;
    expect(made).toBeDefined();
    expect(made.hasAttribute('data-ref')).toBe(false);
    expect(made.hasAttribute('tabindex')).toBe(false);
    expect(item(el, 'temp').getAttribute('data-ref')).toBe('displayitems:0');
    // The editor cursor still outlines the config entry.
    window.dispatchEvent(new CustomEvent('config-selection', { detail: { ref: { kind: 'displayitem', list: 'displayitems', index: 0 } } }));
    await settle();
    expect(item(el, 'temp').hasAttribute('data-selected')).toBe(true);
  });

  it('config.json edited while it runs: it keeps running on the old config; a note says "Run again" for a moment and the toolbar keeps a mark', async () => {
    const el = await mount('fb-virtual-device');
    const bar = await mount('fb-toolbar');
    await run(el);
    vi.spyOn(workspace, 'snapshot').mockReturnValue({ ...workspace.snapshot(), currentKey: 'config.json' });
    // The preview model moves on with the edit; what is drawn is the running device.
    setPreviewConfig(configToModel({ ...BASIC_CONFIG, displayitems: [{ name: 'temp', x: 0, y: 0, label: 'Edited' }] }));
    window.dispatchEvent(new CustomEvent('editor-doc-change'));
    await settle();
    expect(notice(el)).toBe('config.json changed: Run again to use it');
    expect(emulatorSession.snapshot().status).toBe('running');
    expect(item(el, 'temp').textContent).toContain('Temperature');
    const again = bar.querySelector('[data-testid="toolbar-run-again"]') as HTMLElement;
    expect(again.querySelector('[data-testid="toolbar-run-stale"]')).not.toBeNull();
    expect(again.title).toContain('config.json changed since Run');

    use(SCRIPT, JSON.stringify({ ...BASIC_CONFIG, displayitems: [{ name: 'temp', x: 0, y: 0, label: 'Edited' }] }));
    await run(el);
    expect(bar.querySelector('[data-testid="toolbar-run-stale"]')).toBeNull();
    expect(item(el, 'temp').textContent).toContain('Edited');
  });
});

describe('the consoles: Device and Virtual are separate', () => {
  const output = (pane: HTMLElement, id: string) => pane.querySelector(`[data-console="${id}"] fb-console-output`) as HTMLElement;
  const lineTexts = (pane: HTMLElement, id: string) => Array.from(output(pane, id)?.querySelectorAll('[data-line]') ?? [])
    .map(n => n.textContent?.replace(/\s+/g, ' ').trim());
  const tab = (pane: HTMLElement, id: string) => pane.querySelector(`[data-testid="tab-${id}"]`) as HTMLElement;
  const unread = (pane: HTMLElement, id: string) => tab(pane, id).querySelector('[data-unread]')?.getAttribute('data-unread') ?? null;
  const textarea = (pane: HTMLElement, id: string) => pane.querySelector(`[data-console="${id}"] fb-console-input textarea`) as HTMLTextAreaElement;
  const deviceLine = (text: string, type = 'log') =>
    window.dispatchEvent(new CustomEvent('console-line', { detail: { text, type, to: 'device' } }));

  function type(pane: HTMLElement, id: string, code: string) {
    const input = textarea(pane, id);
    input.value = code;
    input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true }));
  }

  it('before the first Run: one console, as it was (no tabs); a typed line goes to the device', async () => {
    const pane = await mount('fb-console-pane');
    expect(pane.querySelector('[role="tablist"]')).toBeNull();
    expect(pane.querySelector('[data-console="virtual"]')).toBeNull();
    expect(lineTexts(pane, 'device')[0]).toBe('Console ready. Type JavaScript to evaluate on device.');
    const evals: CustomEvent[] = [];
    const onEval = (e: Event) => evals.push(e as CustomEvent);
    window.addEventListener('console-eval', onEval);
    type(pane, 'device', '1 + 1');
    window.removeEventListener('console-eval', onEval);
    await settle();
    expect(evals.map(e => e.detail)).toEqual([{ code: '1 + 1', target: 'device' }]);
    expect(lineTexts(pane, 'device')).toContain('> 1 + 1');
  });

  it('Run: three tabs, the Virtual one shown; the script\'s output is only in the virtual console', async () => {
    const pane = await mount('fb-console-pane');
    await run();
    expect(Array.from(pane.querySelectorAll('[role="tab"]')).map(t => t.textContent?.trim())).toEqual(['Device', 'Virtual', 'MQTT']);
    expect(tab(pane, 'virtual').getAttribute('aria-selected')).toBe('true');
    expect(pane.querySelector('[data-console="virtual"]')!.classList.contains('invisible')).toBe(false);
    expect(pane.querySelector('[data-console="device"]')!.classList.contains('invisible')).toBe(true);
    expect(pane.querySelector('[data-console="device"]')!.hasAttribute('inert')).toBe(true);

    expect(lineTexts(pane, 'virtual')).toContain(`started ${virtualDeviceId()}`);
    // Nothing of the emulator in the device's console.
    expect(lineTexts(pane, 'device').some(l => l?.includes('started') || l?.includes('Virtual device'))).toBe(false);
    // No markers: the console itself says whose lines these are.
    expect(output(pane, 'virtual').textContent).not.toContain('virt ');
  });

  it('a line typed in a console is evaluated there and echoed there only', async () => {
    const pane = await mount('fb-console-pane');
    await run();
    type(pane, 'virtual', 'n + 40');
    await settle();
    expect(lineTexts(pane, 'virtual').slice(-2)).toEqual(['> n + 40', '40']);
    expect(lineTexts(pane, 'device').some(l => l?.includes('n + 40'))).toBe(false);

    // The device's console: its line is not evaluated in the page, and not shown in the virtual console.
    tab(pane, 'device').click();
    await settle();
    type(pane, 'device', 'n + 41');
    await settle();
    expect(lineTexts(pane, 'device')).toContain('> n + 41');
    expect(lineTexts(pane, 'virtual').some(l => l === '41' || l?.includes('n + 41'))).toBe(false);
  });

  it('messages of the IDE and the real device go to the Device console only', async () => {
    const pane = await mount('fb-console-pane');
    await run();
    deviceLine('Saved /config.json (10 bytes)', 'info');
    deviceLine('hello from the device');
    await settle();
    expect(lineTexts(pane, 'device').slice(-2)).toEqual(['Saved /config.json (10 bytes)', 'hello from the device']);
    expect(lineTexts(pane, 'virtual').some(l => l?.includes('Saved') || l?.includes('from the device'))).toBe(false);
  });

  it('a dot on a tab that got lines while another was shown; red for an error; gone when the tab is shown', async () => {
    const pane = await mount('fb-console-pane');
    await run();
    expect(unread(pane, 'virtual')).toBeNull();          // it is the tab shown
    deviceLine('a device line');
    await settle();
    expect(unread(pane, 'device')).toBe('new');
    deviceLine('a device error', 'error');
    deviceLine('and a line after it');
    await settle();
    expect(unread(pane, 'device')).toBe('error');        // the error stays flagged
    tab(pane, 'device').click();
    await settle();
    expect(unread(pane, 'device')).toBeNull();

    // An error of the script while the Device tab is shown.
    window.dispatchEvent(new CustomEvent('console-eval', { detail: { code: 'nope(', target: 'virtual' } }));
    await settle();
    expect(unread(pane, 'virtual')).toBe('error');
    expect(tab(pane, 'virtual').querySelector('[data-unread]')!.classList.contains('bg-status-red')).toBe(true);
    // MQTT: something was logged while it was not shown.
    emulatorSession.inject(0, 'home/temp', '20');
    await wait(60);
    expect(unread(pane, 'mqtt')).toBe('new');
  });

  it('Clear clears the console that is shown; the other keeps its lines', async () => {
    const pane = await mount('fb-console-pane');
    await run();
    deviceLine('keep me');
    await settle();
    (pane.querySelector('[data-testid="console-clear"]') as HTMLElement).click();
    await settle();
    expect(lineTexts(pane, 'virtual')).toEqual([]);
    expect(lineTexts(pane, 'device')).toContain('keep me');

    tab(pane, 'device').click();
    await settle();
    type(pane, 'virtual', 'print("still here")');
    await settle();
    // Ctrl/Cmd+L (no console named): the one that is shown.
    window.dispatchEvent(new CustomEvent('console-clear', { detail: {} }));
    await settle();
    expect(lineTexts(pane, 'device')).toEqual([]);
    expect(lineTexts(pane, 'virtual')).toContain('still here');
  });

  it('each console has its own input and input history', async () => {
    const pane = await mount('fb-console-pane');
    await run();
    type(pane, 'virtual', 'n');
    tab(pane, 'device').click();
    await settle();
    type(pane, 'device', 'system.id()');
    const up = (id: string) => {
      const input = textarea(pane, id);
      input.setSelectionRange(0, 0);
      input.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowUp', bubbles: true, cancelable: true }));
      return input.value;
    };
    expect(up('device')).toBe('system.id()');
    expect(up('virtual')).toBe('n');
    expect(textarea(pane, 'device')).not.toBe(textarea(pane, 'virtual'));
    // Completions work in both (each input has its own list).
    for (const id of ['device', 'virtual']) {
      const input = textarea(pane, id);
      input.value = 'mqtt.pub';
      input.setSelectionRange(8, 8);
      input.dispatchEvent(new Event('input', { bubbles: true }));
      const list = input.parentElement!.querySelector('.console-autocomplete') as HTMLElement;
      expect(list.style.display).toBe('block');
      expect(list.textContent).toContain('publish');
      input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    }
  });

  it('Stop: the Virtual and MQTT tabs stay, the output can be read, the input is off with a note', async () => {
    const pane = await mount('fb-console-pane');
    await run();
    emulatorSession.stop();
    await settle();
    expect(Array.from(pane.querySelectorAll('[role="tab"]')).length).toBe(3);
    expect(lineTexts(pane, 'virtual')).toContain(`started ${virtualDeviceId()}`);
    expect(lineTexts(pane, 'virtual').slice(-1)).toEqual(['Virtual device stopped']);
    const input = textarea(pane, 'virtual');
    expect(input.disabled).toBe(true);
    expect(input.placeholder).toBe('The script is stopped: Run to type here');
    expect(textarea(pane, 'device').disabled).toBe(false);

    // The next Run keeps what was there, after a separator, and the input works again.
    await run();
    const texts = lineTexts(pane, 'virtual');
    expect(texts.filter(t => t === `started ${virtualDeviceId()}`).length).toBe(2);
    expect(texts.some(t => t?.startsWith('──── Run again'))).toBe(true);
    expect(textarea(pane, 'virtual').disabled).toBe(false);
  });

  it('Run shows the Virtual tab, a Deploy the Device tab', async () => {
    const pane = await mount('fb-console-pane');
    await run();
    expect(tab(pane, 'virtual').getAttribute('aria-selected')).toBe('true');
    window.dispatchEvent(new CustomEvent('deploy-request'));
    await settle();
    expect(tab(pane, 'device').getAttribute('aria-selected')).toBe('true');
    await run();
    expect(tab(pane, 'virtual').getAttribute('aria-selected')).toBe('true');
  });

  it('errors of the script are error lines of the virtual console, with the line', async () => {
    use('print("a");\nnotDefined();\n');
    const pane = await mount('fb-console-pane');
    await run();
    const error = output(pane, 'virtual').querySelector('[data-line="error"]') as HTMLElement;
    expect(error.textContent).toContain('line 2');
    expect(output(pane, 'device').querySelector('[data-line="error"]')).toBeNull();
  });

  it('the MQTT tab: subscriptions, inject, log, sensors; after Stop the log stays readable', async () => {
    const el = await mount('fb-virtual-device');
    const pane = await mount('fb-console-pane');
    await run();
    tab(pane, 'mqtt').click();
    await wait(80);
    expect(tab(pane, 'mqtt').getAttribute('aria-selected')).toBe('true');
    const panel = pane.querySelector('fb-emulator-panel') as HTMLElement;
    expect(panel.getAttribute('role')).toBe('tabpanel');
    expect(panel.querySelector('[data-testid="mqtt-mode-words"]')?.textContent).toContain('In-page broker: nothing leaves this page');
    expect(Array.from(panel.querySelectorAll('[data-subscription]')).map(n => n.textContent)).toEqual(['home/temp']);
    // So far the log has only the firmware's birth message.
    expect(Array.from(panel.querySelectorAll('[data-testid="mqtt-log"] li')).map(li => [li.getAttribute('data-origin'), li.querySelector('[data-topic]')?.textContent]))
      .toEqual([['firmware', `buttonplus/${virtualDeviceId()}/status`]]);
    expect(Array.from(panel.querySelectorAll('#fb-mqtt-topics option')).map(o => (o as HTMLOptionElement).value)).toEqual(['home/temp']);

    // A click on a subscription fills the topic; then the payload and Enter (submit).
    (panel.querySelector('[data-subscription]') as HTMLElement).click();
    await settle();
    const topic = panel.querySelector('[data-testid="mqtt-topic"]') as HTMLInputElement;
    expect(topic.value).toBe('home/temp');
    const payload = panel.querySelector('[data-testid="mqtt-payload"]') as HTMLInputElement;
    payload.value = '21.5';
    payload.dispatchEvent(new Event('input', { bubbles: true }));
    (panel.querySelector('[data-testid="mqtt-inject"]') as HTMLFormElement).dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
    await wait(100);

    expect(item(el, 'temp').textContent).toContain('21.5');
    // The firmware's own publishes aside (its birth message).
    const rows = () => Array.from(panel.querySelectorAll('[data-testid="mqtt-log"] li:not([data-origin="firmware"])')).map(li => [
      li.getAttribute('data-kind'), li.querySelector('[data-topic]')?.textContent, li.querySelector('[data-payload]')?.textContent,
      li.querySelector('[data-note]')?.textContent,
    ]);
    const logged = [
      ['published', 'home/ack', 'got 21.5', 'not sent: offline (in-page broker only)'],
      ['delivered', 'home/temp', '21.5', 'to the script'],
      ['injected', 'home/temp', '21.5', 'local: a subscription matches'],
    ];
    expect(rows()).toEqual(logged);

    // A filter is not a topic.
    topic.value = 'home/#';
    topic.dispatchEvent(new Event('input', { bubbles: true }));
    (panel.querySelector('[data-testid="mqtt-inject"]') as HTMLFormElement).dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
    await settle();
    expect(panel.querySelector('[data-testid="mqtt-message"]')?.textContent).toContain('replace + and #');

    // Sensors: a value typed here is the sensor's reading.
    const temp = panel.querySelector('[data-sensor="5"]') as HTMLInputElement;
    expect(temp.value).toBe('21.5');
    temp.value = '30';
    temp.dispatchEvent(new Event('change', { bubbles: true }));
    expect(emulatorSession.sensors().find(s => s.id === 5)!.value).toBe(30);

    // Loopback.
    const loopback = panel.querySelector('[data-testid="mqtt-loopback"]') as HTMLInputElement;
    loopback.checked = true;
    loopback.dispatchEvent(new Event('change', { bubbles: true }));
    expect(emulatorSession.loopback).toBe(true);

    // Stopped: no form, the log of the run can still be read.
    emulatorSession.stop();
    await settle();
    expect(panel.querySelector('[data-testid="mqtt-stopped"]')).not.toBeNull();
    expect(panel.querySelector('[data-testid="mqtt-inject"]')).toBeNull();
    expect(rows()).toEqual(logged);

    // The consoles kept their lines behind this tab.
    tab(pane, 'virtual').click();
    await settle();
    expect(pane.querySelector('fb-emulator-panel')).toBeNull();
    expect(lineTexts(pane, 'virtual')).toContain(`started ${virtualDeviceId()}`);
  });
});

describe('MQTT tab: live modes', () => {
  let broker: TestBroker;
  const LIVE_SCRIPT = `
    mqtt.subscribe(0, "home/temp", function (t, p) { fb.main.display.setValue("temp", p); });
    fb.key("light").button.onClick(function () {
      mqtt.publish(0, "home/light/set", "ON", 0, 1);
      mqtt.publish(0, "buttonplus/btn_a00001/cover/raam/set", "stop", 0, 0);
    });
  `;

  beforeEach(async () => {
    broker = await TestBroker.start();
    localStorage.setItem('fb-mqtt-ws:broker.test', broker.url);
    emulatorSession.socket = nodeSocket;
    emulatorSession.securePage = false;
    emulatorSession.liveTiming = { backoffMs: 20 };
    use(LIVE_SCRIPT);
  });

  afterEach(async () => {
    emulatorSession.destroy();
    emulatorSession.socket = undefined;
    emulatorSession.securePage = undefined;
    emulatorSession.ask = null;
    await broker.stop();
  });

  async function openPanel() {
    const el = await mount('fb-virtual-device');
    const bar = await mount('fb-toolbar');
    const pane = await mount('fb-console-pane');
    await run();
    (pane.querySelector('[data-testid="tab-mqtt"]') as HTMLElement).click();
    await wait(80);
    const panel = pane.querySelector('fb-emulator-panel') as HTMLElement;
    const mode = (name: string) => panel.querySelector(`[role="radio"][data-mode="${name}"]`) as HTMLElement;
    return { el, bar, pane, panel, mode };
  }

  async function pressKey(el: HTMLElement) {
    pointer('pointerdown', key(el, 3));
    await wait(80);
    pointer('pointerup', key(el, 3));
    await wait(120);
  }

  it('offline by default: the mode control, the broker with its WebSocket address, no live mark anywhere', async () => {
    const { el, bar, pane, panel, mode } = await openPanel();
    const group = panel.querySelector('[role="radiogroup"][aria-label="MQTT mode"]') as HTMLElement;
    expect(Array.from(group.querySelectorAll('[role="radio"]')).map(r => [r.textContent?.trim(), r.getAttribute('aria-checked')]))
      .toEqual([['Offline', 'true'], ['Live read-only', 'false'], ['Live publish', 'false']]);
    expect((panel.querySelector('[data-testid="mqtt-ws-url"]') as HTMLInputElement).value).toBe(broker.url);
    expect(panel.querySelector('[data-testid="mqtt-link-state"]')?.getAttribute('data-state')).toBe('offline');
    expect(panel.querySelector('[data-testid="mqtt-optins"]')).toBeNull();
    expect(panel.querySelector('[data-testid="mqtt-loopback"]')).not.toBeNull();
    expect(bar.querySelector('[data-testid="toolbar-live"]')).toBeNull();
    expect(pane.querySelector('[data-testid="tab-live"]')).toBeNull();
    expect(el.querySelector('.fb-device-live')).toBeNull();
    expect(mode('offline').classList.contains('bg-purple-90')).toBe(true);
  });

  it('live read-only: connected, marked "read" in the toolbar and on the tab; no opt-ins; a message from the broker shows', async () => {
    const { el, bar, pane, panel, mode } = await openPanel();
    mode('live-readonly').click();
    await until(() => emulatorSession.snapshot().mqttLink === 'connected');
    await settle();
    expect(mode('live-readonly').getAttribute('aria-checked')).toBe('true');
    expect(panel.querySelector('[data-testid="mqtt-link-state"]')?.getAttribute('data-state')).toBe('connected');
    expect(panel.querySelector('[data-testid="mqtt-mode-words"]')?.textContent).toContain('nothing is published');
    expect(panel.querySelector('[data-testid="mqtt-optins"]')).toBeNull();
    expect(bar.querySelector('[data-testid="toolbar-live"]')?.textContent?.trim()).toBe('read');
    expect(pane.querySelector('[data-testid="tab-live"]')?.textContent?.trim()).toBe('read');
    expect(el.querySelector('.fb-device-live')).toBeNull();
    // Inject is local; loopback is the in-page broker's.
    expect(panel.querySelector('[data-testid="mqtt-inject-button"]')?.textContent?.trim()).toBe('Inject (local)');
    expect(panel.querySelector('[data-testid="mqtt-loopback"]')).toBeNull();

    await until(() => broker.subscriptionsOf(virtualDeviceId()).length === 1);
    broker.publish('home/temp', '18.5');
    await until(() => item(el, 'temp').textContent!.includes('18.5'));
    await settle();
    const first = panel.querySelector('[data-testid="mqtt-log"] li') as HTMLElement;
    expect([first.getAttribute('data-kind'), first.querySelector('[data-note]')?.textContent]).toEqual(['delivered', 'from the broker']);

    await pressKey(el);
    await settle();
    expect(broker.published).toEqual([]);
    expect(Array.from(panel.querySelectorAll('[data-testid="mqtt-log"] li')).slice(0, 2).map(li => li.getAttribute('data-kind')))
      .toEqual(['would-publish', 'would-publish']);
  });

  it('live publish: marked "live" in the warning colour (toolbar, tab, glow); the opt-ins with what they cost', async () => {
    const { el, bar, pane, panel, mode } = await openPanel();
    mode('live-publish').click();
    await until(() => emulatorSession.snapshot().mqttLink === 'connected');
    await settle();
    expect(mode('live-publish').classList.contains('bg-status-orange')).toBe(true);
    const pill = bar.querySelector('[data-testid="toolbar-live"]') as HTMLElement;
    expect([pill.textContent?.trim(), pill.getAttribute('data-mode'), pill.classList.contains('bg-status-orange')]).toEqual(['live', 'live-publish', true]);
    expect(bar.querySelector('[data-testid="toolbar-virtual"]')!.classList.contains('border-status-orange')).toBe(true);
    expect(pane.querySelector('[data-testid="tab-live"]')?.textContent?.trim()).toBe('live');
    expect(el.querySelector('.device-flip-container')!.classList.contains('fb-device-live')).toBe(true);

    const optins = panel.querySelector('[data-testid="mqtt-optins"]') as HTMLElement;
    expect(optins.textContent).toContain('A retained message stays on the broker');
    expect(optins.textContent).toContain('make entities for the virtual device in Home Assistant');
    expect(optins.textContent).toContain('about eight messages a second');
    expect((optins.querySelector('[data-testid="mqtt-own-states"]') as HTMLInputElement).checked).toBe(false);
    const retained = optins.querySelector('[data-testid="mqtt-send-retained"]') as HTMLInputElement;
    expect(retained.checked).toBe(false);
    expect((optins.querySelector('[data-testid="mqtt-announce"]') as HTMLInputElement).checked).toBe(false);
    retained.checked = true;
    retained.dispatchEvent(new Event('change', { bubbles: true }));
    expect(emulatorSession.mqttStatus()!.sendRetained).toBe(true);

    // Stop: every mark is gone.
    emulatorSession.stop();
    await settle();
    expect(bar.querySelector('[data-testid="toolbar-live"]')).toBeNull();
    expect(pane.querySelector('[data-testid="tab-live"]')).toBeNull();
    expect(el.querySelector('.fb-device-live')).toBeNull();
  });

  it('a real device\'s topic: the in-app dialog asks; "Once" publishes it', async () => {
    const confirm = vi.fn();
    (window as unknown as { confirm: unknown }).confirm = confirm;
    const { el, panel, mode } = await openPanel();
    mode('live-publish').click();
    await until(() => emulatorSession.snapshot().mqttLink === 'connected');
    await pressKey(el);
    await until(() => !!document.querySelector('fb-dialog [data-testid="dialog"]'));
    const dialog = document.querySelector('fb-dialog [data-testid="dialog"]') as HTMLElement;
    expect(dialog.textContent).toContain('This topic belongs to the real device btn_a00001. Publish as it?');
    expect(dialog.textContent).toContain('buttonplus/btn_a00001/cover/raam/set');
    expect(Array.from(dialog.querySelectorAll('button')).map(b => b.textContent?.trim())).toEqual(['No', 'Once', 'For this session']);
    // The plain topic went already; the real device's waits.
    await until(() => broker.published.length === 1);
    expect(broker.published[0]!.topic).toBe('home/light/set');
    (dialog.querySelector('[data-choice="once"]') as HTMLElement).click();
    await until(() => broker.published.length === 2);
    expect(broker.published[1]).toMatchObject({ topic: 'buttonplus/btn_a00001/cover/raam/set', payload: 'stop' });
    await settle();
    const row = Array.from(panel.querySelectorAll('[data-testid="mqtt-log"] li')).find(li => li.querySelector('[data-topic]')?.textContent?.includes('btn_a00001')) as HTMLElement;
    expect([row.getAttribute('data-kind'), row.querySelector('[data-note]')?.textContent]).toEqual(
      ['sent', 'sent to the broker · as the real device btn_a00001 (this once)']);
    expect(confirm).not.toHaveBeenCalled();
  });

  it('no websockets listener at the address: the error with the two Mosquitto lines; a corrected address connects', async () => {
    const closed = await TestBroker.start();
    const dead = closed.url;
    await closed.stop();
    localStorage.setItem('fb-mqtt-ws:broker.test', dead);
    const { panel, mode } = await openPanel();
    mode('live-readonly').click();
    await until(() => emulatorSession.snapshot().mqttLink === 'error');
    await settle();
    const error = panel.querySelector('[data-testid="mqtt-link-error"]') as HTMLElement;
    expect(error.getAttribute('data-error')).toBe('unreachable');
    expect(error.getAttribute('role')).toBe('alert');
    expect(panel.querySelector('[data-testid="mqtt-listener-help"]')?.textContent).toBe('listener 1884\nprotocol websockets');
    expect(panel.querySelector('[data-testid="mqtt-link-state"]')?.textContent?.trim()).toBe('not connected');

    const input = panel.querySelector('[data-testid="mqtt-ws-url"]') as HTMLInputElement;
    input.value = broker.url;
    input.dispatchEvent(new Event('change', { bubbles: true }));
    await until(() => emulatorSession.snapshot().mqttLink === 'connected');
    await settle();
    expect(panel.querySelector('[data-testid="mqtt-link-error"]')).toBeNull();
    expect(localStorage.getItem('fb-mqtt-ws:broker.test')).toBe(broker.url);
  });

  it('https page: said in the panel; a client id in config.json: said that it is not used', async () => {
    emulatorSession.securePage = true;
    use(LIVE_SCRIPT, JSON.stringify({ ...BASIC_CONFIG, brokers: [{ brokerid: 'local', url: 'mqtt://broker.test/', clientid: 'btn_a00001' }] }));
    const { panel, mode } = await openPanel();
    expect(panel.querySelector('[data-testid="mqtt-secure-page"]')?.textContent).toContain('https');
    expect(panel.querySelector('[data-testid="mqtt-client-id-note"]')?.textContent?.replace(/\s+/g, ' ')).toContain(`btn_a00001: not used here. The virtual device always connects as ${virtualDeviceId()}`);
    mode('live-readonly').click();
    await settle();
    expect(panel.querySelector('[data-testid="mqtt-link-error"]')?.getAttribute('data-error')).toBe('mixed-content');
    expect(broker.connects).toEqual([]);
  });

  it('the info side says where MQTT goes', async () => {
    const { el, mode } = await openPanel();
    mode('live-publish').click();
    await until(() => emulatorSession.snapshot().mqttLink === 'connected');
    (el.querySelector('[data-testid="gear-icon"]') as HTMLElement).click();
    await settle();
    const mqtt = el.querySelector('fb-virtual-info [data-field="mqtt"]') as HTMLElement;
    expect(mqtt.textContent).toBe('live, publishing (connected)');
    expect(mqtt.classList.contains('text-status-orange')).toBe(true);
  });
});
