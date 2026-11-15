import { describe, it, expect, afterEach, beforeEach, vi } from 'vitest';
import '../fb-virtual-device.js';
import '../fb-tft-display.js';
import '../fb-button-grid.js';
import '../fb-dialog.js';
import { pageLabel } from '../fb-virtual-device.js';
import { DRAG_THRESHOLD_PX } from '../fb-tft-display.js';
import { initEditor, openDocument, getCode, getEditorView } from '../../js/editor.js';
import { undo } from '@codemirror/commands';
import { workspace } from '../../js/workspace.js';
import { configToModel, parseConfigText } from '../../js/config-model.js';
import { clearPreviewConfig, loadDefaultConfig, markPreviewStale, setPreviewConfig } from '../../js/device-config.js';
import { currentPopover } from '../../js/popover.js';

const SCALE = 283 / 320;
/** Pointer movement (CSS px on the preview) for a number of % units. */
const move = (ux: number, uy: number) => ({ dx: ux * 3.2 * SCALE, dy: uy * 2.4 * SCALE });

const CONFIG = `{
  "style": {
    "fonts": [
      { "name": "pacifico", "src": "pacifico.ttf" }
    ]
  },
  "buttons": [
    { "name": "up", "position": 1, "page": 1, "label": "Up", "toplabel": "Volume" }
  ],
  "displayitems": [
    {
      "name": "temp",
      "page": 1,
      "x": 10,
      "y": 10,
      "width": 20,
      "align": 1,
      "boxtype": 1,
      "label": "Temp"
    },
    {
      "name": "right",
      "page": 1,
      "x": -10,
      "y": 50,
      "width": 20,
      "align": 3,
      "boxtype": 1,
      "label": "Right"
    }
  ],
  "lists": [
    { "name": "scenes", "page": 1, "x": 4, "y": 70, "width": 50, "rows": 1, "items": ["A", "B"] }
  ],
  "images": [
    { "name": "logo", "page": 1, "x": 200, "y": 100, "width": 48, "height": 48, "src": "logo.bin" }
  ]
}
`;

async function frame(): Promise<void> {
  await new Promise(r => setTimeout(r, 0));
}

/** config.json open in a real editor, the workspace reporting it as the open file. */
function openConfig(text: string, key = 'config.json') {
  const parent = document.createElement('div');
  document.body.appendChild(parent);
  initEditor(parent);
  openDocument(text, 'json', { isConfig: key === 'config.json' });
  vi.spyOn(workspace, 'snapshot').mockReturnValue({ ...workspace.snapshot(), currentKey: key });
  vi.spyOn(workspace, 'flushConfigPreview').mockImplementation(showDraft);
  showDraft();
}

function showDraft() {
  const model = parseConfigText(getCode());
  if (model) setPreviewConfig(model); else markPreviewStale();
}

async function device(): Promise<HTMLElement> {
  const el = document.createElement('fb-virtual-device');
  document.body.appendChild(el);
  await frame();
  await frame();
  return el;
}

function item(el: HTMLElement, name: string): HTMLElement {
  return el.querySelector(`[data-display-item="${name}"], [data-list="${name}"], [data-image="${name}"]`) as HTMLElement;
}

function pointer(type: string, target: EventTarget, x: number, y: number, init: Record<string, unknown> = {}) {
  target.dispatchEvent(new MouseEvent(type, { bubbles: true, cancelable: true, clientX: x, clientY: y, button: 0, ...init }));
}

/** Press on `target`, move by dx / dy in steps, release. The browser's click follows a press and release on one element. */
function drag(target: HTMLElement, dx: number, dy: number, init: Record<string, unknown> = {}, release = true) {
  pointer('pointerdown', target, 100, 100);
  pointer('pointermove', window, 100 + dx / 2, 100 + dy / 2, init);
  pointer('pointermove', window, 100 + dx, 100 + dy, init);
  if (release) {
    pointer('pointerup', window, 100 + dx, 100 + dy, init);
    target.click();
  }
}

function parsed(): Record<string, Array<Record<string, unknown>>> {
  return JSON.parse(getCode());
}

beforeEach(() => { loadDefaultConfig(); });

