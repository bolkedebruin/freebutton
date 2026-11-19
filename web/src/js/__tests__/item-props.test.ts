import { describe, it, expect } from 'vitest';
import { fieldProblem, fieldText, fieldValue, itemFields, styleFieldNames, toggleStyle, type Field } from '../item-props';

const keys = (fields: Field[]) => fields.map(f => f.key);
const field = (fields: Field[], key: string) => fields.find(f => f.key === key)!;
const STYLES = ['big', 'alarm', 'muted', 'small', 'tiny'];

describe('item-props itemFields', () => {
  it('display item: texts, styles, align, width, position; no font, size or colour', () => {
    expect(keys(itemFields('displayitem'))).toEqual(['name', 'label', 'unit', 'style', 'align', 'width', 'x', 'y']);
  });

  it('list: items, styles, width, rows, position; no font, size or colours', () => {
    expect(keys(itemFields('list'))).toEqual(['name', 'items', 'style', 'width', 'rows', 'x', 'y']);
  });

  it('image: size and position in px, colour; no text and no style', () => {
    expect(keys(itemFields('image', STYLES))).toEqual(['name', 'width', 'height', 'x', 'y', 'color']);
  });

  it('button: both labels and the styles', () => {
    expect(keys(itemFields('button'))).toEqual(['name', 'label', 'toplabel', 'style']);
  });

  it('name: only a valid name is written; empty removes it, except where the device needs one; a clock has none', () => {
    const name = (kind: Parameters<typeof itemFields>[0]) => itemFields(kind).find(f => f.key === 'name');
    expect(name('clock')).toBeUndefined();
    const item = name('displayitem')!;
    expect(fieldValue(item, 'woonkamer_temp')).toBe('woonkamer_temp');
    expect([fieldValue(item, '1temp'), fieldValue(item, 'a b'), fieldValue(item, 'a'.repeat(25))]).toEqual([null, null, null]);
    expect(fieldValue(item, '')).toBeUndefined();
    expect(fieldValue(name('button')!, '')).toBeUndefined();
    expect([fieldValue(name('list')!, ''), fieldValue(name('image')!, '')]).toEqual([null, null]);
  });

  it('none of the removed keys is a field', () => {
    const removed = [
      'font', 'fontsize', 'topfont', 'topfontsize', 'labelfont', 'labelfontsize', 'topstyle', 'labelstyle', 'focusstyle',
      'iconcolor', 'focuscolor', 'focustextcolor',
    ];
    for (const kind of ['displayitem', 'list', 'button', 'image'] as const) {
      for (const key of keys(itemFields(kind, STYLES))) expect(removed, `${kind}.${key}`).not.toContain(key);
    }
    // "color" is a field of an image only.
    expect(keys(itemFields('list', STYLES))).not.toContain('color');
  });

  it('ranges are the firmware\'s: rows 1..7; align 1..9', () => {
    expect([field(itemFields('list'), 'rows').min, field(itemFields('list'), 'rows').max]).toEqual([1, 7]);
    expect(field(itemFields('displayitem'), 'align').options?.map(o => o.value).sort()).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9]);
  });
});

