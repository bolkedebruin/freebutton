// @vitest-environment node
/**
 * DeviceState: built from config.json, changed by a script through the
 * firmware's bindings, observable change by change.
 */
import { describe, expect, it } from 'vitest';
import { configToModel } from '../config-model';
import { DeviceState, frontLedIndex, overlayPages, type StateChange } from '../emulator';
import { BASIC_CONFIG, makeEmulator } from './emulator-helpers';

describe('DeviceState from config.json', () => {
  it('has the items, keys, lists, images, brokers and topology of the config', () => {
    const state = new DeviceState(BASIC_CONFIG);
    expect(state.deviceName).toBe('Test Button');
    expect(state.connectors).toEqual([2, 1, 0, 0]);
    expect(state.physicalKeys()).toEqual([1, 2, 3, 4]);
    expect(state.brokers).toEqual([{ index: 0, name: 'local', url: 'mqtt://broker.test/', hassDiscovery: false }]);
    expect(state.items).toEqual([
      {
        id: 7, ref: { kind: 'displayitem', list: 'displayitems', index: 0 }, name: 'temp',
        x: 10, y: 20, width: 40, boxType: 0, fontSize: 3, align: 1, round: 0, page: 1,
        label: 'Temperature', value: '', unit: '°C', icon: '', font: null,
      },
      {
        id: 9, ref: { kind: 'displayitem', list: 'displayitems', index: 1 }, name: 'power',
        x: 50, y: 20, width: 40, boxType: 2, fontSize: 0, align: 0, round: 1, page: 2,
        label: 'Power', value: '', unit: 'W', icon: '', font: null,
      },
    ]);
    expect(state.keys.map(k => [k.position, k.name, k.label, k.topLabel])).toEqual([
      [1, undefined, 'M-L', 'Main'], [2, undefined, 'M-R', ''], [3, 'light', 'Light', 'Room'], [4, 'blind', 'Blind', ''],
    ]);
    expect(state.lists).toEqual([{
      index: 0, ref: { kind: 'list', list: 'lists', index: 0 }, name: 'scenes',
      items: ['Morning', 'Evening', 'Night'], selected: 1, font: null,
    }]);
    expect(state.images).toEqual([{
      index: 0, ref: { kind: 'image', list: 'images', index: 0 }, name: 'weather', kind: 'icon', value: 'sun',
    }]);
    expect(state.leds).toHaveLength(12);
    expect(state.shownPage).toBe(1);
    expect(state.visibleItems().map(i => i.name)).toEqual(['temp']);
  });

  it('entries carry the same ref as the IDE\'s preview model, so a renderer can overlay them', () => {
    const state = new DeviceState(BASIC_CONFIG);
    const model = configToModel(BASIC_CONFIG);
    const previewRefs = model.pages.flatMap(p => [
      ...p.displayItems.map(i => i.ref), ...p.buttons.map(b => b.ref), ...(p.lists ?? []).map(l => l.ref),
      ...(p.images ?? []).map(m => m.ref),
    ]);
    const stateRefs = [...state.items, ...state.keys, ...state.lists, ...state.images].map(e => e.ref);
    for (const ref of stateRefs) expect(previewRefs).toContainEqual(ref);
    expect(stateRefs).toHaveLength(8);
  });

  it('gives display items the ids the firmware gives', () => {
    const ids = (items: unknown[]): number[] => new DeviceState({ displayitems: items }).items.map(i => i.id);
    expect(ids([{}, {}, {}])).toEqual([1, 2, 3]);
    // A numeric string counts; auto ids start above every configured id, and
    // every item takes a number from the counter.
    expect(ids([{}, { displayitemid: '5' }, {}])).toEqual([6, 5, 8]);
    // A duplicate gets the next free id.
    expect(ids([{ displayitemid: 4 }, { displayitemid: 4 }])).toEqual([4, 7]);
    expect(ids([{ displayitemid: 'x' }, { displayitemid: 0 }])).toEqual([1, 2]);
  });

  it('reads both spellings of the lists, and takes the first valid name', () => {
    const state = new DeviceState({
      info: { connectors: [{ id: 0, type: 1 }] },
      mqttbuttons: [{ position: 1, label: 'old', name: 'k' }],
      buttons: [{ position: 2, label: 'new', name: 'k' }, { position: 1, label: 'again' }, { position: 5, label: 'no module' }],
      mqttdisplays: [{ name: 'a', label: 'legacy' }, { name: '1bad' }],
      displayitems: [{ name: 'a', label: 'dup' }],
    });
    expect(state.keys.map(k => [k.position, k.label, k.name])).toEqual([[1, 'old', 'k'], [2, 'new', undefined]]);
    expect(state.host.keys.findByName('k')).toBe(1);
    expect(state.items.map(i => [i.label, i.name])).toEqual([['legacy', 'a'], ['', undefined], ['dup', undefined]]);
    expect(state.item('a')!.label).toBe('legacy');
    expect(state.host.display.findByName('1bad')).toBeNull();
  });

  it('infers the modules when the config has no info.connectors', () => {
    const state = new DeviceState({ buttons: [{ position: 7, label: 'a' }, { position: 8, label: 'b' }] });
    // A bar where the keys are, the main display on the first free connector.
    expect(state.connectors).toEqual([2, 0, 0, 1]);
    expect(new DeviceState({}, { connectors: [1, 2] }).connectors).toEqual([1, 2, 0, 0]);
  });

  it('survives a config that is not an object', () => {
    for (const config of [null, 42, 'x', []]) {
      const state = new DeviceState(config);
      expect(state.items).toEqual([]);
      expect(state.brokers).toEqual([]);
    }
  });
});