afterEach(() => {
  currentPopover()?.close();
  vi.restoreAllMocks();
  clearPreviewConfig();
  loadDefaultConfig();
  document.body.innerHTML = '';
});

describe('page stepper', () => {
  const pagesOf = (numbers: number[]) => configToModel({
    displayitems: numbers.map(n => ({ page: n, x: 0, y: 0, label: `p${n}` })),
  });

  it('pageLabel: "Page n of m"; numbers that are not 1..m show the position too', () => {
    const p = (ns: number[]) => ns.map(number => ({ number }));
    expect(pageLabel(p([1]), 1)).toBe('Page 1 of 1');
    expect(pageLabel(p([1, 2, 3, 4, 5]), 2)).toBe('Page 2 of 5');
    expect(pageLabel(p([1, 3, 7]), 7)).toBe('Page 7 (3/3)');
    expect(pageLabel(p(Array.from({ length: 12 }, (_, i) => i + 1)), 12)).toBe('Page 12 of 12');
  });

  for (const count of [1, 2, 5, 12]) {
    it(`${count} page(s): three controls whatever the count, ends disabled`, async () => {
      setPreviewConfig(pagesOf(Array.from({ length: count }, (_, i) => i + 1)));
      const el = await device();
      const stepper = el.querySelector('[data-testid="page-stepper"]') as HTMLElement;
      expect(stepper.children.length).toBe(3);
      const prev = stepper.querySelector('[data-testid="page-prev"]') as HTMLButtonElement;
      const next = stepper.querySelector('[data-testid="page-next"]') as HTMLButtonElement;
      const label = () => stepper.querySelector('[data-testid="page-label"]') as HTMLElement;
      expect(label().textContent?.trim()).toBe(`Page 1 of ${count}`);
      expect(prev.disabled).toBe(true);
      expect(next.disabled).toBe(count === 1);
      expect(prev.getAttribute('aria-label')).toBe('Previous page');
      expect(next.getAttribute('aria-label')).toBe('Next page');
      // The list of pages only with more than two.
      expect(label().tagName).toBe(count > 2 ? 'BUTTON' : 'SPAN');

      for (let i = 1; i < count; i++) next.click();
      await frame();
      expect(label().textContent?.trim()).toBe(`Page ${count} of ${count}`);
      expect(next.disabled).toBe(true);
      expect(prev.disabled).toBe(count === 1);
      expect(el.querySelector(`[data-display-item]`)?.textContent).toContain(`p${count}`);
    });
  }

  it('page numbers with gaps: steps go to the next page that exists', async () => {
    setPreviewConfig(pagesOf([1, 3, 7]));
    const el = await device();
    const label = () => el.querySelector('[data-testid="page-label"]')!.textContent!.trim();
    (el.querySelector('[data-testid="page-next"]') as HTMLElement).click();
    await frame();
    expect(label()).toBe('Page 3 (2/3)');
    (el.querySelector('[data-testid="page-next"]') as HTMLElement).click();
    await frame();
    expect(label()).toBe('Page 7 (3/3)');
    (el.querySelector('[data-testid="page-prev"]') as HTMLElement).click();
    await frame();
    expect(label()).toBe('Page 3 (2/3)');
  });

  it('the label opens a menu of all pages; choosing one goes there', async () => {
    setPreviewConfig(pagesOf([1, 2, 3, 4, 5]));
    const el = await device();
    const label = el.querySelector('[data-testid="page-label"]') as HTMLButtonElement;
    label.click();
    await frame();
    const menu = document.querySelector('[data-testid="page-menu"]') as HTMLElement;
    expect(menu.getAttribute('role')).toBe('menu');
    expect(label.getAttribute('aria-expanded')).toBe('true');
    const entries = Array.from(menu.querySelectorAll('[role="menuitemradio"]')) as HTMLElement[];
    expect(entries.map(e => e.textContent?.replace('●', '').trim())).toEqual(['Page 1', 'Page 2', 'Page 3', 'Page 4', 'Page 5']);
    expect(entries.map(e => e.getAttribute('aria-checked'))).toEqual(['true', 'false', 'false', 'false', 'false']);
    expect(document.activeElement).toBe(entries[0]);

    entries[3].click();
    await frame();
    expect(document.querySelector('[data-testid="page-menu"]')).toBeNull();
    expect(el.querySelector('[data-testid="page-label"]')?.textContent?.trim()).toBe('Page 4 of 5');
    expect(label.getAttribute('aria-expanded')).toBe('false');
  });

  it('the editor cursor still switches the page', async () => {
    const model = pagesOf([1, 2, 3, 4, 5]);
    setPreviewConfig(model);
    const el = await device();
    window.dispatchEvent(new CustomEvent('config-selection', { detail: { ref: model.pages[3].displayItems[0].ref } }));
    await frame();
    expect(el.querySelector('[data-testid="page-label"]')?.textContent?.trim()).toBe('Page 4 of 5');
  });
});

