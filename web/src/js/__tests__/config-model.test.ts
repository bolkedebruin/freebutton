import { describe, it, expect } from 'vitest';
import {
  configToModel, parseConfigText, positionsForConnector, mapButton, mapDisplayItem,
  configNames, validateConfigNames, NAME_PATTERN, mapList, listSelectedRow,
} from '../config-model';
import { fontStyles, styleNames } from '../font-config';
import { StyleElement, styleDefaults } from '../style-config';

/** The styles every config has: the looks of the kinds, with what each holds. */
const KINDS = styleNames({});
/** What the built-in look gives a button and a display item. */
const D = styleDefaults();
const BUTTON_LOOK = {
  labelFont: { face: 0, px: 32 }, topFont: { face: 1, px: 18 },
  labelStyle: D[StyleElement.KeyLabel], topStyle: D[StyleElement.KeyTopLabel],
};

describe('config.json -> button model', () => {
  it('maps label and toplabel per position/page', () => {
    const model = configToModel({
      buttons: [
        { position: 7, page: 1, label: 'Open', toplabel: 'Raam' },
        { position: 8, page: 1, label: 'Dicht', toplabel: 'Raam' },
        { position: 5, page: 2, label: 'Lamp' },
      ],
    });
    expect(model.pages.map(p => p.number)).toEqual([1, 2]);
    const p1 = model.pages[0];
    expect(p1.buttons.map(b => [b.position, b.label, b.topLabel])).toEqual([
      [7, 'Open', 'Raam'],
      [8, 'Dicht', 'Raam'],
    ]);
    expect(model.pages[1].buttons[0]).toMatchObject({ position: 5, page: 2, label: 'Lamp', topLabel: '' });
  });

  it('reads "toplabel" only: the spelling "topLabel" is not read any more', () => {
    expect(mapButton({ position: 1, topLabel: 'Old' })?.topLabel).toBe('');
    expect(mapButton({ position: 1, toplabel: 'New', topLabel: 'Old' })?.topLabel).toBe('New');
  });

  it('does not read "mqttbuttons": the buttons under it are not shown', () => {
    const model = configToModel({ mqttbuttons: [{ position: 3, label: 'Legacy' }] });
    expect(model.pages[0].buttons).toEqual([]);
  });

  it('skips buttons without a valid position and defaults page to 1', () => {
    const model = configToModel({ buttons: [{ label: 'x' }, { position: -1 }, { position: '4', label: 'str' }, null] });
    expect(model.pages[0].buttons).toEqual([
      {
        position: 4, page: 1, label: 'str', topLabel: '', ledOn: false, ledColor: 'rgb(0,0,0)', ...BUTTON_LOOK,
        // Where the button comes from in the file (config-locate.ts).
        ref: { kind: 'button', list: 'buttons', index: 2 },
      },
    ]);
  });

  it('keeps MDI icon codepoints in labels as-is', () => {
    expect(mapButton({ position: 3, label: '\u{F075E}' })?.label).toBe('\u{F075E}');
  });

  it('always has page 1, even for an empty config', () => {
    expect(configToModel({}).pages).toEqual([{ number: 1, buttons: [], displayItems: [], lists: [] }]);
    expect(configToModel(null).pages).toHaveLength(1);
  });
});

describe('display items', () => {
  it('maps displayitems with page 0 shown on every page', () => {
    const model = configToModel({
      buttons: [{ position: 1, page: 2 }],
      displayitems: [
        { displayitemid: '1', x: 0, y: 0, page: 0, align: 1, width: 50, label: 'Home', unit: '' },
        { displayitemid: '2', x: 10, y: 60, page: 2, width: 35, label: 'Temp', unit: '°C' },
      ],
    });
    expect(model.pages[0].displayItems.map(d => d.id)).toEqual(['1']);
    expect(model.pages[1].displayItems.map(d => d.id)).toEqual(['1', '2']);
    expect(model.pages[1].displayItems[1]).toMatchObject({ x: 10, y: 60, width: 35, label: 'Temp', unit: '°C', valueFont: { face: 0, px: 14 } });
    expect(model.pages[0].displayItems[0].align).toBe(1);
  });

  it('does not read "mqttdisplays": the items under it are not shown', () => {
    const model = configToModel({ mqttdisplays: [{ x: 1, y: 2, width: 3, label: 'L' }] });
    expect(model.pages[0].displayItems).toEqual([]);
  });

  it('display item pages create page tabs', () => {
    expect(configToModel({ displayitems: [{ page: 3 }] }).pages.map(p => p.number)).toEqual([1, 3]);
  });

  it('ignores non-object entries', () => {
    expect(mapDisplayItem('x')).toBeNull();
  });
});

