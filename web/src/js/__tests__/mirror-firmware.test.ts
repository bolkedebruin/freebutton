/**
 * The IDE computes the same looks as the firmware.
 *
 * The IDE has its own copy of the style rules (style-config.ts,
 * font-config.ts, config-model.ts) for the config problems, the script
 * checks and the preview it draws itself. Here that copy is compared with
 * the firmware, which runs in the emulator: for a set of configs that
 * covers every rule (docs/specs/named-styles.md), what the IDE computes
 * for each item (the style list, the font and the colour of every part,
 * the icon's tint, the padding, the list's line space and backgrounds, the
 * looks of the kinds) must be what the emulator's state records say the
 * device has. The alignment of a text is not in a record: it is compared
 * through the pixels, against the same text with the \a markup the IDE
 * says is equivalent. What fb.style.set accepts is compared call by call
 * with the firmware's error.
 */
import { describe, expect, it } from 'vitest';
import type { Emulator } from '../emulator';
import type { StyleState } from '../emulator/device-state';
import { configToModel, mapButton, mapDisplayItem, mapList } from '../config-model';
import { fontStyles, fontTable, itemStyleList, styleNames } from '../font-config';
import {
  DECOR_LINE_THROUGH, DECOR_UNDERLINE, lookupIconColor, lookupPart, parseScriptStyleList, scriptSetNoPartMessage, scriptSetStatus,
  StyleAlign, styleBgOpa, styleColorHex, StyleElement, styleKind, styleLineSpace, styleListAdd, styleListRemove, styleTextAlign,
  type ConfigStyles, type StyleProps,
} from '../style-config';
import { BASIC_CONFIG, makeEmulator } from './emulator-helpers';
import { repoMedia } from './emulator-pixels';

type Obj = Record<string, unknown>;
type Extra = Record<string, Obj>;

/** Styles that between them use every property, every part and every value form. */
const STYLES: Record<string, unknown> = {
  alarm: { color: '#ff0000', title: { color: '#ff8080' } },
  muted: { color: '#808080' },
  big: { 'font-family': 'montserrat_semibold', 'font-size': 48 },
  semi: { 'font-family': 'montserrat_semibold' },
  huge: { 'font-size': 48 },
  small: { 'font-size': 20 },
  tiny: { 'font-size': 14 },
  mid: { 'font-size': 28 },
  loud: { color: '#ff0000', 'font-size': 20 },
  parts: {
    color: '#00ff00',
    title: { color: '#00ffff', 'font-size': 20 },
    unit: { color: '#ff00ff', 'font-size': 10 },
    icon: { color: '#ffff00' },
    selected: { color: '#ffffff', 'background-color': '#00c853' },
  },
  unit_face: { unit: { 'font-family': 'montserrat_semibold' } },
  picked: { selected: { 'background-color': '#00c853', color: '#0000ff' } },
  bold_row: { selected: { 'font-family': 'montserrat_semibold' } },
  big_row: { selected: { 'font-size': 40 } },
  roomy: { 'font-size': 20, 'line-height': 1.5 },
  tight: { 'line-height': 1 },
  panel: { 'background-color': '#0000ff80', selected: { 'background-color': '#00ff0080' } },
  center: { 'text-align': 'center' },
  center_title: { title: { 'text-align': 'center' } },
  right: { 'text-align': 'right' },
  left: { 'text-align': 'left', title: { 'text-align': 'right' } },
  unit_right: { unit: { 'text-align': 'right' }, selected: { 'text-align': 'right' } },
  spaced: { 'letter-spacing': 3, opacity: 0.5, 'text-decoration': 'underline' },
  struck: { 'text-decoration': 'line-through underline', opacity: 0.25, 'letter-spacing': -2 },
  tint: { icon: { color: '#00ff00' } },
  by_id: { 'font-family': 2 },
  empty: {},
};

const FONTS = [{ id: 2, name: 'second', src: 'semibold.ttf' }];