describe('a script changes the state through the firmware bindings', () => {
  it('display items: every setter, with one change record each', async () => {
    const { emu, rec } = await makeEmulator();
    rec.changes.length = 0;
    emu.run(`
      var d = fb.main.display;
      d.setValue("temp", "21.5"); d.setLabel("temp", "Inside"); d.setUnit(7, "C"); d.setIcon("temp", "sun");
      d.setPosition("temp", 5, 6); d.setPage("temp", 2); d.setFont("temp", "montserrat_semibold", 24);
      d.setValue("temp", "21.5");
      d.showPage(2);
    `);
    expect(emu.state.item('temp')).toMatchObject({
      value: '21.5', label: 'Inside', unit: 'C', icon: 'sun', x: 5, y: 6, page: 2, font: { face: 1, size: 24 },
    });
    expect(rec.changes).toEqual<StateChange[]>([
      { kind: 'reset' },
      { kind: 'item', id: 7, field: 'value' },
      { kind: 'item', id: 7, field: 'label' },
      { kind: 'item', id: 7, field: 'unit' },
      { kind: 'item', id: 7, field: 'icon' },
      { kind: 'item', id: 7, field: 'position' },
      { kind: 'item', id: 7, field: 'page' },
      { kind: 'item', id: 7, field: 'font' },
      { kind: 'page', page: 2 },
    ]);
    expect(emu.state.visibleItems().map(i => i.name)).toEqual(['temp', 'power']);
  });

  it('display items: create, get, getAll, count, remove', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run(`
      var d = fb.main.display;
      var id = d.create(1, 2, 30, 4, 5, 6, "Made", "u", 2, 3);
      print(id, d.count(), JSON.stringify(d.getAll()), JSON.stringify(d.get(id)));
      d.setLabel(id, "shown");
      print(d.get(id).label, d.get(99), d.setValue(99, "x"), d.remove(99));
      print(d.remove("power"), d.count(), d.remove(id), d.count());
    `);
    expect(rec.console).toEqual([
      '10 3 [7,9,10] {"id":10,"x":1,"y":2,"width":30,"boxType":4,"fontSize":5,"alignment":6,"label":"Made","unit":"u","round":2,"page":3}',
      // get() reports the configured caption, not the one shown.
      'Made null false false',
      'true 2 true 1',
    ]);
    expect(rec.changes.filter(c => c.kind === 'item-added' || c.kind === 'item-removed')).toEqual([
      { kind: 'item-added', id: 10 }, { kind: 'item-removed', id: 9 }, { kind: 'item-removed', id: 10 },
    ]);
  });

  it('the firmware\'s argument checks and error texts apply', async () => {
    const { emu } = await makeEmulator();
    emu.run('');
    const message = (code: string): string => {
      const result = emu.eval(code);
      return result.ok ? `ok ${result.value}` : result.error.message;
    };
    expect(message('fb.main.display.setValue("nope", "1")')).toBe('ReferenceError: unknown display item "nope"');
    expect(message('fb.main.display.setIcon("temp", "not an icon!")')).toBe('TypeError: invalid icon name "not an icon!"');
    expect(message('fb.main.display.setIcon("temp", "' + 'a'.repeat(32) + '")')).toMatch(/^TypeError: invalid icon name/);
    expect(message('fb.main.display.setIcon("temp", "' + 'a'.repeat(31) + '")')).toBe('ok true');
    expect(message('fb.main.display.setFont("temp", "comic", 12)')).toBe('ReferenceError: unknown font "comic"');
    expect(message('fb.main.display.setFont("temp", "montserrat", 73)')).toBe('TypeError: display.setFont(): the size must be an integer 1..72');
    expect(message('fb.main.display.showPage(0)')).toBe('RangeError: display.showPage(): page must be 1 or more');
    expect(message('fb.key("nobody")')).toBe('ReferenceError: unknown key "nobody"');
    expect(message('fb.key("light").display.setIcon("bad name")')).toBe('TypeError: invalid icon name "bad name"');
    expect(message('fb.main.list.next("nolist")')).toBe('ReferenceError: unknown list "nolist"');
    expect(message('fb.main.image.setIcon("nothere", "sun")')).toBe('ReferenceError: unknown image "nothere"');
    expect(message('fb.main.image.setSrc("weather", "' + 'k'.repeat(97) + '")')).toMatch(/^TypeError/);
    expect(message('fb.bars[0].left.led.setColor(1, 2)')).toBe('TypeError: setColor requires r, g, b');
    expect(message('typeof fb.bars[1]')).toBe('ok undefined');
    expect(message('typeof fb.main.left.led')).toBe('ok undefined');
  });

  it('keys: labels, top labels, icons and fonts; getLabel reads them back', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run(`
      var k = fb.key("light").display;
      print(k.getLabel(), k.getTopLabel());
      k.setLabel("On"); k.setTopLabel("Lamp"); k.setIcon("moon"); k.setFont("montserrat", 3); k.setTopFont("montserrat", 18);
      print(k.getLabel(), k.getTopLabel());
      fb.main.left.display.setLabel("main key");
      k.setFont("");
    `);
    expect(rec.console).toEqual(['Light Room', 'On Lamp']);
    expect(emu.state.key(3)).toMatchObject({ label: 'On', topLabel: 'Lamp', icon: 'moon', font: null, topFont: { face: 0, size: 18 } });
    expect(emu.state.key(1)!.label).toBe('main key');
    expect(rec.changes.filter(c => c.kind === 'key').map(c => c.kind === 'key' && `${c.position}:${c.field}`)).toEqual([
      '3:label', '3:topLabel', '3:icon', '3:font', '3:topFont', '1:label', '3:font',
    ]);
  });

  it('LEDs: four per bar, front and back, left and right', async () => {
    const { emu } = await makeEmulator();
    emu.run(`
      var bar = fb.bars[0];
      bar.left.led.setColor(1, 2, 3); bar.left.backLed.setColor(4, 5, 6);
      bar.right.led.on(); bar.right.backLed.setColor(300, -1, 7);
      bar.left.led.off();
    `);
    expect(emu.state.leds.slice(0, 5)).toEqual([
      { index: 0, r: 0, g: 0, b: 0 },
      { index: 1, r: 4, g: 5, b: 6 },
      { index: 2, r: 255, g: 255, b: 255 },
      { index: 3, r: emu.state.leds[3]!.r, g: emu.state.leds[3]!.g, b: 7 },
      { index: 4, r: 0, g: 0, b: 0 },
    ]);
  });

  it('lists: selection clamped by the firmware, items cut to its limits', async () => {
    const { emu, rec } = await makeEmulator();
    emu.run(`
      var l = fb.main.list;
      print(l.count("scenes"), l.getSelected("scenes"), l.next("scenes"), l.next("scenes"), l.prev("scenes"), l.select("scenes", 0), l.select("scenes", 3));
      var many = []; for (var i = 0; i < 40; i++) many.push("item " + i);
      print(l.setItems("scenes", many), l.count("scenes"));
      l.setItems("scenes", ["${'é'.repeat(30)}", "two\\nlines"]);
      l.setFont("scenes", "montserrat", 24);
    `);
    expect(rec.console).toEqual(['3 1 2 2 1 true false', 'true 32']);
    const list = emu.state.list('scenes')!;
    // 48 bytes at most, cut on a character boundary; a line break becomes a space.
    expect(list.items).toEqual(['é'.repeat(24), 'two lines']);
    expect(list.selected).toBe(0);
    expect(list.font).toEqual({ face: 0, size: 24 });
  });

  it('images: an icon or a key of the images bucket', async () => {
    const { emu, rec } = await makeEmulator();
    rec.changes.length = 0;
    emu.run('fb.main.image.setSrc("weather", "radar.bin"); fb.main.image.setIcon("weather", "");');
    expect(emu.state.image('weather')).toMatchObject({ kind: 'none', value: '' });
    expect(rec.changes.filter(c => c.kind === 'image')).toHaveLength(2);
  });

  it('without lists or images in the config there is no fb.main.list / fb.main.image', async () => {
    const { emu } = await makeEmulator({ config: { info: BASIC_CONFIG.info, buttons: BASIC_CONFIG.buttons } });
    expect(emu.eval('typeof fb.main.list + " " + typeof fb.main.image')).toEqual({ ok: true, value: 'undefined undefined' });
  });
});

