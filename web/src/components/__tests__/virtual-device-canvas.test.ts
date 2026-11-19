/**
 * The virtual device made by the firmware in the page (the real wasm). While
 * nothing runs: its layout as outlines with names, no pixels. While a script
 * runs: its displays on canvases. The idle device made again on every config
 * edit, and the page's own layout when the firmware cannot start. happy-dom
 * has no 2D canvas: fake-canvas.ts keeps the pixels that are put.
 */
import { describe, it, expect, afterEach, beforeEach, vi } from 'vitest';
import '../fb-virtual-device.js';
import '../fb-tft-display.js';
import '../fb-button-grid.js';
import '../fb-virtual-info.js';
import '../fb-dialog.js';
import type { FbButtonGrid } from '../fb-button-grid.js';
import { initEditor, openDocument, getCode, getEditorView } from '../../js/editor.js';
import { workspace } from '../../js/workspace.js';
import { parseConfigText } from '../../js/config-model.js';
import { clearPreviewConfig, loadDefaultConfig, markPreviewStale, setPreviewConfig } from '../../js/device-config.js';
import { currentPopover } from '../../js/popover.js';
import { emulatorSession, _forgetCanDraw } from '../../js/emulator-session.js';
import { virtualMedia } from '../../js/virtual-media.js';
import type { Emulator, LayoutRect } from '../../js/emulator';
import { emulatorModule } from '../../js/__tests__/emulator-helpers';
import { repoMedia, testRaster } from '../../js/__tests__/emulator-pixels';
import { canvasPixels, installFakeCanvas } from './fake-canvas';

const CONFIG = `{
  "info": { "connectors": [{ "id": 0, "type": 2 }, { "id": 1, "type": 1 }] },
  "icons": { "sun": "/icons/sun.svg" },
  "buttons": [
    { "name": "light", "position": 3, "page": 1, "label": "Light", "toplabel": "Room" },
    { "name": "blind", "position": 4, "page": 1, "label": "Blind" }
  ],
  "displayitems": [
    {
      "name": "temp",
      "page": 1,
      "x": 10,
      "y": 10,
      "width": 40,
      "align": 1,
      "boxtype": 1,
      "label": "Temp"
    },
    {
      "name": "power",
      "page": 2,
      "x": 50,
      "y": 20,
      "width": 40,
      "align": 1,
      "boxtype": 1,
      "label": "Power"
    },
    { "name": "later", "page": 1, "x": 60, "y": 40, "width": 30, "align": 1, "boxtype": 1 }
  ],
  "lists": [
    { "name": "scenes", "page": 1, "x": 4, "y": 60, "width": 50, "rows": 1, "items": ["A", "B"] }
  ],
  "images": [
    { "name": "weather", "page": 1, "x": 250, "y": 150, "width": 48, "height": 48, "icon": "sun" }
  ]
}
`;

const SCRIPT = `
  fb.button("light").button.onClick(function () {
    fb.button("light").display.setLabel("On");
    fb.bars[0].left.led.setColor(0, 255, 0);
  });
`;

const wait = (ms: number) => new Promise(r => setTimeout(r, ms));

async function until(what: string, cond: () => unknown, ms = 3000): Promise<void> {
  const end = Date.now() + ms;
  while (!cond()) {
    if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
    await wait(10);
  }
  await wait(0);
}

/** config.json open in a real editor; the virtual device shows its text. */
function openConfig(text: string) {
  const parent = document.createElement('div');
  document.body.appendChild(parent);
  initEditor(parent);
  openDocument(text, 'json', { isConfig: true });
  vi.spyOn(workspace, 'snapshot').mockReturnValue({ ...workspace.snapshot(), currentKey: 'config.json' });
  vi.spyOn(workspace, 'flushConfigPreview').mockImplementation(showDraft);
  emulatorSession.readConfig = () => getCode();
  showDraft();
}

function showDraft() {
  const model = parseConfigText(getCode());
  if (model) setPreviewConfig(model); else markPreviewStale();
}

function setText(text: string) {
  const view = getEditorView()!;
  view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: text } });
  showDraft();
}

const face = (): Emulator => emulatorSession.face()!;
const screen = (el: HTMLElement) => el.querySelector('[data-screen]') as HTMLElement;
const drawnBy = (el: HTMLElement) => screen(el)?.getAttribute('data-drawn-by');
const laidOutBy = (el: HTMLElement) => screen(el)?.getAttribute('data-layout');
/** The box of an entry on the main display: its outline (nothing runs) or the box over the canvas (a script runs). */
const box = (el: HTMLElement, ref: string) => el.querySelector(`fb-tft-display [data-ref="${ref}"]`) as HTMLElement;
const front = (el: HTMLElement) => el.querySelector('.device-flip-front') as HTMLElement;
const mainCanvas = (el: HTMLElement) => el.querySelector('canvas[data-display="main"]') as HTMLCanvasElement;
const keyCell = (el: HTMLElement, position: number) => el.querySelector(`fb-button-grid [data-position="${position}"]`) as HTMLElement;
const note = (el: HTMLElement) => el.querySelector('[data-testid="face-note"]') as HTMLElement | null;
const rectOf = (ref: string): LayoutRect => face().layout('main').find(r => r.ref && `${r.ref.list}:${r.ref.index}` === ref)!;
const lit = (pixels: Uint8ClampedArray) => { let n = 0; for (let i = 0; i < pixels.length; i += 4) if (pixels[i]! + pixels[i + 1]! + pixels[i + 2]! > 0) n++; return n; };
const parsed = (): Record<string, Array<Record<string, unknown>>> => JSON.parse(getCode());