/** BASIC_CONFIG's items, each with what `extra` adds to it (by name), the item "co2", and `styles`. */
function config(extra: Extra = {}, styles: Record<string, unknown> | null = STYLES) {
  const add = <T extends { name?: string }>(entries: readonly T[]) => entries.map(e => ({ ...e, ...(e.name ? extra[e.name] : {}) }));
  const { styles: _basic, ...rest } = BASIC_CONFIG;
  return {
    ...rest,
    fonts: FONTS,
    ...(styles ? { styles } : {}),
    buttons: add(BASIC_CONFIG.buttons),
    displayitems: [
      ...add(BASIC_CONFIG.displayitems.map(i => ({ ...i, style: undefined }))),
      { name: 'co2', displayitemid: 8, x: 10, y: 60, width: 40, page: 1, label: 'CO2', unit: 'ppm', boxtype: 1, ...extra.co2 },
    ],
    lists: add(BASIC_CONFIG.lists),
  };
}

const ITEMS = ['temp', 'co2'];
const BUTTONS = ['light', 'blind'];
const SCRIPT = `fb.main.display.setValue("temp", "21"); fb.main.display.setValue("co2", "612");
  fb.main.display.setIcon("temp", "sun"); fb.main.display.setIcon("co2", "sun");
  fb.button("light").display.setIcon("moon"); fb.button("blind").display.setIcon("moon");`;

async function start(cfg: unknown, script = SCRIPT) {
  const made = await makeEmulator({ config: cfg as never, media: repoMedia({}, { icons: true }) });
  expect(made.emu.run(script)).toEqual({ ok: true });
  made.clock.advance(100);
  made.emu.render();
  return made;
}

type Run = { color: string; font: string; px: number };
const first = (t: { runs: Run[] } | undefined) => (t && t.runs[0] ? `${t.runs[0].color} ${t.runs[0].font} ${t.runs[0].px}` : '');

/** What the device has, from the emulator's state records. */
function seen(emu: Emulator) {
  const s = emu.state;
  const roller = s.list('scenes')!.roller!;
  return {
    styleNames: [...s.styleNames],
    kinds: s.styles,
    items: Object.fromEntries(ITEMS.map((name) => {
      const item = s.item(name)!;
      const box = item.box!;
      return [name, {
        style: item.style, configStyle: item.configStyle,
        caption: first(box.caption), value: first(box.value), unit: first(box.unit),
        pad: [box.captionPad, box.boxPad], tint: box.icon?.tint ?? null,
      }];
    })),
    buttons: Object.fromEntries(BUTTONS.map((name) => {
      const key = s.key(name)!;
      // The label is hidden behind the icon: its font is still the one the firmware chose.
      return [name, {
        style: key.style, configStyle: key.configStyle,
        font: `${key.font!.face} ${key.font!.size}`, topFont: `${key.topFont!.face} ${key.topFont!.size}`,
        top: first(key.display!.topLabel), tint: key.display!.icon?.tint ?? null,
      }];
    })),
    list: {
      style: s.list('scenes')!.style, configStyle: s.list('scenes')!.configStyle,
      color: roller.color, selectedColor: roller.selectedColor, selectedBg: roller.selectedBg, bg: roller.bg,
      font: `${roller.font.font} ${roller.font.px}`, selectedFont: `${roller.selectedFont.font} ${roller.selectedFont.px}`,
      lineSpace: roller.lineSpace,
      // Opacity, letter spacing and decoration as LVGL resolves them: the selected row has the rows' unless "selected" sets one.
      text: roller.text, selectedText: roller.selectedText,
    },
  };
}

const ALIGN_NAME: Record<number, 'left' | 'center' | 'right'> = {
  [StyleAlign.Left]: 'left', [StyleAlign.Center]: 'center', [StyleAlign.Right]: 'right',
};

/** A kind's part as the device record has it, from what the IDE resolved. */
function kindRecord(p: Readonly<StyleProps>, fontName: (face: number) => string): StyleState {
  return {
    font: p.face !== undefined ? fontName(p.face) : null,
    face: p.face ?? -1,
    px: p.px ?? 0,
    color: styleColorHex(p.color ?? 0),
    opa: p.opa ?? 255,
    align: p.align !== undefined ? ALIGN_NAME[p.align]! : null,
    letterSpace: p.letterSpace ?? 0,
    lineHeight: p.lineHeight ?? 0,
    underline: ((p.decor ?? 0) & DECOR_UNDERLINE) !== 0,
    lineThrough: ((p.decor ?? 0) & DECOR_LINE_THROUGH) !== 0,
    ...(p.bg !== undefined ? { bgColor: styleColorHex(p.bg), bgOpa: styleBgOpa(p.bg) } : {}),
  };
}