describe('moving a display item', () => {
  it('a press without movement is a click: the entry is revealed, nothing is written', async () => {
    openConfig(CONFIG);
    const el = await device();
    const reveals: CustomEvent[] = [];
    const onReveal = (e: Event) => reveals.push(e as CustomEvent);
    window.addEventListener('config-reveal', onReveal);
    const target = item(el, 'temp');
    pointer('pointerdown', target, 100, 100);
    pointer('pointermove', window, 100 + DRAG_THRESHOLD_PX - 2, 101);
    pointer('pointerup', window, 100 + DRAG_THRESHOLD_PX - 2, 101);
    target.click();
    window.removeEventListener('config-reveal', onReveal);
    expect(reveals.map(r => r.detail.ref)).toEqual([{ kind: 'displayitem', list: 'displayitems', index: 0 }]);
    expect(getCode()).toBe(CONFIG);
    expect(el.querySelector('[data-testid="move-position"]')).toBeNull();
  });

  it('a drag shows the position, writes x and y on drop and is not a click', async () => {
    openConfig(CONFIG);
    const el = await device();
    const reveals: Event[] = [];
    const onReveal = (e: Event) => reveals.push(e);
    window.addEventListener('config-reveal', onReveal);
    const target = item(el, 'temp');
    const { dx, dy } = move(30, 20);

    drag(target, dx, dy, {}, false);
    await frame();
    expect(el.querySelector('[data-testid="move-position"]')?.textContent?.trim()).toBe('x 40 · y 30 %');
    expect(target.style.left).toBe('40%');
    expect(target.style.top).toBe('30%');
    expect(getCode()).toBe(CONFIG);                      // nothing written before the drop

    pointer('pointerup', window, 100 + dx, 100 + dy);
    target.click();
    await frame();
    await frame();
    window.removeEventListener('config-reveal', onReveal);
    expect(reveals).toEqual([]);
    // Exactly the two values changed; the rest of the text is as it was.
    expect(getCode()).toBe(CONFIG.replace('"x": 10,\n      "y": 10,', '"x": 40,\n      "y": 30,'));
    expect(item(el, 'temp').style.left).toBe('40%');
    expect(el.querySelector('[data-testid="move-position"]')).toBeNull();

    // One step for undo.
    undo(getEditorView()!);
    expect(getCode()).toBe(CONFIG);
  });

  it('Shift keeps the drag on one axis', async () => {
    openConfig(CONFIG);
    const el = await device();
    const { dx, dy } = move(30, 8);
    drag(item(el, 'temp'), dx, dy, { shiftKey: true });
    await frame();
    await frame();
    expect(parsed().displayitems[0]).toMatchObject({ x: 40, y: 10 });
  });

  it('Escape cancels a drag in progress', async () => {
    openConfig(CONFIG);
    const el = await device();
    const target = item(el, 'temp');
    const { dx, dy } = move(30, 20);
    drag(target, dx, dy, {}, false);
    await frame();
    expect(target.style.left).toBe('40%');
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    pointer('pointerup', window, 100 + dx, 100 + dy);
    target.click();
    await frame();
    expect(target.style.left).toBe('10%');
    expect(el.querySelector('[data-testid="move-position"]')).toBeNull();
    expect(getCode()).toBe(CONFIG);
  });

  it('stays on the screen', async () => {
    openConfig(CONFIG);
    const el = await device();
    const { dx, dy } = move(500, -500);
    drag(item(el, 'temp'), dx, dy);
    await frame();
    await frame();
    // 20 % wide: x at most 80.
    expect(parsed().displayitems[0]).toMatchObject({ x: 80, y: 0 });
  });

  it('align right: drawn from the right edge, and the drag changes that offset', async () => {
    openConfig(CONFIG);
    const el = await device();
    const target = item(el, 'right');
    expect(target.style.right).toBe('10%');
    expect(target.style.left).toBe('');
    const { dx, dy } = move(-25, 10);
    drag(target, dx, dy);
    await frame();
    await frame();
    expect(parsed().displayitems[1]).toMatchObject({ x: -35, y: 60, align: 3 });
    expect(item(el, 'right').style.right).toBe('35%');
    // Cannot leave the screen to the right: the offset stops at 0.
    const far = move(200, 0);
    drag(item(el, 'right'), far.dx, far.dy);
    await frame();
    await frame();
    expect(parsed().displayitems[1].x).toBe(0);
  });

  it('lists move in %, images in pixels', async () => {
    openConfig(CONFIG);
    const el = await device();
    const l = move(10, -20);
    drag(item(el, 'scenes'), l.dx, l.dy);
    await frame();
    await frame();
    expect(parsed().lists[0]).toMatchObject({ x: 14, y: 50 });
    drag(item(el, 'logo'), -50 * SCALE, 20 * SCALE);
    await frame();
    await frame();
    expect(parsed().images[0]).toMatchObject({ x: 150, y: 120 });
    expect(getCode()).toContain('{ "name": "logo", "page": 1, "x": 150, "y": 120, "width": 48, "height": 48, "src": "logo.bin" }');
  });

  it('arrow keys move the focused item by 1, with Shift by 5; Enter is a click', async () => {
    openConfig(CONFIG);
    const el = await device();
    const target = item(el, 'temp');
    expect(target.getAttribute('tabindex')).toBe('0');
    expect(target.getAttribute('aria-label')).toBe('Display item temp, x 10, y 10');
    const press = (key: string, shiftKey = false) =>
      item(el, 'temp').dispatchEvent(new KeyboardEvent('keydown', { key, shiftKey, bubbles: true, cancelable: true }));
    press('ArrowRight');
    await frame();
    await frame();
    expect(parsed().displayitems[0]).toMatchObject({ x: 11, y: 10 });
    press('ArrowDown', true);
    await frame();
    await frame();
    expect(parsed().displayitems[0]).toMatchObject({ x: 11, y: 15 });
    press('ArrowLeft', true);
    press('ArrowUp');
    await frame();
    await frame();
    expect(parsed().displayitems[0]).toMatchObject({ x: 6, y: 14 });

    const reveals: Event[] = [];
    const onReveal = (e: Event) => reveals.push(e);
    window.addEventListener('config-reveal', onReveal);
    press('Enter');
    window.removeEventListener('config-reveal', onReveal);
    expect(reveals.length).toBe(1);
  });

  it('touch: a long press without movement opens the properties, a moving finger drags', async () => {
    openConfig(CONFIG);
    const el = await device();
    vi.useFakeTimers();
    const touch = (type: string, target: EventTarget, x: number, y: number) =>
      target.dispatchEvent(Object.assign(
        new MouseEvent(type, { bubbles: true, cancelable: true, clientX: x, clientY: y, button: 0 }), { pointerType: 'touch' }));
    touch('pointerdown', item(el, 'temp'), 100, 100);
    vi.advanceTimersByTime(600);
    vi.useRealTimers();
    touch('pointerup', window, 100, 100);
    await frame();
    await frame();
    expect(document.querySelector('[data-testid="item-popover-title"]')?.textContent).toContain('temp');
    currentPopover()?.close();

    // Moving before the time is up: a drag, no menu.
    vi.useFakeTimers();
    const { dx, dy } = move(10, 10);
    touch('pointerdown', item(el, 'temp'), 100, 100);
    touch('pointermove', window, 100 + dx, 100 + dy);
    vi.advanceTimersByTime(600);
    vi.useRealTimers();
    touch('pointerup', window, 100 + dx, 100 + dy);
    await frame();
    await frame();
    expect(document.querySelector('[data-testid="item-popover"]')).toBeNull();
    expect(parsed().displayitems[0]).toMatchObject({ x: 20, y: 20 });
  });

  it('legacy "mqttdisplays": moved in place under that name', async () => {
    const legacy = '{\n\t"mqttdisplays": [\n\t\t{ "x": 0, "y": 0, "width": 50, "label": "Temp" }\n\t]\n}';
    openConfig(legacy);
    const el = await device();
    const { dx, dy } = move(12, 34);
    drag(el.querySelector('[data-ref="mqttdisplays:0"]') as HTMLElement, dx, dy);
    await frame();
    await frame();
    expect(getCode()).toBe('{\n\t"mqttdisplays": [\n\t\t{ "x": 12, "y": 34, "width": 50, "label": "Temp" }\n\t]\n}');
  });

  it('invalid JSON: no drag, no nudge, no menu; a hint says why and nothing is thrown', async () => {
    openConfig(CONFIG);
    const el = await device();
    const broken = CONFIG.replace('"x": 10,', '"x": 10');
    openDocument(broken, 'json', { isConfig: true });
    showDraft();
    await frame();
    const notice = () => el.querySelector('[data-testid="device-notice"]')?.textContent?.trim();
    expect(el.querySelector('[data-testid="preview-hint"]')?.getAttribute('data-state')).toBe('stale');

    const { dx, dy } = move(30, 20);
    drag(item(el, 'temp'), dx, dy);
    await frame();
    expect(notice()).toContain('not valid JSON');
    expect(item(el, 'temp').style.left).toBe('10%');
    expect(getCode()).toBe(broken);

    item(el, 'temp').dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true, cancelable: true }));
    item(el, 'temp').dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true }));
    await frame();
    await frame();
    expect(document.querySelector('[data-testid="item-popover"]')).toBeNull();
    expect(getCode()).toBe(broken);
  });

  it('config.json is not the open file: it is opened first; when that is refused nothing moves', async () => {
    openConfig(CONFIG, 'scripts/main.js');
    setPreviewConfig(parseConfigText(CONFIG)!);
    const open = vi.spyOn(workspace, 'open').mockResolvedValue(false);
    const el = await device();
    const { dx, dy } = move(30, 20);
    drag(item(el, 'temp'), dx, dy);
    await frame();
    await frame();
    expect(open).toHaveBeenCalledWith('config.json');
    expect(item(el, 'temp').style.left).toBe('10%');
  });
});

