import { describe, it, expect } from 'vitest';
import {
  normalizeKey, fileKind, isSystemKey, otherBucketOf, isEditableKey, canDeleteKey, validateKey, validateJson,
  validateBody, validateConfigShape, resolveNewFileKey, initialContentFor, lineColumnAt, findJsonErrorOffset,
  MAX_BODY_BYTES, CONFIG_KEY, MAIN_SCRIPT_KEY,
} from '../fs-rules';

describe('path rules', () => {
  it('normalizes leading slashes and whitespace', () => {
    expect(normalizeKey(' /scripts/main.js ')).toBe('scripts/main.js');
    expect(normalizeKey('//config.json')).toBe('config.json');
  });

  it('detects file kinds by extension', () => {
    expect(fileKind('scripts/main.js')).toBe('js');
    expect(fileKind('config.json')).toBe('json');
    expect(fileKind('notes.TXT')).toBe('text');
    expect(fileKind('fonts/mdi.bin')).toBe('other');
    expect(fileKind('Makefile')).toBe('other');
  });

  it('treats the read-only buckets\' directories as not editable', () => {
    expect(isSystemKey('www/index.html.gz')).toBe(true);
    expect(isSystemKey('/fonts/a.json')).toBe(false);  // bucket fonts is in the media store, not fonts/
    expect(isSystemKey('scripts/examples/button.js')).toBe(true);
    expect(isSystemKey('wwwx/a.js')).toBe(false);
    expect(isSystemKey('scripts/examplesx.js')).toBe(false);
    expect(otherBucketOf('www/index.html.gz')).toBe('web');
    expect(otherBucketOf('fonts/a.bin')).toBeNull();
    expect(otherBucketOf('scripts/examples/a.js')).toBe('examples');
    expect(otherBucketOf('scripts/main.js')).toBeNull();
    expect(isEditableKey('scripts/examples/a.js')).toBe(false);
    expect(isEditableKey('www/app.js')).toBe(false);
    expect(isEditableKey('fonts/x.txt')).toBe(true);
  });

  it('allows only .js/.json/.txt user files', () => {
    expect(isEditableKey(MAIN_SCRIPT_KEY)).toBe(true);
    expect(isEditableKey(CONFIG_KEY)).toBe(true);
    expect(isEditableKey('notes.txt')).toBe(true);
    expect(isEditableKey('main.jsc')).toBe(false);
    expect(isEditableKey('image.png')).toBe(false);
  });

  it('never allows deleting config.json or system files', () => {
    expect(canDeleteKey(CONFIG_KEY)).toBe(false);
    expect(canDeleteKey('/config.json')).toBe(false);
    expect(canDeleteKey('www/index.html')).toBe(false);
    expect(canDeleteKey('scripts/util.js')).toBe(true);
  });

  it('allows deleting files the IDE cannot open', () => {
    expect(canDeleteKey('screenshot-main.rgb565')).toBe(true);
    expect(canDeleteKey('notes.bin')).toBe(true);
    expect(canDeleteKey('icons/a.svg')).toBe(true);
    expect(canDeleteKey('')).toBe(false);
  });

  it('validateKey mirrors the device rules', () => {
    expect(validateKey('scripts/main.js')).toBeNull();
    expect(validateKey('')).toMatch(/empty/);
    expect(validateKey('/config.json')).toMatch(/must not start/);
    expect(validateKey('www/index.js')).toMatch(/read-only/);
    expect(validateKey('fonts/a.txt')).toBeNull();
    expect(validateKey('scripts/examples/x.js')).toMatch(/read-only \(device bucket "examples"\)/);
    expect(validateKey('a.png')).toMatch(/Only \.js/);
    expect(validateKey('scripts/../x.js')).toMatch(/relative/);
    expect(validateKey('scripts//x.js')).toMatch(/empty/);
    expect(validateKey('a\\b.js')).toMatch(/invalid/);
  });

  it('limits keys to 63 bytes (UTF-8)', () => {
    const ok = 'scripts/' + 'a'.repeat(63 - 'scripts/'.length - 3) + '.js';
    expect(ok.length).toBe(63);
    expect(validateKey(ok)).toBeNull();
    expect(validateKey('b' + ok)).toMatch(/too long/);
    // 'é' is 2 bytes: 31 of them + ".js" = 65 bytes although only 34 chars.
    expect(validateKey('é'.repeat(31) + '.js')).toMatch(/too long/);
  });
});

describe('new file names', () => {
  it('puts bare .js names in scripts/ and defaults to .js', () => {
    expect(resolveNewFileKey('util.js')).toBe('scripts/util.js');
    expect(resolveNewFileKey('util')).toBe('scripts/util.js');
    expect(resolveNewFileKey('/lib/x.js')).toBe('lib/x.js');
    expect(resolveNewFileKey('data.json')).toBe('data.json');
    expect(resolveNewFileKey('  ')).toBe('');
  });

  it('starts JSON files as {} so they validate', () => {
    expect(initialContentFor('data.json')).toBe('{}\n');
    expect(validateJson(initialContentFor('data.json'))).toBeNull();
    expect(initialContentFor('scripts/a.js')).toBe('');
  });
});