/** Opacity, letter spacing and decoration of a part of a list, as the roller's record has them. */
function rollerText(p: Readonly<StyleProps>) {
  return { opa: p.opa ?? 255, letterSpace: p.letterSpace ?? 0, decor: p.decor ?? 0 };
}

/** The same, as the IDE computes it from config.json. `lineHeights`: the fonts' line boxes, which only the device knows. */
function computed(cfg: Obj, lineHeight: number) {
  const table = fontTable(cfg);
  const styles: ConfigStyles = fontStyles(cfg, table);
  const fontName = (face: number) => table.faces[face]!.name;
  const text = (font: { face: number; px: number }, look: Readonly<StyleProps>) => `${styleColorHex(look.color ?? 0)} ${fontName(font.face)} ${font.px}`;
  const entry = (list: string, name: string) => (cfg[list] as Obj[]).find(e => e.name === name)!;
  const E = StyleElement;
  const rawList = entry('lists', 'scenes');
  const list = mapList(rawList, styles)!;
  return {
    styleNames: [...styles.named.keys()],
    kinds: {
      button: kindRecord(styles.elements[E.KeyLabel]!, fontName),
      'button.title': kindRecord(styles.elements[E.KeyTopLabel]!, fontName),
      displayitem: kindRecord(styles.elements[E.DisplayValue]!, fontName),
      'displayitem.unit': kindRecord(styles.elements[E.DisplayUnit]!, fontName),
      'displayitem.title': kindRecord(styles.elements[E.DisplayCaption]!, fontName),
      list: kindRecord(styles.elements[E.List]!, fontName),
      'list.selected': kindRecord(styles.elements[E.ListSelected]!, fontName),
    },
    items: Object.fromEntries(ITEMS.map((name) => {
      const raw = entry('displayitems', name);
      const item = mapDisplayItem(raw, styles)!.item;
      const names = itemStyleList(raw, styles).join(' ');
      return [name, {
        style: names, configStyle: names,
        caption: text(item.labelFont!, item.captionStyle!), value: text(item.valueFont!, item.valueStyle!), unit: text(item.unitFont!, item.unitStyle!),
        pad: item.pad ? [item.pad.captionPad, item.pad.boxPad] : [null, null],
        tint: styleColorHex(lookupIconColor(styles, 'displayitem', itemStyleList(raw, styles))),
      }];
    })),
    buttons: Object.fromEntries(BUTTONS.map((name) => {
      const raw = entry('buttons', name);
      const button = mapButton(raw, styles)!;
      const names = itemStyleList(raw, styles).join(' ');
      return [name, {
        style: names, configStyle: names,
        font: `${button.labelFont!.face} ${button.labelFont!.px}`, topFont: `${button.topFont!.face} ${button.topFont!.px}`,
        top: button.topLabel ? text(button.topFont!, button.topStyle!) : '',
        tint: styleColorHex(lookupIconColor(styles, 'button', itemStyleList(raw, styles))),
      }];
    })),
    list: {
      style: itemStyleList(rawList, styles).join(' '), configStyle: itemStyleList(rawList, styles).join(' '),
      color: styleColorHex(list.style.color ?? 0), selectedColor: styleColorHex(list.selectedStyle.color ?? 0),
      selectedBg: styleColorHex(list.selectedStyle.bg ?? 0), bg: list.style.bg !== undefined ? styleColorHex(list.style.bg) : null,
      font: `${fontName(list.font.face)} ${list.font.px}`, selectedFont: `${fontName(list.selectedFont.face)} ${list.selectedFont.px}`,
      lineSpace: styleLineSpace(lineHeight, list.style.lineHeight ?? 100),
      text: rollerText(list.style), selectedText: rollerText(list.selectedStyle),
    },
  };
}

