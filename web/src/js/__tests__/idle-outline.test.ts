/**
 * The outlines the virtual device shows while nothing runs, laid out from
 * the config model (what it uses until the firmware in the page gave its own
 * layout): names, positions, sizes. And the display geometry behind the
 * face's proportions.
 */
import { describe, it, expect } from 'vitest';
import { configToModel, configListKind, mapClock } from '../config-model';
import { buttonName, clockOrigin, displayItemName, displayItemSize, pageOutlines } from '../idle-outline';
import { BEZEL_W, FACE_W, KEY_H, KEY_SCALE, KEY_W, MAIN_SCALE, SCREEN_W, squarePixels } from '../display-geometry';

const outlines = (cfg: unknown, page = 1) => pageOutlines(configToModel(cfg).pages.find(p => p.number === page)!);

describe('names', () => {
  it('a display item: its name, else "#<displayitemid>", else its place in config.json; never its label', () => {
    const boxes = outlines({ displayitems: [
      { name: 'temp', displayitemid: 3, label: 'Temperature' },
      { displayitemid: 4, label: 'Humidity' },
      { displayitemid: 'wind', label: 'Wind' },
      { label: 'Rain' },
    ] });
    expect(boxes.map(b => b.name)).toEqual(['temp', '#4', '#wind', 'displayitems[3]']);
    expect(boxes.map(b => b.ref)).toEqual([0, 1, 2, 3].map(index => ({ kind: 'displayitem', list: 'displayitems', index })));
  });

  it('an entry without a ref (the built-in sample): its place on the page', () => {
    expect(displayItemName({}, 1)).toBe('displayitems[1]');
  });

  it('a key: its name, else "Key <position>"', () => {
    expect(buttonName({ name: 'light', position: 3 })).toBe('light');
    expect(buttonName({ position: 3 })).toBe('Key 3');
    expect(buttonName({ name: '', position: 8 })).toBe('Key 8');
  });

  it('lists and images by their name; the clock is "clock"', () => {
    const boxes = outlines({
      lists: [{ name: 'scenes', items: ['Morning'] }],
      images: [{ name: 'radar', src: 'radar.bin' }],
      clocks: [{ clockid: '1' }],
    });
    expect(boxes.map(b => [b.kind, b.name])).toEqual([['clock', 'clock'], ['list', 'scenes'], ['image', 'radar']]);
  });
});

describe('boxes on the 320 x 240 main display', () => {
  it('a display item: x, y, width in percent, from the corner its "align" names', () => {
    const [left, right, centre] = outlines({ displayitems: [
      { name: 'a', x: 10, y: 20, width: 40, align: 1 },
      { name: 'b', x: -10, y: 50, width: 20, align: 3 },
      { name: 'c', x: 0, y: 0, width: 50, align: 9 },
    ] });
    expect([left!.left, left!.top, left!.width]).toEqual([32, 48, 128]);
    // Top right: 64 px wide, 32 px from the right edge.
    expect([right!.left, right!.top, right!.width]).toEqual([224, 120, 64]);
    expect(centre!.left).toBe(80);
    expect(centre!.top).toBe(Math.trunc((240 - centre!.height) / 2));
  });

  it('a display item\'s height is an estimate from the fonts of its styles: a caption line when it has a label, a value line, the paddings', () => {
    const item = (raw: unknown, styles: unknown = {}) => configToModel({ styles, displayitems: [raw] }).pages[0]!.displayItems[0]!;
    const bare = displayItemSize(item({ name: 'a' })).height;
    const labelled = displayItemSize(item({ name: 'a', label: 'Temp' })).height;
    const big = displayItemSize(item({ name: 'a', label: 'Temp', style: 'big' }, { big: { 'font-size': 40 } })).height;
    expect(bare).toBeGreaterThanOrEqual(14);
    expect(bare).toBe(Math.round(14 * 1.22));
    expect(labelled).toBeGreaterThan(bare);
    expect(big).toBeGreaterThanOrEqual(Math.round((14 + 40) * 1.22));
  });

  it('a list: its rows high; an image: its size, 40 x 40 without one, in pixels', () => {
    const [list, sized, unsized] = outlines({
      lists: [{ name: 'l', x: 4, y: 10, width: 50, rows: 3 }],
      images: [{ name: 'a', x: 250, y: 150, width: 48, height: 32, icon: 'sun' }, { name: 'b', x: 5, y: 6, src: 'b.bin' }],
    });
    expect([list!.left, list!.top, list!.width]).toEqual([12, 24, 160]);
    expect(list!.height).toBeGreaterThan(3 * 14);
    expect([sized!.left, sized!.top, sized!.width, sized!.height]).toEqual([250, 150, 48, 32]);
    expect([unsized!.left, unsized!.top, unsized!.width, unsized!.height]).toEqual([5, 6, 40, 40]);
  });

  it('pages: an entry of page 0 (or less) is on every page', () => {
    const cfg = { displayitems: [{ name: 'all', page: 0 }, { name: 'one', page: 1 }, { name: 'two', page: 2 }] };
    expect(outlines(cfg, 1).map(b => b.name)).toEqual(['all', 'one']);
    expect(outlines(cfg, 2).map(b => b.name)).toEqual(['all', 'two']);
  });
});

