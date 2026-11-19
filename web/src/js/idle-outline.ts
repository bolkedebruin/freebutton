/**
 * What the virtual device shows while no script runs: one outline per
 * configured element of the main display, at its position and size, with the
 * element's name in it (pure).
 *
 * No data is shown: no label, no value, no icon, no clock hands. The view
 * says where things are and what they are called; what they show is the
 * running device's business (Run).
 *
 *   pageOutlines(page)        the boxes of one page, in device pixels
 *   displayItemName(item, i)  the name written in a display item's box
 *   buttonName(button)        the name written on a key's display
 *
 * Positions are the firmware's (display-geometry.ts boxOrigin). Sizes:
 *   display item   the width is config.json's (percent). The height is not in
 *                  the config: the firmware sizes the box by its content. It
 *                  is estimated from the fonts of the item's styles: a
 *                  caption line (when the entry has a label), a value line
 *                  and the paddings that go with the value's size.
 *   list           its rows: a font line with the line space of "line-height".
 *   image          its width and height; without them 40 x 40.
 *   clock          its width and height; "align": 9 centres it and x and y
 *                  are not used (ui_clock.c clock_create).
 */
import type { ButtonConfig, ClockConfig, ConfigRef, DisplayItem, ImageConfig, ListConfig, PageConfig } from './app-state';
import { boxOrigin, SCREEN_H, SCREEN_W, type PlacedKind, type Size } from './display-geometry';

export type OutlineKind = PlacedKind | 'clock';

export interface OutlineBox {
  /** The entry in config.json; absent for the built-in sample. */
  ref?: ConfigRef;
  kind: OutlineKind;
  /** What is written in the box. */
  name: string;
  /** Device pixels on the 320 x 240 main display. */
  left: number;
  top: number;
  width: number;
  height: number;
}

/** A font's line relative to its size (about LVGL's line height for the system fonts: 39 px at 32 px). */
export const FONT_LINE = 1.22;
/** The least height of a box (device px): its name stays readable and it can be pointed at. */
export const MIN_OUTLINE_H = 14;
/** The size that stands in for an image without width and height. */
export const UNSIZED_IMAGE = 40;
/** The font size used where an entry has none (the built-in sample). */
const DEFAULT_FONT_PX = 14;
/** lv_align_t LV_ALIGN_CENTER. */
const ALIGN_CENTER = 9;

/**
 * A display item's name: its "name"; without one "#<displayitemid>"; without
 * that its place in config.json ("displayitems[2]"). Never its label or
 * value: those are data.
 */
export function displayItemName(item: Pick<DisplayItem, 'name' | 'id' | 'ref'>, index: number): string {
  if (item.name) return item.name;
  if (item.id !== undefined && item.id !== '') return `#${item.id}`;
  return `displayitems[${item.ref?.index ?? index}]`;
}

/** A key's name: its "name", else "Key <position>". */
export function buttonName(button: Pick<ButtonConfig, 'name' | 'position'>): string {
  return button.name || `Key ${button.position}`;
}

/** The box of a display item as far as the config tells: the height is an estimate (see above). */
export function displayItemSize(item: DisplayItem): Size {
  const caption = item.label ? (item.labelFont?.px ?? DEFAULT_FONT_PX) * FONT_LINE : 0;
  const value = (item.valueFont?.px ?? DEFAULT_FONT_PX) * FONT_LINE;
  const pad = (item.pad?.captionPad ?? 0) + (item.pad?.boxPad ?? 0);
  return {
    width: Math.max(0, Math.trunc(item.width * SCREEN_W / 100)),
    height: Math.max(MIN_OUTLINE_H, Math.round(caption + value + pad)),
  };
}

export function listSize(list: ListConfig): Size {
  const rowH = list.font.px * FONT_LINE * (list.style.lineHeight ?? 125) / 100;
  return {
    width: Math.max(0, Math.trunc(list.width * SCREEN_W / 100)),
    height: Math.max(MIN_OUTLINE_H, Math.round(list.rows * rowH)),
  };
}

export function imageSize(image: ImageConfig): Size {
  return { width: image.width ?? UNSIZED_IMAGE, height: image.height ?? UNSIZED_IMAGE };
}

/** Top-left corner of the clock: centred with align 9, else at x, y. */
export function clockOrigin(clock: ClockConfig): { left: number; top: number } {
  if (clock.align === ALIGN_CENTER) {
    return { left: Math.trunc((SCREEN_W - clock.width) / 2), top: Math.trunc((SCREEN_H - clock.height) / 2) };
  }
  return { left: clock.x, top: clock.y };
}

/** The outlines of one page of the main display, in the order they are drawn. */
export function pageOutlines(page: Pick<PageConfig, 'displayItems' | 'lists' | 'images' | 'clocks'>): OutlineBox[] {
  const boxes: OutlineBox[] = [];
  const ref = (r: ConfigRef | undefined) => (r ? { ref: r } : {});
  (page.clocks ?? []).forEach((clock) => {
    boxes.push({ ...ref(clock.ref), kind: 'clock', name: 'clock', ...clockOrigin(clock), width: clock.width, height: clock.height });
  });
  page.displayItems.forEach((item, index) => {
    const size = displayItemSize(item);
    boxes.push({
      ...ref(item.ref), kind: 'displayitem', name: displayItemName(item, index),
      ...boxOrigin('displayitem', item.align, { x: item.x, y: item.y }, size), ...size,
    });
  });
  (page.lists ?? []).forEach((list) => {
    const size = listSize(list);
    boxes.push({ ...ref(list.ref), kind: 'list', name: list.name, ...boxOrigin('list', undefined, { x: list.x, y: list.y }, size), ...size });
  });
  (page.images ?? []).forEach((image) => {
    const size = imageSize(image);
    boxes.push({ ...ref(image.ref), kind: 'image', name: image.name, ...boxOrigin('image', undefined, { x: image.x, y: image.y }, size), ...size });
  });
  return boxes;
}