/** The virtual device, mounted and laid out by the firmware (nothing runs: outlines). */
async function device(): Promise<HTMLElement> {
  const el = document.createElement('fb-virtual-device');
  document.body.appendChild(el);
  await until('the firmware\'s layout', () => laidOutBy(el) === 'firmware' && el.querySelector('[data-outline]'));
  return el;
}

/** The same, with SCRIPT running in it. */
async function runningDevice(script = SCRIPT): Promise<HTMLElement> {
  emulatorSession.readSource = async () => ({ script, config: getCode() });
  const el = await device();
  await emulatorSession.run();
  await until('the running device', () => emulatorSession.running && face().running && drawnBy(el) === 'firmware');
  await wait(0);
  return el;
}

function pointer(type: string, target: EventTarget, x: number, y: number, init: Record<string, unknown> = {}) {
  target.dispatchEvent(new MouseEvent(type, { bubbles: true, cancelable: true, clientX: x, clientY: y, button: 0, ...init }));
}

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

let restoreCanvas: (() => void) | null = null;

beforeEach(() => {
  vi.stubGlobal('localStorage', memoryStorage());
  restoreCanvas = installFakeCanvas();
  _forgetCanDraw();
  loadDefaultConfig();
  emulatorSession.wasm = emulatorModule();
  // The repo's fonts and a raster for every icon: text and icons as on a device.
  emulatorSession.mediaSource = repoMedia({}, { icons: true });
});

afterEach(() => {
  emulatorSession.destroy();
  emulatorSession.readConfig = null;
  emulatorSession.readSource = null;
  emulatorSession.mediaSource = null;
  emulatorSession.wasm = undefined;
  currentPopover()?.close();
  restoreCanvas?.();
  _forgetCanDraw();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  clearPreviewConfig();
  loadDefaultConfig();
  document.body.innerHTML = '';
});

describe('nothing runs: outlines with names, no pixels', () => {
  it('no canvas and no text of the config: an outline per entry with its name; the keys too, LEDs off', async () => {
    openConfig(CONFIG);
    const el = await device();
    const emu = face();
    expect(emu.running).toBe(false);
    expect(emu.displays().map(d => d.id)).toEqual(['main', 'key3', 'key4']);
    expect(drawnBy(el)).toBe('outline');
    // The firmware drew its displays (the layout comes from that), but none of its pixels are shown.
    expect(lit(emu.readPixels('main'))).toBeGreaterThan(300);
    expect(el.querySelector('canvas')).toBeNull();
    expect([...el.querySelectorAll<HTMLElement>('fb-tft-display [data-outline]')].map(b => b.textContent!.trim()).sort())
      .toEqual(['later', 'scenes', 'temp', 'weather']);
    // Labels, top labels and list items are data: not shown.
    for (const data of ['Temp', 'Light', 'Room', 'Blind', 'Power']) expect(front(el).textContent).not.toContain(data);
    expect(front(el).textContent).not.toMatch(/\bA\b|\bB\b/);
    for (const [position, name] of [[3, 'light'], [4, 'blind']] as const) {
      const key = keyCell(el, position).querySelector('button')!;
      expect(key.getAttribute('data-drawn-by')).toBe('outline');
      expect(key.textContent!.trim()).toBe(name);
      expect(keyCell(el, position).querySelector('[data-led]')!.hasAttribute('data-led-on')).toBe(false);
    }
    expect(keyCell(el, 3).querySelector('button')!.getAttribute('aria-label')).toBe('Key 3 light');
    expect(note(el)).toBeNull();
  });

  it('names: "name", else "#<displayitemid>", else "displayitems[i]"; a key "Key <position>"; the clock "clock"', async () => {
    openConfig(JSON.stringify({
      info: { connectors: [{ id: 0, type: 2 }, { id: 1, type: 1 }] },
      buttons: [{ position: 3, page: 1, label: 'Light' }],
      displayitems: [
        { name: 'temp', displayitemid: 7, page: 1, x: 0, y: 0, width: 30, label: 'Temp' },
        { displayitemid: 8, page: 1, x: 0, y: 30, width: 30, label: 'Humidity' },
        { page: 1, x: 0, y: 60, width: 30, label: 'Wind' },
      ],
      clocks: [{ x: 200, y: 20, width: 100, height: 100, page: 1 }],
    }));
    const el = await device();
    expect(box(el, 'displayitems:0').textContent!.trim()).toBe('temp');
    expect(box(el, 'displayitems:1').textContent!.trim()).toBe('#8');
    expect(box(el, 'displayitems:2').textContent!.trim()).toBe('displayitems[2]');
    for (const data of ['Temp', 'Humidity', 'Wind', 'Light']) expect(front(el).textContent).not.toContain(data);
    expect(keyCell(el, 3).querySelector('button')!.textContent!.trim()).toBe('Key 3');
    // A position without a configured key stays empty.
    expect(keyCell(el, 4).querySelector('button')!.getAttribute('data-drawn-by')).toBe('none');
    expect(keyCell(el, 4).querySelector('button')!.textContent!.trim()).toBe('');
    const clock = box(el, 'clocks:0');
    expect(clock.getAttribute('data-outline')).toBe('clock');
    expect(clock.textContent!.trim()).toBe('clock');
    expect([clock.style.left, clock.style.top, clock.style.width, clock.style.height]).toEqual(['200px', '20px', '100px', '100px']);
    expect(clock.getAttribute('aria-label')).toBe('Clock clock, x 200, y 20');
    // It can be found in config.json; it is not moved here.
    const reveals: unknown[] = [];
    const onReveal = (e: Event) => reveals.push((e as CustomEvent).detail.ref);
    window.addEventListener('config-reveal', onReveal);
    clock.click();
    clock.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true, cancelable: true }));
    window.removeEventListener('config-reveal', onReveal);
    expect(reveals).toEqual([{ kind: 'clock', list: 'clocks', index: 0 }]);
    await wait(30);
    expect(parsed().clocks![0]!.x).toBe(200);
  });
});

