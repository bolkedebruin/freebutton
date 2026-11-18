import { describe, it, expect } from 'vitest';
import { fieldText, fieldValue, itemFields, type Field } from '../item-props';
import type { NamedStyles } from '../style-config';

const keys = (fields: Field[]) => fields.map(f => f.key);
const field = (fields: Field[], key: string) => fields.find(f => f.key === key)!;

describe('item-props itemFields', () => {
  it('display item: text, font, size, align, width, position; the icon colour only with an icon', () => {
    expect(keys(itemFields('displayitem', {}))).toEqual(['label', 'unit', 'font', 'fontsize', 'align', 'width', 'x', 'y']);
    expect(keys(itemFields('displayitem', { icon: 'rain' }))).toContain('iconcolor');
  });

  it('list: items, font, size, width, rows, position and its three colours', () => {
    expect(keys(itemFields('list'))).toEqual([
      'items', 'font', 'fontsize', 'width', 'rows', 'x', 'y', 'color', 'focuscolor', 'focustextcolor',
    ]);
  });

  it('image: size and position in px, colour; no text or font', () => {
    expect(keys(itemFields('image'))).toEqual(['width', 'height', 'x', 'y', 'color']);
  });

  it('key: both labels with their font and size', () => {
    expect(keys(itemFields('button'))).toEqual(['label', 'font', 'fontsize', 'toplabel', 'topfont', 'topfontsize']);
  });

  it('sizes are the firmware\'s: 1..72 (1-5 steps, 6-72 px); rows 1..7; align 1..9', () => {
    const size = field(itemFields('displayitem'), 'fontsize');
    expect([size.min, size.max]).toEqual([1, 72]);
    expect([field(itemFields('list'), 'rows').min, field(itemFields('list'), 'rows').max]).toEqual([1, 7]);
    expect(field(itemFields('displayitem'), 'align').options?.map(o => o.value).sort()).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9]);
  });
});

describe('item-props fieldValue', () => {
  const d = itemFields('displayitem', { icon: 'x' });
  const l = itemFields('list');

  it('numbers: whole, inside the range; empty removes the key; text is not a value', () => {
    expect(fieldValue(field(d, 'fontsize'), '32')).toBe(32);
    expect(fieldValue(field(d, 'fontsize'), '200')).toBe(72);
    expect(fieldValue(field(d, 'fontsize'), '0')).toBe(1);
    expect(fieldValue(field(d, 'width'), '33.6')).toBe(34);
    expect(fieldValue(field(d, 'x'), '-20')).toBe(-20);
    expect(fieldValue(field(d, 'fontsize'), '')).toBeUndefined();
    expect(fieldValue(field(d, 'fontsize'), 'abc')).toBeNull();
  });

  it('text is kept as typed; a font of "" removes the key', () => {
    expect(fieldValue(field(d, 'label'), ' Buiten \\cFF0000')).toBe(' Buiten \\cFF0000');
    expect(fieldValue(field(d, 'label'), '')).toBe('');
    expect(fieldValue(field(d, 'font'), 'pacifico')).toBe('pacifico');
    expect(fieldValue(field(d, 'font'), '')).toBeUndefined();
  });

  it('align: one of its options', () => {
    expect(fieldValue(field(d, 'align'), '3')).toBe(3);
    expect(fieldValue(field(d, 'align'), '11')).toBeNull();
  });

  it('colours: "#rrggbb" for display items and images, "rrggbb" for lists', () => {
    expect(fieldValue(field(d, 'iconcolor'), '#FFD700')).toBe('#ffd700');
    expect(fieldValue(field(l, 'focuscolor'), '#FFD700')).toBe('ffd700');
    expect(fieldValue(field(itemFields('image'), 'color'), 'ff0000')).toBe('#ff0000');
    expect(fieldValue(field(l, 'color'), 'red')).toBeNull();
    expect(fieldValue(field(l, 'color'), '')).toBeUndefined();
  });

  it('list items: one per line, empty lines dropped', () => {
    expect(fieldValue(field(l, 'items'), 'Bed Time\r\n\n Evening \n')).toEqual(['Bed Time', 'Evening']);
  });
});

describe('item-props fieldText', () => {
  it('shows the entry\'s own value; nothing for an absent key or another type', () => {
    const d = itemFields('displayitem');
    const l = itemFields('list');
    expect(fieldText(field(d, 'label'), 'Temp')).toBe('Temp');
    expect(fieldText(field(d, 'label'), undefined)).toBe('');
    expect(fieldText(field(d, 'fontsize'), 32)).toBe('32');
    expect(fieldText(field(d, 'x'), '10')).toBe('10');
    expect(fieldText(field(d, 'x'), 'left')).toBe('');
    expect(fieldText(field(l, 'items'), ['a', 'b', 3])).toBe('a\nb');
    expect(fieldText(field(l, 'color'), 'FFD700')).toBe('#ffd700');
    expect(fieldText(field(l, 'color'), 'gold')).toBe('');
  });
});

describe('item-props named styles', () => {
  const styles: NamedStyles = new Map([['alarm', { color: 0xff0000 }], ['picked', { bgColor: 0x00c853 }], ['empty', {}]]);

  it('no style fields without named styles (the lists above), unless the entry names one', () => {
    expect(keys(itemFields('button', { style: 'gone' }))).toEqual(['label', 'font', 'fontsize', 'toplabel', 'topfont', 'topfontsize', 'style']);
    expect(field(itemFields('button', { style: 'gone' }), 'style')).toMatchObject({ type: 'style', styles: [] });
  });

  it('with named styles: two per kind, after the fonts', () => {
    expect(keys(itemFields('button', {}, styles))).toEqual(['label', 'font', 'fontsize', 'toplabel', 'topfont', 'topfontsize', 'style', 'topstyle']);
    expect(keys(itemFields('displayitem', {}, styles)).slice(0, 6)).toEqual(['label', 'unit', 'font', 'fontsize', 'style', 'labelstyle']);
    expect(keys(itemFields('list', {}, styles)).slice(0, 5)).toEqual(['items', 'font', 'fontsize', 'style', 'focusstyle']);
    expect(keys(itemFields('image', {}, styles))).toEqual(['width', 'height', 'x', 'y', 'color']);
  });

  it('a style that sets bg_* is offered to a list only', () => {
    for (const key of ['style', 'topstyle']) expect(field(itemFields('button', {}, styles), key).styles).toEqual(['alarm', 'empty']);
    for (const key of ['style', 'labelstyle']) expect(field(itemFields('displayitem', {}, styles), key).styles).toEqual(['alarm', 'empty']);
    for (const key of ['style', 'focusstyle']) expect(field(itemFields('list', {}, styles), key).styles).toEqual(['alarm', 'picked', 'empty']);
  });

  it('the value: the name; "None" removes the key', () => {
    const f = field(itemFields('list', {}, styles), 'focusstyle');
    expect(fieldValue(f, 'picked')).toBe('picked');
    expect(fieldValue(f, '')).toBeUndefined();
    expect(fieldText(f, 'picked')).toBe('picked');
    expect(fieldText(f, undefined)).toBe('');
  });
});
