// @vitest-environment node
/**
 * The displays, as the firmware's own LVGL draws them in the wasm: frames,
 * pixels, layout rectangles, media, the canvas helper, and golden images of
 * what the device shows (__tests__/golden/*.png, compared pixel for pixel;
 * emulator-pixels.ts says how to renew them).
 */
import { existsSync, readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import {
  attachCanvas, Emulator, ManualClock, MediaCache, type CanvasContextLike, type MediaSource, type Screenshot,
} from '../emulator';
import { BASIC_CONFIG, emulatorModule, makeEmulator, readRepoFile, repoPath, VICTRON_PORTAL, deviceId, withDeviceIds } from './emulator-helpers';
import { compareGolden, isPanelColours, litPixels, repoMedia, testRaster } from './emulator-pixels';

const AT = Date.UTC(2026, 0, 15, 9, 10, 30);   // 10:10:30 on the device (Central European Time)
const MODULES = { connectors: [{ id: 0, type: 2 }, { id: 1, type: 1 }] };
const BROKER = [{ brokerid: 'local', url: 'mqtt://broker.test/', port: 1883, hassdiscovery: false }];
/** The fonts table of the real devices: the icon font as fallback of both system fonts. */
const FONTS = [
  { id: 1, name: 'mdi', src: 'icons.ttf' },
  { name: 'montserrat', fallback: 'mdi' },
  { name: 'montserrat_semibold', fallback: 'mdi' },
];
/** A style per value size the tests use: an item names one where it had a size of its own. */
const SIZES = Object.fromEntries([14, 18, 24, 32, 48, 72].map(px => [`size${px}`, { 'font-size': px }]));

async function device(config: Record<string, unknown>, options: { media?: MediaSource; script?: string } = {}) {
  const made = await makeEmulator({
    config: { info: MODULES, brokers: BROKER, styles: SIZES, ...config },
    clock: new ManualClock(AT),
    media: options.media ?? repoMedia({}, { icons: true }),
  });
  if (options.script !== undefined) {
    const result = made.emu.run(options.script);
    expect(result).toEqual({ ok: true });
  }
  return made;
}

/** The display is the golden image, and every colour is one the panel has. */
function expectGolden(emu: Emulator, display: string, name: string): Screenshot {
  const shot = emu.screenshot(display);
  expect(isPanelColours(shot)).toBe(true);
  expect(compareGolden(name, shot)).toBeNull();
  return shot;
}

describe('displays', () => {
  it('are the firmware\'s: the main display and one per bar key, in device pixels', async () => {
    const { emu } = await makeEmulator({ media: repoMedia() });
    expect(emu.displays()).toEqual([
      { id: 'main', index: 0, kind: 'main', width: 320, height: 240 },
      { id: 'key3', index: 1, kind: 'key', width: 160, height: 80, position: 3 },
      { id: 'key4', index: 2, kind: 'key', width: 160, height: 80, position: 4 },
    ]);
    expect(emu.readPixels('main')).toHaveLength(320 * 240 * 4);
    expect(emu.readPixels(1)).toHaveLength(160 * 80 * 4);
    expect(() => emu.readPixels('key5')).toThrow('no display "key5"');
  });

  it('show the configured state when create() resolves, without a script (the design-time preview)', async () => {
    const { emu } = await makeEmulator({ media: repoMedia() });
    expect(emu.running).toBe(false);
    // Read without drawing: the first frame is already there.
    const main = { width: 320, height: 240, data: emu.readPixels('main') };
    const key = { width: 160, height: 80, data: emu.readPixels('key3') };
    expect(litPixels(main)).toBeGreaterThan(2000);
    expect(litPixels(key)).toBeGreaterThan(500);
    expect(isPanelColours(main)).toBe(true);
  });

  it('a rectangle of readPixels is that part of the frame, clipped to the display', async () => {
    const { emu } = await makeEmulator({ media: repoMedia() });
    const all = emu.readPixels('key3');
    const part = emu.readPixels('key3', { x: 40, y: 30, width: 50, height: 20 });
    expect(part).toHaveLength(50 * 20 * 4);
    for (let row = 0; row < 20; row++) {
      const from = ((30 + row) * 160 + 40) * 4;
      expect(Array.from(part.subarray(row * 200, row * 200 + 200))).toEqual(Array.from(all.subarray(from, from + 200)));
    }
    expect(emu.readPixels('key3', { x: 150, y: 70, width: 50, height: 50 })).toHaveLength(10 * 10 * 4);
    expect(emu.readPixels('key3', { x: 500, y: 0, width: 5, height: 5 })).toHaveLength(0);
  });

  it('a change is drawn by the LVGL task within its refresh period, and only where it changed', async () => {
    const { emu, clock } = await makeEmulator({ media: repoMedia() });
    emu.run('');
    clock.advance(100);
    const frames: Array<{ display: string; x: number; y: number; width: number; height: number }> = [];
    emu.on('frame', frame => frames.push(frame));
    const before = emu.readPixels('key3');
    emu.eval('fb.button("light").display.setLabel("Off")');
    // The LVGL task runs after the script's turn and then when its refresh timer is due (33 ms).
    clock.advance(50);
    expect(frames.length).toBeGreaterThan(0);
    expect(frames.every(f => f.display === 'key3')).toBe(true);
    // The label's rows, not the top label above it.
    expect(Math.min(...frames.map(f => f.y))).toBeGreaterThan(20);
    expect(Array.from(emu.readPixels('key3'))).not.toEqual(Array.from(before));
    // Nothing more is drawn, and no timer polls, while nothing changes.
    frames.length = 0;
    clock.advance(5000);
    expect(frames).toEqual([]);
    expect(clock.timers).toBe(0);
  });

  it('render() draws what is pending at once; screenshot() does that first', async () => {
    const { emu } = await makeEmulator({ media: repoMedia() });
    emu.run('fb.button("light").display.setLabel("Dim")');
    const before = emu.readPixels('key3');
    const shot = emu.screenshot('key3');
    expect(Array.from(shot.data)).not.toEqual(Array.from(before));
    expect(Array.from(emu.readPixels('key3'))).toEqual(Array.from(shot.data));
  });

  it('stats() has LVGL\'s pool, as /api/device reports it', async () => {
    const { emu } = await makeEmulator({ media: repoMedia() });
    const stats = emu.stats();
    expect(stats.lvglTotal).toBeGreaterThan(50_000);
    expect(stats.lvglTotal).toBeLessThanOrEqual(61_440);   // LV_MEM_SIZE of src/lv_conf.h
    expect(stats.lvglFree).toBeGreaterThan(20_000);
    expect(stats.lvglLowest).toBeLessThanOrEqual(stats.lvglFree);
    expect(stats).toMatchObject({ textSkipped: 0, textCollapsed: 0 });
    expect(stats.mediaBytes).toBeGreaterThan(390_000);     // medium.ttf + semibold.ttf, once
    expect(stats.fontInstances).toBeGreaterThan(0);
    expect(stats.wasmMemoryBytes).toBeGreaterThanOrEqual(2 * 1024 * 1024);
  });
});

describe('layout: where LVGL put each config entry', () => {
  it('has a rectangle per widget of the shown page, with the config ref', async () => {
    const { emu } = await makeEmulator({ media: repoMedia({}, { icons: true }) });
    const main = emu.layout('main');
    expect(main.map(r => `${r.kind}:${r.id}`)).toEqual(['displayitem:7', 'list:0', 'image:0']);
    // x 10 %, y 20 %, width 40 % of 320x240; the height is the content's (caption, value and unit rows).
    expect(main[0]).toMatchObject({
      ref: { kind: 'displayitem', list: 'displayitems', index: 0 }, x: 32, y: 48, width: 128, page: 1, visible: true,
    });
    expect(main[0]!.height).toBeGreaterThan(40);
    expect(main[1]).toMatchObject({ ref: { kind: 'list', list: 'lists', index: 0 }, x: 0, y: 0, width: 160 });
    expect(main[2]).toMatchObject({ ref: { kind: 'image', list: 'images', index: 0 }, x: 0, y: 0, width: 48, height: 48 });
    expect(emu.layout('key3')).toEqual([
      { ref: { kind: 'button', list: 'buttons', index: 2 }, kind: 'button', id: 3, x: 0, y: 0, width: 160, height: 80, page: 1, visible: true },
    ]);
  });

  it('follows the page, a script and the content, with a layout event', async () => {
    const { emu } = await makeEmulator({ media: repoMedia() });
    const events: string[] = [];
    emu.on('layout', event => events.push(`${event.display}:${event.rects.map(r => r.id).join(',')}`));
    emu.run('fb.main.display.showPage(2)');
    // Page 2 has the other item; the list and the image are on page 1.
    const power = emu.state.item('power')!.id;
    expect(emu.layout('main').map(r => `${r.kind}:${r.id}:${r.page}`)).toEqual([`displayitem:${power}:2`]);
    expect(emu.layout('main')[0]!.ref).toEqual({ kind: 'displayitem', list: 'displayitems', index: 1 });
    expect(events[events.length - 1]).toBe(`main:${power}`);
    const before = emu.layout('main')[0]!;
    emu.eval('fb.main.display.setPosition("power", 0, 50)');
    expect(emu.layout('main')[0]).toMatchObject({ x: 0, y: 120, width: before.width });
    // No caption: the box is as high as what is left.
    emu.eval('fb.main.display.setLabel("power", "")');
    emu.render();
    expect(emu.layout('main')[0]!.height).toBeLessThan(before.height);
  });
});

describe('media: the fonts and images the firmware reads', () => {
  it('create() waits for the files the config uses when the source answers later', async () => {
    const asked: string[] = [];
    const files = repoMedia({}, { icons: true });
    const media: MediaSource = {
      read: (bucket, key) => {
        asked.push(`${bucket}/${key}`);
        return new Promise(resolve => { setTimeout(() => resolve(files.read(bucket, key) as Uint8Array | null), 1); });
      },
    };
    const emu = await Emulator.create({
      config: { ...BASIC_CONFIG, fonts: FONTS }, wasm: emulatorModule(), identity: 'virt_btn_0a1b2c', clock: new ManualClock(AT),
      sampleSensors: false, media,
    });
    // The firmware named them: the system fonts, the style's font, the image's raster. Each asked once.
    expect([...asked].sort()).toEqual(['fonts/icons.ttf', 'fonts/medium.ttf', 'fonts/semibold.ttf', 'images/ico-sun-48x48.bin']);
    expect(emu.state.fonts.map(f => `${f.name}:${f.loaded}`)).toEqual(['montserrat:true', 'montserrat_semibold:true', 'mdi:true']);
    expect(emu.state.item('temp')!.box!.caption.font).toMatchObject({ font: 'montserrat_semibold', px: 14, real: true });
    // The same frame as with files that are there at once.
    const direct = await makeEmulator({ config: { ...BASIC_CONFIG, fonts: FONTS }, clock: new ManualClock(AT), media: files });
    expect(Array.from(emu.readPixels('main'))).toEqual(Array.from(direct.emu.readPixels('main')));
    expect(emu.stats().mediaBytes).toBe(direct.emu.stats().mediaBytes);
  });

  it('one MediaCache serves every emulator: a file is read once', async () => {
    let reads = 0;
    const files = repoMedia();
    const cache = new MediaCache({ read: (bucket, key) => { reads++; return files.read(bucket, key); } });
    const first = await makeEmulator({ media: cache });
    const after = reads;
    expect(after).toBeGreaterThan(0);
    const second = await makeEmulator({ media: cache });
    expect(reads).toBe(after);
    expect(first.emu.media).toBe(cache);
    expect(Array.from(second.emu.readPixels('key3'))).toEqual(Array.from(first.emu.readPixels('key3')));
    expect(cache.bytes).toBeGreaterThan(390_000);
  });

  it('a font file that is not there: the firmware draws the text in its built-in font', async () => {
    const { emu } = await device({
      fonts: [{ name: 'fancy', src: 'nosuch.ttf' }],
      styles: { ...SIZES, fancy: { 'font-family': 'fancy' } },
      buttons: [{ position: 3, label: 'Light', toplabel: 'Room' }],
      displayitems: [{ name: 'a', x: 5, y: 10, width: 90, style: 'size48 fancy', label: 'Fancy', unit: 'kWh' }],
    }, { script: 'fb.main.display.setValue("a", "12.5")' });
    expect(emu.state.fonts.find(f => f.name === 'fancy')).toMatchObject({ src: 'nosuch.ttf', loaded: false });
    // The item still names its font and size; what draws is LVGL's 12 px font.
    expect(emu.state.item('a')!.box!.value.font).toMatchObject({ font: 'fancy', px: 48, real: false, lineHeight: 15 });
    expectGolden(emu, 'main', 'font-missing-main');
  });

  it('no font files at all (an empty fonts bucket): everything in the built-in font', async () => {
    const { emu } = await device({
      buttons: [{ position: 3, label: 'Light', toplabel: 'Room' }],
      displayitems: [{ name: 'a', x: 5, y: 10, width: 90, style: 'size48', label: 'Power', unit: 'W' }],
    }, { media: { read: () => null }, script: 'fb.main.display.setValue("a", "230")' });
    expect(emu.state.fonts.every(f => f.loaded === false)).toBe(true);
    expectGolden(emu, 'main', 'font-none-main');
    expectGolden(emu, 'key3', 'font-none-key');
  });

  it('without the media option: no fonts, and icons take their room without pixels', async () => {
    const { emu } = await makeEmulator();
    expect(emu.state.fonts.every(f => f.loaded === false)).toBe(true);
    expect(emu.state.image('weather')!.raster).toMatchObject({ name: 'sun', width: 48, height: 48 });
    expect(emu.layout('main').find(r => r.kind === 'image')).toMatchObject({ width: 48, height: 48 });
    // Nothing of it is drawn: the frame is that of a device without the raster.
    const without = await makeEmulator({ mediaExists: () => false });
    expect(without.emu.state.image('weather')!.raster).toBeNull();
    expect(Array.from(emu.readPixels('main'))).toEqual(Array.from(without.emu.readPixels('main')));
  });

  it('a file that arrives or changes later is shown as after an upload to the device', async () => {
    const cache = new MediaCache(repoMedia());
    const { emu, clock } = await makeEmulator({
      media: cache,
      config: {
        info: MODULES, brokers: BROKER, icons: { sun: '/icons/sun.svg' },
        buttons: [{ position: 3, label: 'Light', toplabel: 'Room' }],
        images: [{ name: 'weather', x: 0, y: 0, width: 48, height: 48, icon: 'sun' }],
      },
    });
    emu.run('');
    clock.advance(100);
    // The image's raster was not there: nothing drawn, no size.
    expect(emu.state.image('weather')!.raster).toBeNull();
    expect(litPixels({ width: 48, height: 48, data: emu.readPixels('main', { x: 0, y: 0, width: 48, height: 48 }) })).toBe(0);
    cache.set('images', 'ico-sun-48x48.bin', testRaster(48, 48, 'rgb565a8'));
    clock.advance(100);
    expect(emu.state.image('weather')!.raster).toMatchObject({ name: 'sun', width: 48, height: 48 });
    expect(litPixels({ width: 48, height: 48, data: emu.readPixels('main', { x: 0, y: 0, width: 48, height: 48 }) })).toBeGreaterThan(1000);
    // A font: read again 1.5 s after the last change (the firmware's rule), then the text is redrawn.
    const before = Array.from(emu.readPixels('key3'));
    cache.set('fonts', 'medium.ttf', new Uint8Array(repoMedia().read('fonts', 'semibold.ttf') as Uint8Array));
    clock.advance(1000);
    expect(Array.from(emu.readPixels('key3'))).toEqual(before);
    clock.advance(1000);
    expect(Array.from(emu.readPixels('key3'))).not.toEqual(before);
    // And the poll that waited for it sleeps again.
    clock.advance(5000);
    expect(clock.timers).toBe(0);
  });
});

describe('attachCanvas', () => {
  function fakeCanvas() {
    const calls: Array<{ x: number; y: number; width: number; height: number }> = [];
    const pixels = new Uint8ClampedArray(160 * 80 * 4);
    const context: CanvasContextLike = {
      createImageData: (width, height) => ({ width, height, data: new Uint8ClampedArray(width * height * 4) }),
      putImageData: (image, dx, dy) => {
        calls.push({ x: dx, y: dy, width: image.width, height: image.height });
        for (let row = 0; row < image.height; row++) {
          pixels.set(image.data.subarray(row * image.width * 4, (row + 1) * image.width * 4), ((dy + row) * 160 + dx) * 4);
        }
      },
    };
    const canvas = { width: 300, height: 150, getContext: () => context };
    return { canvas, calls, pixels };
  }

  it('draws the whole display once, then only the rectangles that change, one canvas pixel per device pixel', async () => {
    const { emu, clock } = await makeEmulator({ media: repoMedia() });
    emu.run('');
    clock.advance(100);
    const { canvas, calls, pixels } = fakeCanvas();
    const detach = attachCanvas(emu, 'key3', canvas);
    expect(canvas).toMatchObject({ width: 160, height: 80 });
    expect(calls).toEqual([{ x: 0, y: 0, width: 160, height: 80 }]);
    expect(Array.from(pixels)).toEqual(Array.from(emu.readPixels('key3')));

    calls.length = 0;
    emu.eval('fb.button("light").display.setLabel("Off")');
    clock.advance(50);
    expect(calls.length).toBeGreaterThan(0);
    expect(calls.every(c => c.width * c.height < 160 * 80)).toBe(true);
    // The canvas is the display again, from the dirty rectangles alone.
    expect(Array.from(pixels)).toEqual(Array.from(emu.readPixels('key3')));

    // Another display's frames are not drawn here; after detach nothing is.
    calls.length = 0;
    emu.eval('fb.button("blind").display.setLabel("Up")');
    clock.advance(50);
    expect(calls).toEqual([]);
    detach();
    emu.eval('fb.button("light").display.setLabel("On")');
    clock.advance(50);
    expect(calls).toEqual([]);
  });

  it('refuses a display the device does not have, and a canvas without a 2d context', async () => {
    const { emu } = await makeEmulator({ media: repoMedia() });
    expect(() => attachCanvas(emu, 'key7', fakeCanvas().canvas)).toThrow('no display "key7"');
    expect(() => attachCanvas(emu, 'main', { width: 1, height: 1, getContext: () => null })).toThrow('no 2d context');
  });
});

describe('golden images: what the device shows', () => {
  it('display items: each box type, with caption, value and unit', async () => {
    const { emu } = await device({
      fonts: FONTS,
      displayitems: [
        { name: 'line', x: 2, y: 2, width: 45, boxtype: 0, style: 'size32', label: 'Underlined', unit: '°C' },
        { name: 'plain', x: 52, y: 2, width: 45, boxtype: 1, style: 'size32', label: 'Plain', unit: 'W' },
        { name: 'small', x: 2, y: 40, width: 45, boxtype: 2, style: 'size14', label: 'Type 2', unit: 'kWh' },
        { name: 'bare', x: 52, y: 40, width: 45, boxtype: 1, style: 'size24' },
        { name: 'big', x: 2, y: 62, width: 96, boxtype: 0, style: 'size72', unit: '%' },
      ],
    }, {
      script: `var d = fb.main.display;
        d.setValue("line", "21.5"); d.setValue("plain", "1250"); d.setValue("small", "13.8"); d.setValue("bare", "no caption");
        d.setValue("big", "94");`,
    });
    expectGolden(emu, 'main', 'boxes-main');
  });

  it('display items: every alignment of the box on the display', async () => {
    // lv_align_t: 1 top left, 2 top middle, 3 top right, 4 bottom left, 5 bottom middle, 6 bottom right, 7 left middle, 8 right middle, 9 centre.
    const { emu } = await device({
      fonts: FONTS,
      displayitems: [1, 2, 3, 4, 5, 6, 7, 8, 9].map(align => ({
        name: `a${align}`, x: 0, y: 0, width: 28, boxtype: 0, style: 'size18', align, label: `align ${align}`,
      })),
    }, { script: 'for (var i = 1; i <= 9; i++) fb.main.display.setValue("a" + i, "v" + i);' });
    expectGolden(emu, 'main', 'align-main');
    // Nine boxes of the same size at nine places.
    const places = new Set(emu.layout('main').map(r => `${r.x},${r.y}`));
    expect(places.size).toBe(9);
  });

  it('text markup: colours, sizes, fonts, alignment and icons of the icon font', async () => {
    const { emu } = await device({
      fonts: FONTS,
      buttons: [{ position: 3, label: '\\cff0000Red \\c00ff00Green', toplabel: '\\c00ffffCyan' }, { position: 4, label: '\u{F0335}', toplabel: 'Lamp' }],
      displayitems: [
        { name: 'colours', x: 2, y: 2, width: 96, style: 'size24' },
        { name: 'sizes', x: 2, y: 20, width: 96, style: 'size14' },
        { name: 'icons', x: 2, y: 42, width: 96, style: 'size32' },
        { name: 'right', x: 2, y: 62, width: 96, style: 'size18' },
        { name: 'centre', x: 2, y: 76, width: 96, style: 'size18', label: '\\acCentred caption' },
      ],
    }, {
      script: `var d = fb.main.display;
        d.setValue("colours", "\\\\cff0000red \\\\c00ff00green \\\\c0000ffblue \\\\cffff00yellow");
        d.setValue("sizes", "small \\\\s{32}large \\\\s{18}\\\\f{montserrat_semibold}bold");
        d.setValue("icons", "\\\\c00ff00\\u{F07E4} \\\\c87ceeb\\u{F058E} \\\\cffd700\\u{F0599}\\\\cffffff 21\\u00b0");
        d.setValue("right", "\\\\arto the right");
        d.setValue("centre", "\\\\acin the middle");`,
    });
    expectGolden(emu, 'main', 'markup-main');
    expectGolden(emu, 'key3', 'markup-key-rich');
    expectGolden(emu, 'key4', 'markup-key-icon-glyph');
  });

  it('text-align in a style places a text exactly as \\al, \\ac and \\ar at its start do', async () => {
    const items = (styled: boolean) => [
      { name: 'c', x: 2, y: 2, width: 96, boxtype: 0, style: styled ? 'size24 centre' : 'size24', label: styled ? 'Caption' : '\\arCaption', unit: 'W' },
      { name: 'r', x: 2, y: 40, width: 96, style: styled ? 'size18 right' : 'size18' },
      { name: 'l', x: 2, y: 60, width: 96, style: styled ? 'size18 left' : 'size18' },
      // The text's own \\a wins over the style's.
      { name: 'o', x: 2, y: 80, width: 96, style: styled ? 'size18 centre' : 'size18' },
    ];
    const styles = {
      ...SIZES,
      centre: { 'text-align': 'center', title: { 'text-align': 'right' } },
      right: { 'text-align': 'right' },
      left: { 'text-align': 'left' },
    };
    const markup = await device({ fonts: FONTS, styles, displayitems: items(false) }, {
      script: `var d = fb.main.display;
        d.setValue("c", "\\\\acin the middle"); d.setValue("r", "\\\\arto the right"); d.setValue("l", "\\\\alto the left");
        d.setValue("o", "\\\\arover the style");`,
    });
    const styled = await device({ fonts: FONTS, styles, displayitems: items(true) }, {
      script: `var d = fb.main.display;
        d.setValue("c", "in the middle"); d.setValue("r", "to the right"); d.setValue("l", "to the left");
        d.setValue("o", "\\\\arover the style");`,
    });
    const a = markup.emu.screenshot('main');
    const b = styled.emu.screenshot('main');
    expect(litPixels(a)).toBeGreaterThan(1000);
    expect(Array.from(b.data)).toEqual(Array.from(a.data));
    // And it is not where the same texts are without either.
    const plain = await device({ fonts: FONTS, styles, displayitems: items(false).map(i => ({ ...i, label: i.label?.replace('\\ar', '') })) }, {
      script: `var d = fb.main.display;
        d.setValue("c", "in the middle"); d.setValue("r", "to the right"); d.setValue("l", "to the left");
        d.setValue("o", "over the style");`,
    });
    expect(Array.from(plain.emu.screenshot('main').data)).not.toEqual(Array.from(a.data));
  });

  it('long text: a value wider than its box is clipped, a key label wraps', async () => {
    const { emu } = await device({
      fonts: FONTS,
      buttons: [{ position: 3, label: 'A label of many words', toplabel: 'A top label that is too long' }],
      displayitems: [
        { name: 'long', x: 2, y: 5, width: 60, boxtype: 0, style: 'size24', label: 'A caption that is longer than its box is wide', unit: 'unit' },
        { name: 'next', x: 2, y: 55, width: 96, style: 'size14', label: 'Below' },
      ],
    }, {
      script: `fb.main.display.setValue("long", "The quick brown fox jumps over the lazy dog");
        fb.main.display.setValue("next", "Lorem ipsum dolor sit amet, consectetur adipiscing elit");`,
    });
    expectGolden(emu, 'main', 'long-main');
    expectGolden(emu, 'key3', 'long-key');
    // The firmware cut the value to its slot (58 bytes) before drawing.
    expect(emu.state.item('next')!.value.length).toBeLessThanOrEqual(58);
  });

  it('a list: the roller with its selection, after its animation', async () => {
    const { emu, clock } = await device({
      fonts: FONTS,
      styles: { list: { 'font-size': 24, color: '#808080', selected: { 'background-color': '#ffd700', color: '#000000' } } },
      lists: [{ name: 'scenes', x: 4, y: 10, width: 92, rows: 3, items: ['Morning', 'Evening', 'Night', 'Away'], selected: 1 }],
    }, { script: '' });
    const rest = expectGolden(emu, 'main', 'list-main');
    expect(emu.layout('main')).toMatchObject([{ kind: 'list', x: 12, y: 24, width: 294 }]);

    // select() animates: frames come while the rows move, then it rests on the new row.
    let frames = 0;
    emu.on('frame', () => { frames++; });
    emu.eval('fb.main.list.select("scenes", 3)');
    clock.advance(120);
    expect(emu.stats().animations).toBe(1);
    expect(Array.from(emu.screenshot('main').data)).not.toEqual(Array.from(rest.data));   // on its way
    clock.advance(2000);
    expect(frames).toBeGreaterThan(3);
    expect(emu.stats().animations).toBe(0);
    expectGolden(emu, 'main', 'list-main-selected-last');
    expect(emu.state.list('scenes')!.roller!.selected).toBe(3);
    // The animation is over: no frames, no timer.
    frames = 0;
    clock.advance(3000);
    expect(frames).toBe(0);
    expect(clock.timers).toBe(0);
  });

  it('images and icons: a raster of one colour takes the text colour, a colour raster is drawn as it is', async () => {
    const { emu } = await device({
      fonts: FONTS,
      icons: { sun: '/icons/sun.svg', colorwheel: '/icons/colorwheel.svg' },
      buttons: [{ position: 3, label: 'Hidden', toplabel: 'Sun', icon: 'sun' }, { position: 4, label: 'Hidden', toplabel: 'Wheel', icon: 'colorwheel' }],
      images: [
        { name: 'mono', x: 5, y: 5, width: 64, height: 64, icon: 'sun' },
        { name: 'colour', x: 40, y: 5, width: 64, height: 64, icon: 'colorwheel' },
        { name: 'file', x: 70, y: 5, src: 'radar.bin' },
      ],
      displayitems: [{ name: 'withIcon', x: 5, y: 60, width: 90, style: 'size32', label: 'Icon before the value', unit: '°C', icon: 'colorwheel' }],
    }, {
      media: repoMedia({ 'images/radar.bin': testRaster(80, 60, 'rgb565a8') }, { icons: true }),
      script: 'fb.main.display.setValue("withIcon", "21.5")',
    });
    expectGolden(emu, 'main', 'images-main');
    expectGolden(emu, 'key3', 'images-key-mono');
    expectGolden(emu, 'key4', 'images-key-colour');
    expect(emu.layout('main').filter(r => r.kind === 'image').map(r => `${r.width}x${r.height}`)).toEqual(['64x64', '64x64', '80x60']);
  });

  it('the analog clock at 10:10:30, and a script\'s digital clock beside it', async () => {
    const { emu, clock } = await device({
      fonts: FONTS,
      clocks: [{ x: 0, y: 0, width: 180, height: 180, align: 9, page: 1 }],
      displayitems: [{ name: 'time', x: 2, y: 2, width: 30, style: 'size24' }, { name: 'date', x: 70, y: 2, width: 30, style: 'size14' }],
    }, {
      script: `function two(n) { return n < 10 ? "0" + n : "" + n; }
        var now = new Date();
        fb.main.display.setValue("time", two(now.getHours()) + ":" + two(now.getMinutes()));
        fb.main.display.setValue("date", now.getDate() + "-" + (now.getMonth() + 1) + "-" + now.getFullYear());`,
    });
    expectGolden(emu, 'main', 'clock-main-101030');
    expect(emu.layout('main').find(r => r.kind === 'clock')).toMatchObject({ x: 70, y: 30, width: 180, height: 180 });
    // The hands follow the device's clock: 15 s later the second hand is a quarter further.
    clock.advance(15_000);
    expectGolden(emu, 'main', 'clock-main-101045');
  });

  it('key displays: label, top label, an icon instead of the label, a key without top label', async () => {
    const { emu } = await device({
      fonts: FONTS,
      info: { connectors: [{ id: 0, type: 2 }, { id: 1, type: 1 }, { id: 2, type: 1 }] },
      icons: { sun: '/icons/sun.svg' },
      buttons: [
        { position: 3, label: 'Open', toplabel: 'Window' },
        { position: 4, label: 'Dicht' },
        { position: 5, label: 'Not shown', toplabel: 'Icon', icon: 'sun' },
      ],
    }, { script: '' });
    expectGolden(emu, 'key3', 'key-label-toplabel');
    expectGolden(emu, 'key4', 'key-label-only');
    expectGolden(emu, 'key5', 'key-icon');
    // A bar key without a config entry: its display stays black, as on the device.
    expect(litPixels(emu.screenshot('key6'))).toBe(0);
  });

  it('a page switch: the items of the other page, and the key labels of that page', async () => {
    const { emu } = await device({
      fonts: FONTS,
      buttons: [{ position: 3, label: 'One', toplabel: 'Page 1', page: 1 }, { position: 3, label: 'Two', toplabel: 'Page 2', page: 2 }],
      displayitems: [
        { name: 'first', x: 5, y: 10, width: 90, style: 'size32', page: 1, label: 'Page one' },
        { name: 'second', x: 5, y: 50, width: 90, style: 'size32', page: 2, label: 'Page two' },
        { name: 'both', x: 5, y: 80, width: 90, style: 'size14', page: 0, label: 'On every page' },
      ],
    }, { script: 'fb.main.display.setValue("first", "1"); fb.main.display.setValue("second", "2"); fb.main.display.setValue("both", "0");' });
    expectGolden(emu, 'main', 'page-1-main');
    emu.eval('fb.main.display.showPage(2)');
    expectGolden(emu, 'main', 'page-2-main');
    expect(emu.layout('main').map(r => r.page).sort()).toEqual([0, 2]);
    expectGolden(emu, 'key3', 'page-key');
  });

  it('a script changes things: values, labels, styles, position, an item it creates, a key\'s label', async () => {
    const { emu, clock } = await device({
      fonts: FONTS,
      styles: { ...SIZES, semibold: { 'font-family': 'montserrat_semibold' } },
      buttons: [{ name: 'light', position: 3, label: 'Light', toplabel: 'Room' }],
      displayitems: [{ name: 'temp', x: 5, y: 5, width: 60, style: 'size24', label: 'Temperature', unit: '°C' }],
    }, { script: '' });
    expectGolden(emu, 'main', 'script-before-main');
    emu.eval(`var d = fb.main.display;
      d.setValue("temp", "21.5"); d.setLabel("temp", "Inside"); d.setUnit("temp", "C"); d.setStyle("temp", "size48 semibold");
      d.setPosition("temp", 30, 40);
      d.create(2, 2, 60, 1, "size18", 1, "Made by the script", "", 0, 1);
      fb.button("light").display.setLabel("\\\\cffd700On"); fb.button("light").display.setTopLabel("Kitchen");
      setTimeout(function () { d.setValue("temp", "22.0"); }, 500);`);
    expectGolden(emu, 'main', 'script-after-main');
    expectGolden(emu, 'key3', 'script-after-key');
    clock.advance(600);
    expectGolden(emu, 'main', 'script-timer-main');
  });

  it('LVGL pool low: a value in several formats is drawn in one, then not at all (the firmware\'s rule)', async () => {
    const { emu, rec } = await device({
      fonts: FONTS,
      displayitems: [{ name: 'v', x: 5, y: 10, width: 90, style: 'size32', label: 'Formats' }],
    }, { script: 'fb.main.display.setValue("v", "\\\\cff0000A\\\\c00ff00B\\\\c0000ffC")' });
    expectGolden(emu, 'main', 'pool-ok-main');
    expect(emu.state.item('v')!.box!.value.runs).toHaveLength(3);

    // Too little left for one label per format (the firmware keeps 4 KB + 256 B per label free), enough for one.
    const free = emu.stats().lvglFree;
    emu.holdLvglMemory(free - 4200);
    emu.eval('fb.main.display.setValue("v", "\\\\cff0000D\\\\c00ff00E\\\\c0000ffF")');
    emu.render();
    expect(emu.stats()).toMatchObject({ textCollapsed: 1, textSkipped: 0 });
    expect(emu.state.item('v')!.box!.value.runs.map(r => r.text)).toEqual(['DEF']);
    expectGolden(emu, 'main', 'pool-collapsed-main');

    // 3 KB left: no room for the text at all. The old text is gone, the caption stays.
    emu.holdLvglMemory(free - 3000);
    emu.eval('fb.main.display.setValue("v", "GHI")');
    emu.render();
    expect(emu.stats().textSkipped).toBeGreaterThan(0);
    expect(emu.state.item('v')!.box!.value.runs).toEqual([]);
    expect(rec.logs.some(line => /pool low/.test(line))).toBe(true);
    expectGolden(emu, 'main', 'pool-skipped-main');

    // Memory back: the firmware draws the value again at its next change.
    emu.holdLvglMemory(0);
    emu.eval('fb.main.display.setValue("v", "\\\\cff0000A\\\\c00ff00B\\\\c0000ffC")');
    expectGolden(emu, 'main', 'pool-ok-main');
  });
});

/** Fonts of the repo and of the device's folder; icon rasters by rule (the IDE renders the device's SVGs in a browser). */
function deviceMedia(device: string): MediaSource {
  const repo = repoMedia({}, { icons: true });
  return {
    read: (bucket, key) => {
      if (bucket === 'fonts') {
        const own = readRepoBytes(`devices/${device}/fonts/${key}`);
        if (own) return own;
      }
      return repo.read(bucket, key);
    },
  };
}

function readRepoBytes(relative: string): Uint8Array | null {
  const path = repoPath(relative);
  return existsSync(path) ? new Uint8Array(readFileSync(path)) : null;
}

const REAL = [
  {
    device: 'btn_a00001',
    inject: [
      ['buttonplus/btn_a00001/display/outside_temperature', '9'],
      ['buttonplus/btn_a00001/display/date', '12 Nov 2026'],
      ['buttonplus/btn_a00001/display/carbon_dioxide', '720'],
      ['buttonplus/btn_a00001/display/humidity', '55'],
      ['buttonplus/btn_a00001/display/volume_level_percent', '12'],
      ['buttonplus/btn_a00001/filter/state', 'ok'],
      ['home/weather/current/condition', 'rainy'],
      ['home/weather/forecast/today/1/condition', 'partlycloudy'],
      [`victron/N/${VICTRON_PORTAL}/system/0/Dc/Battery/Soc`, '{"value":94.0}'],
      [`victron/N/${VICTRON_PORTAL}/system/0/Dc/Battery/Power`, '{"value":1000.0}'],
      [`victron/N/${VICTRON_PORTAL}/system/0/Ac/Grid/L1/Power`, '{"value":17.0}'],
      [`victron/N/${VICTRON_PORTAL}/system/0/Ac/PvOnGrid/L1/Power`, '{"value":1400.0}'],
      [`victron/N/${VICTRON_PORTAL}/system/0/Ac/Consumption/L1/Power`, '{"value":227.0}'],
    ],
    shots: ['main', 'key3', 'key5'],
  },
  {
    device: 'btn_a00002',
    inject: [
      ['buttonplus/btn_a00001/display/outside_temperature', '6\u00b0'],
      ['buttonplus/btn_a00001/display/date', '10 Nov 2026'],
      ['buttonplus/btn_a00001/display/carbon_dioxide', '\\c00ff00\u{F07E4}'],
      ['buttonplus/btn_a00001/display/humidity', '\\c87ceeb\u{F058E}'],
      ['buttonplus/btn_a00001/filter/state', 'ok'],
      ['home/weather/current/icon', '\u{F0595}'],
      ['home/weather/forecast/today/1/icon', '\u{F0590}'],
      ['buttonplus/btn_a00001/display/media_title', 'Love Me Not'],
      ['buttonplus/btn_a00001/display/media_artist', 'Ravyn Lenae'],
      ['buttonplus/btn_a00001/display/volume_level_percent', '12'],
    ],
    // No golden image of its main display: that shows the name of a place.
    shots: ['key7', 'key8'],
  },
] as const;

describe('golden images: the real device configs with their scripts', () => {
  for (const real of REAL) {
    const config = readRepoFile(`devices/${deviceId(real.device)}/config.json`);
    const script = readRepoFile(withDeviceIds(`devices/${real.device}/${real.device}.js`));
    it.skipIf(config === null || script === null)(`${real.device}: the config alone, then its script after its topics`, async () => {
      const clock = new ManualClock(AT);
      const emu = await Emulator.create({
        config: config!, wasm: emulatorModule(), identity: 'virt_btn_0a1b2c', clock, sampleSensors: false,
        media: deviceMedia(deviceId(real.device)),
      });
      const problems: string[] = [];
      emu.on('error', error => problems.push(error.message));
      // The design-time preview: what the config shows before any script.
      if ((real.shots as readonly string[]).includes('main')) expectGolden(emu, 'main', `${real.device}-idle-main`);
      expect(emu.run(script!)).toEqual({ ok: true });
      clock.advance(1000);
      for (const [topic, payload] of real.inject) emu.broker(0).inject(withDeviceIds(topic), payload);
      clock.advance(3000);
      for (const display of real.shots) expectGolden(emu, display, `${real.device}-run-${display}`);
      if (real.device === 'btn_a00001') {
        // The key right of the main display: the next page (the home battery).
        emu.click(2);
        clock.advance(3000);
        expect(emu.state.shownPage).toBe(2);
        expectGolden(emu, 'main', `${real.device}-run-main-page2`);

        // Page 3, the weather forecast: now (from the current condition and the
        // outside temperature above) and three days, today first (the clock's Thursday).
        emu.broker(0).inject('home/weather/forecast/daily3',
          'do|rainy|10|6|70|3.2;vr|partlycloudy|7|2|20|0.4;za|sunny|5|-1|0|0');
        emu.click(2);
        clock.advance(3000);
        expect(emu.state.shownPage).toBe(3);
        expectGolden(emu, 'main', `${real.device}-run-main-page3`);

        // Page 4, the scenes: the title and the list, opened on the scene for the
        // hour (10:10, "Vertrek"), after the roller's animation. The six keys
        // show their role on this page (the script changes them 150 ms later).
        emu.click(2);
        clock.advance(3000);
        expect(emu.state.shownPage).toBe(4);
        expectGolden(emu, 'main', `${real.device}-run-main-page4`);
        const keys = emu.displays().filter(display => display.kind === 'key');
        expect(keys).toHaveLength(6);
        for (const key of keys) expectGolden(emu, key.id, `${real.device}-run-${key.id}-page4`);

        // Page 5, good morning: Home Assistant asks for it (the page keys do not
        // reach it). Date and time are the injected clock's.
        emu.broker(0).inject(withDeviceIds('buttonplus/btn_a00001/display/morning'), 'show');
        clock.advance(3000);
        expect(emu.state.shownPage).toBe(5);
        expectGolden(emu, 'main', `${real.device}-run-main-page5`);
        // The keys have their own labels again.
        for (const display of real.shots) {
          if (display !== 'main') expectGolden(emu, display, `${real.device}-run-${display}`);
        }
      }
      expect(problems).toEqual([]);
      const stats = emu.stats();
      expect(stats).toMatchObject({ textSkipped: 0, textCollapsed: 0 });
      // The pool is as tight as on the device: these configs leave 10 to 25 KB of its 59 KB
      // (btn_a00001: 9.6 KB up to its battery page, 7.9 KB on its filled forecast page).
      expect(stats.lvglLowest).toBeGreaterThan(7_000);
      expect(stats.lvglLowest).toBeLessThan(30_000);
    });
  }
});
