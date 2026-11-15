import { describe, it, expect } from 'vitest';
import {
  alignAnchor, boxOrigin, clampUnits, dragTo, hitTest, itemRect, realign, unitPx, unitRange, unitsAt,
} from '../display-geometry';

// The preview draws the 320 x 240 screen 283 px wide.
const SCALE = { x: 283 / 320, y: 283 / 320 };
/** CSS px of the preview for a number of units of a display item. */
const css = (ux: number, uy: number) => ({ dx: ux * 3.2 * SCALE.x, dy: uy * 2.4 * SCALE.y });

describe('display-geometry: units', () => {
  it('display items and lists are in % of 320 x 240, images in px', () => {
    expect(unitPx('displayitem')).toEqual({ x: 3.2, y: 2.4 });
    expect(unitPx('list')).toEqual({ x: 3.2, y: 2.4 });
    expect(unitPx('image')).toEqual({ x: 1, y: 1 });
  });

  it('places a top-left box where the firmware does: x * 320 / 100, whole pixels', () => {
    expect(boxOrigin('displayitem', 1, { x: 83, y: 5 }, { width: 54, height: 40 })).toEqual({ left: 265, top: 12 });
    expect(boxOrigin('displayitem', undefined, { x: 33, y: 33 }, { width: 10, height: 10 })).toEqual({ left: 105, top: 79 });
    expect(boxOrigin('image', undefined, { x: 10, y: 20 }, { width: 48, height: 48 })).toEqual({ left: 10, top: 20 });
  });
});

describe('display-geometry: align', () => {
  it('maps lv_align_t 1..9; 0 and unknown values are top left', () => {
    expect(alignAnchor(1)).toEqual({ h: 0, v: 0 });
    expect(alignAnchor(2)).toEqual({ h: 0.5, v: 0 });
    expect(alignAnchor(3)).toEqual({ h: 1, v: 0 });
    expect(alignAnchor(4)).toEqual({ h: 0, v: 1 });
    expect(alignAnchor(5)).toEqual({ h: 0.5, v: 1 });
    expect(alignAnchor(6)).toEqual({ h: 1, v: 1 });
    expect(alignAnchor(7)).toEqual({ h: 0, v: 0.5 });
    expect(alignAnchor(8)).toEqual({ h: 1, v: 0.5 });
    expect(alignAnchor(9)).toEqual({ h: 0.5, v: 0.5 });
    expect(alignAnchor(0)).toEqual({ h: 0, v: 0 });
    expect(alignAnchor(undefined)).toEqual({ h: 0, v: 0 });
    expect(alignAnchor(12)).toEqual({ h: 0, v: 0 });
  });

  it('x and y are offsets from the align corner: right and bottom count back from the far edge', () => {
    const size = { width: 64, height: 40 };
    expect(boxOrigin('displayitem', 3, { x: 0, y: 0 }, size)).toEqual({ left: 256, top: 0 });
    expect(boxOrigin('displayitem', 3, { x: -10, y: 5 }, size)).toEqual({ left: 224, top: 12 });
    expect(boxOrigin('displayitem', 6, { x: 0, y: 0 }, size)).toEqual({ left: 256, top: 200 });
    expect(boxOrigin('displayitem', 9, { x: 0, y: 0 }, size)).toEqual({ left: 128, top: 100 });
    expect(boxOrigin('displayitem', 2, { x: 10, y: 0 }, size)).toEqual({ left: 160, top: 0 });
    // Lists are always placed from the top left.
    expect(boxOrigin('list', 3, { x: 10, y: 10 }, size)).toEqual({ left: 32, top: 24 });
  });

  it('the range of x and y keeps the box on the screen, per align', () => {
    const size = { width: 64, height: 48 };       // 20 % x 20 %
    expect(unitRange('displayitem', 1, size)).toEqual({ minX: 0, maxX: 80, minY: 0, maxY: 80 });
    expect(unitRange('displayitem', 3, size)).toEqual({ minX: -80, maxX: 0, minY: 0, maxY: 80 });
    expect(unitRange('displayitem', 6, size)).toEqual({ minX: -80, maxX: 0, minY: -80, maxY: 0 });
    expect(unitRange('displayitem', 9, size)).toEqual({ minX: -40, maxX: 40, minY: -40, maxY: 40 });
    expect(unitRange('image', undefined, size)).toEqual({ minX: 0, maxX: 256, minY: 0, maxY: 192 });
    // 17 % wide at x = 83 (devices/btn_a00001): 54 px, the last column that fits.
    expect(unitRange('displayitem', 1, { width: 54, height: 30 }).maxX).toBe(83);
    // Wider than the screen: stays at its edge.
    expect(unitRange('displayitem', 1, { width: 400, height: 10 })).toMatchObject({ minX: 0, maxX: 0 });
  });

  it('realign: the box stays where it is, x and y become the offset from the new corner', () => {
    const size = { width: 64, height: 48 };
    expect(realign(1, 3, { x: 60, y: 10 }, size)).toEqual({ x: -20, y: 10 });
    expect(realign(3, 1, { x: -20, y: 10 }, size)).toEqual({ x: 60, y: 10 });
    expect(realign(1, 9, { x: 40, y: 40 }, size)).toEqual({ x: 0, y: 0 });
    expect(realign(1, 6, { x: 0, y: 0 }, size)).toEqual({ x: -80, y: -80 });
    // Where the box is drawn does not change (to the unit).
    const before = boxOrigin('displayitem', 1, { x: 37, y: 21 }, size);
    const at = realign(1, 5, { x: 37, y: 21 }, size);
    const after = boxOrigin('displayitem', 5, at, size);
    expect(Math.abs(after.left - before.left)).toBeLessThanOrEqual(3);
    expect(Math.abs(after.top - before.top)).toBeLessThanOrEqual(2);
  });
});

