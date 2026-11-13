import { describe, it, expect } from 'vitest';
import {
  configToModel, parseConfigText, positionsForConnector, mapButton, mapDisplayItem,
  configNames, validateConfigNames, NAME_PATTERN, mapList,
} from '../config-model';
import { StyleElement } from '../style-config';

describe('config.json -> button model', () => {
  it('maps label and toplabel per position/page', () => {
    const model = configToModel({
      buttons: [
        { id: 0, position: 7, buttonuid: '7-1', page: 1, label: 'Open', toplabel: 'Raam' },
        { id: 1, position: 8, page: 1, label: 'Dicht', toplabel: 'Raam' },
        { id: 2, position: 5, page: 2, label: 'Lamp' },
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

  it('falls back to topLabel (older files) and prefers toplabel', () => {
    expect(mapButton({ position: 1, topLabel: 'Old' })?.topLabel).toBe('Old');
    expect(mapButton({ position: 1, toplabel: 'New', topLabel: 'Old' })?.topLabel).toBe('New');
  });

  it('reads legacy mqttbuttons when buttons is absent', () => {
    const model = configToModel({ mqttbuttons: [{ position: 3, label: 'Legacy', topLabel: 'T' }] });
    expect(model.pages[0].buttons[0]).toMatchObject({ position: 3, label: 'Legacy', topLabel: 'T', page: 1 });
  });

  it('skips buttons without a valid position and defaults page to 1', () => {
    const model = configToModel({ buttons: [{ label: 'x' }, { position: -1 }, { position: '4', label: 'str' }, null] });
    expect(model.pages[0].buttons).toEqual([
      { position: 4, page: 1, label: 'str', topLabel: '', ledOn: false, ledColor: 'rgb(0,0,0)' },
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
        { displayitemid: '1', x: 0, y: 0, page: 0, fontsize: 4, align: 1, width: 50, label: 'Home', unit: '' },
        { displayitemid: '2', x: 10, y: 60, page: 2, fontsize: 1, width: 35, label: 'Temp', unit: '°C' },
      ],
    });
    expect(model.pages[0].displayItems.map(d => d.id)).toEqual(['1']);
    expect(model.pages[1].displayItems.map(d => d.id)).toEqual(['1', '2']);
    expect(model.pages[1].displayItems[1]).toMatchObject({ x: 10, y: 60, width: 35, fontSize: 1, label: 'Temp', unit: '°C' });
    expect(model.pages[0].displayItems[0].align).toBe(1);
  });

  it('reads legacy mqttdisplays', () => {
    const model = configToModel({ mqttdisplays: [{ x: 1, y: 2, width: 3, label: 'L' }] });
    expect(model.pages[0].displayItems[0]).toMatchObject({ x: 1, y: 2, width: 3, label: 'L' });
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
        { id: 1, position: 8, page: 1, label: 'Dicht', toplabel: 'Raam' },
      ],
      displayitems: [{ displayitemid: '1', x: 0, y: 0, page: 0, width: 50, label: 'Home' }],
    });
    const model = parseConfigText(text)!;
    expect(model.connectors.map(c => c.type)).toEqual(['display', 'bar']);
    expect(model.pages[0].buttons.map(b => b.label)).toEqual(['Open', 'Dicht']);
    expect(model.pages[0].displayItems[0].label).toBe('Home');
  });
});

describe('key and display item names', () => {
  it('keeps "name" on mapped keys and display items', () => {
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
      mqttdisplays: [{ displayitemid: '1', name: 'volume' }, { displayitemid: '2', name: 'x'.repeat(25) }],
      displayitems: [{ displayitemid: '3', name: 'clock' }],
    });
    expect(names).toEqual({ keys: ['sceneUp', 'volume'], displayItems: ['volume', 'clock'], lists: [], images: [], icons: [] });
  });

  it('reads legacy mqttbuttons and is empty for a config without names', () => {
    expect(configNames({ mqttbuttons: [{ position: 1, name: 'old' }] }).keys).toEqual(['old']);
    expect(configNames({})).toEqual({ keys: [], displayItems: [], lists: [], images: [], icons: [] });
    expect(configNames(null)).toEqual({ keys: [], displayItems: [], lists: [], images: [], icons: [] });
  });

  it('configToModel carries the names', () => {
    const model = configToModel({ buttons: [{ position: 1, name: 'a' }], mqttdisplays: [{ name: 'b' }] });
    expect(model.names).toEqual({ keys: ['a'], displayItems: ['b'], lists: [], images: [], icons: [] });
  });

  it('NAME_PATTERN: letters, digits and _, not starting with a digit, 1..24 chars', () => {
    for (const ok of ['a', '_', 'sceneUp', 'A_1', 'x'.repeat(24)]) expect(NAME_PATTERN.test(ok)).toBe(true);
    for (const bad of ['', '1a', 'a-b', 'a b', 'é', 'x'.repeat(25)]) expect(NAME_PATTERN.test(bad)).toBe(false);
  });
});