describe('JSON validation', () => {
  it('accepts valid JSON', () => {
    expect(validateJson('{"buttons":[]}')).toBeNull();
  });

  it('reports position as line/column', () => {
    const text = '{\n  "a": 1,\n  "b": }\n';
    const err = validateJson(text)!;
    expect(err).not.toBeNull();
    expect(err.message).toMatch(/Invalid JSON/);
    expect(err.line).toBe(3);
    expect(err.offset).toBeGreaterThan(0);
  });

  it('locates errors independent of the engine message', () => {
    expect(findJsonErrorOffset('{"a": 1,}')).toBe(8);      // trailing comma
    expect(findJsonErrorOffset('{"a": tru}')).toBe(9);     // bad literal
    expect(findJsonErrorOffset('{"a": [1, 2')).toBe(11);   // unexpected end
    expect(findJsonErrorOffset('{"a": 1} x')).toBe(9);     // trailing garbage
    expect(findJsonErrorOffset('{"a": "x\\q"}')).toBe(9);  // bad escape
    expect(findJsonErrorOffset("{'a': 1}")).toBe(1);       // single quotes
  });

  it('treats an empty document as invalid', () => {
    expect(validateJson('')).not.toBeNull();
  });

  it('lineColumnAt counts newlines', () => {
    expect(lineColumnAt('ab\ncd', 4)).toEqual({ line: 2, column: 2 });
    expect(lineColumnAt('abc', 0)).toEqual({ line: 1, column: 1 });
  });

  it('checks the config.json shape', () => {
    expect(validateConfigShape({ buttons: [{ position: 7 }] })).toEqual([]);
    expect(validateConfigShape([])).toHaveLength(1);
    expect(validateConfigShape({ buttons: {} })).toEqual(['"buttons" must be an array']);
    expect(validateConfigShape({ buttons: [{ position: 0 }, { label: 'x' }] })).toHaveLength(2);
    expect(validateConfigShape({ displayitems: 'x' })).toEqual(['"displayitems" must be an array']);
  });

  it('includes invalid and duplicate button / display item names', () => {
    expect(validateConfigShape({
      buttons: [{ position: 1, name: 'up' }, { position: 2, name: 'up' }],
      displayitems: [{ displayitemid: '1', name: 'bad-name' }],
    })).toEqual([
      'buttons[1].name "up" is already used by buttons[0] (button names must be unique)',
      'displayitems[0].name "bad-name" is not a valid name (1-24 letters, digits or _, not starting with a digit)',
    ]);
  });

  it('the removed array names are not arrays the IDE checks: they are removed keys', () => {
    expect(validateConfigShape({ mqttbuttons: {}, mqttdisplays: [{ name: 'bad-name' }], mqttbrokers: [] })).toEqual([
      '"mqttbuttons" is no longer read: use "buttons"',
      '"mqttdisplays" is no longer read: use "displayitems"',
      '"mqttbrokers" is no longer read: use "brokers"',
    ]);
  });

  it('accepts well-formed lists', () => {
    expect(validateConfigShape({
      lists: [
        { name: 'scenes', page: 2, x: 4, y: 18, width: 92, rows: 3, items: ['Bed Time', 'Evening'], selected: 0, style: 'rows' },
        { name: 'empty', items: [] },
        { name: 'bare' },
      ],
      styles: { rows: { 'font-size': 24, color: '#808080', selected: { 'background-color': '#ffd45f', color: '#000000' } } },
    })).toEqual([]);
  });

  it('checks the lists shape', () => {
    expect(validateConfigShape({ lists: {} })).toEqual(['"lists" must be an array']);
    expect(validateConfigShape({
      lists: [
        'x',
        { items: ['a'] },
        { name: 's', items: 'a\nb' },
        { name: 't', items: ['ok', 3, 'two\nlines'] },
        { name: 'u', rows: 0, x: 101, width: '50', page: 1.5, selected: -1 },
        { name: 'v', color: '808080', focuscolor: 'fff', focustextcolor: 0, fontsize: 3, font: 'x', focusstyle: 'y' },
      ],
    })).toEqual([
      'lists[0] must be an object',
      'lists[1].name is required',
      'lists[2].items must be an array of strings',
      'lists[3].items[1] must be a string',
      'lists[3].items[2] must not contain a newline',
      'lists[4].page must be an integer >= 0',
      'lists[4].x must be a number 0..100',
      'lists[4].width must be a number 0..100',
      'lists[4].rows must be an integer 1..7',
      'lists[4].selected must be an integer >= 0',
      // The font and colour fields of a list are gone: each is reported with what replaces it.
      '"color" is no longer read: use a style with "color" (lists[5])',
      '"focuscolor" is no longer read: use a style with "selected": {"background-color"} (lists[5])',
      '"focustextcolor" is no longer read: use a style with "selected": {"color"} (lists[5])',
      '"fontsize" is no longer read: use a style with "font-size" (lists[5])',
      '"font" is no longer read: use a style with "font-family" (lists[5])',
      '"focusstyle" is no longer read: use a style with a "selected" block (lists[5])',
    ]);
  });

  it('includes invalid and duplicate list names', () => {
    expect(validateConfigShape({ lists: [{ name: 'a' }, { name: 'a' }, { name: '9' }] })).toEqual([
      'lists[1].name "a" is already used by lists[0] (list names must be unique)',
      'lists[2].name "9" is not a valid name (1-24 letters, digits or _, not starting with a digit)',
    ]);
  });
});