describe('connectors', () => {
  it('maps info.connectors sorted by id', () => {
    const model = configToModel({
      info: { connectors: [{ id: 3, connector: 4, type: 1 }, { id: 2, connector: 3, type: 2 }, { id: 0, type: 0 }] },
    });
    expect(model.connectors).toEqual([
      { id: 0, type: 'empty' },
      { id: 2, type: 'display' },
      { id: 3, type: 'bar' },
    ]);
  });

  it('is empty when the layout is unknown', () => {
    expect(configToModel({ buttons: [] }).connectors).toEqual([]);
    expect(configToModel({ info: { connectors: 'x' } }).connectors).toEqual([]);
  });

  it('positions follow the firmware mapping (connector*2 + side, 1-based)', () => {
    expect(positionsForConnector(0)).toEqual([1, 2]);
    expect(positionsForConnector(2)).toEqual([5, 6]);
    expect(positionsForConnector(3)).toEqual([7, 8]);
  });
});

describe('parseConfigText', () => {
  it('returns null for invalid JSON', () => {
    expect(parseConfigText('{')).toBeNull();
  });

  it('parses a real device config (btn_a00002 layout)', () => {
    const text = JSON.stringify({
      info: { connectors: [{ id: 3, connector: 4, type: 1 }, { id: 2, connector: 3, type: 2 }] },
      buttons: [
        { id: 0, position: 7, page: 1, label: 'Open', toplabel: 'Raam' },
        { position: 8, page: 1, label: 'Dicht', toplabel: 'Raam' },
      ],
      displayitems: [{ displayitemid: '1', x: 0, y: 0, page: 0, width: 50, label: 'Home' }],
    });
    const model = parseConfigText(text)!;
    expect(model.connectors.map(c => c.type)).toEqual(['display', 'bar']);
    expect(model.pages[0].buttons.map(b => b.label)).toEqual(['Open', 'Dicht']);
    expect(model.pages[0].displayItems[0].label).toBe('Home');
  });
});

describe('button and display item names', () => {
  it('keeps "name" on mapped buttons and display items', () => {
    expect(mapButton({ position: 3, name: 'sceneUp' })?.name).toBe('sceneUp');
    expect(mapButton({ position: 3 })).not.toHaveProperty('name');
    expect(mapDisplayItem({ displayitemid: '1', name: 'volume' })?.item.name).toBe('volume');
  });

  it('collects valid names per namespace; the first duplicate wins', () => {
    const names = configNames({
      buttons: [
        { position: 3, name: 'sceneUp' },
        { position: 4, name: 'volume' },
        { position: 5, name: 'sceneUp' },
        { position: 6, name: '9lives' },
        { position: 7 },
      ],
      displayitems: [{ displayitemid: '1', name: 'volume' }, { displayitemid: '2', name: 'x'.repeat(25) }, { displayitemid: '3', name: 'clock' }],
    });
    expect(names).toEqual({ keys: ['sceneUp', 'volume'], displayItems: ['volume', 'clock'], lists: [], images: [], icons: [], styles: KINDS });
  });

  it('is empty for a config without names; the removed array names are not read', () => {
    expect(configNames({ mqttbuttons: [{ position: 1, name: 'old' }], mqttdisplays: [{ name: 'old' }] }).keys).toEqual([]);
    expect(configNames({})).toEqual({ keys: [], displayItems: [], lists: [], images: [], icons: [], styles: KINDS });
    expect(configNames(null)).toEqual({ keys: [], displayItems: [], lists: [], images: [], icons: [], styles: KINDS });
  });

  it('configToModel carries the names', () => {
    const model = configToModel({ buttons: [{ position: 1, name: 'a' }], displayitems: [{ name: 'b' }] });
    expect(model.names).toEqual({ keys: ['a'], displayItems: ['b'], lists: [], images: [], icons: [], styles: KINDS });
  });

  it('NAME_PATTERN: letters, digits and _, not starting with a digit, 1..24 chars', () => {
    for (const ok of ['a', '_', 'sceneUp', 'A_1', 'x'.repeat(24)]) expect(NAME_PATTERN.test(ok)).toBe(true);
    for (const bad of ['', '1a', 'a-b', 'a b', 'é', 'x'.repeat(25)]) expect(NAME_PATTERN.test(bad)).toBe(false);
  });
});