describe('display-geometry: dragTo', () => {
  const range = unitRange('displayitem', 1, { width: 64, height: 48 });

  it('converts the pointer movement on the scaled preview to whole units', () => {
    expect(dragTo('displayitem', { x: 10, y: 10 }, css(20, 5), SCALE, range)).toEqual({ x: 30, y: 15 });
    expect(dragTo('displayitem', { x: 10, y: 10 }, css(-4, -3), SCALE, range)).toEqual({ x: 6, y: 7 });
    // Snaps: less than half a unit does not move it, more does.
    expect(dragTo('displayitem', { x: 10, y: 10 }, css(0.4, 0.4), SCALE, range)).toEqual({ x: 10, y: 10 });
    expect(dragTo('displayitem', { x: 10, y: 10 }, css(0.6, 0.6), SCALE, range)).toEqual({ x: 11, y: 11 });
    // Another zoom of the preview: the same device movement.
    expect(dragTo('displayitem', { x: 10, y: 10 }, { dx: 64, dy: 48 }, { x: 2, y: 2 }, range)).toEqual({ x: 20, y: 20 });
  });

  it('images move by device pixels', () => {
    const r = unitRange('image', undefined, { width: 48, height: 48 });
    expect(dragTo('image', { x: 10, y: 20 }, { dx: 100 * SCALE.x, dy: -5 * SCALE.y }, SCALE, r)).toEqual({ x: 110, y: 15 });
  });

  it('keeps the box inside the screen', () => {
    expect(dragTo('displayitem', { x: 10, y: 10 }, css(500, 500), SCALE, range)).toEqual({ x: 80, y: 80 });
    expect(dragTo('displayitem', { x: 10, y: 10 }, css(-500, -500), SCALE, range)).toEqual({ x: 0, y: 0 });
  });

  it('with align right the same drag changes the offset the firmware uses, inside its own range', () => {
    const right = unitRange('displayitem', 3, { width: 64, height: 48 });
    expect(dragTo('displayitem', { x: 0, y: 0 }, css(-25, 10), SCALE, right)).toEqual({ x: -25, y: 10 });
    expect(dragTo('displayitem', { x: -25, y: 0 }, css(40, 0), SCALE, right)).toEqual({ x: 0, y: 0 });
    // Dropped 25 units left of the right edge: that is where the firmware draws it.
    expect(boxOrigin('displayitem', 3, { x: -25, y: 10 }, { width: 64, height: 48 })).toEqual({ left: 176, top: 24 });
  });

  it('Shift: only the axis that moved further', () => {
    expect(dragTo('displayitem', { x: 10, y: 10 }, css(20, 5), SCALE, range, true)).toEqual({ x: 30, y: 10 });
    expect(dragTo('displayitem', { x: 10, y: 10 }, css(2, 30), SCALE, range, true)).toEqual({ x: 10, y: 40 });
  });

  it('clampUnits rounds and limits', () => {
    expect(clampUnits({ x: 3.6, y: -2 }, range)).toEqual({ x: 4, y: 0 });
  });
});

describe('display-geometry: boxes', () => {
  it('unitsAt: a point of the screen in %, inside the screen', () => {
    expect(unitsAt('displayitem', { x: 160, y: 120 })).toEqual({ x: 50, y: 50 });
    expect(unitsAt('displayitem', { x: -5, y: 900 })).toEqual({ x: 0, y: 99 });
  });

  it('hitTest: the topmost box at a point, from boxes alone', () => {
    const a = { id: 'a', rect: itemRect('displayitem', 1, { x: 0, y: 0 }, { width: 160, height: 60 }) };
    const b = { id: 'b', rect: itemRect('image', undefined, { x: 100, y: 20 }, { width: 48, height: 48 }) };
    expect(hitTest([a, b], { x: 10, y: 10 })?.id).toBe('a');
    expect(hitTest([a, b], { x: 110, y: 30 })?.id).toBe('b');     // drawn later: on top
    expect(hitTest([b, a], { x: 110, y: 30 })?.id).toBe('a');
    expect(hitTest([a, b], { x: 300, y: 200 })).toBeNull();
  });
});