describe('validateBody', () => {
  it('blocks invalid JSON in .json files', () => {
    expect(validateBody('data.json', '{"a":')).toMatch(/Invalid JSON/);
    expect(validateBody('data.json', '{"a":1}')).toBeNull();
  });

  it('applies the shape check to config.json only', () => {
    expect(validateBody('config.json', '[]')).toMatch(/config\.json/);
    expect(validateBody('other.json', '[]')).toBeNull();
  });

  it('blocks saving config.json with a duplicate name', () => {
    const text = JSON.stringify({ displayitems: [{ name: 'v' }, { name: 'v' }] });
    expect(validateBody('config.json', text)).toBe(
      'config.json: displayitems[1].name "v" is already used by displayitems[0] (display item names must be unique)'
    );
    expect(validateBody('config.json', JSON.stringify({ displayitems: [{ name: 'v' }], buttons: [{ position: 1, name: 'v' }] }))).toBeNull();
  });

  it('blocks saving config.json with a malformed list', () => {
    expect(validateBody('config.json', JSON.stringify({ lists: [{ name: 's', rows: 9 }] })))
      .toBe('config.json: lists[0].rows must be an integer 1..7');
    expect(validateBody('config.json', JSON.stringify({ lists: [{ name: 'v' }], displayitems: [{ name: 'v' }] }))).toBeNull();
  });

  it('does not syntax-check .js (the source is always saved)', () => {
    expect(validateBody('scripts/main.js', 'function (')).toBeNull();
  });

  it('enforces the 64 KiB body limit in UTF-8 bytes', () => {
    expect(validateBody('a.txt', 'x'.repeat(MAX_BODY_BYTES))).toBeNull();
    expect(validateBody('a.txt', 'x'.repeat(MAX_BODY_BYTES + 1))).toMatch(/too large/);
    expect(validateBody('a.txt', 'é'.repeat(MAX_BODY_BYTES / 2 + 1))).toMatch(/too large/);
  });
});

describe('validateConfigShape: fonts, styles and removed keys', () => {
  it('includes the font table and the style lists; save refuses them', () => {
    const cfg = { fonts: [{ name: 'a', src: 'a.ttf', fallback: 'a' }], buttons: [{ position: 1, style: 'zz' }] };
    expect(validateConfigShape(cfg)).toEqual([
      'fonts: fallback cycle a -> a',
      'buttons[0].style: unknown style "zz", left out',
    ]);
    expect(validateBody('config.json', JSON.stringify(cfg))).toContain('fallback cycle');
    expect(validateConfigShape({
      fonts: [{ name: 'a', src: 'a.ttf' }], styles: { big: { 'font-family': 'a', 'font-size': 40 } }, displayitems: [{ style: 'big' }],
    })).toEqual([]);
  });

  it('includes the problems in "styles"; save refuses them', () => {
    const cfg = { styles: { displayitem: { title: { color: 'gold' } }, button: { lable: {} }, bold: { 'font-weight': 700 } } };
    expect(validateConfigShape(cfg)).toEqual([
      'styles.displayitem.title.color: bad value, ignored (it takes a colour "#rrggbb")',
      'styles.button: unknown property "lable", ignored ("lable" is not a part: title, unit, selected, icon)',
      'styles.bold: unknown property "font-weight", ignored (there is no font-weight: a bold face is another font, chosen with "font-family")',
    ]);
    expect(validateBody('config.json', JSON.stringify(cfg))).toContain('styles.displayitem.title.color: bad value');
  });

  it('a config in the old format: every removed key with its replacement; save refuses it', () => {
    const cfg = {
      style: { fonts: [], text: { text_color: '#ffffff' } },
      buttons: [{ position: 1, fontsize: 3 }],
      displayitems: [{ fontsize: 2, iconcolor: '#ff0000' }],
      // "color" stays valid on an image.
      images: [{ name: 'logo', color: '#ff0000' }],
    };
    expect(validateConfigShape(cfg)).toEqual([
      '"style" is no longer read: use the top-level "fonts" and "styles"',
      '"fontsize" is no longer read: use a style with "font-size" (buttons[0] and 1 more)',
      '"iconcolor" is no longer read: use a style with "icon": {"color"} (displayitems[0])',
    ]);
    expect(validateBody('config.json', JSON.stringify(cfg))).toContain('"fontsize" is no longer read: use a style with "font-size"');
  });
});
