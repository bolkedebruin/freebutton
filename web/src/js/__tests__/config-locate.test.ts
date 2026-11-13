import { describe, it, expect } from 'vitest';
import { EditorState } from '@codemirror/state';
import { json } from '@codemirror/lang-json';
import { itemAt, rangeOf } from '../config-locate';
import { configToModel } from '../config-model';

const TEXT = `{
  "info": { "connectors": [ { "id": 0, "type": "display" } ] },
  "buttons": [
    { "position": 1, "label": "Light" },
    { "position": 2, "label": "Fan", "topics": [ { "topic": "a/b" } ] }
  ],
  "mqttdisplays": [
    { "x": 0, "y": 0, "label": "Temp" },
    { "x": 50, "y": 0, "label": "Hum", "page": 2 }
  ],
  "lists": [ { "name": "modes", "items": ["a", "b"] } ],
  "images": [ { "name": "logo", "x": 1, "y": 2 } ]
}`;

function state(text = TEXT): EditorState {
  return EditorState.create({ doc: text, extensions: [json()] });
}

describe('config-locate itemAt', () => {
  it('finds the entry a position lies in', () => {
    const s = state();
    expect(itemAt(s, TEXT.indexOf('"Light"'))).toEqual({ kind: 'button', list: 'buttons', index: 0 });
    expect(itemAt(s, TEXT.indexOf('"Fan"'))).toEqual({ kind: 'button', list: 'buttons', index: 1 });
    expect(itemAt(s, TEXT.indexOf('"Hum"'))).toEqual({ kind: 'displayitem', list: 'mqttdisplays', index: 1 });
    expect(itemAt(s, TEXT.indexOf('"modes"'))).toEqual({ kind: 'list', list: 'lists', index: 0 });
    expect(itemAt(s, TEXT.indexOf('"logo"'))).toEqual({ kind: 'image', list: 'images', index: 0 });
  });

  it('resolves a position nested deeper in an entry to that entry', () => {
    expect(itemAt(state(), TEXT.indexOf('"a/b"'))).toEqual({ kind: 'button', list: 'buttons', index: 1 });
    expect(itemAt(state(), TEXT.indexOf('"b"]'))).toEqual({ kind: 'list', list: 'lists', index: 0 });
  });

  it('returns null outside the drawn lists', () => {
    const s = state();
    expect(itemAt(s, 0)).toBeNull();
    expect(itemAt(s, TEXT.indexOf('"display"'))).toBeNull();
    expect(itemAt(s, TEXT.indexOf('"buttons"'))).toBeNull();
  });

  it('still answers while the text is not valid JSON', () => {
    const broken = TEXT.replace('"label": "Fan",', '"label": "Fan" "oops"');
    expect(itemAt(state(broken), broken.indexOf('"Light"'))).toEqual({ kind: 'button', list: 'buttons', index: 0 });
  });
});

describe('config-locate rangeOf', () => {
  it('returns the entry text', () => {
    const s = state();
    const r = rangeOf(s, { list: 'buttons', index: 1 })!;
    expect(TEXT.slice(r.from, r.to)).toBe('{ "position": 2, "label": "Fan", "topics": [ { "topic": "a/b" } ] }');
    const d = rangeOf(s, { list: 'mqttdisplays', index: 0 })!;
    expect(TEXT.slice(d.from, d.to)).toBe('{ "x": 0, "y": 0, "label": "Temp" }');
  });

  it('returns null for an entry that is not there', () => {
    expect(rangeOf(state(), { list: 'buttons', index: 9 })).toBeNull();
    expect(rangeOf(state(), { list: 'displayitems', index: 0 })).toBeNull();
  });

  it('agrees with the refs configToModel sets', () => {
    const model = configToModel(JSON.parse(TEXT));
    const s = state();
    const all = model.pages.flatMap(p => [...p.buttons, ...p.displayItems, ...(p.lists ?? []), ...(p.images ?? [])]);
    expect(all.length).toBeGreaterThan(0);
    for (const item of all) {
      expect(item.ref).toBeDefined();
      const r = rangeOf(s, item.ref!)!;
      expect(itemAt(s, r.from + 1)).toEqual(item.ref);
      // Both ends of an entry belong to it (a click on the device selects the whole entry).
      expect(itemAt(s, r.from)).toEqual(item.ref);
      expect(itemAt(s, r.to)).toEqual(item.ref);
    }
  });
});