describe('validateConfigNames', () => {
  it('accepts unique valid names; a button and a display item may share one', () => {
    expect(validateConfigNames({
      buttons: [{ position: 1, name: 'volume' }, { position: 2, name: 'mute' }, { position: 3 }],
      displayitems: [{ displayitemid: '1', name: 'volume' }],
    })).toEqual([]);
  });

  it('reports invalid names with the entry', () => {
    expect(validateConfigNames({ buttons: [{ position: 1, name: '1up' }, { position: 2, name: 7 }] })).toEqual([
      'buttons[0].name "1up" is not a valid name (1-24 letters, digits or _, not starting with a digit)',
      'buttons[1].name 7 is not a valid name (1-24 letters, digits or _, not starting with a digit)',
    ]);
    expect(validateConfigNames({ displayitems: [{ name: '' }] })[0]).toMatch(/^displayitems\[0\]\.name "" is not a valid name/);
  });

  it('reports duplicates within buttons and within display items, naming both entries', () => {
    expect(validateConfigNames({
      buttons: [{ position: 1, name: 'a' }, { position: 2, name: 'b' }, { position: 3, name: 'a' }],
      displayitems: [{ name: 'v' }, { name: 'v' }],
    })).toEqual([
      'buttons[2].name "a" is already used by buttons[0] (button names must be unique)',
      'displayitems[1].name "v" is already used by displayitems[0] (display item names must be unique)',
    ]);
  });

  it('ignores non-objects', () => {
    expect(validateConfigNames(null)).toEqual([]);
    expect(validateConfigNames({ buttons: [null, 'x'] })).toEqual([]);
  });
});

describe('lists', () => {
  const scenes = {
    name: 'scenes', page: 2, x: 4, y: 18, width: 92, rows: 5,
    items: ['Bed Time', 'Evening', 'Movie'], selected: 1,
  };
  const LIST_LOOK = {
    font: { face: 0, px: 32 }, selectedFont: { face: 0, px: 32 },
    style: D[StyleElement.List], selectedStyle: D[StyleElement.ListSelected],
  };

  it('maps every field; the look is the built-in one without styles', () => {
    expect(mapList(scenes)).toEqual({
      name: 'scenes', page: 2, x: 4, y: 18, width: 92, rows: 5,
      items: ['Bed Time', 'Evening', 'Movie'], selected: 1, ...LIST_LOOK,
    });
  });

  it('applies the defaults', () => {
    expect(mapList({ name: 'a', items: ['x'] })).toEqual({
      name: 'a', page: 1, x: 0, y: 0, width: 50, rows: 3, items: ['x'], selected: 0, ...LIST_LOOK,
    });
  });

  it('clamps rows and selected; -1 without items; the removed colour and font fields are not read', () => {
    const l = mapList({ name: 'a', rows: 12, fontsize: 2, items: ['x', 'y'], selected: 9, color: 'ff0000', focuscolor: '00ff00' })!;
    expect([l.rows, l.selected]).toEqual([7, 1]);
    expect(l).toMatchObject(LIST_LOOK);
    expect(mapList({ name: 'a', selected: 2 })!.selected).toBe(-1);
    expect(mapList({ name: 'a', items: 'x' })!.items).toEqual([]);
  });

  it('skips entries without a valid name', () => {
    expect(mapList({ items: ['x'] })).toBeNull();
    expect(mapList({ name: '1st' })).toBeNull();
    expect(mapList('scenes')).toBeNull();
  });

  it('puts lists on their page (page 0 = every page) and adds the page', () => {
    const model = configToModel({ lists: [scenes, { name: 'all', page: 0 }, { name: 'p1' }] });
    expect(model.pages.map(p => p.number)).toEqual([1, 2]);
    expect(model.pages[0].lists!.map(l => l.name)).toEqual(['all', 'p1']);
    expect(model.pages[1].lists!.map(l => l.name)).toEqual(['scenes', 'all']);
  });

  it('collects list names as their own namespace', () => {
    expect(configNames({
      buttons: [{ position: 1, name: 'scenes' }],
      lists: [{ name: 'scenes' }, { name: 'rooms' }, { name: 'scenes' }, { name: '-' }, {}],
    })).toEqual({ keys: ['scenes'], displayItems: [], lists: ['scenes', 'rooms'], images: [], icons: [], styles: KINDS });
  });

  it('validateConfigNames: invalid and duplicate list names', () => {
    expect(validateConfigNames({
      displayitems: [{ name: 'x' }],
      lists: [{ name: 'x' }, { name: 'x' }, { name: 'a b' }],
    })).toEqual([
      'lists[1].name "x" is already used by lists[0] (list names must be unique)',
      'lists[2].name "a b" is not a valid name (1-24 letters, digits or _, not starting with a digit)',
    ]);
  });
});

