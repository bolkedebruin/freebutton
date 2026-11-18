import { describe, it, expect } from 'vitest';
import { readFileSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { EditorState } from '@codemirror/state';
import { json } from '@codemirror/lang-json';
import {
  addItem, applyChanges, itemRange, listKeyFor, readItem, removeItem, setItemProps, type EditResult,
} from '../config-edit';
import { configToModel } from '../config-model';

/** The modern style: two spaces, a key per line (devices/btn_a00001/config.json). */
const MODERN = `{
  "core": {
    "name": "one"
  },
  "buttons": [
    {
      "name": "up",
      "position": 3,
      "page": 1,
      "label": "Up",
      "toplabel": "Volume"
    }
  ],
  "displayitems": [
    {
      "name": "outsideTemp",
      "page": 1,
      "x": 83,
      "y": 5,
      "width": 17,
      "align": 1,
      "boxtype": 1,
      "fontsize": 32
    },
    {
      "name": "title",
      "page": 2,
      "x": 0,
      "y": 0,
      "align": 1,
      "boxtype": 1,
      "label": "Home",
      "topics": [
        { "topic": "a/b", "x": 99, "label": "nested" }
      ]
    }
  ],
  "lists": [
    { "name": "scenes", "page": 4, "x": 4, "y": 18, "width": 92, "rows": 3, "items": ["Bed Time", "Evening"] }
  ],
  "images": [
    {"name":"logo","x":10,"y":20,"width":48,"height":48,"icon":"rain"}
  ]
}
`;

const LEGACY = `{
\t"mqttbuttons": [
\t\t{ "position": 1, "label": "Light", "topLabel": "Hall" }
\t],
\t"mqttdisplays": [
\t\t{ "x": 0, "y": 0, "label": "Temp" },
\t\t{ "x": 50, "y": 0, "label": "Temp", "page": 2 }
\t]
}`;

function state(text: string): EditorState {
  return EditorState.create({ doc: text, extensions: [json()] });
}

function changes(r: EditResult) {
  if (!r.ok) throw new Error(`refused: ${r.error}`);
  return r.changes;
}

/** The edit on the text and on an editor state give the same result; it is valid JSON. */
function edited(text: string, edit: (doc: EditorState | string) => EditResult): string {
  const fromText = applyChanges(text, changes(edit(text)));
  if (!text.includes('\r')) {
    const s = state(text);
    const after = s.update({ changes: changes(edit(s)) }).state.doc.toString();
    expect(after).toBe(fromText);
  }
  expect(() => JSON.parse(fromText)).not.toThrow();
  return fromText;
}

/** The lines that differ between two texts (same line count). */
function changedLines(before: string, after: string): string[] {
  const a = before.split('\n');
  const b = after.split('\n');
  expect(b.length).toBe(a.length);
  return b.filter((line, i) => line !== a[i]);
}

describe('config-edit setItemProps: position', () => {
  it('changes x and y of exactly that entry and nothing else', () => {
    const out = edited(MODERN, d => setItemProps(d, { list: 'displayitems', index: 0 }, { x: 40, y: 12 }));
    expect(changedLines(MODERN, out)).toEqual(['      "x": 40,', '      "y": 12,']);
    expect(JSON.parse(out).displayitems[0]).toMatchObject({ name: 'outsideTemp', x: 40, y: 12, width: 17 });
  });

  it('is one change per value, each only as wide as the number', () => {
    const r = setItemProps(MODERN, { list: 'displayitems', index: 0 }, { x: 40, y: 5 });
    expect(changes(r)).toEqual([{ from: MODERN.indexOf('83'), to: MODERN.indexOf('83') + 2, insert: '40' }]);
  });

  it('changes nothing when the values are what the text says', () => {
    expect(changes(setItemProps(MODERN, { list: 'displayitems', index: 0 }, { x: 83, y: 5 }))).toEqual([]);
  });

  it('edits the direct property, not one of the same name deeper in the entry', () => {
    const out = edited(MODERN, d => setItemProps(d, { list: 'displayitems', index: 1 }, { x: 7, label: 'Away' }));
    const item = JSON.parse(out).displayitems[1];
    expect(item.x).toBe(7);
    expect(item.label).toBe('Away');
    expect(item.topics[0]).toEqual({ topic: 'a/b', x: 99, label: 'nested' });
  });

  it('adds y where the entry has none, on its own line, after x', () => {
    const text = MODERN.replace('      "y": 5,\n', '');
    const out = edited(text, d => setItemProps(d, { list: 'displayitems', index: 0 }, { y: 9 }));
    expect(out).toBe(MODERN.replace('"y": 5', '"y": 9'));
  });

  it('edits an entry on one line in that style (lists, images)', () => {
    const list = edited(MODERN, d => setItemProps(d, { list: 'lists', index: 0 }, { x: 10, fontsize: 24 }));
    expect(list).toContain('{ "name": "scenes", "page": 4, "x": 10, "y": 18, "width": 92, "rows": 3, "fontsize": 24, "items": ["Bed Time", "Evening"] }');
    const image = edited(MODERN, d => setItemProps(d, { list: 'images', index: 0 }, { y: 30, color: '#ffd700' }));
    expect(image).toContain('{"name":"logo","x":10,"y":30,"width":48,"height":48,"icon":"rain","color":"#ffd700"}');
  });
});

describe('config-edit setItemProps: properties', () => {
  const ref = { list: 'displayitems', index: 0 };

  it('adds an absent key in the entry\'s style: own line, same indent, comma on the line before', () => {
    const out = edited(MODERN, d => setItemProps(d, ref, { label: 'Outside' }));
    expect(out).toContain('      "fontsize": 32,\n      "label": "Outside"\n    },');
    expect(changedLines(MODERN + '\n', out).length).toBeGreaterThan(0);
  });

  it('puts a new key after the keys that come before it, not at the end', () => {
    const out = edited(MODERN, d => setItemProps(d, { list: 'displayitems', index: 1 }, { width: 50 }));
    expect(out).toContain('      "y": 0,\n      "width": 50,\n      "align": 1,');
  });

  it('puts the named style fields next to the fonts they go with', () => {
    const out = edited(MODERN, d => setItemProps(d, ref, { style: 'alarm', labelstyle: 'muted' }));
    expect(Object.keys(JSON.parse(out).displayitems[0])).toEqual([
      'name', 'page', 'x', 'y', 'width', 'align', 'boxtype', 'fontsize', 'style', 'labelstyle',
    ]);
    // Removing them again gives the file back.
    expect(edited(out, d => setItemProps(d, ref, { style: undefined, labelstyle: undefined }))).toBe(MODERN);
  });

  it('sets text, font, size, alignment, width and colour', () => {
    const out = edited(MODERN, d => setItemProps(d, ref, {
      label: 'Buiten', unit: '°C', font: 'pacifico', fontsize: 48, align: 3, width: 30, iconcolor: '#ff0000',
    }));
    expect(JSON.parse(out).displayitems[0]).toEqual({
      name: 'outsideTemp', page: 1, x: 83, y: 5, width: 30, align: 3, boxtype: 1, fontsize: 48,
      font: 'pacifico', label: 'Buiten', unit: '°C', iconcolor: '#ff0000',
    });
    // The other entries and the rest of the file are as they were.
    expect(out.slice(out.indexOf('"name": "title"'))).toBe(MODERN.slice(MODERN.indexOf('"name": "title"')));
  });

  it('writes text as JSON: quotes, backslashes (markup), icon glyphs', () => {
    const glyph = '\u{F075E}';
    const out = edited(MODERN, d => setItemProps(d, ref, { label: 'a "b" \\cFF0000 ' + glyph }));
    expect(out).toContain('"label": "a \\"b\\" \\\\cFF0000 ' + glyph + '"');
    expect(JSON.parse(out).displayitems[0].label).toBe('a "b" \\cFF0000 ' + glyph);
  });

  it('removes a key with its comma (undefined), also the first and the last one', () => {
    const last = edited(MODERN, d => setItemProps(d, ref, { fontsize: undefined }));
    expect(last).toContain('      "boxtype": 1\n    },');
    const first = edited(MODERN, d => setItemProps(d, ref, { name: undefined }));
    expect(first).toContain('    {\n      "page": 1,\n      "x": 83,');
    const two = edited(MODERN, d => setItemProps(d, ref, { name: undefined, page: undefined, fontsize: undefined }));
    expect(Object.keys(JSON.parse(two).displayitems[0])).toEqual(['x', 'y', 'width', 'align', 'boxtype']);
    // A key that is not there: nothing to remove.
    expect(changes(setItemProps(MODERN, ref, { unit: undefined }))).toEqual([]);
  });

  it('removes one key and adds another in one edit', () => {
    const out = edited(MODERN, d => setItemProps(d, ref, { fontsize: undefined, font: 'mdi', label: 'x' }));
    expect(JSON.parse(out).displayitems[0]).toEqual({
      name: 'outsideTemp', page: 1, x: 83, y: 5, width: 17, align: 1, boxtype: 1, font: 'mdi', label: 'x',
    });
  });

  it('sets a list\'s items as an array on one line', () => {
    const out = edited(MODERN, d => setItemProps(d, { list: 'lists', index: 0 }, { items: ['A', 'B "c"', 'D'] }));
    expect(out).toContain('"items": ["A", "B \\"c\\"", "D"] }');
  });

  it('fills an empty entry', () => {
    const text = '{ "displayitems": [ {} ] }';
    expect(edited(text, d => setItemProps(d, { list: 'displayitems', index: 0 }, { x: 1, y: 2 })))
      .toBe('{ "displayitems": [ {"x": 1, "y": 2} ] }');
  });

  it('refuses a value that cannot be written', () => {
    expect(setItemProps(MODERN, ref, { x: NaN })).toEqual({ ok: false, error: '"x" cannot be set to that value' });
  });
});

describe('config-edit: which entry', () => {
  it('goes by the index, also when two entries have the same text', () => {
    const out = edited(LEGACY, d => setItemProps(d, { list: 'mqttdisplays', index: 1 }, { x: 60 }));
    expect(out).toContain('{ "x": 0, "y": 0, "label": "Temp" },');
    expect(out).toContain('{ "x": 60, "y": 0, "label": "Temp", "page": 2 }');
  });

  it('looks at root arrays only: an array of that name inside another object is not it', () => {
    const text = '{ "other": { "displayitems": [ { "x": 1 } ] }, "displayitems": [ { "x": 2 } ] }';
    expect(edited(text, d => setItemProps(d, { list: 'displayitems', index: 0 }, { x: 5 })))
      .toBe('{ "other": { "displayitems": [ { "x": 1 } ] }, "displayitems": [ { "x": 5 } ] }');
    const nested = '{ "pages": [ { "displayitems": [ { "x": 1 } ] } ] }';
    expect(setItemProps(nested, { list: 'displayitems', index: 0 }, { x: 5 }))
      .toEqual({ ok: false, error: 'displayitems[0] is not in config.json (any more)' });
  });

  it('every entry of every page the model maps is edited where the model says it is', () => {
    const model = configToModel(JSON.parse(MODERN));
    const refs = model.pages.flatMap(p => [...p.displayItems, ...(p.lists ?? []), ...(p.images ?? [])]).map(i => i.ref!);
    expect(refs.length).toBeGreaterThanOrEqual(4);
    for (const ref of refs) {
      const out = edited(MODERN, d => setItemProps(d, ref, { x: 77 }));
      const lists = JSON.parse(out);
      const before = JSON.parse(MODERN);
      before[ref.list][ref.index].x = 77;
      expect(lists).toEqual(before);
    }
  });

  it('refuses an entry that is not there, or is not an object', () => {
    expect(setItemProps(MODERN, { list: 'displayitems', index: 9 }, { x: 1 })).toMatchObject({ ok: false });
    expect(setItemProps(MODERN, { list: 'clocks', index: 0 }, { x: 1 })).toMatchObject({ ok: false });
    expect(setItemProps('{ "displayitems": [ 5 ] }', { list: 'displayitems', index: 0 }, { x: 1 }))
      .toEqual({ ok: false, error: 'displayitems[0] is not an object' });
    expect(setItemProps('{ "displayitems": {} }', { list: 'displayitems', index: 0 }, { x: 1 }))
      .toEqual({ ok: false, error: '"displayitems" in config.json is not an array' });
  });

  it('refuses when the file has the array or the key twice', () => {
    const twice = '{ "displayitems": [ { "x": 1 } ], "displayitems": [ { "x": 2 } ] }';
    expect(setItemProps(twice, { list: 'displayitems', index: 0 }, { x: 5 }))
      .toEqual({ ok: false, error: 'config.json has "displayitems" twice: remove one of them first' });
    const key = '{ "displayitems": [ { "x": 1, "x": 2 } ] }';
    expect(setItemProps(key, { list: 'displayitems', index: 0 }, { x: 5 }))
      .toEqual({ ok: false, error: 'displayitems[0] has "x" twice: remove one of them first' });
    // Another key of that entry can still be set.
    expect(setItemProps(key, { list: 'displayitems', index: 0 }, { y: 5 })).toMatchObject({ ok: true });
  });
});

describe('config-edit: legacy names', () => {
  it('edits mqttdisplays and mqttbuttons in place, tabs kept', () => {
    const out = edited(LEGACY, d => setItemProps(d, { list: 'mqttdisplays', index: 0 }, { x: 10, y: 20, width: 40 }));
    expect(out).toContain('\t\t{ "x": 10, "y": 20, "width": 40, "label": "Temp" },');
    expect(out).toContain('\t"mqttdisplays": [');
    expect(out).not.toContain('displayitems');
  });

  it('updates the spelling the file has ("topLabel"), no second key', () => {
    const out = edited(LEGACY, d => setItemProps(d, { list: 'mqttbuttons', index: 0 }, { toplabel: 'Room' }));
    expect(out).toContain('{ "position": 1, "label": "Light", "topLabel": "Room" }');
    const removed = edited(LEGACY, d => setItemProps(d, { list: 'mqttbuttons', index: 0 }, { toplabel: undefined }));
    expect(removed).toContain('{ "position": 1, "label": "Light" }');
  });

  it('a file with both names: each array is edited under its own name', () => {
    const both = '{ "mqttdisplays": [ { "x": 1 } ], "displayitems": [ { "x": 2 } ] }';
    expect(edited(both, d => setItemProps(d, { list: 'mqttdisplays', index: 0 }, { x: 5 })))
      .toBe('{ "mqttdisplays": [ { "x": 5 } ], "displayitems": [ { "x": 2 } ] }');
    expect(listKeyFor(both, 'displayitem')).toBe('displayitems');
  });

  it('listKeyFor: the array the model reads, the current name for a file without one', () => {
    expect(listKeyFor(LEGACY, 'displayitem')).toBe('mqttdisplays');
    expect(listKeyFor(LEGACY, 'button')).toBe('mqttbuttons');
    expect(listKeyFor(MODERN, 'displayitem')).toBe('displayitems');
    expect(listKeyFor('{}', 'displayitem')).toBe('displayitems');
    expect(listKeyFor('{}', 'list')).toBe('lists');
  });
});

describe('config-edit: line ends and indentation', () => {
  it('CRLF file: added lines end in CRLF, no lone LF appears', () => {
    const crlf = MODERN.replace(/\n/g, '\r\n');
    const out = edited(crlf, d => setItemProps(d, { list: 'displayitems', index: 0 }, { x: 1, label: 'L', unit: 'u' }));
    expect(out.replace(/\r\n/g, '')).not.toMatch(/[\r\n]/);
    expect(out).toContain('      "fontsize": 32,\r\n      "label": "L",\r\n      "unit": "u"\r\n    },');
    const added = edited(crlf, d => addItem(d, 'displayitems', { name: 'n', x: 1 }));
    expect(added.replace(/\r\n/g, '')).not.toMatch(/[\r\n]/);
    expect(JSON.parse(added).displayitems[2]).toEqual({ name: 'n', x: 1 });
  });

  it('tabs: a new key and a new entry are indented with tabs', () => {
    const tabs = '{\n\t"displayitems": [\n\t\t{\n\t\t\t"x": 1,\n\t\t\t"y": 2\n\t\t}\n\t]\n}';
    expect(edited(tabs, d => setItemProps(d, { list: 'displayitems', index: 0 }, { label: 'L' })))
      .toBe('{\n\t"displayitems": [\n\t\t{\n\t\t\t"x": 1,\n\t\t\t"y": 2,\n\t\t\t"label": "L"\n\t\t}\n\t]\n}');
    expect(edited(tabs, d => addItem(d, 'displayitems', { x: 3, y: 4 })))
      .toBe('{\n\t"displayitems": [\n\t\t{\n\t\t\t"x": 1,\n\t\t\t"y": 2\n\t\t},\n\t\t{\n\t\t\t"x": 3,\n\t\t\t"y": 4\n\t\t}\n\t]\n}');
  });

  it('four spaces and "key":value without a space are kept', () => {
    const text = '{\n    "displayitems": [\n        {\n            "x":1,\n            "y":2\n        }\n    ]\n}';
    expect(edited(text, d => setItemProps(d, { list: 'displayitems', index: 0 }, { width: 50 })))
      .toContain('            "y":2,\n            "width":50\n        }');
  });
});

describe('config-edit removeItem', () => {
  it('takes out the entry with its comma: first, middle, last, only', () => {
    const three = '{ "displayitems": [ { "x": 1 }, { "x": 2 }, { "x": 3 } ] }';
    const rm = (index: number) => edited(three, d => removeItem(d, { list: 'displayitems', index }));
    expect(rm(0)).toBe('{ "displayitems": [ { "x": 2 }, { "x": 3 } ] }');
    expect(rm(1)).toBe('{ "displayitems": [ { "x": 1 }, { "x": 3 } ] }');
    expect(rm(2)).toBe('{ "displayitems": [ { "x": 1 }, { "x": 2 } ] }');
    expect(edited('{ "lists": [ { "name": "a" } ] }', d => removeItem(d, { list: 'lists', index: 0 }))).toBe('{ "lists": [] }');
  });

  it('a key per line: the lines of the entry go, the rest stays', () => {
    const out = edited(MODERN, d => removeItem(d, { list: 'displayitems', index: 0 }));
    expect(out).toBe(MODERN.replace(/    \{\n      "name": "outsideTemp",[^}]*\},\n/, ''));
    const last = edited(MODERN, d => removeItem(d, { list: 'displayitems', index: 1 }));
    expect(JSON.parse(last).displayitems.map((d: { name: string }) => d.name)).toEqual(['outsideTemp']);
    expect(last).toContain('      "fontsize": 32\n    }\n  ],');
  });

  it('refuses an entry that is not there', () => {
    expect(removeItem(MODERN, { list: 'displayitems', index: 5 })).toMatchObject({ ok: false });
  });
});

