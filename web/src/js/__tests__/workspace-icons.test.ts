import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { Workspace, type EditorAdapter } from '../workspace';
import { _setClientFactory, type S3Like } from '../device-storage';
import { emit } from '../app-state';
import { loadDefaultConfig, getConfig } from '../device-config';
import { _clearIconSources, iconSources } from '../icon-sync';

function sdkError(name: string, status: number) {
  const err = new Error(name) as Error & { $metadata: { httpStatusCode: number } };
  err.name = name;
  err.$metadata = { httpStatusCode: status };
  return err;
}

const SVG = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/></svg>';

/** Device with bucket app (text) and, unless withImages is false, bucket images (bytes). */
function fakeDevice(app: Record<string, string>, withImages = true) {
  const files = new Map(Object.entries(app));
  const images = new Map<string, Uint8Array>();
  const calls: string[] = [];
  const client: S3Like = {
    async send(command: unknown) {
      const name = (command as object).constructor.name.replace('Command', '');
      const input = (command as { input: { Bucket: string; Key?: string; Body?: string | Uint8Array; ContentType?: string } }).input;
      calls.push(`${name} ${input.Bucket}${input.Key ? '/' + input.Key : ''}`);
      if (input.Bucket === 'images') {
        if (!withImages) throw sdkError('NoSuchBucket', 404);
        if (name === 'ListObjectsV2') {
          return {
            KeyCount: images.size, IsTruncated: false,
            Contents: [...images.entries()].map(([Key, v]) => ({ Key, Size: v.length })),
          };
        }
        if (name === 'PutObject') {
          expect(input.Body).toBeInstanceOf(Uint8Array);
          expect(input.ContentType).toBe('application/octet-stream');
          images.set(input.Key!, input.Body as Uint8Array);
          return {};
        }
        if (name === 'DeleteObject') { images.delete(input.Key!); return {}; }
        throw new Error('unexpected ' + name);
      }
      if (input.Bucket !== 'app') throw sdkError('NoSuchBucket', 404);
      switch (name) {
        case 'ListObjectsV2':
          return {
            KeyCount: files.size, IsTruncated: false,
            Contents: [...files.entries()].map(([Key, v]) => ({ Key, Size: v.length })),
          };
        case 'GetObject': {
          const v = files.get(input.Key!);
          if (v === undefined) throw sdkError('NoSuchKey', 404);
          return { Body: { transformToString: async () => v } };
        }
        case 'HeadObject':
          if (!files.has(input.Key!)) throw sdkError('NotFound', 404);
          return {};
        case 'PutObject':
          if (input.Key!.endsWith('.svg')) expect(input.ContentType).toBe('image/svg+xml');
          files.set(input.Key!, String(input.Body ?? ''));
          return {};
        case 'DeleteObject':
          files.delete(input.Key!);
          return {};
      }
      throw new Error('unexpected ' + name);
    },
  };
  _setClientFactory(() => client);
  return { files, images, calls };
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

function type(ed: { content: string }, text: string) {
  ed.content = text;
  emit('editor-doc-change');
}

const rasterize = async (_svg: string, w: number, h: number) => new Uint8ClampedArray(w * h * 4).fill(255);

let ws: Workspace;
let lines: string[];
let questions: string[];
let answer = true;
const onLine = (e: Event) => lines.push((e as CustomEvent<{ text: string }>).detail.text);

beforeEach(() => {
  ws = new Workspace();
  lines = [];
  questions = [];
  answer = true;
  loadDefaultConfig();
  _clearIconSources();
  window.addEventListener('console-line', onLine);
});

afterEach(() => {
  window.removeEventListener('console-line', onLine);
  ws.stop();
  _setClientFactory(null);
});

async function start() {
  const ed = fakeEditor();
  await ws.start({ editor: ed, confirm: (q) => { questions.push(q); return answer; }, rasterize });
  return ed;
}

const ICON_CONFIG = {
  icons: { rain: '/icons/rain.svg' },
  buttons: [{ position: 1, icon: 'rain' }],
};

describe('workspace icons', () => {
  it('loads icon sources for the preview at start', async () => {
    fakeDevice({ 'config.json': JSON.stringify(ICON_CONFIG), 'icons/rain.svg': SVG });
    await start();
    expect(iconSources().get('rain')).toBe(SVG);
    expect(getConfig()[0].buttons[0]).toMatchObject({ icon: 'rain', iconSize: 48 });
  });

  it('pushes rasters after an SVG that config.json uses is uploaded again', async () => {
    const dev = fakeDevice({ 'config.json': JSON.stringify(ICON_CONFIG), 'icons/rain.svg': SVG });
    await start();
    expect([...dev.images.keys()]).toEqual([]);
    expect(await ws.uploadSvg({ name: 'rain.svg', text: async () => SVG })).toBe('icons/rain.svg');
    await ws.pendingIconSync;
    expect([...dev.images.keys()]).toEqual(['ico-rain-48x48.bin']);
  });

  it('syncs rasters after config.json is saved, and deletes them when icons are removed', async () => {
    const dev = fakeDevice({ 'config.json': '{}', 'icons/rain.svg': SVG });
    const ed = await start();
    await ws.open('config.json');
    type(ed, JSON.stringify(ICON_CONFIG));
    expect(await ws.save()).toBe(true);
    await ws.pendingIconSync;
    expect([...dev.images.keys()]).toEqual(['ico-rain-48x48.bin']);
    expect(dev.images.get('ico-rain-48x48.bin')!.length).toBe(12 + 48 * 48); // white -> A8
    expect(lines.some(l => /^Icons: 1 raster .* 1 uploaded/.test(l))).toBe(true);

    type(ed, JSON.stringify({ buttons: [{ position: 1 }] }));
    expect(await ws.save()).toBe(true);
    await ws.pendingIconSync;
    expect(dev.images.size).toBe(0);
  });

  it('never touches bucket images for a config without icons', async () => {
    const dev = fakeDevice({ 'config.json': '{}' }, false);
    const ed = await start();
    await ws.open('config.json');
    type(ed, JSON.stringify({ buttons: [{ position: 1, label: 'x' }] }));
    expect(await ws.save()).toBe(true);
    expect(ws.pendingIconSync).toBeNull();
    expect(dev.calls.filter(c => c.includes('images'))).toEqual([]);
  });

  it('says the firmware is too old when the device has no images bucket', async () => {
    fakeDevice({ 'config.json': '{}', 'icons/rain.svg': SVG }, false);
    const ed = await start();
    await ws.open('config.json');
    type(ed, JSON.stringify(ICON_CONFIG));
    expect(await ws.save()).toBe(true);
    await ws.pendingIconSync;
    expect(ws.snapshot().message).toBe('Icons not synced: this firmware has no "images" storage bucket (update the device firmware)');
  });

  it('manual sync reads the stored config.json', async () => {
    const dev = fakeDevice({ 'config.json': JSON.stringify(ICON_CONFIG), 'icons/rain.svg': SVG });
    await start();
    const result = await ws.syncIcons();
    expect(result?.uploaded).toEqual(['ico-rain-48x48.bin']);
    expect(dev.images.has('ico-rain-48x48.bin')).toBe(true);
    expect(ws.snapshot().iconSyncing).toBe(false);
  });

  it('queues a sync requested while one runs', async () => {
    const dev = fakeDevice({ 'config.json': JSON.stringify(ICON_CONFIG), 'icons/rain.svg': SVG });
    await start();
    const first = ws.syncIcons();
    expect(ws.snapshot().iconSyncing).toBe(true);
    const second = ws.syncIcons(JSON.stringify({ ...ICON_CONFIG, buttons: [{ position: 1, icon: 'rain', iconsize: 20 }] }));
    expect(second).toBe(first);
    await first;
    expect([...dev.images.keys()]).toEqual(['ico-rain-20x20.bin']);
  });

  it('uploads an SVG from disk to icons/', async () => {
    const dev = fakeDevice({ 'config.json': '{}' });
    await start();
    const key = await ws.uploadSvg({ name: 'Weather Rain.svg', text: async () => SVG });
    expect(key).toBe('icons/Weather_Rain.svg');
    expect(dev.files.get('icons/Weather_Rain.svg')).toBe(SVG);
    expect(ws.snapshot().files.map(f => f.key)).toContain('icons/Weather_Rain.svg');

    expect(await ws.uploadSvg({ name: 'x.svg', text: async () => '<html/>' })).toBeNull();
    expect(ws.snapshot().message).toBe('Cannot upload x.svg: not an SVG document');
    expect(await ws.uploadSvg({ name: 'x.png', text: async () => SVG })).toBeNull();
  });

  it('adds the uploaded SVG to config.json "icons" after asking, keeping the indentation', async () => {
    const original = '{\n    "buttons": [\n        {"position": 1, "icon": "Weather_Rain"}\n    ]\n}\n';
    const dev = fakeDevice({ 'config.json': original });
    await start();
    const key = (await ws.uploadSvg({ name: 'Weather Rain.svg', text: async () => SVG }))!;
    expect(await ws.addIconToConfig(key)).toBe(true);
    expect(questions).toEqual(['Add icon "Weather_Rain" (/icons/Weather_Rain.svg) to config.json "icons"?']);
    const stored = dev.files.get('config.json')!;
    expect(JSON.parse(stored)).toEqual({
      buttons: [{ position: 1, icon: 'Weather_Rain' }],
      icons: { Weather_Rain: '/icons/Weather_Rain.svg' },
    });
    expect(stored).toContain('\n    "icons": {\n        "Weather_Rain"');
    // The config now uses the icon: its raster is synced.
    await ws.pendingIconSync;
    expect([...dev.images.keys()]).toEqual(['ico-Weather_Rain-48x48.bin']);

    // Same entry again: nothing to ask, nothing written.
    const puts = dev.calls.filter(c => c === 'PutObject app/config.json').length;
    expect(await ws.addIconToConfig(key)).toBe(true);
    expect(questions).toHaveLength(1);
    expect(dev.calls.filter(c => c === 'PutObject app/config.json')).toHaveLength(puts);
  });

  it('asks before changing an existing entry and keeps config.json when declined', async () => {
    const original = JSON.stringify({ icons: { rain: '/old/rain.svg' } });
    const dev = fakeDevice({ 'config.json': original });
    await start();
    answer = false;
    expect(await ws.addIconToConfig('icons/rain.svg')).toBe(false);
    expect(questions).toEqual(['Change icon "rain" in config.json from "/old/rain.svg" to /icons/rain.svg?']);
    expect(dev.files.get('config.json')).toBe(original);
  });

  it('does not touch config.json while it has unsaved edits, and refreshes a clean open one', async () => {
    const dev = fakeDevice({ 'config.json': '{}' });
    const ed = await start();
    await ws.open('config.json');
    type(ed, '{"buttons": []}');
    expect(await ws.addIconToConfig('icons/rain.svg')).toBe(false);
    expect(ws.snapshot().message).toBe('config.json has unsaved changes: add "icons": {"rain": "/icons/rain.svg"} yourself');
    expect(dev.files.get('config.json')).toBe('{}');

    await ws.open('config.json', { force: true });
    expect(await ws.addIconToConfig('icons/rain.svg')).toBe(true);
    expect(JSON.parse(ed.content)).toEqual({ icons: { rain: '/icons/rain.svg' } });
    expect(ws.snapshot().dirty).toBe(false);
  });
});