describe('fonts and styles: the look of each entry', () => {
  const cfg = {
    fonts: [{ id: 3, name: 'roboto', src: 'roboto.ttf' }],
    styles: {
      displayitem: { 'font-size': 24, unit: { color: '#00ff00' }, icon: { color: '#00ffff' } },
      list: { 'line-height': 2, selected: { 'font-size': 40 } },
      alarm: { color: '#ff0000', title: { color: '#ff8080', 'text-align': 'center' }, icon: { color: '#ffff00' } },
      big: { 'font-family': 'roboto', 'font-size': 48 },
      tiny: { 'font-size': 14 },
      parts: { unit: { 'font-size': 10 }, selected: { 'background-color': '#00c85380', color: '#ffffff' } },
      panel: { 'background-color': '#0000ff' },
    },
    buttons: [
      { position: 1, label: 'A', toplabel: 'T', style: 'big alarm', icon: 'sun' },
      { position: 2, label: 'B' },
    ],
    displayitems: [
      { name: 'plain', label: 'P', unit: 'u' },
      { name: 'styled', label: 'S', unit: 'u', style: 'alarm big parts', icon: 'sun' },
      { name: 'small', style: 'tiny', boxtype: 1 },
      { name: 'odd', style: 'nosuch displayitem big' },
    ],
    lists: [{ name: 'a' }, { name: 'b', style: 'big parts panel' }],
    icons: { sun: '/icons/sun.svg' },
  };
  const model = configToModel(cfg);
  const item = (name: string) => model.pages[0].displayItems.find(d => d.name === name)!;

  it('the model carries the font table and the styles', () => {
    expect(model.fonts.faces.map(f => f.name)).toEqual(['montserrat', 'montserrat_semibold', 'roboto']);
    expect([...model.styles.named.keys()]).toEqual(['alarm', 'big', 'tiny', 'parts', 'panel']);
    expect(model.styles.elements[StyleElement.DisplayValue]).toEqual({ face: 0, px: 24, color: 0xffffff });
    expect(model.styles.kindIcon).toEqual({ displayitem: 0x00ffff });
  });

  it('names: fonts and ids only with a "fonts" table; the styles always, the kinds included', () => {
    expect(model.names.fonts).toEqual(['montserrat', 'montserrat_semibold', 'roboto']);
    expect(model.names.fontIds).toEqual([3]);
    expect(configNames({}).fonts).toBeUndefined();
    expect(Object.keys(model.names.styles!)).toEqual(['button', 'displayitem', 'list', 'alarm', 'big', 'tiny', 'parts', 'panel']);
    // What fb.style.set can set: the blocks "alarm" has (main, "title"), and its icon colour.
    expect(model.names.styles!.alarm).toEqual([
      'color', 'text-align', 'letter-spacing', 'text-decoration', 'opacity', 'background-color',
      'title.color', 'title.text-align', 'title.letter-spacing', 'title.text-decoration', 'title.opacity', 'title.background-color',
      'icon.color',
    ]);
    expect(model.names.styles!.displayitem).toContain('icon.color');
    expect(Object.keys(configNames({}).styles!)).toEqual(['button', 'displayitem', 'list']);
  });

  it('a button: the label takes the main blocks, the top label the "title" blocks, the icon the "icon" colour', () => {
    const [a, b] = model.pages[0].buttons;
    expect(a).toMatchObject({
      labelFont: { face: 2, px: 48 }, topFont: { face: 1, px: 18 }, iconColor: '#ffff00',
      labelStyle: { color: 0xff0000, face: 2, px: 48 }, topStyle: { color: 0xff8080, align: 2 },
    });
    expect(b).toMatchObject(BUTTON_LOOK);
    expect(b.iconColor).toBeUndefined();
  });

  it('a display item: the kind\'s look, then its styles; the unit follows the value\'s font and keeps its colour', () => {
    expect(item('plain')).toMatchObject({
      valueFont: { face: 0, px: 24 }, unitFont: { face: 0, px: 24 }, labelFont: { face: 1, px: 14 },
      valueStyle: { color: 0xffffff }, unitStyle: { color: 0x00ff00 }, captionStyle: { color: 0xffd700 },
      // "displayitem" sets an icon colour for every item.
      iconColor: '#00ffff',
      // 24 px from the kind: step 2.
      pad: { captionPad: 4, boxPad: 3 },
    });
    expect(item('styled')).toMatchObject({
      valueFont: { face: 2, px: 48 }, unitFont: { face: 2, px: 10 }, labelFont: { face: 1, px: 14 },
      valueStyle: { color: 0xff0000 }, unitStyle: { color: 0x00ff00, px: 10 }, captionStyle: { color: 0xff8080, align: 2 },
      iconColor: '#ffff00', icon: 'sun', iconSize: 32,
      pad: { captionPad: 10, boxPad: 8 },
    });
    // 14 px that a style of the item sets: step 1.
    expect(item('small')).toMatchObject({ valueFont: { face: 0, px: 14 }, pad: { captionPad: 2, boxPad: 1 } });
    // The names the device cannot use are left out.
    expect(item('odd')).toMatchObject({ valueFont: { face: 2, px: 48 }, valueStyle: { color: 0xffffff } });
  });

  it('a display item without styles in a config without styles: 14 px, no padding of its own, no icon tint', () => {
    const plain = mapDisplayItem({ label: 'x', unit: 'u' })!.item;
    expect(plain).toMatchObject({
      valueFont: { face: 0, px: 14 }, unitFont: { face: 0, px: 14 }, labelFont: { face: 1, px: 14 },
      valueStyle: D[StyleElement.DisplayValue], captionStyle: D[StyleElement.DisplayCaption], unitStyle: D[StyleElement.DisplayUnit],
    });
    expect(plain.pad).toBeUndefined();
    expect(plain.iconColor).toBeUndefined();
    // The removed fields are not read.
    const old = mapDisplayItem({ fontsize: 4, font: 'montserrat_semibold', labelfontsize: 3, iconcolor: '#ff0000', icon: 'sun' })!.item;
    expect(old).toMatchObject({ valueFont: { face: 0, px: 14 }, labelFont: { face: 1, px: 14 } });
    expect(old.iconColor).toBeUndefined();
    expect(old).not.toHaveProperty('fontSize');
  });

  it('a list: the rows take the main blocks, the selected row the "selected" blocks and the rows\' font', () => {
    const [a, b] = model.pages[0].lists!;
    expect(a).toMatchObject({
      font: { face: 0, px: 32 }, selectedFont: { face: 0, px: 40 },
      style: { color: 0x808080, lineHeight: 200 }, selectedStyle: { color: 0, bg: 0xffffd700, px: 40 },
    });
    expect(a.style.bg).toBeUndefined();
    expect(b).toMatchObject({
      font: { face: 2, px: 48 }, selectedFont: { face: 2, px: 40 },
      style: { color: 0x808080, lineHeight: 200, bg: 0xff0000ff }, selectedStyle: { color: 0xffffff, bg: 0x8000c853 },
    });
  });

  it('the selected row as it is drawn: the rows\' opacity, letter-spacing, text-decoration and text-align', () => {
    const styles = fontStyles({ styles: {
      under: { 'text-decoration': 'underline', opacity: 0.5, 'letter-spacing': 4, 'text-align': 'center', color: '#ff0000', 'background-color': '#000080' },
      own: { selected: { 'text-decoration': 'none', opacity: 1, 'text-align': 'right' } },
      list: { selected: { 'letter-spacing': 1 } },
    } });
    const row = (style: string) => mapList({ name: 'l', items: ['a'], style }, styles)!.selectedStyle;
    // "list" sets a spacing for the selected row; nothing else.
    expect(row('')).toEqual({ color: 0, bg: 0xffffd700, letterSpace: 1 });
    expect(listSelectedRow(styles, [])).toEqual(row(''));
    // The rows' decoration, opacity and alignment; not their colour, background or (set by "selected") spacing.
    expect(row('under')).toEqual({ color: 0, bg: 0xffffd700, letterSpace: 1, decor: 1, opa: 128, align: 2 });
    // What "selected" sets wins; its "text-align" is ignored: the rows'.
    expect(row('under own')).toEqual({ color: 0, bg: 0xffffd700, letterSpace: 1, decor: 0, opa: 255, align: 2 });
    expect(row('own')).toEqual({ color: 0, bg: 0xffffd700, letterSpace: 1, decor: 0, opa: 255 });
  });
});
