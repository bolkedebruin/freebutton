import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { Workspace, deviceHttpBase, LOCAL_MESSAGE, type EditorAdapter } from '../workspace';
import { _setClientFactory, REBOOT_FIELD, type S3Like } from '../device-storage';
import { emit } from '../app-state';
import { getConfig, getConnectors, loadDefaultConfig } from '../device-config';
import { virtualFS } from '../virtual-fs';
import { MAIN_SCRIPT_TEMPLATE } from '../fs-rules';

function sdkError(name: string, status: number) {
  const err = new Error(name) as Error & { $metadata: { httpStatusCode: number } };
  err.name = name;
  err.$metadata = { httpStatusCode: status };
  return err;
}

/**
 * In-memory S3 "device" behind the SDK wrapper: `initial` is bucket `app`,
 * `readOnly` the other buckets (listing only). Calls on another bucket than
 * `app` are logged with the bucket name ("ListObjectsV2 web").
 */
function fakeDevice(initial: Record<string, string>, readOnly: Record<string, Record<string, string>> = {}) {
  const files = new Map(Object.entries(initial));
  const calls: string[] = [];
  const bases: string[] = [];
  const client: S3Like = {
    async send(command: unknown) {
      const name = (command as object).constructor.name.replace('Command', '');
      const input = (command as { input: { Bucket?: string; Key?: string; Body?: string } }).input;
      const bucket = input.Bucket ?? '';
      calls.push(`${name}${bucket !== 'app' ? ' ' + bucket : ''}${input.Key ? ' ' + input.Key : ''}`);
      if (bucket !== 'app') {
        const other = readOnly[bucket];
        if (!other) throw sdkError('NoSuchBucket', 404);
        if (name !== 'ListObjectsV2') throw new Error(`unexpected ${name} on ${bucket}`);
        return {
          KeyCount: Object.keys(other).length, IsTruncated: false,
          Contents: Object.entries(other).map(([Key, v]) => ({ Key, Size: v.length })),
        };
      }
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
          files.set(input.Key!, input.Body ?? '');
          return input.Key === 'config.json' ? { [REBOOT_FIELD]: true } : {};
        case 'DeleteObject':
          files.delete(input.Key!);
          return {};
      }
      throw new Error('unexpected ' + name);
    },
  };
  _setClientFactory((base) => { bases.push(base); return client; });
  return { files, calls, bases };
}

function fakeEditor(): EditorAdapter & { content: string; kind: string } {
  const ed = {
    content: '',
    kind: '',
    isReady: () => true,
    getCode: () => ed.content,
    open: (content: string, kind: string) => {
      ed.content = content;
      ed.kind = kind;
      emit('editor-doc-change'); // like the real editor
    },
  };
  return ed;
}

/** Simulate the user typing in the editor. */
function type(ed: { content: string }, text: string) {
  ed.content = text;
  emit('editor-doc-change');
}

const CONFIG = JSON.stringify({
  info: { connectors: [{ id: 2, type: 2 }, { id: 3, type: 1 }] },
  buttons: [{ position: 7, page: 1, label: 'Open', toplabel: 'Raam' }],
});

let ws: Workspace;
let confirms: string[];

beforeEach(() => {
  ws = new Workspace();
  confirms = [];
  loadDefaultConfig();
  for (const f of virtualFS.getFileList()) virtualFS.deleteFile(f.name);
});

afterEach(() => {
  ws.stop();
  _setClientFactory(null);
});

function start(editor = fakeEditor(), answer = true) {
  return ws.start({ editor, confirm: (m) => { confirms.push(m); return answer; } }).then(() => editor);
}