describe('item-props: the styles field', () => {
  it('offers the styles of the config an entry can name, in their order; one field for every part', () => {
    for (const kind of ['displayitem', 'list', 'button'] as const) {
      const f = field(itemFields(kind, STYLES), 'style');
      expect(f).toMatchObject({ type: 'styles', styles: STYLES, label: 'Styles' });
      expect(f.unset).toBe(`the look of "${kind}"`);
      expect(f.hint).toBe('Up to 4 styles; where two set the same property the last one wins');
    }
  });

  it('a config without styles: the field is there, and says so', () => {
    const f = field(itemFields('button'), 'style');
    expect(f.styles).toEqual([]);
    expect(f.hint).toBe('config.json "styles" defines no style an entry can name');
  });

  it('the value: the names joined by one space; none removes the key', () => {
    const f = field(itemFields('displayitem', STYLES), 'style');
    expect(fieldValue(f, 'big alarm')).toBe('big alarm');
    expect(fieldValue(f, '  big    alarm ')).toBe('big alarm');
    expect(fieldValue(f, 'big')).toBe('big');
    expect(fieldValue(f, '')).toBeUndefined();
    expect(fieldValue(f, '   ')).toBeUndefined();
    // What is typed is written as it is: config problems say what the device cannot use.
    expect(fieldValue(f, 'nosuch button')).toBe('nosuch button');
    expect(fieldText(f, 'big alarm')).toBe('big alarm');
    expect(fieldText(f, undefined)).toBe('');
  });

  it('a press on a style: in the list it goes out, else it is added as the last; a fifth is refused', () => {
    expect(toggleStyle('', 'big')).toBe('big');
    expect(toggleStyle('big', 'alarm')).toBe('big alarm');
    expect(toggleStyle('big alarm', 'big')).toBe('alarm');
    expect(toggleStyle('big', 'big')).toBe('');
    expect(toggleStyle(' big   alarm ', 'muted')).toBe('big alarm muted');
    expect(toggleStyle('big alarm muted small', 'tiny')).toBeNull();
    expect(toggleStyle('big alarm muted small', 'muted')).toBe('big alarm small');
  });

  it('the names are counted as the device counts them: a name given twice is one style, at its last place', () => {
    expect(styleFieldNames('a a a a')).toEqual(['a']);
    expect(styleFieldNames('alarm night alarm')).toEqual(['night', 'alarm']);
    expect(styleFieldNames('  big   alarm big muted big ')).toEqual(['alarm', 'muted', 'big']);
    expect(styleFieldNames('')).toEqual([]);
    // "a a a a" is one style, not four: another can be added (and the written list has it once).
    expect(toggleStyle('a a a a', 'b')).toBe('a b');
    expect(toggleStyle('a a a a', 'a')).toBe('');
    expect(toggleStyle('alarm night alarm big alarm', 'small')).toBe('night big alarm small');
    // Six names that are four styles: full. Five that are four with the pressed one among them: it goes out.
    expect(toggleStyle('a b a c d b', 'e')).toBeNull();
    expect(toggleStyle('a b a c d', 'a')).toBe('b c d');
  });

  it('a "style" that is no string: shown as empty, with what the device makes of it', () => {
    const f = field(itemFields('displayitem', STYLES), 'style');
    for (const value of [7, 0, true, ['big'], { big: 1 }]) {
      expect(fieldText(f, value), JSON.stringify(value)).toBe('');
      expect(fieldProblem(f, value), JSON.stringify(value)).toBe(`"style" is not a string of style names, ignored (it is ${JSON.stringify(value)})`);
    }
    // A string, and no value at all: no problem.
    expect(fieldProblem(f, 'big alarm')).toBeNull();
    expect(fieldProblem(f, '')).toBeNull();
    expect(fieldProblem(f, undefined)).toBeNull();
    expect(fieldProblem(f, null)).toBeNull();
    // Other fields have no such check, and a number field still shows a number.
    const width = field(itemFields('displayitem', STYLES), 'width');
    expect(fieldProblem(width, 'x')).toBeNull();
    expect(fieldText(width, 7)).toBe('7');
  });
});

describe('item-props fieldValue', () => {
  const d = itemFields('displayitem');
  const l = itemFields('list');

  it('numbers: whole, inside the range; empty removes the key; text is not a value', () => {
    expect(fieldValue(field(l, 'rows'), '5')).toBe(5);
    expect(fieldValue(field(l, 'rows'), '200')).toBe(7);
    expect(fieldValue(field(l, 'rows'), '0')).toBe(1);
    expect(fieldValue(field(d, 'width'), '33.6')).toBe(34);
    expect(fieldValue(field(d, 'x'), '-20')).toBe(-20);
    expect(fieldValue(field(d, 'width'), '')).toBeUndefined();
    expect(fieldValue(field(d, 'width'), 'abc')).toBeNull();
  });

  it('text is kept as typed', () => {
    expect(fieldValue(field(d, 'label'), ' Buiten \\cFF0000')).toBe(' Buiten \\cFF0000');
    expect(fieldValue(field(d, 'label'), '')).toBe('');
  });

  it('align: one of its options', () => {
    expect(fieldValue(field(d, 'align'), '3')).toBe(3);
    expect(fieldValue(field(d, 'align'), '11')).toBeNull();
  });

  it('an image\'s colour: "#rrggbb"', () => {
    const color = field(itemFields('image'), 'color');
    expect(fieldValue(color, '#FFD700')).toBe('#ffd700');
    expect(fieldValue(color, 'ff0000')).toBe('#ff0000');
    expect(fieldValue(color, 'red')).toBeNull();
    expect(fieldValue(color, '')).toBeUndefined();
  });

  it('list items: one per line, empty lines dropped', () => {
    expect(fieldValue(field(l, 'items'), 'Bed Time\r\n\n Evening \n')).toEqual(['Bed Time', 'Evening']);
  });
});

describe('item-props fieldText', () => {
  it('shows the entry\'s own value; nothing for an absent key or another type', () => {
    const d = itemFields('displayitem');
    const l = itemFields('list');
    const color = field(itemFields('image'), 'color');
    expect(fieldText(field(d, 'label'), 'Temp')).toBe('Temp');
    expect(fieldText(field(d, 'label'), undefined)).toBe('');
    expect(fieldText(field(l, 'rows'), 5)).toBe('5');
    expect(fieldText(field(d, 'x'), '10')).toBe('10');
    expect(fieldText(field(d, 'x'), 'left')).toBe('');
    expect(fieldText(field(l, 'items'), ['a', 'b', 3])).toBe('a\nb');
    expect(fieldText(color, 'FFD700')).toBe('#ffd700');
    expect(fieldText(color, 'gold')).toBe('');
  });
});