/** One config: the device's records equal what the IDE computes. */
async function same(cfg: Obj, what: string) {
  const { emu, rec } = await start(cfg);
  expect(rec.errors, what).toEqual([]);
  const device = seen(emu);
  const lineHeight = emu.state.list('scenes')!.roller!.font.lineHeight;
  expect(device, what).toEqual(computed(cfg, lineHeight));
  // And the model the preview draws from carries the same.
  expect(configToModel(cfg).styles.named.size, what).toBe(device.styleNames.length);
}

describe('the IDE computes the same looks as the firmware', () => {
  it('without "styles": the built-in look', async () => {
    await same(config({}, null), 'no styles');
    await same(config({}, {}), 'empty styles');
  });

  it('every style alone, on a button, a display item and a list', async () => {
    for (const name of Object.keys(STYLES)) {
      await same(config({ light: { style: name }, blind: { style: name }, temp: { style: name }, co2: { style: name }, scenes: { style: name } }), name);
    }
  });

  it('two, three and four styles: the order, the winner per property, face and size apart', async () => {
    const lists = [
      'alarm muted', 'muted alarm', 'mid muted alarm', 'huge alarm muted small', 'huge semi', 'semi huge', 'big small', 'small big',
      'semi small', 'small bold_row', 'big bold_row big_row', 'tight roomy big', 'roomy tight', 'parts alarm', 'alarm parts tint',
      'tint parts', 'panel picked', 'picked panel', 'spaced struck', 'struck spaced alarm', 'by_id small', 'unit_face big parts',
      'parts unit_face huge', 'tiny alarm', 'alarm tiny', 'huge tiny', 'tiny mid', 'left center right', 'center_title left',
    ];
    for (const style of lists) {
      await same(config({ light: { style }, temp: { style }, co2: { style }, scenes: { style } }), style);
    }
  });

  it('a "style" the device cannot use in full, a name given twice, spaces', async () => {
    const lists: unknown[] = [
      'button alarm', 'nosuch muted 1bad', 'alarm muted big small tiny', 'list alarm displayitem', 'alarm muted alarm',
      '  muted    alarm  ', 'big big big big big muted', 'Alarm alarm', '', 7, null, ['alarm'], 'alarm\tmuted',
    ];
    for (const style of lists) {
      await same(config({ light: { style }, blind: { style }, temp: { style }, co2: { style }, scenes: { style } }), JSON.stringify(style));
    }
  });

  it('"button", "displayitem" and "list" in config, with every part and property', async () => {
    const kinds = {
      button: {
        color: '#00ffff', 'font-size': 24, 'letter-spacing': 2, opacity: 0.75, 'text-align': 'left', 'text-decoration': 'underline',
        'line-height': 1.2, title: { color: '#ffffff', 'font-family': 'montserrat', 'text-align': 'right' }, icon: { color: '#0000ff' },
      },
      displayitem: {
        'font-size': 20, 'font-family': 'montserrat_semibold', unit: { color: '#00ff00', 'font-size': 12 },
        title: { 'font-size': 12, 'text-decoration': 'line-through' }, icon: { color: '#00ffff' },
      },
      list: {
        color: '#ffffff', 'font-size': 20, 'line-height': 2, 'background-color': '#10203040',
        selected: { 'background-color': '#ff00ff', 'font-family': 'second', 'font-size': 24, opacity: 0.5 },
      },
    };
    await same(config({}, { ...STYLES, ...kinds }), 'kinds');
    for (const style of ['alarm', 'big', 'parts', 'small tint', 'roomy', 'tight picked', 'tiny', 'unit_face']) {
      await same(config({ light: { style }, temp: { style }, co2: { style }, scenes: { style } }, { ...STYLES, ...kinds }), `kinds + ${style}`);
    }
    // The display item's own size of 14 px in its kind does not pad; another one does.
    await same(config({}, { displayitem: { 'font-size': 14 } }), 'displayitem 14');
    await same(config({ co2: { boxtype: 0 } }, { displayitem: { 'font-size': 48 } }), 'displayitem 48');
    await same(config({}, { displayitem: { 'font-size': 10 } }), 'displayitem 10');
  });

  it('the padding step of every size, for both box types', async () => {
    for (const px of [6, 13, 14, 15, 19, 20, 24, 28, 29, 39, 40, 41, 48, 60, 61, 72]) {
      const styles = { size: { 'font-size': px } };
      await same(config({ temp: { style: 'size', boxtype: 0 }, co2: { style: 'size', boxtype: 1 } }, styles), `${px} px`);
    }
  });

  it('what the device leaves out of "styles": sub-blocks a kind does not have, blocks that are no objects, bad properties and values', async () => {
    const odd = {
      button: { unit: { color: '#123456' }, selected: { color: '#123456' }, title: 5, color: 'red', 'font-size': 30 },
      displayitem: { selected: { color: '#123456' }, icon: { color: 'blue', size: 3 }, 'font-family': 'comic', opacity: 0.5 },
      list: { title: { color: '#123456' }, icon: { color: '#123456' }, 'line-height': 9, 'letter-spacing': 1 },
      mixed: {
        color: 'red', 'font-size': 5, 'font-weight': 700, 'text-align': 'justify', opacity: 2, 'letter-spacing': 2,
        'font-family': 'comic', text_color: '#00ff00', title: { color: '#00ffff', 'line-height': 9 }, icon: 7,
      },
      short: { color: '00ff00', 'background-color': '#12345', 'line-height': 1.255, opacity: 0.333, 'font-size': 20.5, 'font-family': 3 },
      values: { color: '#ABCDEF', opacity: 0.29, 'line-height': 0.8, 'letter-spacing': -100, 'background-color': '#01020304', 'font-family': 'second' },
      edge: { opacity: 1, 'line-height': 3, 'font-size': 72, 'letter-spacing': 100, 'text-decoration': 'none', title: {}, unit: null, selected: [] },
      notobject: 5,
      '1bad': { color: '#00ff00' },
    };
    await same(config({}, odd), 'odd kinds');
    for (const style of ['mixed', 'short', 'values', 'edge', 'notobject', '1bad']) {
      await same(config({ light: { style }, temp: { style }, co2: { style }, scenes: { style } }, odd), `odd ${style}`);
    }
  });

  it('every opacity and line-height of two decimals loads, also one that is no exact binary fraction (0.29, 1.2)', async () => {
    // The firmware's JSON reader keeps such a number as a float: 1.2 reaches the style loader as
    // 1.2000000476837158 and has to be taken as 1.2 (src/components/StyleRef.h scaled).
    const record = async (styles: Record<string, unknown>) => {
      const cfg = config({}, styles);
      const { emu } = await start(cfg, '');
      const elements = fontStyles(cfg).elements;
      return { device: emu.state.styles, ide: elements };
    };
    for (let i = 0; i <= 100; i++) {
      const { device, ide } = await record({ button: { opacity: i / 100 } });
      expect(ide[StyleElement.KeyLabel]!.opa, `opacity ${i / 100}`).toBeDefined();
      expect(device.button!.opa, `opacity ${i / 100}`).toBe(ide[StyleElement.KeyLabel]!.opa);
    }
    for (let i = 80; i <= 300; i++) {
      const { device, ide } = await record({ list: { 'line-height': i / 100 } });
      expect(ide[StyleElement.List]!.lineHeight, `line-height ${i / 100}`).toBe(i);
      expect(device.list!.lineHeight, `line-height ${i / 100}`).toBe(i);
    }
    // A third decimal, and a value out of range, are left out by both.
    for (const [property, value] of [['opacity', 0.291], ['opacity', 1.01], ['line-height', 1.255], ['line-height', 0.79], ['line-height', 3.01]] as const) {
      const { device, ide } = await record({ list: { [property]: value } });
      expect(ide[StyleElement.List], `${property} ${value}`).toEqual({ face: 0, px: 32, color: 0x808080, lineHeight: 125 });
      expect(device.list, `${property} ${value}`).toMatchObject({ opa: 255, lineHeight: 125 });
    }
  }, 20000);

  it('more than 32 styles: the 30th of the others and the rest are left out', async () => {
    const many: Record<string, unknown> = { 'bad name': {}, button: { color: '#00ff00' }, notobject: 5 };
    for (let i = 1; i <= 31; i++) many[`s${i}`] = { color: i === 29 ? '#00ff00' : '#0000ff' };
    many.list = { color: '#ffffff' };
    await same(config({ light: { style: 's29' }, blind: { style: 's30' }, co2: { style: 's31 s28' } }, many), 'many');
  });
});