describe('workspace on a device', () => {
  it('starts with a template main.js when it does not exist, and loads config.json', async () => {
    const dev = fakeDevice({ 'config.json': CONFIG });
    const ed = await start();
    const snap = ws.snapshot();
    expect(snap.mode).toBe('device');
    expect(snap.currentKey).toBe('scripts/main.js');
    expect(snap.isNew).toBe(true);
    expect(snap.dirty).toBe(false);
    expect(ed.content).toBe(MAIN_SCRIPT_TEMPLATE);
    expect(ed.kind).toBe('js');
    // Button preview reflects config.json (toplabel), layout from info.connectors.
    expect(getConfig()[0].buttons[0]).toMatchObject({ position: 7, label: 'Open', topLabel: 'Raam' });
    expect(getConnectors()).toEqual([{ id: 2, type: 'display' }, { id: 3, type: 'bar' }]);
    // One list, then config + main.js reads: nothing more.
    expect(dev.calls).toEqual(['ListObjectsV2', 'GetObject config.json', 'GetObject scripts/main.js']);
  });

  it('opens an existing main.js', async () => {
    fakeDevice({ 'scripts/main.js': 'print(1);' });
    const ed = await start();
    expect(ed.content).toBe('print(1);');
    expect(ws.snapshot().isNew).toBe(false);
  });

  it('marks edits dirty and saves the source to scripts/main.js', async () => {
    const dev = fakeDevice({});
    const ed = await start();
    type(ed, 'print("hi");');
    expect(ws.snapshot().dirty).toBe(true);

    expect(await ws.save()).toBe(true);
    expect(dev.files.get('scripts/main.js')).toBe('print("hi");');
    const snap = ws.snapshot();
    expect(snap.dirty).toBe(false);
    expect(snap.isNew).toBe(false);
    expect(snap.files.map(f => f.key)).toEqual(['scripts/main.js']);
    expect(dev.calls.slice(-2)).toEqual(['PutObject scripts/main.js', 'ListObjectsV2']);
  });

  it('save-request events save the open file', async () => {
    const dev = fakeDevice({});
    const ed = await start();
    type(ed, 'x();');
    emit('save-request');
    await new Promise(r => setTimeout(r, 0));
    await new Promise(r => setTimeout(r, 0));
    expect(dev.files.get('scripts/main.js')).toBe('x();');
  });

  it('validates config.json before saving and refuses invalid JSON', async () => {
    const dev = fakeDevice({ 'config.json': CONFIG });
    const ed = await start();
    expect(await ws.open('config.json')).toBe(true);
    expect(ed.kind).toBe('json');
    type(ed, '{"buttons": [}');
    expect(await ws.save()).toBe(false);
    expect(ws.snapshot().message).toMatch(/Not saved: Invalid JSON/);
    expect(dev.calls).not.toContain('PutObject config.json');
    expect(dev.files.get('config.json')).toBe(CONFIG);
  });

  it('saving config.json updates the preview and suggests a reboot', async () => {
    const dev = fakeDevice({ 'config.json': CONFIG });
    const ed = await start();
    await ws.open('config.json');
    let suggested = 0;
    const handler = () => { suggested++; };
    window.addEventListener('reboot-suggested', handler);
    type(ed, JSON.stringify({ buttons: [{ position: 8, label: 'Dicht', topLabel: 'Oud' }] }));
    expect(await ws.save()).toBe(true);
    window.removeEventListener('reboot-suggested', handler);
    expect(suggested).toBe(1);
    expect(dev.files.get('config.json')).toContain('Dicht');
    expect(getConfig()[0].buttons[0]).toMatchObject({ position: 8, label: 'Dicht', topLabel: 'Oud' });
  });

  it('asks before discarding unsaved changes when switching files', async () => {
    fakeDevice({ 'config.json': CONFIG, 'scripts/main.js': 'a' });
    const ed = await start(fakeEditor(), false);
    type(ed, 'changed');
    expect(await ws.open('config.json')).toBe(false);
    expect(confirms[0]).toMatch(/Discard unsaved changes/);
    expect(ws.snapshot().currentKey).toBe('scripts/main.js');
    expect(ed.content).toBe('changed');
  });

  it('refuses to open other buckets\' paths and to delete config.json', async () => {
    const dev = fakeDevice({ 'config.json': CONFIG });
    await start();
    expect(await ws.open('www/index.html.gz')).toBe(false);
    expect(await ws.open('scripts/examples/button.js')).toBe(false);
    expect(await ws.remove('config.json')).toBe(false);
    expect(dev.calls.some(c => c.includes('www/') || c.includes('examples'))).toBe(false);
    expect(dev.calls).not.toContain('DeleteObject config.json');
  });

  it('lists read-only buckets only when asked, once, and again on refresh', async () => {
    const dev = fakeDevice({ 'config.json': CONFIG }, {
      examples: { 'button.js': '// b', 'blink.js': '// bl' },
      web: { 'index.html.gz': 'xxxx' },
      fonts: { 'mnt_medium_14.bin': 'ff' },
    });
    await start();
    expect(dev.calls.some(c => / (examples|web|fonts)/.test(c))).toBe(false);
    expect(ws.snapshot().readOnly).toEqual({});

    await ws.loadReadOnly(['examples']);
    expect(ws.snapshot().readOnly?.examples?.map(f => f.key)).toEqual(['blink.js', 'button.js']);
    await ws.loadReadOnly(['examples', 'web', 'fonts']);
    expect(dev.calls.filter(c => c.startsWith('ListObjectsV2 '))).toEqual([
      'ListObjectsV2 examples', 'ListObjectsV2 web', 'ListObjectsV2 fonts',
    ]);
    expect(ws.snapshot().readOnly?.web).toEqual([{ key: 'index.html.gz', size: 4 }]);
    // Never downloaded.
    expect(dev.calls.some(c => c.startsWith('GetObject ') && / (examples|web|fonts) /.test(c))).toBe(false);

    dev.calls.length = 0;
    await ws.refresh();
    expect(dev.calls).toEqual(['ListObjectsV2', 'ListObjectsV2 examples', 'ListObjectsV2 web', 'ListObjectsV2 fonts']);
  });

  it('explains a firmware without the app bucket', async () => {
    const client: S3Like = { send: async () => { throw sdkError('NoSuchBucket', 404); } };
    _setClientFactory(() => client);
    await start();
    expect(ws.snapshot().mode).toBe('device');
    expect(ws.snapshot().message).toMatch(/no "app" storage bucket/);
  });

  it('creates a file in scripts/ (HEAD first, never clobbering) and opens it', async () => {
    const dev = fakeDevice({});
    const ed = await start();
    expect(await ws.create('util')).toBe(true);
    expect(dev.calls).toContain('HeadObject scripts/util.js');
    expect(dev.files.get('scripts/util.js')).toBe('');
    expect(ws.snapshot().currentKey).toBe('scripts/util.js');
    expect(ed.content).toBe('');
  });

  it('opens instead of overwriting when the new file already exists on the device', async () => {
    const dev = fakeDevice({});
    const ed = await start();
    dev.files.set('scripts/util.js', 'keep me'); // appeared after our last list
    expect(await ws.create('util.js')).toBe(true);
    expect(dev.files.get('scripts/util.js')).toBe('keep me');
    expect(ed.content).toBe('keep me');
  });

  it('deletes a file after confirmation and falls back to main.js', async () => {
    const dev = fakeDevice({ 'scripts/main.js': 'm', 'scripts/old.js': 'o' });
    const ed = await start();
    await ws.open('scripts/old.js');
    expect(await ws.remove('scripts/old.js')).toBe(true);
    expect(confirms.some(m => m.includes('Delete /scripts/old.js'))).toBe(true);
    expect(dev.files.has('scripts/old.js')).toBe(false);
    expect(ws.snapshot().currentKey).toBe('scripts/main.js');
    expect(ed.content).toBe('m');
  });

  it('re-probes when the REPL connects to a different device, not on reconnects', async () => {
    const dev = fakeDevice({});
    await start();
    const lists = () => dev.calls.filter(c => c === 'ListObjectsV2').length;
    expect(lists()).toBe(1);
    emit('connection-change', { connected: true, url: `ws://${location.host}` }); // same origin
    await new Promise(r => setTimeout(r, 0));
    expect(lists()).toBe(1);
    emit('connection-change', { connected: true, url: 'ws://10.0.0.9' });
    await new Promise(r => setTimeout(r, 0));
    await new Promise(r => setTimeout(r, 0));
    expect(lists()).toBe(2);
    expect(ws.deviceBase).toBe('http://10.0.0.9');
    expect(dev.bases).toContain('http://10.0.0.9');
  });
});