describe('sensors', () => {
  it('are the device\'s; onChange hears a new reading at the next sample, once it changed enough', async () => {
    const { emu, clock, rec } = await makeEmulator();
    emu.run(`
      var found = [];
      sensor.forEach(function (id, type) { found.push(id + ":" + type); });
      print(sensor.count(), found.join(","), JSON.stringify(sensor.getInfo(5)), sensor.getValue(5), sensor.getType(4));
      sensor.onChange(5, function (value) { print("changed", value); });
    `);
    expect(rec.console).toEqual([
      '3 3:pressure,4:humidity,5:temperature {"id":5,"name":"BME280 Temperature","type":"temperature","unit":"°C","online":true} 21.5 humidity',
    ]);
    clock.advance(1000);
    expect(rec.console[rec.console.length - 1]).toBe('changed 21.5');
    emu.setSensor(5, 21.55);
    clock.advance(1000);
    expect(rec.console).toHaveLength(2); // less than the firmware's 0.1 threshold
    emu.setSensor(5, 22.5);
    clock.advance(999);
    expect(rec.console).toHaveLength(2);
    clock.advance(1);
    expect(rec.console[rec.console.length - 1]).toBe('changed 22.5');
    expect(rec.changes.filter(c => c.kind === 'sensor')).toEqual([{ kind: 'sensor', id: 5 }, { kind: 'sensor', id: 5 }]);
    expect(emu.setSensor(99, 1)).toBe(false);
  });
});

