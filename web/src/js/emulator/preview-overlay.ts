/**
 * The emulator's state laid over the IDE's preview model.
 *
 * The virtual device draws PageConfig[] (config-model.ts configToModel). While
 * a script runs in the emulator, the same components can draw the running
 * device: overlayPages() returns the pages with every entry's runtime values
 * (texts, icons, positions, pages, list items and selection, image sources,
 * LED colours), found by the `ref` both models share. Items a script created
 * are added, items it removed are left out.
 *
 * Pure: the result is new objects; neither the model nor the state changes.
 * It is runtime state for drawing only, never something to write back to
 * config.json.
 *
 * The virtual device shows the firmware's own pixels (canvas.ts) whenever
 * it can; this model is what the page draws itself when it cannot (no
 * WebAssembly, no canvas), and where the keys' LEDs come from in both cases
 * (a key's back LED is carried next to its front LED). The values are the
 * firmware's results: texts as it cut them to its slots, the fonts its
 * style lookup chose (a script may have given the entry other styles).
 */
import { sameConfigRef, type ButtonConfig, type ConfigRef, type DisplayItem, type ImageConfig, type ItemFont, type ListConfig, type PageConfig } from '../app-state';
import type { DeviceState, ItemState } from './device-state';
import type { FontChoice } from './host';

/**
 * The font the firmware chose, as the preview draws fonts: the face of the
 * font table at the size in px. Undefined: the firmware reported none (the
 * part is not drawn), so the entry's own stays.
 */
function chosenFont(font: FontChoice | null, configured: ItemFont | undefined): ItemFont | undefined {
  if (!font) return undefined;
  return { face: font.face >= 0 ? font.face : configured?.face ?? 0, px: font.size > 0 ? font.size : configured?.px ?? 14 };
}

function byRef<T extends { ref?: ConfigRef }>(pages: readonly PageConfig[], pick: (page: PageConfig) => readonly T[], ref: ConfigRef | undefined): T | undefined {
  if (!ref) return undefined;
  for (const page of pages) {
    const found = pick(page).find(entry => sameConfigRef(entry.ref, ref));
    if (found) return found;
  }
  return undefined;
}

function drawnItem(pages: readonly PageConfig[], item: ItemState): DisplayItem {
  const base: DisplayItem = byRef(pages, p => p.displayItems, item.ref) ?? {
    // Made by a script: the fields a config entry would have given. (Its look is in its
    // styles, which the device resolved: the fonts below.)
    x: item.x, y: item.y, width: item.width, height: 0, type: 'text',
    align: item.align, id: String(item.id),
  };
  // The unit's font is the one the device chose (below), from the styles the item has now. Not
  // the configured one; while the box is not drawn the component derives it from the value's.
  const { icon: _configuredIcon, unitFont: _configuredUnitFont, ...rest } = base;
  return {
    ...rest,
    x: item.x,
    y: item.y,
    label: item.label,
    value: item.value,
    unit: item.unit || undefined,
    ...(item.icon ? { icon: item.icon } : {}),
    ...(item.font ? { valueFont: chosenFont(item.font, base.valueFont) } : {}),
    ...(item.box?.caption.font
      ? { labelFont: chosenFont({ face: item.box.caption.font.face, size: item.box.caption.font.px }, base.labelFont) } : {}),
    ...(item.box?.unit.font
      ? { unitFont: chosenFont({ face: item.box.unit.font.face, size: item.box.unit.font.px }, base.unitFont) } : {}),
  };
}

/** Index of a key's front LED on the strip: 4 per bar, in bar order (device_hardware.cpp). */
export function frontLedIndex(state: DeviceState, position: number): number {
  const connector = Math.floor((position - 1) / 2);
  if (state.connectors[connector] !== 1) return -1;
  const bar = state.connectors.slice(0, connector).filter(kind => kind === 1).length;
  return bar * 4 + ((position - 1) % 2 === 1 ? 2 : 0);
}

function drawnButton(state: DeviceState, button: ButtonConfig): ButtonConfig {
  const key = state.keys.find(k => sameConfigRef(k.ref, button.ref));
  const front = frontLedIndex(state, button.position);
  const led = state.leds[front];
  // The LED at the back of the key (towards the wall) follows the front one on the strip.
  const back = front < 0 ? undefined : state.leds[front + 1];
  const { icon: _configuredIcon, ...rest } = button;
  return {
    ...rest,
    ...(key ? { label: key.label, topLabel: key.topLabel } : {}),
    ...((key ? key.icon : button.icon) ? { icon: key ? key.icon : button.icon } : {}),
    ...(led ? { ledOn: led.r + led.g + led.b > 0, ledColor: `rgb(${led.r},${led.g},${led.b})` } : {}),
    ...(back && back.r + back.g + back.b > 0 ? { backLedColor: `rgb(${back.r},${back.g},${back.b})` } : {}),
    ...(key?.font ? { labelFont: chosenFont(key.font, button.labelFont) } : {}),
    ...(key?.topFont ? { topFont: chosenFont(key.topFont, button.topFont) } : {}),
  };
}

function drawnList(state: DeviceState, list: ListConfig): ListConfig {
  const running = state.lists.find(l => sameConfigRef(l.ref, list.ref));
  if (!running) return list;
  return {
    ...list, items: [...running.items], selected: running.selected,
    ...(running.font ? { font: chosenFont(running.font, list.font) } : {}),
  };
}

function drawnImage(state: DeviceState, image: ImageConfig): ImageConfig {
  const running = state.images.find(m => sameConfigRef(m.ref, image.ref));
  if (!running) return image;
  const { icon: _icon, src: _src, ...rest } = image;
  return {
    ...rest,
    ...(running.kind === 'icon' ? { icon: running.value } : {}),
    ...(running.kind === 'src' ? { src: running.value } : {}),
  };
}

/**
 * `pages` as the running device shows them. Pages the script uses that the
 * config does not have are added (empty of keys). The page to show is
 * state.shownPage.
 */
export function overlayPages(pages: readonly PageConfig[], state: DeviceState): PageConfig[] {
  const numbers = new Set<number>(pages.map(p => p.number));
  numbers.add(state.shownPage);
  for (const item of state.items) if (item.page >= 1) numbers.add(item.page);
  return [...numbers].sort((a, b) => a - b).map(number => {
    const page = pages.find(p => p.number === number);
    return {
      number,
      displayItems: state.items.filter(i => i.page < 1 || i.page === number).map(i => drawnItem(pages, i)),
      buttons: (page?.buttons ?? []).map(b => drawnButton(state, b)),
      ...(page?.lists ? { lists: page.lists.map(l => drawnList(state, l)) } : {}),
      ...(page?.images ? { images: page.images.map(m => drawnImage(state, m)) } : {}),
    };
  });
}