describe('config-edit addItem', () => {
  const props = { name: 'text1', page: 2, x: 10, y: 40, width: 40, align: 1, boxtype: 1, label: 'Text' };

  it('appends an entry written like the last one (a key per line)', () => {
    const r = addItem(MODERN, 'displayitems', props);
    expect(r).toMatchObject({ ok: true, ref: { kind: 'displayitem', list: 'displayitems', index: 2 } });
    const out = edited(MODERN, d => addItem(d, 'displayitems', props));
    expect(out).toContain('      ]\n    },\n    {\n      "name": "text1",\n      "page": 2,\n      "x": 10,\n      "y": 40,\n      "width": 40,\n      "align": 1,\n      "boxtype": 1,\n      "label": "Text"\n    }\n  ],');
    expect(JSON.parse(out).displayitems[2]).toEqual(props);
  });

  it('appends on one line where the last entry is on one line, with its spacing', () => {
    expect(edited(MODERN, d => addItem(d, 'lists', { name: 'b', x: 1 })))
      .toContain('"items": ["Bed Time", "Evening"] },\n    { "name": "b", "x": 1 }\n  ],');
    expect(edited(MODERN, d => addItem(d, 'images', { name: 'b', x: 1 })))
      .toContain('"icon":"rain"},\n    {"name":"b","x":1}\n  ]');
    expect(edited('{ "displayitems": [ { "x": 1 } ] }', d => addItem(d, 'displayitems', { x: 2 })))
      .toBe('{ "displayitems": [ { "x": 1 }, { "x": 2 } ] }');
  });

  it('fills an empty array and makes the array when the file has none, in the file\'s indent', () => {
    const empty = '{\n    "core": {},\n    "displayitems": []\n}';
    expect(edited(empty, d => addItem(d, 'displayitems', { x: 1, y: 2 })))
      .toBe('{\n    "core": {},\n    "displayitems": [\n        {\n            "x": 1,\n            "y": 2\n        }\n    ]\n}');
    const none = '{\n  "core": {}\n}';
    expect(edited(none, d => addItem(d, 'displayitems', { x: 1 })))
      .toBe('{\n  "core": {},\n  "displayitems": [\n    {\n      "x": 1\n    }\n  ]\n}');
    expect(JSON.parse(edited('{}', d => addItem(d, 'displayitems', { x: 1 })))).toEqual({ displayitems: [{ x: 1 }] });
  });

  it('appends to the legacy array under its name', () => {
    const out = edited(LEGACY, d => addItem(d, listKeyFor(d, 'displayitem'), { x: 5, y: 6, label: 'New' }));
    expect(out).toContain('\t\t{ "x": 50, "y": 0, "label": "Temp", "page": 2 },\n\t\t{ "x": 5, "y": 6, "label": "New" }\n\t]');
  });
});