describe('item properties popover', () => {
  async function openMenu(_el: HTMLElement, target: HTMLElement): Promise<HTMLElement> {
    const e = new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 50, clientY: 50 });
    target.dispatchEvent(e);
    expect(e.defaultPrevented).toBe(true);
    await frame();
    await frame();
    return document.querySelector('[data-testid="item-popover"]') as HTMLElement;
  }

  function type(pop: HTMLElement, key: string, value: string, event = 'input') {
    const input = pop.querySelector(`[data-field="${key}"]`) as HTMLInputElement;
    input.value = value;
    input.dispatchEvent(new Event(event, { bubbles: true }));
  }

  it('right-click opens it next to the item, in the popover layer, with the entry\'s values', async () => {
    openConfig(CONFIG);
    const el = await device();
    const pop = await openMenu(el, item(el, 'temp'));
    expect(pop.parentElement?.id).toBe('fb-popover-layer');
    expect(pop.getAttribute('role')).toBe('dialog');
    expect(pop.getAttribute('aria-label')).toBe('Display item properties');
    expect(pop.querySelector('[data-testid="item-popover-title"]')?.textContent?.replace(/\s+/g, ' ').trim()).toBe('Display item temp');
    const value = (key: string) => (pop.querySelector(`[data-field="${key}"]`) as HTMLInputElement).value;
    expect(value('label')).toBe('Temp');
    expect(value('width')).toBe('20');
    expect(value('align')).toBe('1');
    expect(value('fontsize')).toBe('');
    // The fonts of style.fonts and the system fonts.
    const fonts = Array.from(pop.querySelectorAll('[data-field="font"] option')).map(o => (o as HTMLOptionElement).value);
    expect(fonts).toEqual(['', 'montserrat', 'montserrat_semibold', 'pacifico']);
    // Every field has a label.
    for (const input of Array.from(pop.querySelectorAll('[data-field]'))) {
      expect(pop.querySelector(`label[for="${input.id}"]`)).not.toBeNull();
    }
    // The item is outlined.
    expect(item(el, 'temp').hasAttribute('data-selected')).toBe(true);
  });

  it('each change is an edit of that entry and shows on the device', async () => {
    openConfig(CONFIG);
    const el = await device();
    const pop = await openMenu(el, item(el, 'temp'));
    type(pop, 'label', 'Buiten');
    await frame();
    expect(item(el, 'temp').textContent).toContain('Buiten');
    type(pop, 'unit', '°C');
    type(pop, 'font', 'pacifico', 'change');
    type(pop, 'fontsize', '48');
    type(pop, 'width', '30');
    await frame();
    expect(parsed().displayitems[0]).toEqual({
      name: 'temp', page: 1, x: 10, y: 10, width: 30, align: 1, boxtype: 1, fontsize: 48, font: 'pacifico', label: 'Buiten', unit: '°C',
    });
    expect(item(el, 'temp').style.width).toBe('30%');
    // The other entries are untouched.
    expect(getCode().slice(getCode().indexOf('"name": "right"'))).toBe(CONFIG.slice(CONFIG.indexOf('"name": "right"')));

    // Empty size and "Style's font": the keys go.
    type(pop, 'fontsize', '', 'change');
    type(pop, 'font', '', 'change');
    await frame();
    expect(Object.keys(parsed().displayitems[0])).not.toContain('fontsize');
    expect(Object.keys(parsed().displayitems[0])).not.toContain('font');

    // Undo takes one change back.
    undo(getEditorView()!);
    expect(parsed().displayitems[0].font).toBe('pacifico');
  });

  it('a new align keeps the item where it is', async () => {
    openConfig(CONFIG);
    const el = await device();
    const pop = await openMenu(el, item(el, 'temp'));
    type(pop, 'align', '3', 'change');
    await frame();
    // 20 % wide at x = 10: its right edge is 70 % from the right.
    expect(parsed().displayitems[0]).toMatchObject({ align: 3, x: -70, y: 10 });
    expect(item(el, 'temp').style.right).toBe('70%');
  });

  it('Escape closes it and the focus is back on the item; a press outside closes it too', async () => {
    openConfig(CONFIG);
    const el = await device();
    const target = item(el, 'temp');
    await openMenu(el, target);
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    await frame();
    expect(document.querySelector('[data-testid="item-popover"]')).toBeNull();
    expect(document.activeElement).toBe(item(el, 'temp'));

    await openMenu(el, target);
    document.body.dispatchEvent(new Event('pointerdown', { bubbles: true }));
    expect(document.querySelector('[data-testid="item-popover"]')).toBeNull();
  });

  it('the menu key and Shift+F10 open it for the focused item', async () => {
    openConfig(CONFIG);
    const el = await device();
    item(el, 'temp').dispatchEvent(new KeyboardEvent('keydown', { key: 'F10', shiftKey: true, bubbles: true, cancelable: true }));
    await frame();
    await frame();
    expect(document.querySelector('[data-testid="item-popover"]')).not.toBeNull();
    currentPopover()?.close();
    item(el, 'scenes').dispatchEvent(new KeyboardEvent('keydown', { key: 'ContextMenu', bubbles: true, cancelable: true }));
    await frame();
    await frame();
    expect(document.querySelector('[data-testid="item-popover-title"]')?.textContent).toContain('List');
  });

  it('a list: items, rows and colours ("rrggbb"); an image: size and colour ("#rrggbb")', async () => {
    openConfig(CONFIG);
    const el = await device();
    let pop = await openMenu(el, item(el, 'scenes'));
    expect((pop.querySelector('[data-field="items"]') as HTMLTextAreaElement).value).toBe('A\nB');
    type(pop, 'items', 'One\nTwo\nThree', 'change');
    type(pop, 'rows', '3');
    type(pop, 'focuscolor', '#ff0000');
    await frame();
    expect(parsed().lists[0]).toMatchObject({ items: ['One', 'Two', 'Three'], rows: 3, focuscolor: 'ff0000' });
    expect(item(el, 'scenes').textContent).toContain('Three');
    // Reset: the key goes, the style's colour applies again.
    (document.querySelector('[data-clear="focuscolor"]') as HTMLElement).click();
    await frame();
    expect(Object.keys(parsed().lists[0])).not.toContain('focuscolor');

    currentPopover()?.close();
    pop = await openMenu(el, item(el, 'logo'));
    expect(Array.from(pop.querySelectorAll('[data-field]')).map(i => i.getAttribute('data-field'))).toEqual(['width', 'height', 'x', 'y', 'color']);
    type(pop, 'color', '#00ff00');
    type(pop, 'width', '64');
    await frame();
    expect(parsed().images[0]).toMatchObject({ width: 64, color: '#00ff00' });
  });

  it('keys: label and top label are edited the same way', async () => {
    openConfig(CONFIG);
    const el = await device();
    const key = el.querySelector('[data-ref="buttons:0"] button') as HTMLElement;
    const pop = await openMenu(el, key);
    expect(pop.querySelector('[data-testid="item-popover-title"]')?.textContent).toContain('Key');
    type(pop, 'label', 'Louder');
    type(pop, 'toplabel', 'Vol');
    await frame();
    expect(parsed().buttons[0]).toMatchObject({ label: 'Louder', toplabel: 'Vol', position: 1 });
    expect(getCode()).toContain('{ "name": "up", "position": 1, "page": 1, "label": "Louder", "toplabel": "Vol" }');
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    expect(document.activeElement).toBe(key);
  });

  it('Delete asks first (in-app); only "Delete" removes the entry', async () => {
    const confirm = vi.fn();
    (window as unknown as { confirm: unknown }).confirm = confirm;
    openConfig(CONFIG);
    const el = await device();
    let pop = await openMenu(el, item(el, 'temp'));
    (pop.querySelector('[data-testid="item-delete"]') as HTMLElement).click();
    await frame();
    const dialog = document.querySelector('fb-dialog [data-testid="dialog"]') as HTMLElement;
    expect(dialog.textContent).toContain('Delete display item "temp" from config.json?');
    expect(document.querySelector('[data-testid="item-popover"]')).toBeNull();
    (dialog.querySelector('[data-choice="cancel"]') as HTMLElement).click();
    await frame();
    expect(getCode()).toBe(CONFIG);

    pop = await openMenu(el, item(el, 'temp'));
    (pop.querySelector('[data-testid="item-delete"]') as HTMLElement).click();
    await frame();
    (document.querySelector('fb-dialog [data-choice="ok"]') as HTMLElement).click();
    await frame();
    await frame();
    expect(parsed().displayitems.map(d => d.name)).toEqual(['right']);
    expect(item(el, 'temp')).toBeNull();
    expect(confirm).not.toHaveBeenCalled();
    undo(getEditorView()!);
    expect(getCode()).toBe(CONFIG);
  });

  it('right-click on empty display space: "Add text item here" adds a minimal item on the current page', async () => {
    openConfig(CONFIG);
    const el = await device();
    const screen = el.querySelector('[data-screen]') as HTMLElement;
    // 160, 120 device px with the preview's scale (the screen's box is at 0, 0 here).
    screen.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 160 * SCALE, clientY: 120 * SCALE }));
    await frame();
    const menu = document.querySelector('[data-testid="display-menu"]') as HTMLElement;
    expect(menu.getAttribute('role')).toBe('menu');
    (menu.querySelector('[data-testid="menu-add-text"]') as HTMLElement).click();
    await frame();
    await frame();
    await frame();
    expect(parsed().displayitems[2]).toEqual({
      name: 'text1', page: 1, x: 50, y: 50, width: 40, align: 1, boxtype: 1, label: 'Text',
    });
    // Written like the entries before it.
    expect(getCode()).toContain('    },\n    {\n      "name": "text1",\n      "page": 1,\n      "x": 50,');
    expect(item(el, 'text1')).not.toBeNull();
    // Its properties open so the text can be typed.
    expect(document.querySelector('[data-testid="item-popover-title"]')?.textContent).toContain('text1');
  });
});
