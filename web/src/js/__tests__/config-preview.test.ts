import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { Workspace, type EditorAdapter } from '../workspace';
import { _setClientFactory, type S3Like } from '../device-storage';
import { emit } from '../app-state';
import {
  getConfig, getConnectors, getPreviewState, loadDefaultConfig, updateButton, updateDisplayItem,
} from '../device-config';
import { PREVIEW_DEBOUNCE_MS } from '../config-preview';
import '../../components/fb-virtual-device.js';
import '../../components/fb-button-grid.js';
import '../../components/fb-tft-display.js';

function sdkError(name: string, status: number) {
  const err = new Error(name) as Error & { $metadata: { httpStatusCode: number } };
  err.name = name;
  err.$metadata = { httpStatusCode: status };
  return err;
}

/** Minimal S3 "device" (bucket app only); records every call. */
function fakeDevice(initial: Record<string, string>) {
  const files = new Map(Object.entries(initial));
  const calls: string[] = [];
  const client: S3Like = {
    async send(command: unknown) {
      const name = (command as object).constructor.name.replace('Command', '');
      const input = (command as { input: { Key?: string; Body?: string } }).input;
      calls.push(`${name}${input.Key ? ' ' + input.Key : ''}`);
      switch (name) {
        case 'ListObjectsV2':
          return { KeyCount: files.size, IsTruncated: false, Contents: [...files].map(([Key, v]) => ({ Key, Size: v.length })) };
        case 'GetObject': {
          const v = files.get(input.Key!);
          if (v === undefined) throw sdkError('NoSuchKey', 404);
          return { Body: { transformToString: async () => v } };
        }
        case 'PutObject':
          files.set(input.Key!, input.Body ?? '');
          return {};
      }
      throw new Error('unexpected ' + name);
    },
  };
  _setClientFactory(() => client);
  return { files, calls };
}

function fakeEditor(): EditorAdapter & { content: string } {
  const ed = {
    content: '',
    isReady: () => true,
    getCode: () => ed.content,
    open: (content: string) => { ed.content = content; emit('editor-doc-change'); },
  };
  return ed;
}

/** The user typing in the editor. */
function type(ed: { content: string }, text: string) {
  ed.content = text;
  emit('editor-doc-change');
}

const STORED = JSON.stringify({
  info: { connectors: [{ id: 0, type: 2 }, { id: 1, type: 1 }] },
  buttons: [{ position: 3, page: 1, label: 'Open', toplabel: 'Raam' }],
  displayitems: [{ displayitemid: 'temp', x: 0, y: 0, width: 50, label: 'Temp', unit: 'C' }],
});

const EDITED = JSON.stringify({
  info: { connectors: [{ id: 0, type: 2 }, { id: 1, type: 1 }, { id: 2, type: 1 }] },
  buttons: [{ position: 3, page: 1, label: 'Dicht', toplabel: 'Raam' }, { position: 4, page: 2, label: 'Lamp' }],
  displayitems: [{ displayitemid: 'temp', x: 10, y: 20, width: 40, label: 'Temperatuur', unit: 'C' }],
});

let ws: Workspace;

beforeEach(() => {
  ws = new Workspace();
  loadDefaultConfig();
});

afterEach(() => {
  ws.stop();
  _setClientFactory(null);
  vi.useRealTimers();
  document.body.innerHTML = '';
});

/** Start on a device holding `files`, open config.json, then switch to fake timers. */
async function openConfig(files: Record<string, string> = { 'config.json': STORED, 'scripts/main.js': 'print(1);' }) {
  const dev = fakeDevice(files);
  const ed = fakeEditor();
  await ws.start({ editor: ed, confirm: () => true });
  expect(await ws.open('config.json')).toBe(true);
  vi.useFakeTimers();
  return { dev, ed };
}

const label = (pos: number, page = 1) => getConfig().find(p => p.number === page)?.buttons.find(b => b.position === pos)?.label;