describe('the IDE places a text where the firmware does', () => {
  /** `cfg` without any text-align in its styles. */
  function withoutAlign(cfg: Obj): Obj {
    const strip = (v: unknown): unknown => {
      if (Array.isArray(v) || typeof v !== 'object' || v === null) return v;
      return Object.fromEntries(Object.entries(v).filter(([k]) => k !== 'text-align').map(([k, w]) => [k, strip(w)]));
    };
    return { ...cfg, styles: strip(cfg.styles) };
  }
  const samePixels = (a: Uint8ClampedArray, b: Uint8ClampedArray) => a.length === b.length && a.every((v, i) => v === b[i]);
  const MARKUP: Record<number, string> = { 0: '', [StyleAlign.Left]: '\\al', [StyleAlign.Center]: '\\ac', [StyleAlign.Right]: '\\ar' };

  /**
   * The device with `cfg`, and the device with the same config without any
   * text-align but with the \a markup the IDE computes for each text: the
   * same pixels on every display.
   */
  async function placedAlike(cfg: Obj, what: string) {
    const styles = fontStyles(cfg);
    const markup = (kind: 'button' | 'displayitem', part: 'main' | 'title', raw: Obj) =>
      MARKUP[styleTextAlign(lookupPart(styles, kind, part, itemStyleList(raw, styles)).props)]!;
    const plain = withoutAlign(cfg);
    const values: string[] = [];
    plain.displayitems = (cfg.displayitems as Obj[]).map((raw) => {
      if (raw.name === 'temp' || raw.name === 'co2') {
        values.push(`fb.main.display.setValue(${JSON.stringify(raw.name)}, ${JSON.stringify(markup('displayitem', 'main', raw) + '612')});`);
      }
      return { ...raw, label: markup('displayitem', 'title', raw) + String(raw.label ?? '') };
    });
    plain.buttons = (cfg.buttons as Obj[]).map(raw => ({
      ...raw, label: markup('button', 'main', raw) + String(raw.label ?? ''),
      ...(raw.toplabel ? { toplabel: markup('button', 'title', raw) + String(raw.toplabel) } : {}),
    }));
    const styled = await start(cfg, 'fb.main.display.setValue("temp", "612"); fb.main.display.setValue("co2", "612");');
    const marked = await start(plain, values.join('\n'));
    expect(styled.rec.errors.concat(marked.rec.errors), what).toEqual([]);
    for (const display of ['main', 'key3', 'key4']) {
      expect(samePixels(styled.emu.screenshot(display).data, marked.emu.screenshot(display).data), `${what}: ${display}`).toBe(true);
    }
    return styled.emu.screenshot('main').data.slice();
  }

  it('text-align of the item\'s styles and of its kind, per part; ignored in "unit"', async () => {
    const none = await placedAlike(config(), 'no alignment');
    for (const style of ['center', 'right', 'left', 'center_title', 'unit_right', 'center right', 'right center', 'left center_title', 'right alarm big']) {
      const shot = await placedAlike(config({ light: { style }, blind: { style }, temp: { style }, co2: { style } }), style);
      // The alignment is real: the pixels are not those of the config without it (but where "unit" alone has it).
      expect(samePixels(shot, none), style).toBe(style === 'unit_right');
    }
    const kinds = { ...STYLES, button: { 'text-align': 'right', title: { 'text-align': 'left' } }, displayitem: { 'text-align': 'center', title: { 'text-align': 'right' } } };
    await placedAlike(config({}, kinds), 'kinds');
    await placedAlike(config({ light: { style: 'left' }, co2: { style: 'left' }, temp: { style: 'center_title' } }, kinds), 'kinds + styles');
  });
});

