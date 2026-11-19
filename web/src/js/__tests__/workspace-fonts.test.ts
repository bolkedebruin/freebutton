import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { Workspace, type EditorAdapter, type FontUpload } from '../workspace';
import { _setClientFactory, type S3Like } from '../device-storage';
import { emit } from '../app-state';
import { getFontTable, loadDefaultConfig } from '../device-config';
import { fontUploadKey, fontNameForKey } from '../fs-rules';

function sdkError(name: string, status: number) {
  const err = new Error(name) as Error & { $metadata: { httpStatusCode: number } };
  err.name = name;
  err.$metadata = { httpStatusCode: status };
  return err;
}

/** The smallest sfnt ttf-check accepts: the required tables, a (3,1) format 4 cmap. */
function tinyTtf(): Uint8Array {
  const tags = ['cmap', 'glyf', 'head', 'hhea', 'hmtx', 'loca', 'maxp'];
  const cmap = [0, 0, 0, 1, 0, 3, 0, 1, 0, 0, 0, 12, 0, 4, 0, 0, 0, 0, 0, 0];
  const maxp = [0, 1, 0, 0, 0, 5];
  const out = [0, 1, 0, 0, 0, tags.length, 0, 0, 0, 0, 0, 0];
  let off = 12 + 16 * tags.length;
  const data: number[] = [];
  for (const t of tags) {
    const body = t === 'cmap' ? cmap : t === 'maxp' ? maxp : [];
    out.push(...[...t].map(c => c.charCodeAt(0)), 0, 0, 0, 0, 0, 0, off >> 8, off & 255, 0, 0, 0, body.length);
    data.push(...body);
    off += body.length;
  }
  return new Uint8Array([...out, ...data]);
}

function upload(name: string, bytes: Uint8Array): FontUpload {
  return { name, size: bytes.length, arrayBuffer: async () => bytes.slice().buffer as ArrayBuffer };
}

