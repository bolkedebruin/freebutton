import { describe, it, expect } from 'vitest';
import { configToModel, parseConfigText, positionsForConnector, mapButton, mapDisplayItem } from '../config-model';

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
    expect(configToModel({}).pages).toEqual([{ number: 1, buttons: [], displayItems: [] }]);
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