describe('workspace without device storage', () => {
  it('falls back to the browser store and keeps working', async () => {
    _setClientFactory(() => ({ send: async () => { throw new TypeError('Failed to fetch'); } }));
    const ed = await start();
    const snap = ws.snapshot();
    expect(snap.mode).toBe('local');
    expect(snap.message).toBe(LOCAL_MESSAGE);
    expect(ed.content).toBe(MAIN_SCRIPT_TEMPLATE);
    // Sample config stays when there is no device config.
    expect(getConfig()[0].buttons[0].label).toBe('Tolomeo');

    type(ed, 'print(2);');
    expect(await ws.save()).toBe(true);
    expect(virtualFS.readText('scripts/main.js')).toBe('print(2);');
    expect(ws.snapshot().files.map(f => f.key)).toEqual(['scripts/main.js']);
  });

  it('does not offer a reboot in local mode', async () => {
    _setClientFactory(() => ({ send: async () => { throw new TypeError('Failed to fetch'); } }));
    expect(await (await start(), ws.reboot())).toBe(false);
  });
});

describe('deviceHttpBase', () => {
  it('maps the REPL URL to the HTTP base, "" for same origin', () => {
    expect(deviceHttpBase('ws://10.0.0.5', 'http://localhost:5173')).toBe('http://10.0.0.5');
    expect(deviceHttpBase('wss://dev.local/', 'http://x')).toBe('https://dev.local');
    expect(deviceHttpBase('ws://freebutton.local', 'http://freebutton.local')).toBe('');
  });
});