describe('a script runs: the face is the firmware\'s displays', () => {
  it('a canvas per display, showing exactly the engine\'s frame; a key\'s display at the key panel\'s scale; the LEDs stay DOM', async () => {
    openConfig(CONFIG);
    const el = await runningDevice();
    const emu = face();
    expect([...el.querySelectorAll<HTMLCanvasElement>('canvas[data-display]')].map(c => [c.dataset.display, c.width, c.height])).toEqual([
      ['main', 320, 240], ['key3', 160, 80], ['key4', 160, 80],
    ]);
    for (const canvas of el.querySelectorAll<HTMLCanvasElement>('canvas[data-display]')) {
      const pixels = canvasPixels(canvas);
      expect(lit(pixels)).toBeGreaterThan(300);
      expect(Array.from(pixels)).toEqual(Array.from(emu.readPixels(canvas.dataset.display!)));
    }
    // The main display a CSS px per device pixel; a key's 160 x 80 on 120 x 60 (pitch 0.135 mm against 0.180 mm).
    expect([screen(el).style.width, screen(el).style.height]).toEqual(['320px', '240px']);
    const key = keyCell(el, 3).querySelector('button') as HTMLElement;
    expect([key.style.width, key.style.height]).toEqual(['120px', '60px']);
    // A key's canvas is inside its button; its LED is an element next to it.
    expect(key.querySelector('canvas[data-display="key3"]')).not.toBeNull();
    expect(key.getAttribute('aria-label')).toBe('Key 3 light');
    expect(keyCell(el, 3).querySelector('[data-led]')).not.toBeNull();
    // No outlines over the running device.
    expect(el.querySelector('[data-outline]')).toBeNull();
    expect(note(el)).toBeNull();
  });

  it('square pixels per display: only where a device pixel is a whole number of screen pixels', async () => {
    openConfig(CONFIG);
    const el = await runningDevice();
    const rendering = async (dpr: number) => {
      Object.defineProperty(window, 'devicePixelRatio', { value: dpr, configurable: true });
      window.dispatchEvent(new Event('resize'));
      (el as unknown as { requestUpdate(): void }).requestUpdate();
      await wait(0);
      await wait(0);
      return [mainCanvas(el).style.imageRendering, (keyCell(el, 3).querySelector('canvas') as HTMLCanvasElement).style.imageRendering];
    };
    try {
      // 1x: the main display 1 screen pixel per device pixel, a key's 0.75.
      expect(await rendering(1)).toEqual(['pixelated', 'auto']);
      expect(await rendering(1.5)).toEqual(['auto', 'auto']);
      // 4x: 4 and 3 screen pixels.
      expect(await rendering(4)).toEqual(['pixelated', 'pixelated']);
    } finally {
      Object.defineProperty(window, 'devicePixelRatio', { value: 1, configurable: true });
    }
  });

  it('a key without a display is drawn from the model, next to one that has a canvas', async () => {
    const engine = {
      displays: () => [{ id: 'key3', index: 1, kind: 'key' as const, position: 3, width: 160, height: 80 }],
      readPixels: () => new Uint8ClampedArray(160 * 80 * 4),
      on: () => () => undefined,
    };
    const grid = document.createElement('fb-button-grid') as FbButtonGrid;
    grid.positions = [3, 4];
    grid.buttons = [
      { position: 3, page: 1, label: 'Light', topLabel: '', ledOn: false, ledColor: 'rgb(0,0,0)' },
      { position: 4, page: 1, label: 'Blind', topLabel: '', ledOn: true, ledColor: 'rgb(255,0,0)' },
    ];
    grid.engine = engine as unknown as Emulator;
    grid.pressable = true;
    document.body.appendChild(grid);
    await grid.updateComplete;
    expect(grid.querySelector('[data-position="3"] canvas[data-display="key3"]')).not.toBeNull();
    expect(grid.querySelector('[data-position="3"] [data-label]')).toBeNull();
    expect(grid.querySelector('[data-position="4"] canvas')).toBeNull();
    expect(grid.querySelector('[data-position="4"] [data-label]')!.textContent).toContain('Blind');
    expect(grid.querySelector('[data-position="4"] [data-led]')!.hasAttribute('data-led-on')).toBe(true);
  });

  it('the stepper shows another page of config.json on the idle device', async () => {
    openConfig(CONFIG);
    const el = await device();
    expect(box(el, 'displayitems:0')).not.toBeNull();
    expect(box(el, 'displayitems:1')).toBeNull();
    (el.querySelector('[data-testid="page-next"]') as HTMLElement).click();
    await until('page 2', () => face().state.shownPage === 2 && box(el, 'displayitems:1'));
    expect(box(el, 'displayitems:0')).toBeNull();
    expect(emulatorSession.snapshot().status).toBe('idle');
    (el.querySelector('[data-testid="page-prev"]') as HTMLElement).click();
    await until('page 1', () => face().state.shownPage === 1 && box(el, 'displayitems:0'));
  });
});