describe('the IDE accepts and refuses what the firmware does', () => {
  const cfg = config({ temp: { style: 'alarm' } }, {
    ...STYLES,
    button: { opacity: 1, 'letter-spacing': 0, title: { 'text-decoration': 'none' }, icon: { color: '#0000ff' } },
    list: { 'background-color': '#000000' },
  });
  /** Per style, the dotted properties fb.style.set can set for it (style-config styleSettable). */
  const settable = styleNames(cfg);
  const named = { has: (name: string) => Object.prototype.hasOwnProperty.call(settable, name) && !styleKind(name) };

  /** The firmware's answer to a call: "ok", or the kind of its error. */
  const answer = (emu: Emulator, code: string): string => {
    const result = emu.eval(code);
    if (result.ok) return 'ok';
    const message = result.error.message;
    if (/unknown style/.test(message)) return 'unknown-style';
    if (/unknown property/.test(message)) return 'unknown';
    if (/cannot be changed by a script/.test(message)) return 'fixed';
    if (/has no "\w+" (block|part)|has no main block/.test(message)) return 'no-part';
    if (/not a valid value/.test(message)) return 'bad-value';
    if (/is the style of a kind/.test(message)) return 'reserved';
    if (/at most 4 styles/.test(message)) return 'too-many';
    return message;
  };

  // About 16,000 calls into the emulator: more than the default 5 s on a loaded machine.
  it('fb.style.set: every style, every property of every part, good and bad values', { timeout: 120_000 }, async () => {
    const { emu } = await start(cfg);
    const properties = [
      'color', 'font-family', 'font-size', 'text-align', 'letter-spacing', 'text-decoration', 'opacity', 'line-height', 'background-color',
      'colour', 'font-weight', 'text_color', '',
    ];
    const prefixes = ['', 'title.', 'unit.', 'selected.', 'icon.', 'main.'];
    const values: (string | number)[] = [
      '#00ff00', '#00ff0080', 'red', '00ff00', 'left', 'center', 'justify', 'underline', 'line-through underline', 'none', 'montserrat',
      0, 1, 0.5, 0.25, 0.333, 2, 2.5, 20, -3, 100, 101, 255,
    ];
    // Every style, part and property with one value of each class: a colour, a number, a
    // number that is none for a style (a fraction for an integer, a third decimal), a bad
    // string. That is where the styles differ (the blocks each has).
    const few: (string | number)[] = ['#00ff00', 0.5, 14.5, 0.333, 'red'];
    // Every value with every part and property: a style of the table that has every block,
    // one that has only a main block, one that has none, and the three kinds.
    const full = ['parts', 'alarm', 'muted', 'empty', 'picked', 'button', 'displayitem', 'list'];
    let checked = 0;
    const seen = new Set<string>();
    for (const style of [...Object.keys(settable), 'nosuch']) {
      for (const prefix of prefixes) {
        for (const property of properties) {
          for (const value of full.includes(style) ? [...values, 14.5] : few) {
            const dotted = prefix + property;
            const expected = Object.prototype.hasOwnProperty.call(settable, style) ? scriptSetStatus(settable[style]!, dotted, value) : 'unknown-style';
            const code = `fb.style.set(${JSON.stringify(style)}, ${JSON.stringify(dotted)}, ${JSON.stringify(value)})`;
            expect(answer(emu, code), code).toBe(expected);
            seen.add(expected);
            checked++;
          }
        }
      }
    }
    expect(checked).toBeGreaterThan(15000);
    // Every answer there is was given by some call.
    expect([...seen].sort()).toEqual(['bad-value', 'fixed', 'no-part', 'ok', 'unknown', 'unknown-style']);
    // And the text of "no-part" is the device's.
    for (const [style, dotted] of [['muted', 'title.color'], ['picked', 'color'], ['button', 'unit.color'], ['list', 'icon.color']] as const) {
      const result = emu.eval(`fb.style.set("${style}", "${dotted}", "#00ff00")`);
      expect(result.ok ? '' : result.error.message, `${style} ${dotted}`).toBe(`TypeError: ${scriptSetNoPartMessage(style, dotted)}`);
    }
  });

  it('setStyle: a list of styles, with the error the device throws', async () => {
    const { emu } = await start(cfg);
    const lists = [
      '', 'alarm', 'alarm muted', 'alarm  muted big small', 'alarm muted big small tiny', 'alarm muted alarm big small alarm', 'nosuch',
      'alarm Alarm', '1bad', 'button', 'alarm list', 'displayitem alarm', 'nosuch button', 'button nosuch', 'a b c d e', ' alarm ',
    ];
    for (const text of lists) {
      const parsed = parseScriptStyleList(text, named);
      const expected = parsed.ok ? 'ok' : parsed.error === 'unknown' ? 'unknown-style' : parsed.error;
      for (const call of [
        `fb.main.display.setStyle("co2", ${JSON.stringify(text)})`, `fb.button("light").display.setStyle(${JSON.stringify(text)})`,
        `fb.main.list.setStyle("scenes", ${JSON.stringify(text)})`,
      ]) {
        expect(answer(emu, call), call).toBe(expected);
      }
      // What the device then has is the list the IDE computed ("" = the configured one, none here).
      if (parsed.ok) {
        emu.render();
        expect(emu.state.item('co2')!.style, text).toBe(parsed.list.join(' '));
        expect(emu.state.key('light')!.style, text).toBe(parsed.list.join(' '));
        expect(emu.state.list('scenes')!.style, text).toBe(parsed.list.join(' '));
      }
    }
  });

  it('setStyle(""): the list of config.json again, also where that list is not empty', async () => {
    const { emu } = await start(cfg);
    const configured = itemStyleList((cfg.displayitems as Obj[]).find(e => e.name === 'temp')!, fontStyles(cfg)).join(' ');
    expect(configured).toBe('alarm');
    const style = () => { emu.render(); return emu.state.item('temp')!.style; };
    expect(style()).toBe(configured);
    for (const text of ['muted', 'big small', '', 'muted', ' ', 'alarm muted', '  ']) {
      const parsed = parseScriptStyleList(text, named);
      expect(parsed.ok, text).toBe(true);
      expect(answer(emu, `fb.main.display.setStyle("temp", ${JSON.stringify(text)})`), text).toBe('ok');
      // No name: not "no style" but the configured list.
      const expected = parsed.ok && parsed.list.length ? parsed.list.join(' ') : configured;
      expect(style(), JSON.stringify(text)).toBe(expected);
    }
  });

  it('addStyle and removeStyle: the list the IDE computes is the list the device has, call by call', async () => {
    const { emu } = await start(cfg);
    const targets = [
      { call: (m: string, name: string) => `fb.main.display.${m}("temp", ${JSON.stringify(name)})`, now: () => emu.state.item('temp')!.style, list: ['alarm'] },
      { call: (m: string, name: string) => `fb.button("light").display.${m}(${JSON.stringify(name)})`, now: () => emu.state.key('light')!.style, list: [] as string[] },
    ];
    const steps: ['addStyle' | 'removeStyle', string][] = [
      ['addStyle', 'muted'], ['addStyle', 'alarm'], ['addStyle', 'big'], ['removeStyle', 'small'], ['addStyle', 'small'],
      ['addStyle', 'tiny'], ['addStyle', 'muted'], ['addStyle', 'small'], ['removeStyle', 'alarm'], ['addStyle', 'tiny'],
      ['removeStyle', 'nosuch'], ['addStyle', 'nosuch'], ['addStyle', 'button'], ['removeStyle', 'list'], ['addStyle', 'alarm muted'],
      ['removeStyle', 'muted'], ['removeStyle', 'muted'], ['removeStyle', 'big'], ['removeStyle', 'small'], ['removeStyle', 'tiny'],
      ['removeStyle', 'alarm'], ['removeStyle', 'alarm'], ['addStyle', 'empty'],
    ];
    for (const target of targets) {
      for (const [method, name] of steps) {
        // The IDE's answer: the kind of error, or the new list (style-config styleListAdd / styleListRemove).
        let expected = 'ok';
        let next: string[] | null = target.list;
        if (styleKind(name)) expected = 'reserved';
        else if (!named.has(name)) expected = 'unknown-style';
        else if (method === 'removeStyle') next = styleListRemove(target.list, name);
        else {
          next = styleListAdd(target.list, name);
          if (next === null) expected = 'too-many';
        }
        const code = target.call(method, name);
        expect(answer(emu, code), code).toBe(expected);
        if (expected === 'ok') target.list = next!;
        emu.render();
        expect(target.now(), code).toBe(target.list.join(' '));
      }
    }
  });
});