describe('validateConfigNames', () => {
  it('accepts unique valid names; a key and a display item may share one', () => {
    expect(validateConfigNames({
      buttons: [{ position: 1, name: 'volume' }, { position: 2, name: 'mute' }, { position: 3 }],
      mqttdisplays: [{ displayitemid: '1', name: 'volume' }],
    })).toEqual([]);
  });

  it('reports invalid names with the entry', () => {
    expect(validateConfigNames({ buttons: [{ position: 1, name: '1up' }, { position: 2, name: 7 }] })).toEqual([
      'buttons[0].name "1up" is not a valid name (1-24 letters, digits or _, not starting with a digit)',
      'buttons[1].name 7 is not a valid name (1-24 letters, digits or _, not starting with a digit)',
    ]);
    expect(validateConfigNames({ mqttdisplays: [{ name: '' }] })[0]).toMatch(/^mqttdisplays\[0\]\.name "" is not a valid name/);
  });

  it('reports duplicates within keys and within display items, naming both entries', () => {
    expect(validateConfigNames({
      buttons: [{ position: 1, name: 'a' }, { position: 2, name: 'b' }, { position: 3, name: 'a' }],
      mqttdisplays: [{ name: 'v' }],
      displayitems: [{ name: 'v' }],
    })).toEqual([
      'buttons[2].name "a" is already used by buttons[0] (key names must be unique)',
      'displayitems[0].name "v" is already used by mqttdisplays[0] (display item names must be unique)',
    ]);
  });

  it('ignores non-objects', () => {
    expect(validateConfigNames(null)).toEqual([]);
    expect(validateConfigNames({ buttons: [null, 'x'] })).toEqual([]);
  });
});