describe('editing on the outlines', () => {
  it('one outline per entry the firmware laid out, at its rectangle; focusable, labelled', async () => {
    openConfig(CONFIG);
    const el = await device();
    const rects = face().layout('main').filter(r => r.ref && r.visible && r.kind !== 'clock');
    expect(rects.map(r => r.kind).sort()).toEqual(['displayitem', 'image', 'list']);
    const boxes = [...el.querySelectorAll<HTMLElement>('[data-outline]:not([data-empty])')];
    expect(boxes.map(b => [b.dataset.ref, b.style.left, b.style.top, b.style.width, b.style.height])).toEqual(
      rects.map(r => [`${r.ref!.list}:${r.ref!.index}`, `${r.x}px`, `${r.y}px`, `${r.width}px`, `${Math.max(r.height, 14)}px`]));
    // The firmware's rule for a display item: x and y are % of 320 x 240.
    expect([rectOf('displayitems:0').x, rectOf('displayitems:0').y, rectOf('displayitems:0').width]).toEqual([32, 24, 128]);
    const temp = box(el, 'displayitems:0');
    expect(temp.getAttribute('role')).toBe('button');
    expect(temp.getAttribute('tabindex')).toBe('0');
    expect(temp.getAttribute('aria-label')).toBe('Display item temp, x 10, y 10');
    expect(box(el, 'lists:0').getAttribute('aria-label')).toBe('List scenes, x 4, y 60');
    expect(box(el, 'images:0').getAttribute('aria-label')).toBe('Image weather, x 250, y 150');
    expect(temp.childElementCount).toBe(0);
    expect(temp.textContent!.trim()).toBe('temp');
  });

  it('an entry the firmware draws nothing of yet (no caption, no value) has an outline too, with its name, where it will be', async () => {
    openConfig(CONFIG);
    const el = await device();
    const later = face().layout('main').find(r => r.ref?.index === 2 && r.kind === 'displayitem')!;
    expect([later.visible, later.height]).toEqual([false, 0]);
    const holder = box(el, 'displayitems:2');
    expect(holder.hasAttribute('data-empty')).toBe(true);
    expect(holder.textContent!.trim()).toBe('later');
    expect([holder.style.left, holder.style.top, holder.style.width, holder.style.height]).toEqual(['192px', '96px', '96px', '14px']);
    expect(holder.getAttribute('aria-label')).toBe('Display item later, x 60, y 40');
    // It moves as any other entry.
    holder.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowUp', bubbles: true, cancelable: true }));
    await until('the edit', () => parsed().displayitems![2]!.y === 39);
  });

  it('a click shows the entry in config.json; the editor cursor outlines its box', async () => {
    openConfig(CONFIG);
    const el = await device();
    const reveals: CustomEvent[] = [];
    const onReveal = (e: Event) => reveals.push(e as CustomEvent);
    window.addEventListener('config-reveal', onReveal);
    const temp = box(el, 'displayitems:0');
    pointer('pointerdown', temp, 100, 100);
    pointer('pointerup', window, 100, 100);
    temp.click();
    window.removeEventListener('config-reveal', onReveal);
    expect(reveals.map(r => r.detail.ref)).toEqual([{ kind: 'displayitem', list: 'displayitems', index: 0 }]);
    expect(getCode()).toBe(CONFIG);
    window.dispatchEvent(new CustomEvent('config-selection', { detail: { ref: { kind: 'list', list: 'lists', index: 0 } } }));
    await wait(0);
    expect(box(el, 'lists:0').hasAttribute('data-selected')).toBe(true);
    expect(box(el, 'lists:0').style.outline).toContain('solid');
    expect(box(el, 'displayitems:0').hasAttribute('data-selected')).toBe(false);
  });

  it('a drag moves the outline; the drop writes x and y and the firmware lays it out there', async () => {
    openConfig(CONFIG);
    const el = await device();
    const before = face();
    const from = rectOf('displayitems:0');
    const temp = box(el, 'displayitems:0');
    pointer('pointerdown', temp, 100, 100);
    pointer('pointermove', window, 148, 124);
    pointer('pointermove', window, 196, 148);          // 30 % right, 20 % down
    await wait(0);
    expect(el.querySelector('[data-testid="move-position"]')?.textContent?.trim()).toBe('x 40 · y 30 %');
    const moving = box(el, 'displayitems:0');
    expect([moving.style.left, moving.style.top]).toEqual([`${from.x + 96}px`, `${from.y + 48}px`]);
    expect(moving.textContent!.trim()).toBe('temp');
    expect(getCode()).toBe(CONFIG);                      // nothing written before the drop
    expect(face()).toBe(before);                         // and nothing drawn again

    pointer('pointerup', window, 196, 148);
    moving.click();
    await until('the edit', () => getCode() !== CONFIG);
    expect(getCode()).toBe(CONFIG.replace('"x": 10,\n      "y": 10,', '"x": 40,\n      "y": 30,'));
    await until('the new device', () => face() !== before && rectOf('displayitems:0').x === 128);
    await wait(0);
    const dropped = box(el, 'displayitems:0');
    expect([dropped.style.left, dropped.style.top]).toEqual(['128px', '72px']);
    expect(el.querySelector('[data-testid="move-position"]')).toBeNull();
    expect(el.querySelector('canvas')).toBeNull();
  });

  it('Escape cancels a drag: the box goes back, nothing is written', async () => {
    openConfig(CONFIG);
    const el = await device();
    const from = rectOf('lists:0');
    pointer('pointerdown', box(el, 'lists:0'), 100, 100);
    pointer('pointermove', window, 164, 100);
    await wait(0);
    expect(box(el, 'lists:0').style.left).toBe(`${from.x + 64}px`);
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true, cancelable: true }));
    await wait(0);
    expect(box(el, 'lists:0').style.left).toBe(`${from.x}px`);
    expect(getCode()).toBe(CONFIG);
  });

  it('arrow keys nudge the focused box: a display item by 1 %, with Shift 5; an image by pixels', async () => {
    openConfig(CONFIG);
    const el = await device();
    const first = face();
    box(el, 'displayitems:0').dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true, cancelable: true }));
    await until('the edit', () => parsed().displayitems![0]!.x === 11);
    expect(parsed().displayitems![0]).toMatchObject({ x: 11, y: 10 });
    await until('the nudged device', () => face() !== first && rectOf('displayitems:0').x === 35);
    const second = face();
    box(el, 'displayitems:0').dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowDown', shiftKey: true, bubbles: true, cancelable: true }));
    await until('the edit', () => parsed().displayitems![0]!.y === 15);
    expect(parsed().displayitems![0]).toMatchObject({ x: 11, y: 15 });
    await until('the second nudge', () => face() !== second && rectOf('displayitems:0').y === 36);
    const third = face();
    box(el, 'images:0').dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowLeft', bubbles: true, cancelable: true }));
    await until('the edit', () => parsed().images![0]!.x === 249);
    expect(parsed().images![0]).toMatchObject({ x: 249, y: 150 });
    await until('the image', () => face() !== third && rectOf('images:0').x === 249);
    expect(box(el, 'images:0').style.left).toBe('249px');
  });

  it('right-click opens the properties next to the box; a change is an edit the firmware draws', async () => {
    openConfig(CONFIG);
    const el = await device();
    const before = face();
    const pixels = Array.from(before.readPixels('main'));
    box(el, 'displayitems:0').dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 50, clientY: 40 }));
    await until('the popover', () => currentPopover());
    const pop = document.querySelector('[data-testid="item-popover"]') as HTMLElement;
    expect(pop.querySelector('[data-testid="item-popover-title"]')!.textContent).toContain('temp');
    const label = pop.querySelector('[data-field="label"]') as HTMLInputElement;
    expect(label.value).toBe('Temp');
    label.value = 'Outside';
    label.dispatchEvent(new Event('input', { bubbles: true }));
    expect(parsed().displayitems![0]!.label).toBe('Outside');
    await until('the new label', () => face() !== before);
    expect(Array.from(face().readPixels('main'))).not.toEqual(pixels);
    // Still open, on the same entry, anchored to its box.
    expect(currentPopover()).not.toBeNull();
    expect(box(el, 'displayitems:0')).not.toBeNull();
  });

  it('a key keeps its properties: its box is the key, not a layout rectangle', async () => {
    openConfig(CONFIG);
    const el = await device();
    const before = face();
    keyCell(el, 3).dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 50, clientY: 300 }));
    await until('the popover', () => currentPopover());
    const label = document.querySelector('[data-testid="item-popover"] [data-field="label"]') as HTMLInputElement;
    expect(label.value).toBe('Light');
    const key3 = Array.from(before.readPixels('key3'));
    label.value = 'Lamp';
    label.dispatchEvent(new Event('input', { bubbles: true }));
    expect(parsed().buttons![0]!.label).toBe('Lamp');
    await until('the key drawn again', () => face() !== before);
    expect(Array.from(face().readPixels('key3'))).not.toEqual(key3);
  });

  it('right-click on empty space: "Add text item here" adds an item; its properties open when the firmware drew it', async () => {
    openConfig(CONFIG);
    const el = await device();
    screen(el).dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 160, clientY: 216 }));
    await until('the menu', () => document.querySelector('[data-testid="menu-add-text"]'));
    (document.querySelector('[data-testid="menu-add-text"]') as HTMLElement).click();
    await until('the new item\'s properties', () => document.querySelector('[data-testid="item-popover"]'));
    expect(parsed().displayitems![3]).toMatchObject({ name: 'text1', page: 1, x: 50, y: 88, label: 'Text' });
    expect(box(el, 'displayitems:3')).not.toBeNull();
    expect(rectOf('displayitems:3').x).toBe(160);
  });

  it('while a script runs nothing can be moved; a press on the screen is the main display\'s key', async () => {
    openConfig(CONFIG);
    const el = await runningDevice();
    expect(drawnBy(el)).toBe('firmware');
    const temp = box(el, 'displayitems:0');
    temp.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true, cancelable: true }));
    await wait(0);
    expect(getCode()).toBe(CONFIG);
    expect(el.querySelector('[data-testid="device-notice"]')?.textContent).toContain('Stop');
    pointer('pointerdown', temp, 250, 100);
    await wait(0);
    expect(el.querySelector('[data-main-pressed]')).not.toBeNull();
    pointer('pointerup', window, 250, 100);
  });
});