describe('config-edit: text that is not valid JSON', () => {
  const broken = MODERN.replace('"x": 83,', '"x": 83');

  it('refuses every edit and says why; nothing is thrown', () => {
    const why = { ok: false, error: 'config.json is not valid JSON' };
    expect(setItemProps(broken, { list: 'displayitems', index: 0 }, { x: 1 })).toEqual(why);
    expect(setItemProps(state(broken), { list: 'displayitems', index: 0 }, { x: 1 })).toEqual(why);
    expect(removeItem(broken, { list: 'displayitems', index: 0 })).toEqual(why);
    expect(addItem(broken, 'displayitems', { x: 1 })).toEqual(why);
    expect(readItem(broken, { list: 'displayitems', index: 0 })).toBeNull();
    expect(setItemProps('', { list: 'displayitems', index: 0 }, { x: 1 })).toMatchObject({ ok: false });
    expect(setItemProps('[1]', { list: 'displayitems', index: 0 }, { x: 1 })).toMatchObject({ ok: false });
  });
});

describe('config-edit readItem / itemRange', () => {
  it('reads the entry\'s own keys', () => {
    expect(readItem(MODERN, { list: 'lists', index: 0 })).toEqual({
      name: 'scenes', page: 4, x: 4, y: 18, width: 92, rows: 3, items: ['Bed Time', 'Evening'],
    });
    expect(readItem(state(MODERN), { list: 'displayitems', index: 7 })).toBeNull();
  });

  it('gives the entry\'s range', () => {
    const r = itemRange(MODERN, { list: 'images', index: 0 })!;
    expect(MODERN.slice(r.from, r.to)).toBe('{"name":"logo","x":10,"y":20,"width":48,"height":48,"icon":"rain"}');
  });
});

describe('config-edit on the example device configs', () => {
  for (const device of ['btn_a00001', 'btn_a00002']) {
    // Run from web/ (vitest's root); skipped where the device folders are not checked out.
    const path = resolve(process.cwd(), `../devices/${device}/config.json`);
    it.skipIf(!existsSync(path))(`${device}: moving every display item changes only its x and y lines`, () => {
      const text = readFileSync(path, 'utf8');
      const items = JSON.parse(text).displayitems as Array<Record<string, unknown>>;
      items.forEach((item, index) => {
        const out = edited(text, d => setItemProps(d, { list: 'displayitems', index }, { x: 51, y: 52 }));
        const lines = changedLines(text, out);
        expect(lines.map(l => l.trim()).sort()).toEqual([
          ...(item.x === 51 ? [] : ['"x": 51,']), ...(item.y === 52 ? [] : ['"y": 52,']),
        ]);
        expect(JSON.parse(out).displayitems[index]).toEqual({ ...item, x: 51, y: 52 });
      });
    });
  }
});