describe('lists', () => {
  const scenes = {
    name: 'scenes', page: 2, x: 4, y: 18, width: 92, rows: 5, fontsize: 2,
    items: ['Bed Time', 'Evening', 'Movie'], selected: 1,
    color: 'AABBCC', focuscolor: '112233', focustextcolor: 'ffffff',
  };

  it('maps every field, colors as #rrggbb', () => {
    expect(mapList(scenes)).toEqual({
      name: 'scenes', page: 2, x: 4, y: 18, width: 92, rows: 5, fontSize: 2,
      items: ['Bed Time', 'Evening', 'Movie'], selected: 1,
      color: '#aabbcc', focusColor: '#112233', focusTextColor: '#ffffff',
    });
  });

  it('applies the defaults', () => {
    expect(mapList({ name: 'a', items: ['x'] })).toEqual({
      name: 'a', page: 1, x: 0, y: 0, width: 50, rows: 3, fontSize: 3,
      items: ['x'], selected: 0,
      color: '#808080', focusColor: '#ffd700', focusTextColor: '#000000',
    });
  });

  it('clamps rows, fontsize and selected; -1 without items; bad colors fall back', () => {
    const l = mapList({ name: 'a', rows: 12, fontsize: 0, items: ['x', 'y'], selected: 9, color: '#123456' })!;
    expect([l.rows, l.fontSize, l.selected, l.color]).toEqual([7, 1, 1, '#808080']);
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
    })).toEqual({ keys: ['scenes'], displayItems: [], lists: ['scenes', 'rooms'], images: [], icons: [] });
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

describe('fonts', () => {
  const cfg = {
    style: { fonts: [{ id: 1, name: 'roboto', src: 'roboto.ttf' }] },
    displayitems: [{ name: 'temp', font: 'roboto', fontsize: 40, labelfontsize: 2 }, { name: 'plain', fontsize: 3 }],
    buttons: [{ position: 1, label: 'a', font: 1, topfontsize: 20 }, { position: 2, label: 'b' }],
    lists: [{ name: 'l', font: 'roboto', fontsize: 2 }],
  };

  it('maps item fonts against the font table', () => {
    const model = configToModel(cfg);
    expect(model.fonts.faces.map(f => f.name)).toEqual(['montserrat', 'montserrat_semibold', 'roboto']);
    const [temp, plain] = model.pages[0]!.displayItems;
    expect(temp!.valueFont).toEqual({ face: 2, px: 40, configured: true });
    expect(temp!.labelFont).toEqual({ face: 1, px: 18, configured: false });
    expect(plain!.valueFont).toEqual({ face: 0, px: 32, configured: false });
    const [k1, k2] = model.pages[0]!.buttons;
    expect(k1!.labelFont).toEqual({ face: 2, px: 32, configured: true });
    expect(k1!.topFont).toEqual({ face: 1, px: 20, configured: false });
    // A key without font fields draws in the style's fonts (no font fields).
    expect(k2!.labelFont).toBeUndefined();
    expect(model.pages[0]!.lists![0]!.font).toEqual({ face: 2, px: 24, configured: true });
  });

  it('names: fonts and ids only with a "style.fonts" table', () => {
    expect(configNames(cfg)).toMatchObject({ fonts: ['montserrat', 'montserrat_semibold', 'roboto'], fontIds: [1] });
    expect(configNames({}).fonts).toBeUndefined();
    expect(configNames({ fonts: [{ name: 'r', src: 'r.ttf' }] }).fonts).toBeUndefined();
  });

  it('list fontsize keeps px sizes', () => {
    expect(mapList({ name: 'a', fontsize: 40 })!.fontSize).toBe(40);
    expect(mapList({ name: 'a', fontsize: 99 })!.fontSize).toBe(72);
  });
});

describe('style', () => {
  it('the model carries the resolved styles; items without fields take them', () => {
    const model = configToModel({
      style: {
        fonts: [{ name: 'roboto', src: 'roboto.ttf' }],
        text: { text_font: 'roboto' },
        title: { text_color: '#00ff00' },
        key: { label: { text_size: 40 } },
        list: { text_color: '#112233', selected: { bg_color: '#445566', text_color: '#ffffff' } },
      },
      displayitems: [{ name: 'd', fontsize: 2, labelfontsize: 2 }],
      buttons: [{ position: 1, label: 'a', fontsize: 20 }],
      lists: [{ name: 'l', items: ['x'] }, { name: 'm', color: 'aabbcc', focuscolor: '000000' }],
    });
    expect(model.styles[StyleElement.KeyTopLabel]!.color).toBe(0x00ff00);
    const [d] = model.pages[0]!.displayItems;
    expect(d!.valueFont).toEqual({ face: 2, px: 24, configured: false });
    expect(d!.labelFont).toEqual({ face: 1, px: 18, configured: false });
    expect(model.pages[0]!.buttons[0]!.labelFont).toEqual({ face: 2, px: 20, configured: false });
    const [l, m] = model.pages[0]!.lists!;
    expect([l!.color, l!.focusColor, l!.focusTextColor]).toEqual(['#112233', '#445566', '#ffffff']);
    expect(l!.font).toEqual({ face: 2, px: 32, configured: false });
    // Item colours are local: they win over the style.
    expect([m!.color, m!.focusColor, m!.focusTextColor]).toEqual(['#aabbcc', '#000000', '#ffffff']);
  });

  it('without a style: the defaults (gold #ffd700)', () => {
    const model = configToModel({});
    expect(model.styles[StyleElement.DisplayCaption]!.color).toBe(0xffd700);
    expect(model.styles[StyleElement.ListSelected]!.bgColor).toBe(0xffd700);
  });
});