describe('the idle device follows config.json', () => {
  it('an edit of config.json makes a new device; the old one is disposed; the outlines follow its layout', async () => {
    openConfig(CONFIG);
    const el = await device();
    const before = face();
    const dispose = vi.spyOn(before, 'dispose');
    expect(box(el, 'displayitems:0').style.width).toBe('128px');
    setText(CONFIG.replace('"width": 40,\n      "align": 1,\n      "boxtype": 1,\n      "label": "Temp"', '"width": 50,\n      "align": 1,\n      "boxtype": 1,\n      "label": "Kitchen"'));
    await until('a new device', () => face() !== before);
    await wait(0);
    expect(dispose).toHaveBeenCalledTimes(1);
    expect(box(el, 'displayitems:0').style.width).toBe('160px');
    expect(box(el, 'displayitems:0').textContent!.trim()).toBe('temp');
    expect(front(el).textContent).not.toContain('Kitchen');
    // The same text again: nothing is made.
    const same = face();
    showDraft();
    await wait(60);
    expect(face()).toBe(same);
  });

  it('text that is not valid JSON keeps the last layout, and the device says nothing of it; valid again, it is laid out', async () => {
    openConfig(CONFIG);
    const el = await device();
    const before = face();
    setText(CONFIG.replace('"buttons"', '"buttons'));
    await wait(80);
    expect(face()).toBe(before);
    expect(laidOutBy(el)).toBe('firmware');
    expect(box(el, 'displayitems:0')).not.toBeNull();
    // Invalid JSON is the editor's to show.
    expect(el.querySelector('[data-testid="preview-hint"]')).toBeNull();
    expect(el.textContent).not.toMatch(/unsaved|invalid/i);
    setText(CONFIG.replace('"label": "Temp"', '"label": "Hall"'));
    await until('a new device', () => face() !== before);
  });

  it('Run shows the running device\'s pixels, Stop the idle one\'s outlines again; removing the element disposes both', async () => {
    openConfig(CONFIG);
    emulatorSession.readSource = async () => ({ script: SCRIPT, config: getCode() });
    const el = await device();
    const idle = face();
    await emulatorSession.run();
    await until('the running device', () => face() !== idle && face().running);
    const running = face();
    await until('the pixels', () => drawnBy(el) === 'firmware' && mainCanvas(el));
    expect(el.querySelector('[data-outline]')).toBeNull();
    emulatorSession.stop();
    await wait(0);
    expect(face()).toBe(idle);
    expect(drawnBy(el)).toBe('outline');
    expect(el.querySelector('canvas')).toBeNull();
    expect([...el.querySelectorAll<HTMLElement>('fb-tft-display [data-outline]')].map(b => b.textContent!.trim()).sort())
      .toEqual(['later', 'scenes', 'temp', 'weather']);
    expect(keyCell(el, 3).querySelector('button')!.textContent!.trim()).toBe('light');
    const disposeIdle = vi.spyOn(idle, 'dispose');
    expect(running.running).toBe(false);
    el.remove();
    expect(disposeIdle).toHaveBeenCalled();
    expect(emulatorSession.face()).toBeNull();
  });

  it('a key press on a key with a display: the script runs, its canvas and its LED follow', async () => {
    openConfig(CONFIG);
    emulatorSession.readSource = async () => ({ script: SCRIPT, config: getCode() });
    const el = await device();
    await emulatorSession.run();
    await until('the running device', () => emulatorSession.running && face().running);
    await wait(60);
    const canvas = keyCell(el, 3).querySelector('canvas[data-display="key3"]') as HTMLCanvasElement;
    const before = Array.from(canvasPixels(canvas));
    expect(keyCell(el, 3).querySelector('[data-led]')!.hasAttribute('data-led-on')).toBe(false);
    pointer('pointerdown', keyCell(el, 3), 10, 10);
    await wait(100);
    expect(keyCell(el, 3).hasAttribute('data-pressed')).toBe(true);
    pointer('pointerup', keyCell(el, 3), 10, 10);
    await until('the label drawn', () => Array.from(canvasPixels(canvas)).some((v, i) => v !== before[i]));
    await wait(120);
    expect(Array.from(canvasPixels(canvas))).toEqual(Array.from(face().readPixels('key3')));
    const led = keyCell(el, 3).querySelector('[data-led]') as HTMLElement;
    expect(led.hasAttribute('data-led-on')).toBe(true);
    expect(led.style.borderColor.replace(/\s/g, '')).toBe('rgb(0,255,0)');
    // The other key's display did not change.
    expect(keyCell(el, 4).querySelector('[data-led]')!.hasAttribute('data-led-on')).toBe(false);
  });

  it('while a script runs the page is the script\'s: the stepper says which, its buttons are off; after Stop it picks a page of config.json again', async () => {
    openConfig(CONFIG);
    const el = await runningDevice();
    const stepper = el.querySelector('[data-testid="page-stepper"]') as HTMLElement;
    const next = el.querySelector('[data-testid="page-next"]') as HTMLButtonElement;
    const prev = el.querySelector('[data-testid="page-prev"]') as HTMLButtonElement;
    const label = () => el.querySelector('[data-testid="page-label"]')!.textContent!.trim();
    expect(stepper.dataset.mode).toBe('shown');
    expect([prev.disabled, next.disabled]).toEqual([true, true]);
    expect(next.title).toBe('The script decides the page while it runs');
    expect(label()).toBe('Page 1 of 2');
    // Nothing here changes the running device's page.
    const evaluated = vi.spyOn(face(), 'eval');
    next.click();
    next.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    await wait(30);
    expect(evaluated).not.toHaveBeenCalled();
    evaluated.mockRestore();
    expect(emulatorSession.shownPage()).toBe(1);
    // The script goes to page 2: the stepper follows.
    face().eval('fb.main.display.showPage(2)');
    await until('page 2', () => label() === 'Page 2 of 2' && box(el, 'displayitems:1'));
    expect([prev.disabled, next.disabled]).toEqual([true, true]);
    // Stop: the page of config.json looked at before, and the stepper selects again.
    emulatorSession.stop();
    await until('the outlines', () => drawnBy(el) === 'outline' && label() === 'Page 1 of 2');
    expect((el.querySelector('[data-testid="page-stepper"]') as HTMLElement).dataset.mode).toBe('select');
    expect(next.disabled).toBe(false);
    expect(next.title).toBe('Next page');
    next.click();
    await until('page 2 of config.json', () => face().state.shownPage === 2 && box(el, 'displayitems:1')?.hasAttribute('data-outline'));
  });
});