function fakeDevice(app: Record<string, string>) {
  const files = new Map(Object.entries(app));
  const fonts = new Map<string, Uint8Array>([['medium.ttf', new Uint8Array(4)], ['icons.ttf', new Uint8Array(4)]]);
  const calls: string[] = [];
  const client: S3Like = {
    async send(command: unknown) {
      const name = (command as object).constructor.name.replace('Command', '');
      const input = (command as { input: { Bucket: string; Key?: string; Body?: string | Uint8Array; ContentType?: string } }).input;
      calls.push(`${name} ${input.Bucket}${input.Key ? '/' + input.Key : ''}`);
      if (input.Bucket === 'fonts') {
        if (name === 'ListObjectsV2') {
          return { KeyCount: fonts.size, IsTruncated: false, Contents: [...fonts.entries()].map(([Key, v]) => ({ Key, Size: v.length, ETag: '"e"' })) };
        }
        if (name === 'PutObject') {
          expect(input.Body).toBeInstanceOf(Uint8Array);
          expect(input.ContentType).toBe('font/ttf');
          fonts.set(input.Key!, input.Body as Uint8Array);
          return {};
        }
        if (name === 'DeleteObject') { fonts.delete(input.Key!); return {}; }
        throw new Error('unexpected ' + name);
      }
      if (input.Bucket !== 'app') throw sdkError('NoSuchBucket', 404);
      switch (name) {
        case 'ListObjectsV2':
          return { KeyCount: files.size, IsTruncated: false, Contents: [...files.entries()].map(([Key, v]) => ({ Key, Size: v.length })) };
        case 'GetObject': {
          const v = files.get(input.Key!);
          if (v === undefined) throw sdkError('NoSuchKey', 404);
          return { Body: { transformToString: async () => v } };
        }
        case 'HeadObject':
          if (!files.has(input.Key!)) throw sdkError('NotFound', 404);
          return {};
        case 'PutObject':
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
  return { files, fonts, calls };
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

let ws: Workspace;
let lines: { text: string; type: string }[];
let questions: string[];
let answer = true;
const onLine = (e: Event) => lines.push((e as CustomEvent<{ text: string; type: string }>).detail);

beforeEach(() => {
  ws = new Workspace();
  lines = [];
  questions = [];
  answer = true;
  loadDefaultConfig();
  window.addEventListener('console-line', onLine);
});

afterEach(() => {
  window.removeEventListener('console-line', onLine);
  ws.stop();
  _setClientFactory(null);
});

async function start() {
  const ed = fakeEditor();
  await ws.start({ editor: ed, confirm: (q) => { questions.push(q); return answer; } });
  return ed;
}

describe('font file names', () => {
  it('fontUploadKey makes a flat media key with a lower-case extension', () => {
    expect(fontUploadKey('Roboto-Regular.ttf')).toBe('Roboto-Regular.ttf');
    expect(fontUploadKey('C:\\fonts\\My Font (1).TTF')).toBe('My_Font_1.ttf');
    expect(fontUploadKey('a..b.otf')).toBe('a.b.otf');
    expect(fontUploadKey('_.ttf')).toBe('font.ttf');
    expect(fontUploadKey(`${'x'.repeat(120)}.ttf`)!.length).toBe(96);
    expect(fontUploadKey('font.woff2')).toBeNull();
  });

  it('fontNameForKey gives a valid config name', () => {
    expect(fontNameForKey('Roboto-Regular.ttf')).toBe('Roboto_Regular');
    expect(fontNameForKey('3270.otf')).toBe('f_3270');
    expect(fontNameForKey(`${'a'.repeat(40)}.ttf`)).toBe('a'.repeat(24));
  });
});

describe('workspace fonts', () => {
  it('uploads a checked font to bucket fonts, then adds the config entry after asking', async () => {
    const dev = fakeDevice({ 'config.json': '{\n  "buttons": []\n}\n' });
    await start();
    const key = await ws.uploadFont(upload('Roboto Mono.ttf', tinyTtf()));
    expect(key).toBe('Roboto_Mono.ttf');
    expect(dev.fonts.has('Roboto_Mono.ttf')).toBe(true);
    expect(ws.snapshot().readOnly?.fonts?.map(f => f.key)).toContain('Roboto_Mono.ttf');

    expect(await ws.addFontToConfig(key!)).toBe(true);
    expect(questions[questions.length - 1]).toBe('Add font "Roboto_Mono" (Roboto_Mono.ttf) to config.json "fonts"?');
    // "fonts" is a top-level array.
    expect(JSON.parse(dev.files.get('config.json')!)).toEqual({ buttons: [], fonts: [{ name: 'Roboto_Mono', src: 'Roboto_Mono.ttf' }] });
    expect(dev.files.get('config.json')!.startsWith('{\n  "buttons"')).toBe(true);
    // The preview's font table knows it now.
    expect(getFontTable().faces.map(f => f.name)).toContain('Roboto_Mono');
    // Already there: nothing to ask.
    const asked = questions.length;
    expect(await ws.addFontToConfig(key!)).toBe(true);
    expect(questions.length).toBe(asked);
  });

  it('adds to an existing "fonts" keeping its entries and the other sections; a system font file needs no entry', async () => {
    const cfg = { fonts: [{ name: 'mono', src: 'mono.ttf' }], styles: { big: { 'font-size': 48 } } };
    const dev = fakeDevice({ 'config.json': JSON.stringify(cfg) });
    await start();
    expect(await ws.addFontToConfig('roboto.ttf')).toBe(true);
    expect(JSON.parse(dev.files.get('config.json')!)).toEqual({
      fonts: [{ name: 'mono', src: 'mono.ttf' }, { name: 'roboto', src: 'roboto.ttf' }], styles: { big: { 'font-size': 48 } },
    });
    const asked = questions.length;
    expect(await ws.addFontToConfig('medium.ttf')).toBe(true);
    expect(questions.length).toBe(asked);
  });

  it('does not add to "fonts" that is not an array, and says what to add', async () => {
    const text = JSON.stringify({ fonts: { roboto: 'roboto.ttf' } });
    const dev = fakeDevice({ 'config.json': text });
    await start();
    expect(await ws.addFontToConfig('roboto.ttf')).toBe(false);
    expect(questions).toEqual([]);
    expect(lines[lines.length - 1]!.text).toBe('config.json "fonts" is not an array: add "fonts": [{"name": "roboto", "src": "roboto.ttf"}] yourself');
    expect(dev.files.get('config.json')).toBe(text);
  });

  it('a "style" block is not read, and a config that has one is not changed', async () => {
    const text = JSON.stringify({ style: { fonts: [{ name: 'mono', src: 'mono.ttf' }] } });
    const dev = fakeDevice({ 'config.json': text });
    await start();
    // The font of the block is not in the table.
    expect(getFontTable().faces.map(f => f.name)).not.toContain('mono');
    expect(await ws.addFontToConfig('roboto.ttf')).toBe(false);
    expect(lines[lines.length - 1]!.text).toBe(
      'config.json not changed: config.json: "style" is no longer read: use the top-level "fonts" and "styles"');
    expect(dev.files.get('config.json')).toBe(text);
    // Nothing in the block is removed either.
    expect(await ws.removeFontFromConfig('mono.ttf')).toBe(true);
    expect(dev.files.get('config.json')).toBe(text);
  });

  it('refuses a non-TrueType or too large file before any request', async () => {
    const dev = fakeDevice({});
    await start();
    const before = dev.calls.length;
    expect(await ws.uploadFont(upload('x.otf', new TextEncoder().encode('OTTO\u0000\u0001\u0000\u0000xxxxxxxxxxxx')))).toBeNull();
    expect(lines[lines.length - 1]!.text).toContain('CFF');
    expect(await ws.uploadFont(upload('x.woff2', tinyTtf()))).toBeNull();
    expect(await ws.uploadFont({ name: 'big.ttf', size: 3 * 1024 * 1024, arrayBuffer: async () => new ArrayBuffer(0) })).toBeNull();
    expect(lines[lines.length - 1]!.text).toContain('2 MiB');
    expect(dev.calls.length).toBe(before);
  });

  it('deletes a font after asking and offers to remove its config entry', async () => {
    const cfg = { fonts: [{ name: 'roboto', src: 'roboto.ttf' }, { name: 'mono', src: 'mono.ttf' }] };
    const dev = fakeDevice({ 'config.json': JSON.stringify(cfg) });
    dev.fonts.set('roboto.ttf', new Uint8Array(4));
    await start();
    expect(await ws.deleteFont('roboto.ttf')).toBe(true);
    expect(questions[questions.length - 1]).toBe('Delete fonts/roboto.ttf from the device?');
    expect(dev.fonts.has('roboto.ttf')).toBe(false);
    expect(await ws.removeFontFromConfig('roboto.ttf')).toBe(true);
    expect(questions[questions.length - 1]).toBe('Remove font "roboto" (roboto.ttf) from config.json "fonts"?');
    expect(JSON.parse(dev.files.get('config.json')!)).toEqual({ fonts: [{ name: 'mono', src: 'mono.ttf' }] });
  });

  it('refuses to delete a system font (no question, no request); icons.ttf is an ordinary font', async () => {
    const dev = fakeDevice({});
    await start();
    const before = dev.calls.length;
    expect(await ws.deleteFont('medium.ttf')).toBe(false);
    expect(await ws.deleteFont('semibold.ttf')).toBe(false);
    expect(questions).toEqual([]);
    expect(lines[lines.length - 1]!.text).toContain('system font "montserrat_semibold": it cannot be deleted');
    expect(dev.calls.length).toBe(before);
    expect(dev.fonts.has('medium.ttf')).toBe(true);
    expect(await ws.deleteFont('icons.ttf')).toBe(true);
    expect(questions[questions.length - 1]).toBe('Delete fonts/icons.ttf from the device?');
    expect(dev.fonts.has('icons.ttf')).toBe(false);
  });

  it('does not remove a config entry that a style still uses', async () => {
    const cfg = {
      fonts: [{ name: 'roboto', src: 'roboto.ttf' }],
      styles: { title: { 'font-family': 'roboto' } },
      buttons: [{ position: 1, style: 'title' }],
    };
    const dev = fakeDevice({ 'config.json': JSON.stringify(cfg) });
    await start();
    expect(await ws.removeFontFromConfig('roboto.ttf')).toBe(false);
    expect(lines[lines.length - 1]!.text).toContain('styles.title.font-family: unknown font');
    expect(JSON.parse(dev.files.get('config.json')!).fonts).toHaveLength(1);
  });

  it('saving a config with fonts lists the bucket once and warns about missing files', async () => {
    const dev = fakeDevice({ 'config.json': '{}' });
    const ed = await start();
    await ws.open('config.json');
    ed.content = JSON.stringify({ fonts: [{ name: 'roboto', src: 'roboto.ttf' }] });
    emit('editor-doc-change');
    expect(await ws.save()).toBe(true);
    await new Promise(r => setTimeout(r, 0));
    expect(dev.calls.filter(c => c === 'ListObjectsV2 fonts')).toHaveLength(1);
    expect(lines.some(l => l.type === 'warn' && l.text.includes('roboto.ttf is not in the device\'s fonts bucket'))).toBe(true);
  });

  it('a config without fonts never touches bucket fonts on save', async () => {
    const dev = fakeDevice({ 'config.json': '{}' });
    const ed = await start();
    await ws.open('config.json');
    ed.content = '{"buttons": []}';
    emit('editor-doc-change');
    expect(await ws.save()).toBe(true);
    expect(dev.calls.some(c => c.includes(' fonts'))).toBe(false);
  });
});