describe('the clock (config.json "clocks")', () => {
  it('the firmware\'s defaults: 260 x 260 at 0, 0 on page 1', () => {
    expect(mapClock({})).toEqual({ page: 1, x: 0, y: 0, width: 260, height: 260, align: 1 });
    expect(mapClock('no')).toBeNull();
  });

  it('pixels from the top left; "align": 9 centres it and x and y are not used', () => {
    expect(clockOrigin(mapClock({ x: 30, y: 10, width: 100, height: 100 })!)).toEqual({ left: 30, top: 10 });
    expect(clockOrigin(mapClock({ x: 30, y: 10, width: 100, height: 100, align: 9 })!)).toEqual({ left: 110, top: 70 });
    // Another align does not move it (ui_clock.c sets the position only).
    expect(clockOrigin(mapClock({ x: 30, y: 10, width: 100, height: 100, align: 3 })!)).toEqual({ left: 30, top: 10 });
  });

  it('only the first clock is drawn; it has a ref, and its page is a page of the device', () => {
    const model = configToModel({ clocks: [{ page: 2, width: 100, height: 100 }, { page: 1 }] });
    expect(model.pages.map(p => p.number)).toEqual([1, 2]);
    expect(model.pages[0]!.clocks).toBeUndefined();
    expect(model.pages[1]!.clocks).toEqual([{ page: 2, x: 0, y: 0, width: 100, height: 100, align: 1, ref: { kind: 'clock', list: 'clocks', index: 0 } }]);
    expect(configListKind('clocks')).toBe('clock');
  });
});

describe('the face\'s proportions (display-geometry.ts)', () => {
  it('a key\'s display is drawn at 0.75 of the main display\'s scale: 120 x 60 CSS px', () => {
    expect(MAIN_SCALE).toBe(1);
    expect(KEY_SCALE).toBeCloseTo(0.75, 6);
    expect([KEY_W * KEY_SCALE, KEY_H * KEY_SCALE].map(n => Math.round(n * 100) / 100)).toEqual([120, 60]);
  });

  it('the main display is about 0.77 of the face\'s width', () => {
    expect(FACE_W).toBe(416);
    expect(SCREEN_W / FACE_W).toBeCloseTo(0.77, 2);
    expect(BEZEL_W * 2 + SCREEN_W).toBe(FACE_W);
  });

  it('square pixels only where a device pixel is a whole number of screen pixels', () => {
    expect(squarePixels(MAIN_SCALE, 1)).toBe(true);
    expect(squarePixels(MAIN_SCALE, 2)).toBe(true);
    expect(squarePixels(MAIN_SCALE, 1.5)).toBe(false);
    expect(squarePixels(MAIN_SCALE, 0.8)).toBe(false);
    expect(squarePixels(KEY_SCALE, 1)).toBe(false);
    expect(squarePixels(KEY_SCALE, 2)).toBe(false);
    expect(squarePixels(KEY_SCALE, 4)).toBe(true);
  });
});