describe('when the firmware cannot start: the page\'s own layout', () => {
  it('no 2D canvas: the outlines are laid out from the config model, with a small note; editing works as before', async () => {
    restoreCanvas?.();
    restoreCanvas = null;
    _forgetCanDraw();
    openConfig(CONFIG);
    const el = document.createElement('fb-virtual-device');
    document.body.appendChild(el);
    await until('the note', () => note(el));
    expect([drawnBy(el), laidOutBy(el)]).toEqual(['outline', 'model']);
    expect(note(el)!.dataset.state).toBe('failed');
    expect(note(el)!.textContent).toContain('Approximate layout');
    expect(note(el)!.textContent).toContain('no 2D canvas');
    expect(el.querySelector('canvas')).toBeNull();
    const temp = box(el, 'displayitems:0');
    expect(temp.textContent!.trim()).toBe('temp');
    expect([temp.style.left, temp.style.top, temp.style.width]).toEqual(['32px', '24px', '128px']);
    expect(front(el).textContent).not.toContain('Temp');
    expect(keyCell(el, 3).querySelector('button')!.textContent!.trim()).toBe('light');
    temp.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true, cancelable: true }));
    await until('the edit', () => parsed().displayitems![0]!.x === 11);
    expect(emulatorSession.face()).toBeNull();
  });

  it('the engine does not load: the same, with the reason; a script still runs', async () => {
    emulatorSession.wasm = new Uint8Array([0, 1, 2, 3]);
    openConfig(CONFIG);
    const el = document.createElement('fb-virtual-device');
    document.body.appendChild(el);
    await until('the note', () => note(el));
    expect([drawnBy(el), laidOutBy(el)]).toEqual(['outline', 'model']);
    expect(note(el)!.dataset.state).toBe('failed');
    expect(box(el, 'displayitems:0').textContent!.trim()).toBe('temp');
    expect(el.querySelector('[data-testid="face-loading"]')).toBeNull();
  });

  it('no config.json: the sample\'s outlines, no note, and the engine is not loaded', async () => {
    emulatorSession.readConfig = () => null;
    const el = document.createElement('fb-virtual-device');
    document.body.appendChild(el);
    await wait(60);
    expect([drawnBy(el), laidOutBy(el)]).toEqual(['outline', 'model']);
    expect(note(el)).toBeNull();
    expect(emulatorSession.faceInfo().state).toBe('none');
    expect([...el.querySelectorAll('fb-tft-display [data-outline]')].map(b => b.textContent!.trim())).toEqual(['displayitems[0]', 'displayitems[1]']);
    // The sample's texts and its clock value are data.
    expect(front(el).textContent).not.toMatch(/Amsterdam|67005|Tolomeo|\d:\d\d/);
  });
});