describe('live config.json preview', () => {
  it('re-renders the preview from the unsaved text after the debounce, without storing it', async () => {
    const { dev, ed } = await openConfig();
    const callsBefore = dev.calls.length;
    expect(label(3)).toBe('Open');

    type(ed, EDITED);
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS - 1);
    expect(label(3)).toBe('Open');            // still debouncing
    vi.advanceTimersByTime(1);

    expect(label(3)).toBe('Dicht');
    expect(label(4, 2)).toBe('Lamp');
    expect(getConfig().map(p => p.number)).toEqual([1, 2]);
    expect(getConfig()[0].displayItems[0]).toMatchObject({ x: 10, y: 20, width: 40, label: 'Temperatuur', unit: 'C' });
    expect(getConnectors().map(c => c.id)).toEqual([0, 1, 2]);
    expect(getPreviewState()).toBe('live');
    // Nothing went to the device; the stored copy is unchanged.
    expect(dev.calls.length).toBe(callsBefore);
    expect(dev.files.get('config.json')).toBe(STORED);
    expect(ws.snapshot().dirty).toBe(true);
  });

  it('coalesces fast typing into one re-render', async () => {
    const { ed } = await openConfig();
    let events = 0;
    const handler = () => { events++; };
    window.addEventListener('device-config', handler);
    for (let i = 0; i < 5; i++) {
      type(ed, EDITED.replace('Dicht', `Dicht${i}`));
      vi.advanceTimersByTime(50);
    }
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    window.removeEventListener('device-config', handler);
    expect(events).toBe(1);
    expect(label(3)).toBe('Dicht4');
  });

  it('keeps the last valid preview while the text is not valid JSON, flagged stale', async () => {
    const { ed } = await openConfig();
    type(ed, EDITED);
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    const valid = getConfig();

    type(ed, EDITED.slice(0, -5));            // mid-edit
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    expect(getConfig()).toBe(valid);
    expect(label(3)).toBe('Dicht');
    expect(getPreviewState()).toBe('stale');

    type(ed, EDITED.replace('Dicht', 'Half'));
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    expect(label(3)).toBe('Half');
    expect(getPreviewState()).toBe('live');
  });

  it('invalid JSON before any valid edit keeps the stored config', async () => {
    const { ed } = await openConfig();
    type(ed, '{ "buttons": [');
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    expect(label(3)).toBe('Open');
    expect(getPreviewState()).toBe('stale');
  });

  it('switching away without saving restores the stored config (and drops a pending edit)', async () => {
    const { ed, dev } = await openConfig();
    type(ed, EDITED);
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    expect(label(3)).toBe('Dicht');

    type(ed, EDITED.replace('Dicht', 'Pending'));   // debounce still running
    vi.useRealTimers();
    expect(await ws.open('scripts/main.js')).toBe(true);   // confirm(discard) -> yes
    vi.useFakeTimers();
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS * 2);
    expect(label(3)).toBe('Open');
    expect(getConnectors().map(c => c.id)).toEqual([0, 1]);
    expect(getPreviewState()).toBeNull();
    expect(dev.files.get('config.json')).toBe(STORED);

    // Editing a script does not touch the preview.
    type(ed, 'print(2);');
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    expect(label(3)).toBe('Open');
  });

  it('reverting (reopening config.json from storage) restores the stored config', async () => {
    const { ed } = await openConfig();
    type(ed, EDITED);
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    expect(label(3)).toBe('Dicht');
    vi.useRealTimers();
    expect(await ws.open('config.json', { force: true })).toBe(true);
    expect(ed.content).toBe(STORED);
    expect(label(3)).toBe('Open');
    expect(getPreviewState()).toBeNull();
  });

  it('saving stores the edit and the preview stays on it', async () => {
    const { ed, dev } = await openConfig();
    type(ed, EDITED);
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    vi.useRealTimers();
    expect(await ws.save()).toBe(true);
    expect(dev.files.get('config.json')).toBe(EDITED);
    expect(label(3)).toBe('Dicht');
    expect(getPreviewState()).toBeNull();     // the stored config now
    // Leaving the file keeps the saved config.
    expect(await ws.open('scripts/main.js')).toBe(true);
    expect(label(3)).toBe('Dicht');
  });

  it('keeps runtime values (display value, key LED) across a re-render', async () => {
    const { ed } = await openConfig();
    updateDisplayItem(1, 0, { value: '21.5' });
    updateButton(1, 3, { ledOn: true, ledColor: 'rgb(0,255,0)' });
    type(ed, EDITED);
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    expect(getConfig()[0].displayItems[0]).toMatchObject({ label: 'Temperatuur', value: '21.5' });
    expect(getConfig()[0].buttons[0]).toMatchObject({ label: 'Dicht', ledOn: true, ledColor: 'rgb(0,255,0)' });
  });

  /** Wait for the device and its key/display children to render. */
  async function settled(el: Element) {
    await (el as unknown as { updateComplete: Promise<boolean> }).updateComplete;
    for (const c of el.querySelectorAll('fb-button-grid, fb-tft-display')) {
      await (c as unknown as { updateComplete: Promise<boolean> }).updateComplete;
    }
  }

  it('the virtual device shows the draft and says nothing about it: not "unsaved", not "invalid JSON" (the editor marks that)', async () => {
    const { ed } = await openConfig();
    const el = document.createElement('fb-virtual-device') as HTMLElement & { updateComplete: Promise<boolean> };
    document.body.appendChild(el);
    await el.updateComplete;
    expect(el.querySelector('[data-testid="preview-hint"]')).toBeNull();

    type(ed, EDITED);
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    await settled(el);
    // A valid draft, not saved: drawn, and nothing on the device says "unsaved".
    expect(getPreviewState()).toBe('live');
    expect(el.querySelector('[data-testid="preview-hint"]')).toBeNull();
    expect(el.textContent).not.toMatch(/unsaved/i);
    expect(label(3)).toBe('Dicht');
    // The draft is what is laid out: its display item, by its id (labels are data and are not shown).
    expect(el.querySelector('fb-tft-display [data-outline]')?.textContent?.trim()).toBe('#temp');

    type(ed, '{');
    vi.advanceTimersByTime(PREVIEW_DEBOUNCE_MS);
    await settled(el);
    expect(getPreviewState()).toBe('stale');
    expect(el.querySelector('[data-testid="preview-hint"]')).toBeNull();
    expect(el.textContent).not.toMatch(/unsaved|invalid/i);
    // The last valid draft stays.
    expect(el.querySelector('fb-tft-display [data-outline]')?.textContent?.trim()).toBe('#temp');
  });
});
