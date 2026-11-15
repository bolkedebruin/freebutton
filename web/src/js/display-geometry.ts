/**
 * Where an entry of config.json is on the main display, as the firmware
 * computes it (pure), and the way back: from a drag on the preview to the
 * values config.json holds.
 *
 * The screen is 320 x 240 device pixels.
 *   display items, lists   x, y, width in % of the screen: the firmware
 *                          draws at x * 320 / 100 and y * 240 / 100
 *                          (ui_box.c, List.cpp). One unit of x is 3.2 px,
 *                          one of y 2.4 px.
 *   images                 x, y, width, height in pixels (ImageItem.cpp).
 * A display item's "align" (lv_align_t) picks the corner, edge or centre of
 * the screen the box is put against; x and y are then the offset from
 * there (lv_obj_set_align + lv_obj_set_pos): with align 3 (top right) x = 0
 * is flush right and a negative x moves the box left. Lists and images are
 * always placed from the top left corner.
 *
 * So a drag by d device pixels changes x by d / 3.2 units whatever the
 * align: the offset moves, the anchor stays. The limits differ: unitRange
 * gives the x and y that keep the box on the screen.
 */
export const SCREEN_W = 320;
export const SCREEN_H = 240;

export type PlacedKind = 'displayitem' | 'list' | 'image';

export interface Units { x: number; y: number }
export interface Size { width: number; height: number }

/** Device pixels per unit of x and y. */
export function unitPx(kind: PlacedKind): Units {
  return kind === 'image' ? { x: 1, y: 1 } : { x: SCREEN_W / 100, y: SCREEN_H / 100 };
}

/** lv_align_t (1..9; 0 and anything else: top left) as fractions: 0 = left / top, 0.5 = middle, 1 = right / bottom. */
export function alignAnchor(align: number | undefined): { h: 0 | 0.5 | 1; v: 0 | 0.5 | 1 } {
  switch (align) {
    case 2: return { h: 0.5, v: 0 };
    case 3: return { h: 1, v: 0 };
    case 4: return { h: 0, v: 1 };
    case 5: return { h: 0.5, v: 1 };
    case 6: return { h: 1, v: 1 };
    case 7: return { h: 0, v: 0.5 };
    case 8: return { h: 1, v: 0.5 };
    case 9: return { h: 0.5, v: 0.5 };
    default: return { h: 0, v: 0 };
  }
}

/** lv_align_t values a display item can have, for a menu. */
export const ALIGN_OPTIONS: Array<{ value: number; label: string }> = [
  { value: 1, label: 'Top left' }, { value: 2, label: 'Top centre' }, { value: 3, label: 'Top right' },
  { value: 7, label: 'Middle left' }, { value: 9, label: 'Centre' }, { value: 8, label: 'Middle right' },
  { value: 4, label: 'Bottom left' }, { value: 5, label: 'Bottom centre' }, { value: 6, label: 'Bottom right' },
];

/** Top-left corner (device px) of a box at offset 0 for an align; LVGL divides whole pixels. */
function anchorOrigin(align: number | undefined, size: Size): { left: number; top: number } {
  const a = alignAnchor(align);
  return { left: Math.trunc((SCREEN_W - size.width) * a.h), top: Math.trunc((SCREEN_H - size.height) * a.v) };
}

/** Top-left corner (device px) of the box as the firmware places it. */
export function boxOrigin(kind: PlacedKind, align: number | undefined, at: Units, size: Size): { left: number; top: number } {
  const u = unitPx(kind);
  const o = kind === 'displayitem' ? anchorOrigin(align, size) : { left: 0, top: 0 };
  return { left: o.left + Math.trunc(at.x * u.x), top: o.top + Math.trunc(at.y * u.y) };
}

export interface UnitRange { minX: number; maxX: number; minY: number; maxY: number }

/** The x and y (whole units) that keep the box inside the screen; a box larger than the screen stays at its near edge. */
export function unitRange(kind: PlacedKind, align: number | undefined, size: Size): UnitRange {
  const o = kind === 'displayitem' ? anchorOrigin(align, size) : { left: 0, top: 0 };
  // px -> units as px * 100 / 320 (whole numbers first: no rounding error at a whole unit).
  const span = (origin: number, room: number, screen: number): [number, number] => {
    const per = kind === 'image' ? 1 : 100;
    const full = kind === 'image' ? 1 : screen;
    // + 0 : no negative zero.
    const min = Math.ceil(-origin * per / full) + 0;
    const max = Math.floor((room - origin) * per / full) + 0;
    return max < min ? (origin === 0 ? [min, min] : [max, max]) : [min, max];
  };
  const [minX, maxX] = span(o.left, SCREEN_W - size.width, SCREEN_W);
  const [minY, maxY] = span(o.top, SCREEN_H - size.height, SCREEN_H);
  return { minX, maxX, minY, maxY };
}

export function clampUnits(at: Units, range: UnitRange): Units {
  return {
    x: Math.min(range.maxX, Math.max(range.minX, Math.round(at.x))),
    y: Math.min(range.maxY, Math.max(range.minY, Math.round(at.y))),
  };
}

/**
 * The values after a drag: `delta` is the pointer's movement in CSS px,
 * `scale` the CSS px one device px is drawn with (per axis). Whole units,
 * inside `range`. `oneAxis` (Shift): only the axis that moved further.
 */
export function dragTo(
  kind: PlacedKind, start: Units, delta: { dx: number; dy: number }, scale: Units, range: UnitRange, oneAxis = false,
): Units {
  const u = unitPx(kind);
  let { dx, dy } = delta;
  if (oneAxis) {
    if (Math.abs(dx) >= Math.abs(dy)) dy = 0; else dx = 0;
  }
  return clampUnits({ x: start.x + dx / scale.x / u.x, y: start.y + dy / scale.y / u.y }, range);
}

/** The x and y for another align that leave the box where it is (to the nearest unit, on the screen). */
export function realign(from: number | undefined, to: number, at: Units, size: Size): Units {
  const u = unitPx('displayitem');
  const now = boxOrigin('displayitem', from, at, size);
  const base = anchorOrigin(to, size);
  return clampUnits({ x: (now.left - base.left) / u.x, y: (now.top - base.top) / u.y }, unitRange('displayitem', to, size));
}

/** The units of a point of the screen (device px from its top left), for a new top-left aligned entry. */
export function unitsAt(kind: PlacedKind, px: { x: number; y: number }): Units {
  const u = unitPx(kind);
  return {
    x: Math.min(Math.floor((SCREEN_W - 1) / u.x), Math.max(0, Math.round(px.x / u.x))),
    y: Math.min(Math.floor((SCREEN_H - 1) / u.y), Math.max(0, Math.round(px.y / u.y))),
  };
}

/** A box on the screen in device px. */
export interface Rect { left: number; top: number; width: number; height: number }

/** The box of an entry as the firmware places it. */
export function itemRect(kind: PlacedKind, align: number | undefined, at: Units, size: Size): Rect {
  return { ...boxOrigin(kind, align, at, size), width: size.width, height: size.height };
}

/**
 * The entry at a point of the screen (device px): the last one in drawing
 * order that contains it, as that one is on top. Works from boxes alone, so
 * whatever draws the screen (the DOM preview today) only has to supply them.
 */
export function hitTest<T extends { rect: Rect }>(items: readonly T[], px: { x: number; y: number }): T | null {
  for (let i = items.length - 1; i >= 0; i--) {
    const r = items[i].rect;
    if (px.x >= r.left && px.x < r.left + r.width && px.y >= r.top && px.y < r.top + r.height) return items[i];
  }
  return null;
}