describe('media of the virtual device', () => {
  it('no device: no font files, said as a hint, not an error; the firmware\'s fallback font draws', async () => {
    emulatorSession.mediaSource = null;
    openConfig(CONFIG);
    const el = await device();
    await until('the hint', () => note(el));
    expect(note(el)!.dataset.state).toBe('hint');
    expect(note(el)!.textContent).toContain('fallback font');
    expect(virtualMedia.missingFonts()).toContain('medium.ttf');
    expect(lit(face().readPixels('main'))).toBeGreaterThan(100);
    expect(face().state.fonts.some(f => f.loaded === false)).toBe(true);
  });

  it('a raster the IDE uploads later (an icon sync) is drawn without a new device', async () => {
    emulatorSession.mediaSource = null;
    openConfig(CONFIG);
    const el = await device();
    const emu = face();
    const at = { x: 250, y: 150, width: 48, height: 48 };
    expect(lit(emu.readPixels('main', at))).toBe(0);
    virtualMedia.changed('images', 'ico-sun-48x48.bin', testRaster(48, 48, 'a8'));
    await until('the icon', () => lit(emu.readPixels('main', at)) > 100);
    expect(face()).toBe(emu);
    expect(el.querySelector('canvas')).toBeNull();
  });
});

describe('the info side and the screenshot', () => {
  it('the display memory the device would have left, with the lowest value; texts not drawn in full', async () => {
    openConfig(CONFIG);
    const el = await device();
    (el.querySelector('[data-testid="gear-icon"]') as HTMLElement).click();
    await until('the info side', () => el.querySelector('[data-field="lvgl"]'));
    const lvgl = el.querySelector('[data-field="lvgl"]') as HTMLElement;
    expect(lvgl.textContent).toMatch(/^\d+(\.\d)? \/ \d+(\.\d)? KB · lowest \d+(\.\d)? KB$/);
    const bar = lvgl.closest('[title]')!.querySelector('[role="progressbar"]') as HTMLElement;
    // The pool is the device's (LV_MEM_SIZE 61440, less LVGL's own bookkeeping).
    expect(Number(bar.getAttribute('aria-valuemax'))).toBeGreaterThan(55000);
    expect(Number(bar.getAttribute('aria-valuemax'))).toBeLessThanOrEqual(61440);
    expect(Number(bar.getAttribute('aria-valuenow'))).toBeGreaterThan(1000);
    const dropped = el.querySelector('[data-field="text-dropped"]') as HTMLElement;
    expect(dropped.textContent!.trim()).toBe('0 skipped · 0 without formats');
    expect(dropped.className).not.toContain('orange');
    expect(el.querySelector('[data-field="text-dropped-note"]')).toBeNull();
    // The wasm's own memory is not the user's concern.
    expect(el.querySelector('[data-testid="virtual-info"]')!.textContent).not.toMatch(/wasm/i);
  });

  it('texts the device would drop: counted in the warning colour, with what it means', async () => {
    const info = emulatorSession.info();
    vi.spyOn(emulatorSession, 'info').mockReturnValue({
      ...info, lvgl: { free: 900, total: 61440, lowest: 300 }, textSkipped: 2, textCollapsed: 1,
    });
    const el = document.createElement('fb-virtual-info') as HTMLElement & { active: boolean };
    document.body.appendChild(el);
    el.active = true;
    await wait(0);
    const dropped = el.querySelector('[data-field="text-dropped"]') as HTMLElement;
    expect(dropped.textContent!.trim()).toBe('2 skipped · 1 without formats');
    expect(dropped.className).toContain('text-status-orange');
    expect(el.querySelector('[data-field="text-dropped-note"]')!.textContent).toContain('The device would drop text here');
    expect((el.querySelector('[data-field="lvgl"]') as HTMLElement).className).toContain('text-status-red');
  });

  it('Save screenshot: the main display as a PNG at the device\'s resolution', async () => {
    openConfig(CONFIG);
    const el = await device();
    const saved: Array<{ name: string; href: string }> = [];
    vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(function (this: HTMLAnchorElement) {
      saved.push({ name: this.download, href: this.href });
    });
    const button = el.querySelector('[data-testid="virtual-screenshot"]') as HTMLButtonElement;
    expect(button.disabled).toBe(false);
    button.click();
    expect(saved).toEqual([{ name: 'virtual-device-page1.png', href: 'data:image/png;fake,320x240' }]);
    const shot = emulatorSession.screenshot()!;
    expect([shot.width, shot.height, shot.rgba.length]).toEqual([320, 240, 320 * 240 * 4]);
  });

  it('nothing drawn by the firmware: the action is off', async () => {
    emulatorSession.readConfig = () => null;
    const el = document.createElement('fb-virtual-device');
    document.body.appendChild(el);
    await wait(20);
    expect((el.querySelector('[data-testid="virtual-screenshot"]') as HTMLButtonElement).disabled).toBe(true);
    expect(emulatorSession.screenshot()).toBeNull();
  });
});