describe('overlayPages: the running device in the preview model', () => {
  it('is the config model when nothing ran', () => {
    const model = configToModel(BASIC_CONFIG);
    const pages = overlayPages(model.pages, new DeviceState(BASIC_CONFIG));
    expect(pages.map(p => p.number)).toEqual([1, 2]);
    expect(pages[0]!.displayItems.map(i => [i.name, i.label, i.value, i.unit, i.x, i.y])).toEqual([['temp', 'Temperature', '', '°C', 10, 20]]);
    expect(pages[0]!.buttons).toEqual(model.pages[0]!.buttons);
    expect(pages[0]!.lists).toEqual(model.pages[0]!.lists);
    expect(pages[0]!.images).toEqual(model.pages[0]!.images);
  });

  it('carries what a script changed, and leaves the model alone', async () => {
    const { emu } = await makeEmulator();
    const model = configToModel(BASIC_CONFIG);
    const before = JSON.stringify(model.pages);
    emu.run(`
      var d = fb.main.display;
      d.setValue("temp", "21.5"); d.setLabel("temp", "Inside"); d.setPosition("temp", 1, 2); d.setIcon("temp", "sun");
      d.remove("power");
      d.create(5, 6, 30, 0, 2, 1, "Made", "kW", 0, 3);
      fb.key("light").display.setLabel("On"); fb.key("light").display.setIcon("moon");
      fb.bars[0].left.led.setColor(255, 128, 0); fb.bars[0].right.backLed.setColor(9, 9, 9);
      fb.main.list.setItems("scenes", ["A", "B"]); fb.main.list.select("scenes", 1);
      fb.main.image.setSrc("weather", "radar.bin");
      d.showPage(3);
    `);
    const pages = overlayPages(model.pages, emu.state);
    expect(JSON.stringify(model.pages)).toBe(before);
    expect(pages.map(p => p.number)).toEqual([1, 2, 3]);
    expect(emu.state.shownPage).toBe(3);

    const temp = pages[0]!.displayItems[0]!;
    expect(temp).toMatchObject({ name: 'temp', label: 'Inside', value: '21.5', x: 1, y: 2, icon: 'sun', id: '7' });
    expect(temp.ref).toEqual({ kind: 'displayitem', list: 'displayitems', index: 0 });
    // The removed config item is gone from its page, the created one is on page 3.
    expect(pages[1]!.displayItems).toEqual([]);
    expect(pages[2]!.displayItems).toEqual([
      { x: 5, y: 6, width: 30, height: 0, type: 'text', fontSize: 2, align: 1, id: '10', label: 'Made', value: '', unit: 'kW' },
    ]);
    const light = pages[0]!.buttons.find(b => b.position === 3)!;
    expect(light).toMatchObject({ label: 'On', topLabel: 'Room', icon: 'moon', ledOn: true, ledColor: 'rgb(255,128,0)' });
    expect(light.backLedColor).toBeUndefined();
    // The back LED of key 4 is carried next to its front LED, which stays off.
    expect(pages[0]!.buttons.find(b => b.position === 4)).toMatchObject({ label: 'Blind', ledOn: false, backLedColor: 'rgb(9,9,9)' });
    expect(pages[0]!.lists![0]).toMatchObject({ name: 'scenes', items: ['A', 'B'], selected: 1 });
    expect(pages[0]!.images![0]).toMatchObject({ name: 'weather', src: 'radar.bin' });
    expect(pages[0]!.images![0]!.icon).toBeUndefined();
  });

  it('carries a font a script set: the face at its size in px, over the configured one', () => {
    const state = new DeviceState(BASIC_CONFIG);
    const model = configToModel(BASIC_CONFIG);
    const id = state.item('temp')!.id;
    const configured = overlayPages(model.pages, state)[0]!;
    state.host.display.setFont(id, { face: 1, size: 32 });
    state.host.keys.setFont(3, { face: 1, size: 2 }, false);
    state.host.keys.setFont(3, { face: 0, size: 12 }, true);
    state.host.lists.setFont(0, { face: 1, size: 0 });
    const page = overlayPages(model.pages, state)[0]!;
    expect(page.displayItems[0]!.valueFont).toEqual({ face: 1, px: 32, configured: true });
    const light = page.buttons.find(b => b.position === 3)!;
    expect(light.labelFont).toEqual({ face: 1, px: 24, configured: true });   // fontsize 2 = the 24 px step
    expect(light.topFont).toEqual({ face: 0, px: 12, configured: true });
    // Size 0: the face changes, the size stays what the list had.
    expect(page.lists![0]!.font).toEqual({ face: 1, px: configured.lists![0]!.font!.px, configured: true });
    // Entries without a script font keep the configured one.
    expect(page.buttons.find(b => b.position === 4)!.labelFont).toEqual(configured.buttons.find(b => b.position === 4)!.labelFont);
    state.host.display.restoreFont(id);
    expect(overlayPages(model.pages, state)[0]!.displayItems[0]!.valueFont).toEqual(configured.displayItems[0]!.valueFont);
  });

  it('maps a key to its front LED: four LEDs per bar, in bar order', () => {
    const state = new DeviceState({}, { connectors: [1, 2, 0, 1] });
    expect([1, 2, 3, 4, 7, 8].map(position => frontLedIndex(state, position))).toEqual([0, 2, -1, -1, 4, 6]);
  });
});
