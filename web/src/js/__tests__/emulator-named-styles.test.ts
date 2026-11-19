/**
 * Styles (config.json "styles", docs/specs/named-styles.md) in the emulator:
 * the firmware's own config loader, lookup, script calls and restore, as the
 * virtual device shows them. The rules themselves are the firmware's
 * (src/ui/style_logic.c, tested natively in test/test_style_logic); these
 * tests are what a config and a script get to see.
 *
 *   an item's look = its styles ("style": up to four names, the last wins),
 *                    else the style of its kind ("button", "displayitem",
 *                    "list"), else the built-in look.
 */
import { describe, expect, it } from 'vitest';
import type { Emulator, Screenshot } from '../emulator';
import { BASIC_CONFIG, makeEmulator } from './emulator-helpers';
import { repoMedia } from './emulator-pixels';

const STYLES = {
  // A colour for the main text and another for the title (a button's top label, an item's caption).
  alarm: { color: '#ff0000', title: { color: '#ff8080' } },
  muted: { color: '#808080' },
  // Face and size together, and each alone.
  big: { 'font-family': 'montserrat_semibold', 'font-size': 48 },
  semi: { 'font-family': 'montserrat_semibold' },
  huge: { 'font-size': 48 },
  small: { 'font-size': 20 },
  tiny: { 'font-size': 14 },
  loud: { color: '#ff0000', 'font-size': 20 },
  // Every part a style can have.
  parts: {
    color: '#00ff00',
    title: { color: '#00ffff', 'font-size': 20 },
    unit: { color: '#ff00ff', 'font-size': 10 },
    icon: { color: '#ffff00' },
    selected: { color: '#ffffff', 'background-color': '#00c853' },
  },
  picked: { selected: { 'background-color': '#00c853', color: '#0000ff' } },
  bold_row: { selected: { 'font-family': 'montserrat_semibold' } },
  roomy: { 'font-size': 20, 'line-height': 1.5 },
  tight: { 'line-height': 1 },
  panel: { 'background-color': '#0000ff80', selected: { 'background-color': '#00ff0080' } },
  center: { 'text-align': 'center' },
  center_title: { title: { 'text-align': 'center' } },
  right: { 'text-align': 'right' },
  unit_right: { unit: { 'text-align': 'right' } },
  spaced: { 'letter-spacing': 3, opacity: 0.5, 'text-decoration': 'underline' },
  tint: { icon: { color: '#00ff00' } },
  empty: {},
};

type Extra = Record<string, Record<string, unknown>>;

/** BASIC_CONFIG's items, each with what `extra` adds to it (by name), and the item "co2". */
function config(extra: Extra = {}, styles: Record<string, unknown> | null = STYLES) {
  const add = <T extends { name?: string }>(entries: readonly T[]) => entries.map(e => ({ ...e, ...(e.name ? extra[e.name] : {}) }));
  const { styles: _basic, ...rest } = BASIC_CONFIG;
  return {
    ...rest,
    ...(styles ? { styles: { size32: { 'font-size': 32 }, ...styles } } : {}),
    buttons: add(BASIC_CONFIG.buttons),
    displayitems: [
      // "temp" has style "size32" in BASIC_CONFIG; without "styles" it has none.
      ...add(BASIC_CONFIG.displayitems.map(i => (styles ? i : { ...i, style: undefined }))),
      { name: 'co2', displayitemid: 8, x: 10, y: 60, width: 40, page: 1, label: 'CO2', unit: 'ppm', ...extra.co2 },
    ],
    lists: add(BASIC_CONFIG.lists),
  };
}

type Run = { color: string; font: string; px: number };
const text = (t: { runs: Run[] } | undefined) => t?.runs.map(r => `${r.color} ${r.font} ${r.px}`).join(' | ') ?? null;

/** What the device draws, per part: the colour and font of the text's runs. */
function look(emu: Emulator) {
  emu.render();
  const s = emu.state;
  const box = (name: string) => {
    const b = s.item(name)?.box;
    return b ? { caption: text(b.caption), value: text(b.value), unit: text(b.unit) } : null;
  };
  const roller = s.list('scenes')!.roller;
  return {
    light: { label: text(s.key('light')!.display!.label), top: text(s.key('light')!.display!.topLabel) },
    blind: { label: text(s.key('blind')!.display!.label), top: text(s.key('blind')!.display!.topLabel) },
    temp: box('temp'),
    co2: box('co2'),
    list: roller && {
      color: roller.color, selectedBg: roller.selectedBg, selectedColor: roller.selectedColor, bg: roller.bg,
      font: `${roller.font.font} ${roller.font.px}`, selectedFont: `${roller.selectedFont.font} ${roller.selectedFont.px}`,
    },
  };
}

/** The style lists as the device has them now: "current" or "current (config: configured)". */
function lists(emu: Emulator) {
  emu.render();
  const s = emu.state;
  const one = (e: { style: string; configStyle: string } | undefined) =>
    (e ? (e.style === e.configStyle ? e.style : `${e.style} (config: ${e.configStyle})`) : null);
  return { light: one(s.key('light')), blind: one(s.key('blind')), temp: one(s.item('temp')), co2: one(s.item('co2')), list: one(s.list('scenes')) };
}

/** Pixels of (about) one colour; the panel has 5/6/5 bits per channel. */
function pixelsOf(shot: Screenshot, r: number, g: number, b: number): number {
  let n = 0;
  for (let i = 0; i < shot.data.length; i += 4) {
    if (Math.abs(shot.data[i]! - r) <= 8 && Math.abs(shot.data[i + 1]! - g) <= 8 && Math.abs(shot.data[i + 2]! - b) <= 8) n++;
  }
  return n;
}

/** Where two pictures of a display differ: "same", or the number of pixels and the rows they are in. */
function differ(a: Uint8ClampedArray, b: Uint8ClampedArray, width = 320): string {
  let n = 0, top = -1, bottom = -1;
  for (let i = 0; i < a.length; i += 4) {
    if (a[i] === b[i] && a[i + 1] === b[i + 1] && a[i + 2] === b[i + 2]) continue;
    const y = Math.floor(i / 4 / width);
    if (top < 0) top = y;
    bottom = y;
    n++;
  }
  return n === 0 && a.length === b.length ? 'same' : `${n} pixels differ, rows ${top}..${bottom}`;
}

/** An emulator with values on the display items, so that value and unit are drawn. */
async function start(cfg: unknown, script = '', options: { fonts?: boolean; icons?: boolean } = {}) {
  // fonts: the repository's TrueType files. Without them LVGL's built-in font stands in
  // (one line height for every size), which is enough where only face and px are compared.
  const media = options.fonts || options.icons ? { media: repoMedia({}, { icons: options.icons ?? false }) } : {};
  const made = await makeEmulator({ config: cfg as never, ...media });
  expect(made.emu.run(`fb.main.display.setValue("temp", "21"); fb.main.display.setValue("co2", "612");\n${script}`)).toEqual({ ok: true });
  made.clock.advance(100);
  return made;
}

const message = (emu: Emulator, code: string): string => {
  const result = emu.eval(code);
  return result.ok ? `ok ${result.value}` : result.error.message;
};

/** The built-in look: no "styles" in config.json, or styles that no item names. */
const BUILT_IN = {
  light: { label: '#ffffff montserrat 32', top: '#ffd700 montserrat_semibold 18' },
  blind: { label: '#ffffff montserrat 32', top: '' },
  temp: { caption: '#ffd700 montserrat_semibold 14', value: '#ffffff montserrat 14', unit: '#999999 montserrat 14' },
  co2: { caption: '#ffd700 montserrat_semibold 14', value: '#ffffff montserrat 14', unit: '#999999 montserrat 14' },
  list: { color: '#808080', selectedBg: '#ffd700', selectedColor: '#000000', bg: null, font: 'montserrat 32', selectedFont: 'montserrat 32' },
};
/** config(): "temp" names the style "size32". The unit has the value's font. */
const PLAIN = {
  ...BUILT_IN,
  temp: { caption: '#ffd700 montserrat_semibold 14', value: '#ffffff montserrat 32', unit: '#999999 montserrat 32' },
};

describe('styles in config.json', () => {
  it('without "styles" the look is the built-in one, and the script calls are there all the same', async () => {
    const { emu, rec } = await start(config({}, null));
    expect(look(emu)).toEqual(BUILT_IN);
    expect(emu.state.styleNames).toEqual([]);
    expect(rec.errors).toEqual([]);
    // The styles of the kinds always exist, so the methods do too.
    for (const method of [
      'fb.style.set', 'fb.button', 'fb.button("light").display.setStyle', 'fb.button("light").display.addStyle',
      'fb.button("light").display.removeStyle', 'fb.main.display.setStyle', 'fb.main.display.addStyle',
      'fb.main.display.removeStyle', 'fb.main.list.setStyle',
    ]) {
      expect(message(emu, `typeof ${method}`), method).toBe('ok function');
    }
    expect(message(emu, 'fb.style.set("button", "color", "#00ff00")')).toBe('ok undefined');
    expect(look(emu).blind.label).toBe('#00ff00 montserrat 32');
  });

  it('the calls that styles replaced are gone', async () => {
    const { emu } = await start(config());
    for (const gone of [
      'fb.key',
      'fb.button("light").display.setFont', 'fb.button("light").display.setTopFont', 'fb.button("light").display.setTopStyle',
      'fb.main.display.setFont', 'fb.main.display.setLabelStyle',
      'fb.main.list.setFont', 'fb.main.list.setFocusStyle',
      // A list has setStyle only.
      'fb.main.list.addStyle', 'fb.main.list.removeStyle',
    ]) {
      expect(message(emu, `typeof ${gone}`), gone).toBe('ok undefined');
    }
    // The engine's own error for a call that is not there.
    expect(message(emu, 'fb.key("light")')).toMatch(/^TypeError: .*not a function/);
    expect(message(emu, 'fb.main.display.setFont("temp", "montserrat", 20)')).toMatch(/^TypeError: .*not a function/);
  });

  it('styles that no item names change nothing', async () => {
    const { emu } = await start(config());
    expect(look(emu)).toEqual(PLAIN);
    expect(emu.state.styleNames).toEqual(['size32', ...Object.keys(STYLES)]);
  });

  it('a style on a button: the main block on the label, "title" on the top label', async () => {
    const { emu } = await start(config({ light: { style: 'alarm' }, blind: { style: 'big' } }));
    expect(look(emu)).toEqual({
      ...PLAIN,
      light: { label: '#ff0000 montserrat 32', top: '#ff8080 montserrat_semibold 18' },
      // Face and size from the style; the colour still the kind's.
      blind: { label: '#ffffff montserrat_semibold 48', top: '' },
    });
    expect(emu.state.key('blind')).toMatchObject({ font: { face: 1, size: 48 }, style: 'big', configStyle: 'big' });
    // The pixels: the red label is on the button's own display, and only there.
    expect(pixelsOf(emu.screenshot('key3'), 255, 0, 0)).toBeGreaterThan(20);
    expect(pixelsOf(emu.screenshot('key4'), 255, 0, 0)).toBe(0);
  });

  it('a style on a display item: main on the value, "title" on the caption; a main colour does not colour the unit', async () => {
    const { emu } = await start(config({ temp: { style: 'alarm' }, co2: { style: 'big' } }));
    expect(look(emu)).toEqual({
      ...PLAIN,
      // "temp" no longer names size32: the value is 14 px again, and the unit with it.
      temp: { caption: '#ff8080 montserrat_semibold 14', value: '#ff0000 montserrat 14', unit: '#999999 montserrat 14' },
      // The unit has the value's face and size, and its own colour.
      co2: { caption: '#ffd700 montserrat_semibold 14', value: '#ffffff montserrat_semibold 48', unit: '#999999 montserrat_semibold 48' },
    });
    expect(pixelsOf(emu.screenshot('main'), 255, 0, 0)).toBeGreaterThan(10);
    expect(pixelsOf((await start(config())).emu.screenshot('main'), 255, 0, 0)).toBe(0);
  });

  it('a style on a list: main on the rows, "selected" on the selected row; a main colour does not colour that row', async () => {
    const { emu } = await start(config({ scenes: { style: 'alarm' } }));
    expect(look(emu).list).toEqual({ ...PLAIN.list, color: '#ff0000' });
    const picked = await start(config({ scenes: { style: 'picked' } }));
    expect(look(picked.emu).list).toEqual({ ...PLAIN.list, selectedBg: '#00c853', selectedColor: '#0000ff' });
    const shot = picked.emu.screenshot('main');
    expect(pixelsOf(shot, 0, 200, 82)).toBeGreaterThan(1000);   // the green bar
    expect(pixelsOf(shot, 255, 215, 0)).toBeLessThan(400);      // no gold bar (the captions are gold)
  });

  it('the selected row has the rows\' font unless a "selected" block sets one', async () => {
    const rows = await start(config({ scenes: { style: 'big' } }));
    expect(look(rows.emu).list).toMatchObject({ font: 'montserrat_semibold 48', selectedFont: 'montserrat_semibold 48' });
    // A face in "selected": that face, at the rows' size.
    const bold = await start(config({ scenes: { style: 'small bold_row' } }));
    expect(look(bold.emu).list).toMatchObject({ font: 'montserrat 20', selectedFont: 'montserrat_semibold 20' });
  });

  it('two, three and four styles: the last name wins, property by property', async () => {
    const { emu } = await start(config({
      light: { style: 'alarm muted' },             // both set the colour: muted, the last
      blind: { style: 'muted alarm' },
      temp: { style: 'size32 muted alarm' },       // three
      co2: { style: 'huge alarm muted small' },    // four: the size of the last that sets one, the colour likewise
    }));
    expect(look(emu)).toEqual({
      ...PLAIN,
      // muted has no "title" block: the title is alarm's.
      light: { label: '#808080 montserrat 32', top: '#ff8080 montserrat_semibold 18' },
      blind: { label: '#ff0000 montserrat 32', top: '' },
      temp: { caption: '#ff8080 montserrat_semibold 14', value: '#ff0000 montserrat 32', unit: '#999999 montserrat 32' },
      co2: { caption: '#ff8080 montserrat_semibold 14', value: '#808080 montserrat 20', unit: '#999999 montserrat 20' },
    });
    expect(lists(emu)).toEqual({
      light: 'alarm muted', blind: 'muted alarm', temp: 'size32 muted alarm', co2: 'huge alarm muted small', list: '',
    });
  });

  it('the face comes from one style and the size from another', async () => {
    const { emu } = await start(config({
      light: { style: 'huge semi' }, blind: { style: 'semi huge' },
      temp: { style: 'big small' },      // big's face, small's size
      co2: { style: 'small big' },       // big sets both, last
      scenes: { style: 'semi small' },
    }));
    expect(look(emu)).toMatchObject({
      light: { label: '#ffffff montserrat_semibold 48' }, blind: { label: '#ffffff montserrat_semibold 48' },
      temp: { value: '#ffffff montserrat_semibold 20', unit: '#999999 montserrat_semibold 20' },
      co2: { value: '#ffffff montserrat_semibold 48' },
      list: { font: 'montserrat_semibold 20', selectedFont: 'montserrat_semibold 20' },
    });
  });

  it('the sub-blocks: title, unit, icon and selected, each for the part it names', async () => {
    const { emu } = await start(config({ light: { style: 'parts' }, temp: { style: 'size32 parts' }, scenes: { style: 'parts' } }),
      'fb.main.display.setIcon("temp", "sun"); fb.button("blind").display.setIcon("moon");', { icons: true });
    const seen = look(emu);
    expect(seen.light).toEqual({ label: '#00ff00 montserrat 32', top: '#00ffff montserrat_semibold 20' });
    // The unit: its own colour and size from "unit"; its face is the value's.
    expect(seen.temp).toEqual({ caption: '#00ffff montserrat_semibold 20', value: '#00ff00 montserrat 32', unit: '#ff00ff montserrat 10' });
    expect(seen.list).toEqual({ ...PLAIN.list, color: '#00ff00', selectedColor: '#ffffff', selectedBg: '#00c853' });
    // The icon's tint: "icon.color"; without one, the main colour.
    expect(emu.state.item('temp')!.box!.icon).toMatchObject({ name: 'sun', tint: '#ffff00' });
    expect(emu.state.key('blind')!.display!.icon).toMatchObject({ name: 'moon', tint: '#ffffff' });
    // A block a kind does not have is ignored: "unit" and "selected" on a button, "title" on a list.
    expect(seen.blind).toEqual(PLAIN.blind);
  });

  it('the icon tint: "icon.color" of the item\'s styles, else of its kind, else the main colour', async () => {
    const cfg = config({ light: { style: 'alarm' }, blind: { style: 'alarm tint' }, temp: { style: 'muted' }, co2: { style: 'tint' } });
    const script = `fb.button("light").display.setIcon("moon"); fb.button("blind").display.setIcon("moon");
      fb.main.display.setIcon("temp", "sun"); fb.main.display.setIcon("co2", "sun");`;
    const { emu } = await start(cfg, script, { icons: true });
    emu.render();
    const tints = (e: Emulator) => {
      e.render();
      return [e.state.key('light')!.display!.icon!.tint, e.state.key('blind')!.display!.icon!.tint,
        e.state.item('temp')!.box!.icon!.tint, e.state.item('co2')!.box!.icon!.tint];
    };
    expect(tints(emu)).toEqual(['#ff0000', '#00ff00', '#808080', '#00ff00']);
    // The kinds' own icon colour: for the items without "icon" in their styles.
    const kinds = { ...cfg, styles: { ...cfg.styles, button: { icon: { color: '#0000ff' } }, displayitem: { icon: { color: '#00ffff' } } } };
    const second = await start(kinds, script, { icons: true });
    expect(tints(second.emu)).toEqual(['#0000ff', '#00ff00', '#00ffff', '#00ff00']);
    // A script changes it; the icons are drawn again.
    expect(message(second.emu, 'fb.style.set("button", "icon.color", "#ff00ff"); fb.style.set("tint", "icon.color", "#ffffff")')).toBe('ok undefined');
    expect(tints(second.emu)).toEqual(['#ff00ff', '#ffffff', '#00ffff', '#ffffff']);
    // A restart gives the icon colours of config.json back, of the style and of the kind.
    expect(second.emu.restart()).toEqual({ ok: true });
    expect(tints(second.emu)).toEqual(['#0000ff', '#00ff00', '#00ffff', '#00ff00']);
    // Without an icon colour in config a script gives one: to the kind (for the items whose
    // styles have none), then to a style of the item (which wins over the kind's).
    expect(message(emu, 'fb.style.set("button", "icon.color", "#ff00ff")')).toBe('ok undefined');
    expect(tints(emu)).toEqual(['#ff00ff', '#00ff00', '#808080', '#00ff00']);
    expect(message(emu, 'fb.style.set("alarm", "icon.color", "#0000ff"); fb.style.set("displayitem", "icon.color", "#ffff00")')).toBe('ok undefined');
    expect(tints(emu)).toEqual(['#0000ff', '#00ff00', '#ffff00', '#00ff00']);
    // A list draws no icon; "icon" takes a colour only.
    expect(message(emu, 'fb.style.set("list", "icon.color", "#ff00ff")')).toBe('TypeError: fb.style.set(): the style of this kind has no "icon" part');
    expect(message(emu, 'fb.style.set("alarm", "icon.opacity", 0.5)')).toBe('TypeError: fb.style.set(): unknown property "icon.opacity"');
    expect(message(emu, 'fb.style.set("alarm", "icon.color", 5)')).toBe('TypeError: fb.style.set(): not a valid value for "icon.color"');
    expect(tints(emu)).toEqual(['#0000ff', '#00ff00', '#ffff00', '#00ff00']);
    // A restart takes the added colours away again.
    expect(emu.restart()).toEqual({ ok: true });
    expect(tints(emu)).toEqual(['#ff0000', '#00ff00', '#808080', '#00ff00']);
  });

  it('"button", "displayitem" and "list" in config change the look of every item of the kind', async () => {
    const kinds = {
      button: { color: '#00ffff', 'font-size': 24, title: { color: '#ffffff' } },
      displayitem: { 'font-size': 20, 'font-family': 'montserrat_semibold', unit: { color: '#00ff00' }, title: { 'font-size': 12 } },
      list: { color: '#ffffff', 'font-size': 20, selected: { 'background-color': '#ff00ff' } },
    };
    const { emu } = await start(config({ blind: { style: 'alarm' } }, { ...STYLES, ...kinds }));
    expect(look(emu)).toEqual({
      light: { label: '#00ffff montserrat 24', top: '#ffffff montserrat_semibold 18' },
      // An item's own style still wins over its kind's.
      blind: { label: '#ff0000 montserrat 24', top: '' },
      temp: { caption: '#ffd700 montserrat_semibold 12', value: '#ffffff montserrat_semibold 32', unit: '#00ff00 montserrat_semibold 32' },
      co2: { caption: '#ffd700 montserrat_semibold 12', value: '#ffffff montserrat_semibold 20', unit: '#00ff00 montserrat_semibold 20' },
      // The selected row keeps its built-in colour, and has the rows' font.
      list: { color: '#ffffff', selectedBg: '#ff00ff', selectedColor: '#000000', bg: null, font: 'montserrat 20', selectedFont: 'montserrat 20' },
    });
    // The reserved names are not styles an item can name.
    expect(emu.state.styleNames).toEqual(['size32', ...Object.keys(STYLES)]);
    expect(emu.state.styles.button).toMatchObject({ color: '#00ffff', px: 24 });
    expect(emu.state.styles['displayitem.unit']).toMatchObject({ color: '#00ff00', font: null });
  });

  it('a "style" the device cannot use in full: the names it cannot use are left out, the rest stays', async () => {
    const { emu } = await start(config({
      light: { style: 'button alarm' },                  // a reserved name: not for an item
      blind: { style: 'nosuch muted 1bad' },             // unknown, and not a name
      temp: { style: 'size32 alarm muted small tiny' },  // a fifth name
      co2: { style: 7 },                                 // not a string
      scenes: { style: 'list alarm displayitem' },
    }));
    expect(lists(emu)).toEqual({ light: 'alarm', blind: 'muted', temp: 'size32 alarm muted small', co2: '', list: 'alarm' });
    expect(look(emu)).toMatchObject({
      light: { label: '#ff0000 montserrat 32' }, blind: { label: '#808080 montserrat 32' },
      temp: { value: '#808080 montserrat 20' }, co2: PLAIN.co2, list: { color: '#ff0000' },
    });
  });

  it('a name given twice counts once, at its last place; spaces between the names do not matter', async () => {
    const { emu } = await start(config({
      light: { style: 'alarm muted alarm' }, blind: { style: '  muted    alarm  ' }, temp: { style: 'size32 size32 size32 size32 size32 muted' },
    }));
    expect(lists(emu)).toMatchObject({ light: 'muted alarm', blind: 'muted alarm', temp: 'size32 muted' });
    expect(look(emu)).toMatchObject({ light: { label: '#ff0000 montserrat 32' }, temp: { value: '#808080 montserrat 32' } });
  });

  it('bad style names, a block that is no object and a 30th style are left out; the rest loads', async () => {
    const many: Record<string, unknown> = { '1bad': { color: '#00ff00' }, 'has space': {}, notobject: 5 };
    // 32 styles in all, the three kinds included: 29 others (size32 is one).
    for (let i = 1; i <= 29; i++) many[`s${i}`] = { color: i === 28 ? '#00ff00' : '#0000ff' };
    const { emu } = await start(config({ light: { style: 's28' }, blind: { style: 's29' } }, many));
    const seen = look(emu);
    expect(seen.light.label).toBe('#00ff00 montserrat 32');
    expect(seen.blind.label).toBe(PLAIN.blind.label);
    expect(emu.state.styleNames).toHaveLength(29);
    for (const name of ['s29', '1bad', 'notobject', 'has space']) {
      expect(message(emu, `fb.style.set("${name}", "color", "#ffffff")`), name).toBe(`ReferenceError: unknown style "${name}"`);
    }
    expect(message(emu, 'fb.style.set("s28", "color", "#ffffff")')).toBe('ok undefined');
  });

  it('bad properties and values are left out; the rest of the style loads', async () => {
    const odd = {
      mixed: {
        color: 'red', 'font-size': 5, 'font-weight': 700, 'text-align': 'justify', opacity: 2, 'letter-spacing': 2,
        'font-family': 'comic', text_color: '#00ff00', title: { color: '#00ffff', 'line-height': 9 },
      },
      short: { color: '00ff00' },     // a colour needs the "#"
    };
    const { emu } = await start(config({ light: { style: 'mixed' }, blind: { style: 'short' } }, odd));
    expect(look(emu)).toMatchObject({
      light: { label: PLAIN.light.label, top: '#00ffff montserrat_semibold 18' }, blind: PLAIN.blind,
    });
    // What loaded can be changed. What did not load is not in the style, but its block is
    // there: a script can set the property.
    expect(message(emu, 'fb.style.set("mixed", "letter-spacing", 4)')).toBe('ok undefined');
    expect(message(emu, 'fb.style.set("mixed", "color", "#00ff00")')).toBe('ok undefined');
    expect(message(emu, 'fb.style.set("short", "color", "#ff00ff")')).toBe('ok undefined');
    expect(look(emu)).toMatchObject({
      light: { label: '#00ff00 montserrat 32', top: '#00ffff montserrat_semibold 18' }, blind: { label: '#ff00ff montserrat 32' },
    });
  });

  it('the removed config keys are ignored: a config in the old format boots with the built-in look', async () => {
    const old = {
      ...config({}, null),
      // Version 1 and before: the "style" block, the font fields, the part styles, the list colours.
      style: {
        fonts: [{ name: 'extra', src: 'semibold.ttf' }],
        text: { text_color: '#00ff00' }, title: { text_size: 30 },
        key: { label: { text_size: 20 } }, display: { value: { text_size: 40 } }, list: { text_color: '#ff0000' },
        styles: { alarm: { text_color: '#ff0000' } },
      },
      buttons: BASIC_CONFIG.buttons.map(b => (b.name === 'light'
        ? { ...b, font: 'montserrat_semibold', fontsize: 5, topfont: 'montserrat', topfontsize: 12, topstyle: 'alarm', style: 'alarm' } : b)),
      displayitems: [
        { name: 'temp', displayitemid: 7, x: 10, y: 20, width: 40, page: 1, align: 1, label: 'Temperature', unit: '°C',
          fontsize: 3, font: 'extra', labelfont: 'extra', labelfontsize: 2, labelstyle: 'alarm', iconcolor: 'ff0000' },
        { name: 'co2', displayitemid: 8, x: 10, y: 60, width: 40, page: 1, label: 'CO2', unit: 'ppm', fontsize: 40 },
      ],
      lists: [{ ...BASIC_CONFIG.lists[0], color: 'ff0000', focuscolor: '00ff00', focustextcolor: '0000ff', focusstyle: 'alarm', fontsize: 2, font: 'extra' }],
      // The old names of the lists: not read (the item under it is not there).
      mqttdisplays: [{ name: 'legacy', label: 'old', x: 50, y: 60, width: 40, page: 1 }],
      mqttbuttons: [{ name: 'oldkey', position: 4, label: 'Old' }],
    };
    const { emu, rec } = await start(old);
    expect(rec.errors).toEqual([]);
    expect(look(emu)).toEqual(BUILT_IN);
    expect(lists(emu)).toEqual({ light: '', blind: '', temp: '', co2: '', list: '' });
    expect(emu.state.styleNames).toEqual([]);
    expect(emu.state.item('legacy')).toBeUndefined();
    expect(emu.state.key('oldkey')).toBeUndefined();
    expect(emu.state.items.map(i => i.name)).toEqual(['temp', 'co2']);
    // "alarm" was a style of the old block: there is no such style now.
    expect(message(emu, 'fb.main.display.setStyle("temp", "alarm")')).toBe('ReferenceError: unknown style "alarm"');
  });
});

describe('setStyle from a script', () => {
  it('gives an item a list of styles; "" gives it the list of config.json again', async () => {
    const { emu, rec } = await start(config({ temp: { style: 'alarm' }, light: { style: 'muted' } }), `
      var k = fb.button("light").display;
      print(k.setStyle("big alarm"));
      print(fb.main.display.setStyle("co2", "alarm small"), fb.main.display.setStyle(8, "alarm   small"), fb.main.display.setStyle(99, "alarm"));
      print(fb.main.display.setStyle("temp", "muted"));
      print(fb.main.list.setStyle("scenes", "alarm picked"), fb.main.list.setStyle(0, "alarm picked"));
    `);
    expect(rec.errors).toEqual([]);
    // A button's and a list's setStyle return nothing; a display item's says whether the item exists.
    expect(rec.console).toEqual(['undefined', 'true true false', 'true', 'undefined undefined']);
    expect(lists(emu)).toEqual({
      light: 'big alarm (config: muted)', blind: '', temp: 'muted (config: alarm)', co2: 'alarm small (config: )', list: 'alarm picked (config: )',
    });
    expect(look(emu)).toEqual({
      ...PLAIN,
      light: { label: '#ff0000 montserrat_semibold 48', top: '#ff8080 montserrat_semibold 18' },
      // The configured style is gone: muted has no "title".
      temp: { caption: '#ffd700 montserrat_semibold 14', value: '#808080 montserrat 14', unit: '#999999 montserrat 14' },
      co2: { caption: '#ff8080 montserrat_semibold 14', value: '#ff0000 montserrat 20', unit: '#999999 montserrat 20' },
      list: { ...PLAIN.list, color: '#ff0000', selectedBg: '#00c853', selectedColor: '#0000ff' },
    });
    // And back: "" is the list of config.json (none, where it gives none).
    emu.eval('k.setStyle(""); fb.main.display.setStyle("temp", ""); fb.main.display.setStyle("co2", ""); fb.main.list.setStyle("scenes", "")');
    expect(lists(emu)).toEqual({ light: 'muted', blind: '', temp: 'alarm', co2: '', list: '' });
    expect(look(emu)).toEqual({
      ...PLAIN,
      light: { label: '#808080 montserrat 32', top: PLAIN.light.top },
      temp: { caption: '#ff8080 montserrat_semibold 14', value: '#ff0000 montserrat 14', unit: '#999999 montserrat 14' },
    });
  });

  it('the error types: ReferenceError for an unknown style, RangeError for a fifth, TypeError for the rest', async () => {
    const { emu } = await start(config({ temp: { style: 'alarm' } }));
    const before = look(emu);
    const said = (code: string) => message(emu, code);
    // Unknown style (names are case sensitive).
    expect(said('fb.button("light").display.setStyle("nosuch")')).toBe('ReferenceError: unknown style "nosuch"');
    expect(said('fb.main.display.setStyle("temp", "alarm Alarm")')).toBe('ReferenceError: unknown style "Alarm"');
    expect(said('fb.main.list.setStyle("scenes", "nosuch")')).toBe('ReferenceError: unknown style "nosuch"');
    // Unknown item or list.
    expect(said('fb.main.display.setStyle("nope", "alarm")')).toBe('ReferenceError: unknown display item "nope"');
    expect(said('fb.main.list.setStyle("nolist", "alarm")')).toBe('ReferenceError: unknown list "nolist"');
    expect(said('fb.button("nobody")')).toBe('ReferenceError: unknown button "nobody"');
    expect(said('fb.button(3)')).toBe('TypeError: fb.button() requires a button name (string)');
    // Not a string.
    expect(said('fb.button("light").display.setStyle(3)')).toBe('TypeError: setStyle() requires style names (a string)');
    expect(said('fb.button("light").display.setStyle()')).toBe('TypeError: setStyle() requires style names (a string)');
    expect(said('fb.main.display.setStyle("temp")')).toBe('TypeError: display.setStyle() requires id and styles arguments');
    expect(said('fb.main.display.setStyle("temp", 3)')).toBe('TypeError: display.setStyle() requires style names (a string)');
    expect(said('fb.main.list.setStyle("scenes", null)')).toBe('TypeError: fb.main.list.setStyle() requires style names (a string)');
    // The style of a kind: every item of the kind has it anyway.
    expect(said('fb.button("light").display.setStyle("button")')).toBe('TypeError: setStyle(): "button" is the style of a kind and cannot be named by an item');
    expect(said('fb.main.display.setStyle("temp", "alarm list")')).toBe('TypeError: display.setStyle(): "list" is the style of a kind and cannot be named by an item');
    expect(said('fb.main.list.setStyle("scenes", "displayitem")')).toMatch(/^TypeError: fb\.main\.list\.setStyle\(\): "displayitem" is the style of a kind/);
    // A fifth name; a name given twice counts once.
    expect(said('fb.main.display.setStyle("temp", "alarm muted big small tiny")')).toBe('RangeError: display.setStyle(): an item has at most 4 styles');
    expect(said('fb.button("light").display.setStyle("alarm muted big small tiny")')).toBe('RangeError: setStyle(): an item has at most 4 styles');
    expect(said('fb.main.list.setStyle("scenes", "alarm muted big small tiny")')).toBe('RangeError: fb.main.list.setStyle(): an item has at most 4 styles');
    // None of it changed anything.
    expect(look(emu)).toEqual(before);
    expect(said('fb.main.display.setStyle("temp", "alarm muted alarm big small alarm")')).toBe('ok true');
    expect(lists(emu).temp).toBe('muted big small alarm (config: alarm)');
  });

  it('setStyle with the list the button has keeps the font that the label\'s own markup gives it', async () => {
    const { emu, rec } = await start(config(), 'fb.button("blind").display.setLabel("\\\\s{20}Low");', { fonts: true });
    expect(look(emu).blind.label).toBe('#ffffff montserrat 20');
    emu.eval('fb.button("blind").display.setStyle("alarm")');
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 20');
    const once = emu.screenshot('key4').data.slice();
    // The same list again: nothing changes (the markup's 20 px must not become the style's 32).
    emu.eval('fb.button("blind").display.setStyle("alarm")');
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 20');
    expect(differ(emu.screenshot('key4').data, once, 160)).toBe('same');
    emu.eval('fb.button("blind").display.addStyle("alarm"); fb.button("blind").display.setStyle("alarm ")');
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 20');
    expect(differ(emu.screenshot('key4').data, once, 160)).toBe('same');
    // The styles are applied again without a new list: a value of the style changes, the kind's changes.
    emu.eval('fb.style.set("alarm", "color", "#00ff00")');
    expect(look(emu).blind.label).toBe('#00ff00 montserrat 20');
    emu.eval('fb.style.set("button", "title.color", "#00ffff")');
    expect(look(emu).blind.label).toBe('#00ff00 montserrat 20');
    // And with a font of the item's own: a face from one style and a size from another is a local font too.
    emu.eval('fb.button("blind").display.setStyle("semi huge"); fb.button("blind").display.setStyle("semi huge")');
    expect(look(emu).blind.label).toBe('#ffffff montserrat_semibold 20');
    emu.eval('fb.button("blind").display.setLabel("Low"); fb.button("blind").display.setStyle("semi huge")');
    expect(look(emu).blind.label).toBe('#ffffff montserrat_semibold 48');
    // The same for a display item and a list: the list they have is no change.
    emu.eval('fb.main.display.setValue("co2", "\\\\s{20}612"); fb.main.display.setStyle("co2", "alarm"); fb.main.display.setStyle("co2", "alarm")');
    expect(look(emu).co2!.value).toBe('#00ff00 montserrat 20');   // "alarm" is green by now
    emu.eval('fb.main.list.setStyle("scenes", "alarm"); fb.main.list.setStyle("scenes", "alarm")');
    expect(look(emu).list!.color).toBe('#00ff00');
    expect(rec.errors).toEqual([]);
  });

  it('a NUL character in the names is refused: what comes after it is not dropped silently', async () => {
    const { emu } = await start(config({ blind: { style: 'muted' } }));
    const before = lists(emu);
    for (const [call, what] of [
      ['fb.button("blind").display.setStyle("big\\0junk")', 'setStyle'],
      ['fb.main.display.setStyle("co2", "big\\0junk")', 'display.setStyle'],
      ['fb.main.display.setStyle("co2", "\\0")', 'display.setStyle'],
      ['fb.main.list.setStyle("scenes", "big alarm\\0")', 'fb.main.list.setStyle'],
      ['fb.main.display.create(50, 60, 40, 0, "big\\0", 0, "New", "u", 0, 1)', 'display.create'],
    ] as const) {
      expect(message(emu, call), call).toBe(`TypeError: ${what}(): the style names have a NUL character in them`);
    }
    for (const [call, what] of [
      ['fb.button("blind").display.addStyle("big\\0junk")', 'addStyle'],
      ['fb.button("blind").display.removeStyle("muted\\0")', 'removeStyle'],
      ['fb.main.display.addStyle("co2", "big\\0junk")', 'display.addStyle'],
      ['fb.main.display.removeStyle("co2", "\\0big")', 'display.removeStyle'],
    ] as const) {
      expect(message(emu, call), call).toBe(`TypeError: ${what}(): the style name has a NUL character in it`);
    }
    expect(lists(emu)).toEqual(before);
    expect(emu.state.items.map(i => i.name)).toEqual(['temp', 'power', 'co2']);
  });

  it('a button that config.json does not list has no Button to style: InternalError', async () => {
    // Button 4 (the bar's right one) has no entry in "buttons": no name, reached through fb.bars.
    const cfg = config();
    const { emu } = await start({ ...cfg, buttons: cfg.buttons.filter(b => b.position !== 4) });
    const said = (code: string) => message(emu, code);
    expect(said('fb.bars[0].right.display.setStyle("alarm")')).toBe('InternalError: failed to set style for button 4');
    expect(said('fb.bars[0].right.display.setStyle("")')).toBe('InternalError: failed to set style for button 4');
    expect(said('fb.bars[0].right.display.addStyle("alarm")')).toBe('InternalError: failed to set style for button 4');
    expect(said('fb.bars[0].right.display.removeStyle("alarm")')).toBe('InternalError: failed to set style for button 4');
    // The argument is checked first, as on a button with an entry.
    expect(said('fb.bars[0].right.display.setStyle("nosuch")')).toBe('ReferenceError: unknown style "nosuch"');
    // The button beside it is in the config, and is the object fb.button() gives.
    expect(said('fb.bars[0].left.display.setStyle("alarm")')).toBe('ok undefined');
    expect(said('fb.button("light") === fb.bars[0].left')).toBe('ok true');
    emu.render();
    expect(emu.state.key('light')!.display!.label.runs).toMatchObject([{ color: '#ff0000', font: 'montserrat', px: 32 }]);
  });
});

describe('addStyle and removeStyle', () => {
  it('addStyle puts a style on top of what the item has; one it has already moves to the end', async () => {
    const { emu, rec } = await start(config({ light: { style: 'muted' }, co2: { style: 'small muted' } }), `
      var k = fb.button("light").display;
      print(k.addStyle("alarm"), fb.main.display.addStyle("co2", "alarm"), fb.main.display.addStyle(99, "alarm"));
    `);
    expect(rec.errors).toEqual([]);
    expect(rec.console).toEqual(['undefined true false']);
    expect(lists(emu)).toMatchObject({ light: 'muted alarm (config: muted)', co2: 'small muted alarm (config: small muted)' });
    expect(look(emu)).toMatchObject({
      light: { label: '#ff0000 montserrat 32', top: '#ff8080 montserrat_semibold 18' },
      co2: { value: '#ff0000 montserrat 20', caption: '#ff8080 montserrat_semibold 14' },
    });
    // Adding one the item has: it moves to the end, so it wins now.
    emu.eval('k.addStyle("muted"); fb.main.display.addStyle("co2", "muted")');
    expect(lists(emu)).toMatchObject({ light: 'alarm muted (config: muted)', co2: 'small alarm muted (config: small muted)' });
    expect(look(emu)).toMatchObject({ light: { label: '#808080 montserrat 32' }, co2: { value: '#808080 montserrat 20' } });
    // Adding the last one again changes nothing.
    emu.eval('k.addStyle("muted")');
    expect(lists(emu).light).toBe('alarm muted (config: muted)');
  });

  it('a fifth style is a RangeError and changes nothing; one of the four can still move', async () => {
    const { emu } = await start(config({ co2: { style: 'small muted alarm semi' }, light: { style: 'small muted alarm semi' } }));
    const before = look(emu);
    expect(message(emu, 'fb.main.display.addStyle("co2", "tiny")')).toBe('RangeError: display.addStyle(): an item has at most 4 styles');
    expect(message(emu, 'fb.button("light").display.addStyle("tiny")')).toBe('RangeError: addStyle(): an item has at most 4 styles');
    expect(lists(emu)).toMatchObject({ co2: 'small muted alarm semi', light: 'small muted alarm semi' });
    expect(look(emu)).toEqual(before);
    expect(message(emu, 'fb.main.display.addStyle("co2", "muted")')).toBe('ok true');
    expect(lists(emu).co2).toBe('small alarm semi muted (config: small muted alarm semi)');
    // With one taken away there is room again.
    expect(message(emu, 'fb.main.display.removeStyle("co2", "semi"); fb.main.display.addStyle("co2", "tiny")')).toBe('ok true');
    expect(lists(emu).co2).toBe('small alarm muted tiny (config: small muted alarm semi)');
  });

  it('removeStyle takes one style away, also one config.json gave; a style the item does not have: nothing', async () => {
    const { emu, rec } = await start(config({ light: { style: 'muted big' }, co2: { style: 'small alarm' } }), `
      var k = fb.button("light").display;
      print(k.removeStyle("alarm"), fb.main.display.removeStyle("co2", "muted"), fb.main.display.removeStyle(99, "muted"));
    `);
    expect(rec.console).toEqual(['undefined true false']);
    expect(lists(emu)).toMatchObject({ light: 'muted big', co2: 'small alarm' });
    emu.eval('k.removeStyle("muted"); fb.main.display.removeStyle("co2", "small")');
    expect(lists(emu)).toMatchObject({ light: 'big (config: muted big)', co2: 'alarm (config: small alarm)' });
    expect(look(emu)).toMatchObject({
      light: { label: '#ffffff montserrat_semibold 48' }, co2: { value: '#ff0000 montserrat 14', unit: '#999999 montserrat 14' },
    });
    emu.eval('k.removeStyle("big"); fb.main.display.removeStyle("co2", "alarm")');
    expect(lists(emu)).toMatchObject({ light: ' (config: muted big)', co2: ' (config: small alarm)' });
    expect(look(emu)).toMatchObject({ light: PLAIN.light, co2: PLAIN.co2 });
    // setStyle("") still gives the configured list back.
    emu.eval('k.setStyle("")');
    expect(lists(emu).light).toBe('muted big');
  });

  it('switch a state on and off without knowing the item\'s other styles', async () => {
    const { emu } = await start(config({ temp: { style: 'size32 muted' }, blind: { style: 'big' } }));
    const idle = { look: look(emu), main: emu.screenshot('main').data.slice(), key: emu.screenshot('key4').data.slice() };
    emu.eval('fb.main.display.addStyle("temp", "alarm"); fb.button("blind").display.addStyle("alarm")');
    expect(look(emu)).toMatchObject({
      temp: { value: '#ff0000 montserrat 32', caption: '#ff8080 montserrat_semibold 14' }, blind: { label: '#ff0000 montserrat_semibold 48' },
    });
    emu.eval('fb.main.display.removeStyle("temp", "alarm"); fb.button("blind").display.removeStyle("alarm")');
    expect(look(emu)).toEqual(idle.look);
    expect(lists(emu)).toMatchObject({ temp: 'size32 muted', blind: 'big' });
    expect(emu.screenshot('main').data).toEqual(idle.main);
    expect(emu.screenshot('key4').data).toEqual(idle.key);
  });

  it('the argument is one style name', async () => {
    const { emu } = await start(config());
    const said = (code: string) => message(emu, code);
    for (const call of ['fb.button("light").display.addStyle', 'fb.button("light").display.removeStyle']) {
      const name = call.slice(call.lastIndexOf('.') + 1);
      expect(said(`${call}("nosuch")`)).toBe('ReferenceError: unknown style "nosuch"');
      // Not a list of names.
      expect(said(`${call}("alarm muted")`)).toBe('ReferenceError: unknown style "alarm muted"');
      expect(said(`${call}("")`)).toBe(`TypeError: ${name}() requires a style name (a string)`);
      expect(said(`${call}(5)`)).toBe(`TypeError: ${name}() requires a style name (a string)`);
      expect(said(`${call}()`)).toBe(`TypeError: ${name}() requires a style name (a string)`);
      expect(said(`${call}("button")`)).toBe(`TypeError: ${name}(): "button" is the style of a kind and cannot be named by an item`);
    }
    for (const name of ['addStyle', 'removeStyle']) {
      const call = `fb.main.display.${name}`;
      expect(said(`${call}("temp", "nosuch")`)).toBe('ReferenceError: unknown style "nosuch"');
      expect(said(`${call}("nope", "alarm")`)).toBe('ReferenceError: unknown display item "nope"');
      expect(said(`${call}("temp", "")`)).toBe(`TypeError: display.${name}() requires a style name (a string)`);
      expect(said(`${call}("temp", null)`)).toBe(`TypeError: display.${name}() requires a style name (a string)`);
      expect(said(`${call}("temp")`)).toBe(`TypeError: display.${name}() requires id and style arguments`);
      expect(said(`${call}("temp", "displayitem")`)).toBe(`TypeError: display.${name}(): "displayitem" is the style of a kind and cannot be named by an item`);
    }
    expect(lists(emu)).toEqual({ light: '', blind: '', temp: 'size32', co2: '', list: '' });
  });
});

describe('display.create and display.get', () => {
  it('create takes a style string where it took a font size; get returns the item\'s styles now', async () => {
    const { emu, rec } = await start(config(), `
      var d = fb.main.display;
      var id = d.create(50, 60, 40, 0, "big alarm", 0, "New", "u", 0, 1);
      var none = d.create(50, 80, 40, 0, "", 0, "None", "u", 0, 1);
      d.setValue(id, "5"); d.setValue(none, "6");
      print(d.get(id).style, "|" + d.get(none).style + "|", d.get("temp").style, typeof d.get(id).fontSize);
    `);
    expect(rec.errors).toEqual([]);
    expect(rec.console).toEqual(['big alarm || size32 undefined']);
    emu.render();
    const made = emu.state.items.find(i => i.label === 'New')!;
    expect(made).toMatchObject({ style: 'big alarm', configStyle: 'big alarm' });
    expect(text(made.box!.value)).toBe('#ff0000 montserrat_semibold 48');
    expect(text(made.box!.caption)).toBe('#ff8080 montserrat_semibold 14');
    expect(text(made.box!.unit)).toBe('#999999 montserrat_semibold 48');
    expect(text(emu.state.items.find(i => i.label === 'None')!.box!.value)).toBe('#ffffff montserrat 14');
    // get() follows the script; "" is the list the item was made with.
    expect(message(emu, 'd.addStyle(id, "small"); d.get(id).style')).toBe('ok big alarm small');
    expect(message(emu, 'd.setStyle(id, "muted"); d.get(id).style')).toBe('ok muted');
    expect(message(emu, 'd.setStyle(id, ""); d.get(id).style')).toBe('ok big alarm');
    // The same checks as setStyle.
    expect(message(emu, 'd.create(0, 0, 10, 0, 3, 0, "x", "", 0, 1)')).toBe('TypeError: display.create() requires style names (a string)');
    expect(message(emu, 'd.create(0, 0, 10, 0, "nosuch", 0, "x", "", 0, 1)')).toBe('ReferenceError: unknown style "nosuch"');
    expect(message(emu, 'd.create(0, 0, 10, 0, "displayitem", 0, "x", "", 0, 1)')).toMatch(/^TypeError: display\.create\(\): "displayitem" is the style of a kind/);
    expect(message(emu, 'd.count()')).toBe('ok 5');
  });
});

describe('fb.style.set on a style of config.json', () => {
  it('changes a value for every user at once, markup spans included', async () => {
    const { emu, rec } = await start(config({
      temp: { style: 'size32 alarm' }, co2: { style: 'alarm' }, blind: { style: 'alarm' }, light: { style: 'alarm' },
      scenes: { style: 'alarm picked' },
    }), `
      fb.main.display.setValue("temp", "2\\\\c0000ff1\\\\cffff00 C");
      fb.button("blind").display.setLabel("Bl\\\\c0000ffind");
    `);
    expect(rec.errors).toEqual([]);
    const before = look(emu);
    // The first span has the style's colour, the others their own.
    expect(before.temp!.value).toBe('#ff0000 montserrat 32 | #0000ff montserrat 32 | #ffff00 montserrat 32');
    expect(before.blind.label).toBe('#ff0000 montserrat 32 | #0000ff montserrat 32');
    expect(before).toMatchObject({
      temp: { caption: '#ff8080 montserrat_semibold 14' }, co2: { value: '#ff0000 montserrat 14' },
      light: { label: '#ff0000 montserrat 32', top: '#ff8080 montserrat_semibold 18' },
      list: { color: '#ff0000', selectedBg: '#00c853', selectedColor: '#0000ff' },
    });
    expect(pixelsOf(emu.screenshot('main'), 0, 255, 0)).toBe(0);

    // A main property, and the dotted ones of the sub-blocks.
    expect(message(emu, `fb.style.set("alarm", "color", "#00ff00"); fb.style.set("alarm", "title.color", "#00ffff");
      fb.style.set("picked", "selected.background-color", "#ff00ff"); fb.style.set("picked", "selected.color", "#ffffff")`)).toBe('ok undefined');
    const after = look(emu);
    expect(after.temp!.value).toBe('#00ff00 montserrat 32 | #0000ff montserrat 32 | #ffff00 montserrat 32');
    expect(after.blind.label).toBe('#00ff00 montserrat 32 | #0000ff montserrat 32');
    expect(after).toMatchObject({
      temp: { caption: '#00ffff montserrat_semibold 14', unit: PLAIN.temp.unit }, co2: { value: '#00ff00 montserrat 14', caption: '#00ffff montserrat_semibold 14' },
      light: { label: '#00ff00 montserrat 32', top: '#00ffff montserrat_semibold 18' },
      list: { color: '#00ff00', selectedBg: '#ff00ff', selectedColor: '#ffffff' },
    });
    const shot = emu.screenshot('main');
    expect(pixelsOf(shot, 0, 255, 0)).toBeGreaterThan(50);
    expect(pixelsOf(shot, 255, 0, 0)).toBe(0);
    expect(pixelsOf(shot, 255, 0, 255)).toBeGreaterThan(1000);
    const key = emu.screenshot('key4');
    expect(pixelsOf(key, 0, 255, 0)).toBeGreaterThan(5);
    expect(pixelsOf(key, 255, 0, 0)).toBe(0);
  });

  it('takes numbers too: opacity and letter-spacing, as config.json has them', async () => {
    const { emu } = await start(config({ blind: { style: 'spaced' } }), '', { fonts: true });
    const width = () => {
      const shot = emu.screenshot('key4');
      let min = shot.width, max = -1;
      for (let i = 0; i < shot.data.length; i += 4) {
        if (shot.data[i]! + shot.data[i + 1]! + shot.data[i + 2]! > 0) { const x = (i / 4) % shot.width; min = Math.min(min, x); max = Math.max(max, x); }
      }
      return max - min;
    };
    // Half opacity: no pixel is fully white.
    expect(pixelsOf(emu.screenshot('key4'), 255, 255, 255)).toBe(0);
    const spaced = width();
    expect(message(emu, 'fb.style.set("spaced", "opacity", 1)')).toBe('ok undefined');
    expect(pixelsOf(emu.screenshot('key4'), 255, 255, 255)).toBeGreaterThan(50);
    expect(message(emu, 'fb.style.set("spaced", "letter-spacing", 12)')).toBe('ok undefined');
    expect(width()).toBeGreaterThan(spaced + 20);
    expect(message(emu, 'fb.style.set("spaced", "text-decoration", "none")')).toBe('ok undefined');
    expect(message(emu, 'fb.style.set("spaced", "text-decoration", "line-through underline")')).toBe('ok undefined');
    // Out of range, the wrong type, more than two decimals.
    expect(message(emu, 'fb.style.set("spaced", "opacity", 1.5)')).toBe('TypeError: fb.style.set(): not a valid value for "opacity"');
    expect(message(emu, 'fb.style.set("spaced", "opacity", 0.333)')).toBe('TypeError: fb.style.set(): not a valid value for "opacity"');
    expect(message(emu, 'fb.style.set("spaced", "opacity", "0.5")')).toBe('TypeError: fb.style.set(): not a valid value for "opacity"');
    expect(message(emu, 'fb.style.set("spaced", "letter-spacing", 2.5)')).toBe('TypeError: fb.style.set(): not a valid value for "letter-spacing"');
    expect(message(emu, 'fb.style.set("spaced", "letter-spacing", NaN)')).toBe('TypeError: fb.style.set(): not a valid value for "letter-spacing"');
    expect(message(emu, 'fb.style.set("spaced", "letter-spacing", 101)')).toBe('TypeError: fb.style.set(): not a valid value for "letter-spacing"');
    expect(message(emu, 'fb.style.set("spaced", "text-decoration", "blink")')).toBe('TypeError: fb.style.set(): not a valid value for "text-decoration"');
  });

  it('what it refuses: a block the style does not have, the font, the size, the line height, an unknown name, a bad value', async () => {
    const { emu } = await start(config({ temp: { style: 'alarm' }, scenes: { style: 'roomy' } }));
    const before = look(emu);
    const said = (code: string) => message(emu, code);
    expect(said('fb.style.set("nosuch", "color", "#ffffff")')).toBe('ReferenceError: unknown style "nosuch"');
    // A block that config.json does not give the style cannot be made: the table is sized when the config loads.
    expect(said('fb.style.set("muted", "title.color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style has no "title" block in config.json');
    expect(said('fb.style.set("alarm", "unit.color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style has no "unit" block in config.json');
    expect(said('fb.style.set("alarm", "selected.background-color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style has no "selected" block in config.json');
    // The main block is there when the style sets a property of its own: not for a style of sub-blocks only, or {}.
    expect(said('fb.style.set("picked", "color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style has no main block in config.json');
    expect(said('fb.style.set("empty", "color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style has no main block in config.json');
    // The order of the checks: the property name, the three fixed properties, the block, the value.
    expect(said('fb.style.set("big", "font-size", 14.5)')).toBe('TypeError: fb.style.set(): "font-size" cannot be changed by a script');
    expect(said('fb.style.set("big", "font-size", "big")')).toBe('TypeError: fb.style.set(): "font-size" cannot be changed by a script');
    expect(said('fb.style.set("roomy", "line-height", 1.234)')).toBe('TypeError: fb.style.set(): "line-height" cannot be changed by a script');
    expect(said('fb.style.set("muted", "title.font-size", 14.5)')).toBe('TypeError: fb.style.set(): "title.font-size" cannot be changed by a script');
    expect(said('fb.style.set("muted", "title.colour", "red")')).toBe('TypeError: fb.style.set(): unknown property "title.colour"');
    expect(said('fb.style.set("muted", "title.color", "red")')).toBe('TypeError: fb.style.set(): the style has no "title" block in config.json');
    expect(said('fb.style.set("muted", "title.opacity", 0.333)')).toBe('TypeError: fb.style.set(): the style has no "title" block in config.json');
    expect(said('fb.style.set("muted", "opacity", 0.333)')).toBe('TypeError: fb.style.set(): not a valid value for "opacity"');
    expect(said('fb.style.set("muted", "font-weight", 14.5)')).toBe('TypeError: fb.style.set(): unknown property "font-weight"');
    // Per font: fixed.
    expect(said('fb.style.set("big", "font-family", "montserrat")')).toBe('TypeError: fb.style.set(): "font-family" cannot be changed by a script');
    expect(said('fb.style.set("big", "font-size", 20)')).toBe('TypeError: fb.style.set(): "font-size" cannot be changed by a script');
    expect(said('fb.style.set("roomy", "line-height", 2)')).toBe('TypeError: fb.style.set(): "line-height" cannot be changed by a script');
    expect(said('fb.style.set("parts", "title.font-size", 30)')).toBe('TypeError: fb.style.set(): "title.font-size" cannot be changed by a script');
    // Not a property: version 1's names, CSS names the device does not have, a prefix that is no part.
    expect(said('fb.style.set("alarm", "text_color", "#ffffff")')).toBe('TypeError: fb.style.set(): unknown property "text_color"');
    expect(said('fb.style.set("alarm", "font-weight", 700)')).toBe('TypeError: fb.style.set(): unknown property "font-weight"');
    expect(said('fb.style.set("alarm", "main.color", "#ffffff")')).toBe('TypeError: fb.style.set(): unknown property "main.color"');
    expect(said('fb.style.set("alarm", "title.colour", "#ffffff")')).toBe('TypeError: fb.style.set(): unknown property "title.colour"');
    expect(said(`fb.style.set("alarm", "${'x'.repeat(40)}", "#ffffff")`)).toMatch(/^TypeError: fb\.style\.set\(\): unknown property "x+"$/);
    // A bad value.
    expect(said('fb.style.set("alarm", "color", "red")')).toBe('TypeError: fb.style.set(): not a valid value for "color"');
    expect(said('fb.style.set("alarm", "color", "ff0000")')).toBe('TypeError: fb.style.set(): not a valid value for "color"');
    expect(said('fb.style.set("alarm", "color", 5)')).toBe('TypeError: fb.style.set(): not a valid value for "color"');
    expect(said('fb.style.set("alarm", "color", "#ff000080")')).toBe('TypeError: fb.style.set(): not a valid value for "color"');
    for (const value of ['true', 'false', 'null', 'undefined', '{}', '["#00ff00"]', 'function () {}']) {
      expect(said(`fb.style.set("alarm", "color", ${value})`), value).toBe('TypeError: fb.style.set(): the value must be a string or a number');
    }
    // The arguments.
    expect(said('fb.style.set("", "color", "#00ff00")')).toBe('TypeError: fb.style.set() requires a style name, a property name and a value');
    expect(said('fb.style.set(7, "color", "#00ff00")')).toBe('TypeError: fb.style.set() requires a style name, a property name and a value');
    expect(said('fb.style.set("alarm", 5, "#00ff00")')).toBe('TypeError: fb.style.set() requires a style name, a property name and a value');
    expect(said('fb.style.set("alarm", "color")')).toBe('TypeError: fb.style.set() requires a style name, a property name and a value');
    expect(said('fb.style.set()')).toMatch(/^TypeError: /);
    // A C string ends at a NUL: what comes after it must not be dropped silently.
    expect(said('fb.style.set("alarm", "color\\0x", "#00ff00")')).toBe('TypeError: fb.style.set(): the property name has a NUL character in it');
    expect(said('fb.style.set("alarm", "color", "#00ff00\\0zz")')).toBe('TypeError: fb.style.set(): the value has a NUL character in it');
    expect(said('fb.style.set("alarm\\0x", "color", "#00ff00")')).toBe('ReferenceError: unknown style "alarm"');
    // None of it changed anything.
    expect(look(emu)).toEqual(before);
  });

  it('on a style nobody uses: an item that takes the style afterwards shows the new value', async () => {
    const { emu, rec } = await start(config());
    expect(message(emu, 'fb.style.set("alarm", "color", "#00ff00"); fb.style.set("picked", "selected.color", "#ff00ff")')).toBe('ok undefined');
    expect(look(emu)).toEqual(PLAIN);
    emu.eval('fb.main.display.setStyle("co2", "alarm"); fb.button("blind").display.addStyle("alarm"); fb.main.list.setStyle("scenes", "picked")');
    expect(look(emu)).toMatchObject({
      co2: { value: '#00ff00 montserrat 14' }, blind: { label: '#00ff00 montserrat 32' }, list: { selectedColor: '#ff00ff' },
    });
    expect(rec.errors).toEqual([]);
  });
});

describe('fb.style.set on "button", "displayitem" and "list"', () => {
  it('recolours every item of the kind that has no style of its own for that property', async () => {
    const { emu, rec } = await start(config({ blind: { style: 'alarm' }, co2: { style: 'muted' } }), `
      fb.main.display.setValue("temp", "2\\\\c0000ff1");
      fb.button("light").display.setLabel("Li\\\\c0000ffght");
    `);
    expect(rec.errors).toEqual([]);
    expect(message(emu, 'fb.style.set("button", "color", "#00ff00"); fb.style.set("button", "title.color", "#00ffff")')).toBe('ok undefined');
    expect(look(emu)).toEqual({
      ...PLAIN,
      // Also the spans of a text with markup: the first has no colour of its own.
      light: { label: '#00ff00 montserrat 32 | #0000ff montserrat 32', top: '#00ffff montserrat_semibold 18' },
      // "alarm" sets both colours for this button.
      blind: { label: '#ff0000 montserrat 32', top: '' },
      temp: { ...PLAIN.temp, value: '#ffffff montserrat 32 | #0000ff montserrat 32' },
      co2: { ...PLAIN.co2, value: '#808080 montserrat 14' },
    });
    expect(pixelsOf(emu.screenshot('key3'), 0, 255, 0)).toBeGreaterThan(0);
    expect(pixelsOf(emu.screenshot('key3'), 255, 255, 255)).toBe(0);
    expect(pixelsOf(emu.screenshot('key4'), 0, 255, 0)).toBe(0);

    expect(message(emu, `fb.style.set("displayitem", "color", "#ff00ff"); fb.style.set("displayitem", "title.color", "#ffffff");
      fb.style.set("displayitem", "unit.color", "#00ff00")`)).toBe('ok undefined');
    expect(look(emu)).toMatchObject({
      temp: { caption: '#ffffff montserrat_semibold 14', value: '#ff00ff montserrat 32 | #0000ff montserrat 32', unit: '#00ff00 montserrat 32' },
      // The item's own colour stays; the caption and the unit are the kind's.
      co2: { caption: '#ffffff montserrat_semibold 14', value: '#808080 montserrat 14', unit: '#00ff00 montserrat 14' },
      // Another kind is not touched.
      light: { label: '#00ff00 montserrat 32 | #0000ff montserrat 32' },
    });
    expect(pixelsOf(emu.screenshot('main'), 255, 0, 255)).toBeGreaterThan(0);

    expect(message(emu, `fb.style.set("list", "color", "#00ffff"); fb.style.set("list", "selected.color", "#ffffff");
      fb.style.set("list", "selected.background-color", "#0000ff")`)).toBe('ok undefined');
    expect(look(emu).list).toEqual({ ...PLAIN.list, color: '#00ffff', selectedColor: '#ffffff', selectedBg: '#0000ff' });
    expect(pixelsOf(emu.screenshot('main'), 0, 0, 255)).toBeGreaterThan(1000);
    // The state's record of the kinds follows.
    emu.render();
    expect(emu.state.item('temp')!.box!.value.runs[0]).toMatchObject({ color: '#ff00ff' });
    expect(rec.errors).toEqual([]);
  });

  it('every property of every part of the kind can be set, held or not; a part the kind does not have cannot', async () => {
    const kinds = {
      button: { opacity: 1, 'letter-spacing': 0, 'text-align': 'left', title: { 'text-decoration': 'none' } },
      list: { 'background-color': '#000000' },
    };
    const { emu } = await start(config({}, { ...STYLES, ...kinds }), '', { fonts: true });
    const said = (code: string) => message(emu, code);
    const key = () => emu.screenshot('key4').data.slice();
    const left = key();
    // From config: alignment, opacity, spacing, decoration.
    expect(said('fb.style.set("button", "text-align", "right")')).toBe('ok undefined');
    const right = key();
    expect(right).not.toEqual(left);
    expect(said('fb.style.set("button", "text-align", "left")')).toBe('ok undefined');
    expect(key()).toEqual(left);
    expect(said('fb.style.set("button", "opacity", 0.5)')).toBe('ok undefined');
    expect(pixelsOf(emu.screenshot('key4'), 255, 255, 255)).toBe(0);
    expect(said('fb.style.set("button", "opacity", 1); fb.style.set("button", "letter-spacing", 10)')).toBe('ok undefined');
    expect(key()).not.toEqual(left);
    expect(said('fb.style.set("button", "letter-spacing", 0); fb.style.set("button", "title.text-decoration", "underline")')).toBe('ok undefined');
    expect(key()).toEqual(left);   // "blind" has no top label
    expect(said('fb.style.set("list", "background-color", "#0000ff")')).toBe('ok undefined');
    expect(look(emu).list!.bg).toBe('#0000ff');
    // Built in: the colours (above), the selected row's background.
    expect(said('fb.style.set("list", "selected.background-color", "#ffffff80")')).toBe('ok undefined');
    // Not held by the kind: added (the tests of "fb.style.set adds a property" look at the screen).
    expect(said('fb.style.set("displayitem", "opacity", 0.5)')).toBe('ok undefined');
    expect(said('fb.style.set("displayitem", "background-color", "#000000")')).toBe('ok undefined');
    expect(said('fb.style.set("button", "title.opacity", 0.5)')).toBe('ok undefined');
    // A part the kind does not have.
    expect(said('fb.style.set("button", "unit.color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style of this kind has no "unit" part');
    expect(said('fb.style.set("button", "selected.color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style of this kind has no "selected" part');
    expect(said('fb.style.set("list", "title.color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style of this kind has no "title" part');
    expect(said('fb.style.set("list", "icon.color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style of this kind has no "icon" part');
    expect(said('fb.style.set("displayitem", "selected.color", "red")')).toBe('TypeError: fb.style.set(): the style of this kind has no "selected" part');
    // Fixed, unknown and bad, as for any style.
    for (const kind of ['button', 'displayitem', 'list']) {
      expect(said(`fb.style.set("${kind}", "font-size", 20)`)).toBe('TypeError: fb.style.set(): "font-size" cannot be changed by a script');
      expect(said(`fb.style.set("${kind}", "font-family", "montserrat")`)).toBe('TypeError: fb.style.set(): "font-family" cannot be changed by a script');
      expect(said(`fb.style.set("${kind}", "line-height", 2)`)).toBe('TypeError: fb.style.set(): "line-height" cannot be changed by a script');
      expect(said(`fb.style.set("${kind}", "colour", "#ffffff")`)).toBe('TypeError: fb.style.set(): unknown property "colour"');
      expect(said(`fb.style.set("${kind}", "color", "white")`)).toBe('TypeError: fb.style.set(): not a valid value for "color"');
    }
    expect(said('fb.style.set("Button", "color", "#ffffff")')).toBe('ReferenceError: unknown style "Button"');
  });
});

describe('fb.style.set adds a property', () => {
  /** The pixels of a display that are lit at all, and those of one colour. */
  const lit = (shot: Screenshot) => {
    let n = 0;
    for (let i = 0; i < shot.data.length; i += 4) if (shot.data[i]! + shot.data[i + 1]! + shot.data[i + 2]! > 0) n++;
    return n;
  };
  const roller = (emu: Emulator) => {
    emu.render();
    const r = emu.state.list('scenes')!.roller!;
    return { text: r.text, selectedText: r.selectedText };
  };
  const NONE = { opa: 255, letterSpace: 0, decor: 0 };

  it('to a style of config.json: every user changes, markup spans included', async () => {
    const { emu, rec } = await start(config({
      temp: { style: 'size32 muted' }, co2: { style: 'muted' }, blind: { style: 'muted' }, light: { style: 'muted' },
      scenes: { style: 'muted picked' },
    }), `
      fb.main.display.setValue("temp", "2\\\\c0000ff1\\\\cffff00 C");
      fb.button("blind").display.setLabel("Bl\\\\c0000ffind");
    `, { fonts: true });
    expect(rec.errors).toEqual([]);
    // "muted" sets a colour and nothing else.
    expect(pixelsOf(emu.screenshot('key3'), 128, 128, 128)).toBeGreaterThan(20);     // "light": one format
    expect(pixelsOf(emu.screenshot('key4'), 0, 0, 255)).toBeGreaterThan(20);         // "blind": a blue span
    expect(pixelsOf(emu.screenshot('main'), 0, 0, 255)).toBeGreaterThan(5);          // "temp": a blue span
    expect(roller(emu)).toEqual({ text: NONE, selectedText: NONE });
    const before = { main: emu.screenshot('main').data.slice(), key3: emu.screenshot('key3').data.slice() };

    // Opacity: on the label, on every span of a text with markup (their own colours too), on the rows.
    expect(message(emu, 'fb.style.set("muted", "opacity", 0.4)')).toBe('ok undefined');
    expect(pixelsOf(emu.screenshot('key3'), 128, 128, 128)).toBe(0);
    expect(lit(emu.screenshot('key3'))).toBeGreaterThan(20);
    expect(pixelsOf(emu.screenshot('key4'), 0, 0, 255)).toBe(0);
    expect(pixelsOf(emu.screenshot('main'), 0, 0, 255)).toBe(0);
    // The selected row has the rows' opacity: "picked" sets none in "selected".
    expect(roller(emu)).toEqual({ text: { ...NONE, opa: 102 }, selectedText: { ...NONE, opa: 102 } });

    // Decoration and spacing, in the main block and in a sub-block the style has.
    expect(message(emu, `fb.style.set("muted", "text-decoration", "underline"); fb.style.set("muted", "letter-spacing", 6);
      fb.style.set("picked", "selected.letter-spacing", 2); fb.style.set("picked", "selected.opacity", 1)`)).toBe('ok undefined');
    expect(roller(emu)).toEqual({
      text: { opa: 102, letterSpace: 6, decor: 1 }, selectedText: { opa: 255, letterSpace: 2, decor: 1 },
    });
    expect(differ(emu.screenshot('key3').data, before.key3, 160)).not.toBe('same');
    expect(differ(emu.screenshot('main').data, before.main)).not.toBe('same');
    // The values of the added properties change like any other.
    expect(message(emu, 'fb.style.set("muted", "opacity", 1); fb.style.set("muted", "text-decoration", "none"); fb.style.set("muted", "letter-spacing", 0)')).toBe('ok undefined');
    expect(pixelsOf(emu.screenshot('key3'), 128, 128, 128)).toBeGreaterThan(20);
    expect(differ(emu.screenshot('key3').data, before.key3, 160)).toBe('same');
    // The look in words did not change: the same fonts and colours.
    expect(look(emu)).toMatchObject({ light: { label: '#808080 montserrat 32' }, co2: { value: '#808080 montserrat 14' } });
    expect(rec.errors).toEqual([]);
  });

  it('to "button", "displayitem" and "list": every item of the kind', async () => {
    const { emu, rec } = await start(config({ blind: { style: 'alarm' } }), `
      fb.main.display.setValue("temp", "2\\\\c0000ff1");
      fb.button("light").display.setLabel("Li\\\\c0000ffght");
    `, { fonts: true });
    const idle = { main: emu.screenshot('main').data.slice(), key3: emu.screenshot('key3').data.slice(), key4: emu.screenshot('key4').data.slice() };
    expect(pixelsOf(emu.screenshot('key3'), 255, 255, 255)).toBeGreaterThan(10);
    expect(pixelsOf(emu.screenshot('key3'), 0, 0, 255)).toBeGreaterThan(10);
    expect(pixelsOf(emu.screenshot('key4'), 255, 0, 0)).toBeGreaterThan(10);

    // A button: the label (its spans too) of every button, with or without a style of its own.
    expect(message(emu, 'fb.style.set("button", "opacity", 0.4)')).toBe('ok undefined');
    expect(pixelsOf(emu.screenshot('key3'), 255, 255, 255)).toBe(0);
    expect(pixelsOf(emu.screenshot('key3'), 0, 0, 255)).toBe(0);
    expect(pixelsOf(emu.screenshot('key4'), 255, 0, 0)).toBe(0);
    // The top label is another part: it keeps its look until "title." sets it.
    expect(pixelsOf(emu.screenshot('key3'), 255, 215, 0)).toBeGreaterThan(0);
    expect(message(emu, 'fb.style.set("button", "title.opacity", 0.4)')).toBe('ok undefined');
    expect(pixelsOf(emu.screenshot('key3'), 255, 215, 0)).toBe(0);
    expect(differ(emu.screenshot('main').data, idle.main)).toBe('same');

    // A display item: value, caption and unit each by its own block.
    expect(pixelsOf(emu.screenshot('main'), 0, 0, 255)).toBeGreaterThan(0);
    expect(message(emu, 'fb.style.set("displayitem", "opacity", 0.4)')).toBe('ok undefined');
    expect(pixelsOf(emu.screenshot('main'), 0, 0, 255)).toBe(0);
    const value = emu.screenshot('main').data.slice();
    expect(message(emu, 'fb.style.set("displayitem", "unit.text-decoration", "underline"); fb.style.set("displayitem", "title.letter-spacing", 4)')).toBe('ok undefined');
    expect(differ(emu.screenshot('main').data, value)).not.toBe('same');

    // A list: the rows; the selected row with them unless "selected" says otherwise.
    expect(message(emu, 'fb.style.set("list", "letter-spacing", 4); fb.style.set("list", "text-decoration", "line-through")')).toBe('ok undefined');
    expect(roller(emu)).toEqual({ text: { opa: 255, letterSpace: 4, decor: 2 }, selectedText: { opa: 255, letterSpace: 4, decor: 2 } });
    expect(message(emu, 'fb.style.set("list", "selected.text-decoration", "none"); fb.style.set("list", "selected.opacity", 0.5)')).toBe('ok undefined');
    expect(roller(emu)).toEqual({ text: { opa: 255, letterSpace: 4, decor: 2 }, selectedText: { opa: 128, letterSpace: 4, decor: 0 } });
    expect(message(emu, 'fb.style.set("list", "background-color", "#0000ff")')).toBe('ok undefined');
    expect(look(emu).list!.bg).toBe('#0000ff');
    // Fonts and colours in words: as they were.
    expect(look(emu)).toMatchObject({ temp: PLAIN.temp && { caption: PLAIN.temp.caption, unit: PLAIN.temp.unit }, co2: PLAIN.co2 });
    expect(differ(emu.screenshot('key4').data, idle.key4, 160)).not.toBe('same');
    expect(rec.errors).toEqual([]);
  });

  it('to a block that held nothing to draw (a size only): the items that use it get it', async () => {
    const { emu, rec } = await start(config({
      blind: { style: 'huge' }, co2: { style: 'huge' }, scenes: { style: 'small' }, light: { style: 'tiny' },
      // (no unit: at 48 px a value and its unit do not fit the item's width)
    }), 'fb.main.display.setValue("co2", "6\\\\c0000ff12"); fb.main.display.setUnit("co2", "");', { fonts: true });
    // "huge", "small" and "tiny" set a size: nothing of them is on the objects.
    emu.render();
    expect(emu.state.key('blind')!.display!.carried).toEqual({ label: 'huge', topLabel: 'huge' });
    expect(look(emu)).toMatchObject({
      blind: { label: '#ffffff montserrat 48' }, co2: { value: '#ffffff montserrat 48 | #0000ff montserrat 48' },
      list: { color: '#808080', font: 'montserrat 20' },
    });
    const free = emu.stats().lvglFree;

    expect(message(emu, 'fb.style.set("huge", "color", "#ff0000"); fb.style.set("small", "color", "#00ff00")')).toBe('ok undefined');
    expect(look(emu)).toMatchObject({
      blind: { label: '#ff0000 montserrat 48' },
      // the first span has the style's colour now, the second its own
      co2: { value: '#ff0000 montserrat 48 | #0000ff montserrat 48' },
      list: { color: '#00ff00', font: 'montserrat 20', selectedColor: '#000000' },
      light: PLAIN.light && { label: '#ffffff montserrat 14' },
    });
    expect(pixelsOf(emu.screenshot('key4'), 255, 0, 0)).toBeGreaterThan(50);
    expect(pixelsOf(emu.screenshot('main'), 255, 0, 0)).toBeGreaterThan(50);
    expect(pixelsOf(emu.screenshot('main'), 0, 255, 0)).toBeGreaterThan(50);
    // The blocks are on the objects now: it costs LVGL memory while it lasts.
    expect(emu.stats().lvglFree).toBeLessThan(free);

    // What goes on a span label itself: opacity on a block that had nothing.
    expect(pixelsOf(emu.screenshot('key3'), 255, 255, 255)).toBeGreaterThan(5);
    expect(message(emu, 'fb.style.set("tiny", "opacity", 0.4)')).toBe('ok undefined');
    expect(pixelsOf(emu.screenshot('key3'), 255, 255, 255)).toBe(0);
    // An item that takes the style afterwards has the added properties too.
    emu.eval('fb.main.display.setStyle("temp", "huge"); fb.main.list.setStyle("scenes", "huge")');
    expect(look(emu)).toMatchObject({ temp: { value: '#ff0000 montserrat 48' }, list: { color: '#ff0000', font: 'montserrat 48' } });
    // And one that gives it up loses them.
    emu.eval('fb.button("blind").display.setStyle("")');
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 48');   // "": the configured list, which is "huge"
    emu.eval('fb.button("blind").display.removeStyle("huge")');
    expect(look(emu).blind.label).toBe(PLAIN.blind.label);
    expect(rec.errors).toEqual([]);
  });

  it('text-align added by a script places the text as the same value in config.json does', async () => {
    const run = `fb.main.display.setValue("temp", "21"); fb.main.display.setValue("co2", "6\\\\c0000ff12");`;
    const styles = (white: Record<string, unknown>, kinds: Record<string, unknown> = {}) => ({ ...STYLES, white, ...kinds });
    const items = { co2: { style: 'white' }, blind: { style: 'white' }, scenes: { style: 'white' } };
    // A style of the table.
    const wanted = await start(config(items, styles({ color: '#ffffff', 'text-align': 'right', title: { color: '#ffd700', 'text-align': 'center' } })), run, { fonts: true });
    const { emu, rec } = await start(config(items, styles({ color: '#ffffff', title: { color: '#ffd700' } })), run, { fonts: true });
    expect(differ(emu.screenshot('main').data, wanted.emu.screenshot('main').data)).not.toBe('same');
    expect(message(emu, 'fb.style.set("white", "text-align", "right"); fb.style.set("white", "title.text-align", "center")')).toBe('ok undefined');
    expect(differ(emu.screenshot('main').data, wanted.emu.screenshot('main').data)).toBe('same');
    expect(differ(emu.screenshot('key4').data, wanted.emu.screenshot('key4').data, 160)).toBe('same');
    // In "unit" it is taken and ignored, as in config.json.
    expect(message(emu, 'fb.style.set("parts", "unit.text-align", "right")')).toBe('ok undefined');
    expect(differ(emu.screenshot('main').data, wanted.emu.screenshot('main').data)).toBe('same');

    // The kinds.
    const kinds = {
      button: { 'text-align': 'left' }, displayitem: { 'text-align': 'center', title: { 'text-align': 'right' } }, list: { 'text-align': 'right' },
    };
    const wantedKinds = await start(config({}, styles({ color: '#ffffff' }, kinds)), run, { fonts: true });
    const plain = await start(config({}, styles({ color: '#ffffff' })), run, { fonts: true });
    expect(message(plain.emu, `fb.style.set("button", "text-align", "left"); fb.style.set("displayitem", "text-align", "center");
      fb.style.set("displayitem", "title.text-align", "right"); fb.style.set("list", "text-align", "right")`)).toBe('ok undefined');
    for (const display of ['main', 'key3', 'key4']) {
      expect(differ(plain.emu.screenshot(display).data, wantedKinds.emu.screenshot(display).data, display === 'main' ? 320 : 160), display).toBe('same');
    }
    expect(rec.errors).toEqual([]);
    expect(plain.rec.errors).toEqual([]);
  });

  it('a restart takes the added properties away: the same pixels and the same LVGL memory as before, every time', async () => {
    const cfg = config({
      blind: { style: 'huge' }, light: { style: 'muted' }, temp: { style: 'size32 muted' }, co2: { style: 'huge' }, scenes: { style: 'small picked' },
    });
    const { emu, clock, rec } = await start(cfg, 'fb.main.display.setValue("co2", "6\\\\c0000ff12");', { fonts: true });
    const settle = () => { clock.advance(100); for (let i = 0; i < 3; i++) emu.render(); };
    settle();
    const idle = {
      main: emu.screenshot('main').data.slice(), key3: emu.screenshot('key3').data.slice(), key4: emu.screenshot('key4').data.slice(),
      look: look(emu), free: emu.stats().lvglFree, blocks: emu.lvglUsedBlocks(), kinds: JSON.stringify(emu.state.styles),
      carried: [emu.state.key('blind')!.display!.carried, emu.state.item('co2')!.box!.carried, emu.state.list('scenes')!.roller!.carried],
    };
    for (const round of [1, 2, 3]) {
      expect(message(emu, `
        fb.style.set("huge", "color", "#ff0000"); fb.style.set("huge", "opacity", 0.5); fb.style.set("small", "letter-spacing", 3);
        fb.style.set("muted", "text-align", "right"); fb.style.set("muted", "text-decoration", "underline");
        fb.style.set("muted", "icon.color", "#00ff00"); fb.style.set("picked", "selected.opacity", 0.5);
        fb.style.set("button", "letter-spacing", 2); fb.style.set("button", "title.text-decoration", "underline");
        fb.style.set("displayitem", "opacity", 0.8); fb.style.set("displayitem", "unit.letter-spacing", 2);
        fb.style.set("displayitem", "title.text-align", "right"); fb.style.set("displayitem", "icon.color", "#ff00ff");
        fb.style.set("list", "text-decoration", "underline"); fb.style.set("list", "selected.letter-spacing", 1);
        fb.style.set("list", "background-color", "#000040");
      `), `round ${round}`).toBe('ok undefined');
      settle();
      expect(look(emu).blind.label).toBe('#ff0000 montserrat 48');
      expect(differ(emu.screenshot('main').data, idle.main)).not.toBe('same');
      expect(differ(emu.screenshot('key3').data, idle.key3, 160)).not.toBe('same');
      expect(differ(emu.screenshot('key4').data, idle.key4, 160)).not.toBe('same');
      expect(emu.stats().lvglFree).toBeLessThan(idle.free);
      // "huge" and "small" held nothing for LVGL: their property blocks are new in the pool.
      expect(emu.lvglUsedBlocks()).toBeGreaterThan(idle.blocks);
      expect(JSON.stringify(emu.state.styles)).not.toBe(idle.kinds);

      expect(emu.restart()).toEqual({ ok: true });
      settle();
      expect(look(emu), `round ${round}`).toEqual(idle.look);
      expect(differ(emu.screenshot('main').data, idle.main), `round ${round}`).toBe('same');
      expect(differ(emu.screenshot('key3').data, idle.key3, 160), `round ${round}`).toBe('same');
      expect(differ(emu.screenshot('key4').data, idle.key4, 160), `round ${round}`).toBe('same');
      // The kinds hold their config values again: the same record, not only another one.
      expect(JSON.stringify(emu.state.styles), `round ${round}`).toBe(idle.kinds);
      expect([emu.state.key('blind')!.display!.carried, emu.state.item('co2')!.box!.carried, emu.state.list('scenes')!.roller!.carried]).toEqual(idle.carried);
      // Nothing is left in the LVGL pool: as many blocks in use as before the calls (the
      // property blocks of the styles that hold nothing again are gone, and so are the span
      // labels' styles). The free BYTES are not compared exactly: every free block costs a
      // header, so the same blocks in another order give another figure, and a restart of a
      // script that sets no style moves it by some tens of bytes too. It stays near.
      expect(emu.lvglUsedBlocks(), `round ${round}`).toBe(idle.blocks);
      expect(Math.abs(emu.stats().lvglFree - idle.free), `round ${round}`).toBeLessThan(256);
    }
    // After a restart the added properties are gone for a new user too.
    emu.eval('fb.main.display.setStyle("temp", "huge")');
    expect(look(emu).temp!.value).toBe('#ffffff montserrat 48');
    expect(rec.errors).toEqual([]);
  });
});

describe('the selected row of a list', () => {
  const SEL = {
    under: { 'text-decoration': 'underline', opacity: 0.5, 'letter-spacing': 4 },
    own: { selected: { 'text-decoration': 'none', opacity: 1, 'letter-spacing': 0 } },
    own_under: { selected: { 'text-decoration': 'line-through' } },
    sel_right: { selected: { 'text-align': 'right' } },
  };
  const row = (emu: Emulator) => {
    emu.render();
    const r = emu.state.list('scenes')!.roller!;
    return { text: r.text, selectedText: r.selectedText, rule: r.selectedRule };
  };
  /** The lit pixels of the selected row's band that are not the bar's colour: its text. */
  const selectedRows = (emu: Emulator) => {
    emu.render();
    const r = emu.state.list('scenes')!.roller!;
    const shot = emu.screenshot('main');
    const rowH = r.height / r.rows;
    const top = Math.round(r.y + rowH * Math.floor(r.rows / 2));
    return shot.data.slice(top * shot.width * 4, Math.round(top + rowH) * shot.width * 4);
  };

  it('has the rows\' opacity, letter-spacing and text-decoration unless "selected" sets them', async () => {
    const plain = await start(config({}, { ...STYLES, ...SEL }), '', { fonts: true });
    expect(row(plain.emu)).toEqual({
      text: { opa: 255, letterSpace: 0, decor: 0 }, selectedText: { opa: 255, letterSpace: 0, decor: 0 }, rule: { opa: 255, letterSpace: 0, decor: 0 },
    });
    // The rows' three properties show on the selected row.
    const under = await start(config({ scenes: { style: 'under' } }, { ...STYLES, ...SEL }), '', { fonts: true });
    const rows = { opa: 128, letterSpace: 4, decor: 1 };
    expect(row(under.emu)).toEqual({ text: rows, selectedText: rows, rule: rows });
    expect(differ(selectedRows(under.emu), selectedRows(plain.emu))).not.toBe('same');
    // The colour and the background do not come from the rows.
    expect(look(under.emu).list).toMatchObject({ selectedColor: '#000000', selectedBg: '#ffd700' });

    // "selected" sets them: its own, property by property.
    const own = await start(config({ scenes: { style: 'under own' } }, { ...STYLES, ...SEL }), '', { fonts: true });
    const none = { opa: 255, letterSpace: 0, decor: 0 };
    expect(row(own.emu)).toEqual({ text: rows, selectedText: none, rule: none });
    expect(differ(selectedRows(own.emu), selectedRows(plain.emu))).toBe('same');
    const one = await start(config({ scenes: { style: 'under own_under' } }, { ...STYLES, ...SEL }), '', { fonts: true });
    expect(row(one.emu)).toEqual({ text: rows, selectedText: { ...rows, decor: 2 }, rule: { ...rows, decor: 2 } });

    // The same from "list", and from a script.
    const kind = await start(config({}, { ...STYLES, ...SEL, list: { 'text-decoration': 'underline', selected: { opacity: 0.5 } } }), '', { fonts: true });
    expect(row(kind.emu)).toEqual({
      text: { opa: 255, letterSpace: 0, decor: 1 }, selectedText: { opa: 128, letterSpace: 0, decor: 1 }, rule: { opa: 128, letterSpace: 0, decor: 1 },
    });
    expect(message(kind.emu, 'fb.style.set("list", "selected.text-decoration", "none"); fb.style.set("list", "letter-spacing", 3)')).toBe('ok undefined');
    expect(row(kind.emu)).toEqual({
      text: { opa: 255, letterSpace: 3, decor: 1 }, selectedText: { opa: 128, letterSpace: 3, decor: 0 }, rule: { opa: 128, letterSpace: 3, decor: 0 },
    });
  });

  it('is placed as the rows are: "text-align" in "selected" is ignored', async () => {
    const left = await start(config({}, { ...STYLES, ...SEL }), '', { fonts: true });
    const selRight = await start(config({ scenes: { style: 'sel_right' } }, { ...STYLES, ...SEL }), '', { fonts: true });
    expect(differ(selRight.emu.screenshot('main').data, left.emu.screenshot('main').data)).toBe('same');
    const right = await start(config({ scenes: { style: 'right' } }, { ...STYLES, ...SEL }), '', { fonts: true });
    expect(differ(selectedRows(right.emu), selectedRows(left.emu))).not.toBe('same');
  });
});

describe('what the config loader leaves out', () => {
  it('a button without "position" is not there at all: no key, no name, no topics', async () => {
    const cfg = {
      ...config(),
      buttons: [
        { name: 'ghost', label: 'Ghost', topics: [{ brokerid: 'local', topic: 'ghost/label', eventtype: 11 }] },
        { name: 'ghost2', label: 'Ghost 2', id: 3, buttonuid: 'x' },
        ...BASIC_CONFIG.buttons,
      ],
    };
    const { emu, rec } = await start(cfg);
    emu.render();
    expect(emu.state.keys.map(k => k.position)).toEqual([1, 2, 3, 4]);
    expect(emu.state.key('ghost')).toBeUndefined();
    expect(emu.state.key('ghost2')).toBeUndefined();
    expect(message(emu, 'fb.button("ghost")')).toBe('ReferenceError: unknown button "ghost"');
    expect(message(emu, 'fb.button("ghost2")')).toBe('ReferenceError: unknown button "ghost2"');
    // The buttons that have a position are not disturbed by it.
    expect(message(emu, 'typeof fb.button("light").display.setStyle')).toBe('ok function');
    expect(rec.subscribes.filter(t => /ghost/.test(t))).toEqual([]);
  });
});

describe('display.get and display.getAll', () => {
  it('build their result while the engine collects garbage: every field is right, every time', async () => {
    // The engine's collector moves objects; the object under construction is held in a GC ref.
    const { emu, rec } = await start(config({ co2: { style: 'big alarm' } }));
    const result = emu.eval(`
      var bad = 0, i, o, junk = [], all;
      for (i = 0; i < 400; i++) {
        junk = [String(i) + "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", { k: i }];   // garbage, so that a collection comes soon
        o = fb.main.display.get("co2");
        if (!o || o.id !== 8 || o.x !== 10 || o.y !== 60 || o.width !== 40 || o.boxType !== 0 || o.style !== "big alarm" ||
            o.alignment !== 0 || o.label !== "CO2" || o.unit !== "ppm" || o.round !== 0 || o.page !== 1) bad++;
        all = fb.main.display.getAll();
        if (all.length !== 3 || all[0] !== 7 || all[2] !== 8) bad++;
      }
      bad;
    `);
    expect(result).toEqual({ ok: true, value: '0' });
    // An id that does not exist: null, not an exception.
    expect(message(emu, 'fb.main.display.get(9999)')).toBe('ok null');
    expect(message(emu, 'fb.main.display.get("nosuch")')).toBe('ReferenceError: unknown display item "nosuch"');
    expect(rec.errors.filter(e => !/nosuch/.test(e.message))).toEqual([]);
  });
});

describe('text-align', () => {
  /** Two devices that differ in one thing draw the same pixels. */
  async function same(a: { extra: Extra; script: string }, b: { extra: Extra; script: string }, display = 'main') {
    const one = await start(config(a.extra), a.script, { fonts: true });
    const two = await start(config(b.extra), b.script, { fonts: true });
    expect(one.rec.errors.concat(two.rec.errors)).toEqual([]);
    return { a: one.emu.screenshot(display).data, b: two.emu.screenshot(display).data };
  }
  const value = (markup: string) => `fb.main.display.setValue("co2", "${markup}612");`;

  it('places a value in its item as \\ac and \\ar at the start of the text do', async () => {
    const none = (await same({ extra: {}, script: value('') }, { extra: {}, script: value('') })).a;
    for (const [style, markup] of [['center', '\\\\ac'], ['right', '\\\\ar']] as const) {
      const shots = await same({ extra: { co2: { style } }, script: value('') }, { extra: {}, script: value(markup) });
      expect(differ(shots.a, shots.b), style).toBe('same');
      // And it is not where a text without alignment is.
      expect(differ(shots.a, none), style).not.toBe('same');
    }
  });

  it('a \\a in the text wins over the style', async () => {
    const shots = await same({ extra: { co2: { style: 'center' } }, script: value('\\\\ar') }, { extra: {}, script: value('\\\\ar') });
    expect(differ(shots.a, shots.b)).toBe('same');
    const left = await same({ extra: { co2: { style: 'right' } }, script: value('\\\\al') }, { extra: {}, script: value('\\\\al') });
    expect(differ(left.a, left.b)).toBe('same');
  });

  it('a value in several formats is placed as a whole', async () => {
    const spans = '6\\\\cff00001\\\\c00ff002';
    const shots = await same(
      { extra: { co2: { style: 'right' } }, script: `fb.main.display.setValue("co2", "${spans}");` },
      { extra: {}, script: `fb.main.display.setValue("co2", "\\\\ar${spans}");` });
    expect(differ(shots.a, shots.b)).toBe('same');
    const plain = await same({ extra: {}, script: `fb.main.display.setValue("co2", "${spans}");` }, { extra: {}, script: '' });
    expect(differ(shots.a, plain.a)).not.toBe('same');
  });

  it('"title" places the caption; in "unit" it is ignored', async () => {
    const caption = await same({ extra: { co2: { style: 'center_title' } }, script: '' },
      { extra: {}, script: 'fb.main.display.setLabel("co2", "\\\\acCO2");' });
    expect(differ(caption.a, caption.b)).toBe('same');
    const unit = await same({ extra: { co2: { style: 'unit_right' } }, script: '' }, { extra: {}, script: '' });
    expect(differ(unit.a, unit.b)).toBe('same');
    expect(differ(caption.a, unit.a)).not.toBe('same');
  });

  it('a button label: centred without a style, placed by the style as by the markup', async () => {
    const label = (markup: string) => `fb.button("blind").display.setLabel("${markup}Up");`;
    const centred = await same({ extra: { blind: { style: 'center' } }, script: label('') }, { extra: {}, script: label('') }, 'key4');
    expect(differ(centred.a, centred.b, 160)).toBe('same');
    const right = await same({ extra: { blind: { style: 'right' } }, script: label('') }, { extra: {}, script: label('\\\\ar') }, 'key4');
    expect(differ(right.a, right.b, 160)).toBe('same');
    expect(differ(right.a, centred.a, 160)).not.toBe('same');
    // The markup wins over the style here too.
    const back = await same({ extra: { blind: { style: 'right' } }, script: label('\\\\ac') }, { extra: {}, script: label('') }, 'key4');
    expect(differ(back.a, back.b, 160)).toBe('same');
  });
});

describe('line-height on a button and on a display item', () => {
  /** The first and the last row of a display that has a lit pixel. */
  const extent = (shot: Screenshot) => {
    let top = -1, bottom = -1;
    for (let i = 0; i < shot.data.length; i += 4) {
      if (shot.data[i]! + shot.data[i + 1]! + shot.data[i + 2]! === 0) continue;
      const y = Math.floor(i / 4 / shot.width);
      if (top < 0) top = y;
      bottom = y;
    }
    return { top, bottom, height: bottom - top };
  };

  it('a button label of two lines: the lines are further apart; a display item is not changed by it', async () => {
    // "roomy" is "small" (20 px) with a line-height of 1.5.
    const two = 'fb.button("blind").display.setLabel("Blind Blind Blind Blind");';
    const plain = await start(config({ blind: { style: 'small' }, co2: { style: 'small' } }), two, { fonts: true });
    const roomy = await start(config({ blind: { style: 'roomy' }, co2: { style: 'roomy' } }), two, { fonts: true });
    const tight = await start(config({ blind: { style: 'small tight' }, co2: { style: 'small tight' } }), two, { fonts: true });
    const a = extent(plain.emu.screenshot('key4'));
    const b = extent(roomy.emu.screenshot('key4'));
    const c = extent(tight.emu.screenshot('key4'));
    // Two lines of a 20 px font (a line box of 24 px): half a line more between them.
    expect(a.height).toBeGreaterThan(30);
    expect(b.height - a.height).toBeGreaterThanOrEqual(10);
    expect(b.height - a.height).toBeLessThanOrEqual(14);
    // The label stays centred: it grows up and down.
    expect(b.top).toBeLessThan(a.top);
    expect(b.bottom).toBeGreaterThan(a.bottom);
    // "line-height": 1 is the font's own line: LVGL's default gap is gone, or there was none.
    expect(c.height).toBeLessThanOrEqual(a.height);
    // The same fonts in words: only the geometry differs.
    expect(look(roomy.emu).blind).toEqual(look(plain.emu).blind);
    // A display item: accepted, and no pixel moves.
    expect(differ(roomy.emu.screenshot('main').data, plain.emu.screenshot('main').data)).toBe('same');
    expect(differ(tight.emu.screenshot('main').data, plain.emu.screenshot('main').data)).toBe('same');
    expect(look(roomy.emu).co2).toEqual(look(plain.emu).co2);
    expect([roomy.emu.state.item('co2')!.box!.captionPad, roomy.emu.state.item('co2')!.box!.boxPad])
      .toEqual([plain.emu.state.item('co2')!.box!.captionPad, plain.emu.state.item('co2')!.box!.boxPad]);
    expect(roomy.rec.errors.concat(plain.rec.errors)).toEqual([]);
  });

  it('a label of one line has no line to space: the same pixels', async () => {
    const plain = await start(config({ blind: { style: 'small' } }), '', { fonts: true });
    const roomy = await start(config({ blind: { style: 'roomy' } }), '', { fonts: true });
    expect(differ(roomy.emu.screenshot('key4').data, plain.emu.screenshot('key4').data, 160)).toBe('same');
  });
});

describe('a style named like a member of Object.prototype', () => {
  it('"constructor", "__proto__" and "toString" are styles like any other, in config.json and in a script', async () => {
    // JSON.parse: "__proto__" as an own key, as a config.json has it.
    const odd = JSON.parse('{"constructor": {"color": "#ff0000"}, "__proto__": {"font-size": 20}, "toString": {"title": {"color": "#00ff00"}}}');
    const cfg = config({ blind: { style: 'constructor __proto__' }, light: { style: 'toString' }, co2: { style: '__proto__' } }, odd);
    expect(JSON.stringify(cfg)).toContain('"__proto__":{"font-size":20}');
    const { emu, rec } = await start(cfg);
    expect(emu.state.styleNames).toEqual(['size32', 'constructor', '__proto__', 'toString']);
    expect(look(emu)).toMatchObject({
      blind: { label: '#ff0000 montserrat 20' }, light: { label: PLAIN.light.label, top: '#00ff00 montserrat_semibold 18' },
      co2: { value: '#ffffff montserrat 20' },
    });
    expect(lists(emu)).toMatchObject({ blind: 'constructor __proto__', light: 'toString', co2: '__proto__' });
    expect(message(emu, 'fb.main.display.setStyle("temp", "__proto__ constructor"); fb.button("light").display.addStyle("constructor")')).toBe('ok undefined');
    expect(message(emu, 'fb.style.set("constructor", "color", "#0000ff"); fb.style.set("__proto__", "color", "#00ffff")')).toBe('ok undefined');
    // The last name wins: "__proto__" has a colour now, added by the script.
    expect(look(emu)).toMatchObject({ blind: { label: '#00ffff montserrat 20' }, temp: { value: '#0000ff montserrat 20' }, co2: { value: '#00ffff montserrat 20' } });
    expect(message(emu, 'fb.main.display.get("temp").style')).toBe('ok __proto__ constructor');
    // A name of that sort that is no style is unknown, as any other.
    expect(message(emu, 'fb.main.display.setStyle("temp", "hasOwnProperty")')).toBe('ReferenceError: unknown style "hasOwnProperty"');
    expect(message(emu, 'fb.style.set("valueOf", "color", "#ffffff")')).toBe('ReferenceError: unknown style "valueOf"');
    expect(rec.errors.filter(e => !/unknown style/.test(e.message))).toEqual([]);
  });
});

describe('a list: line-height and background-color', () => {
  const rows = 3;
  const geometry = (emu: Emulator) => {
    emu.render();
    const r = emu.state.list('scenes')!.roller!;
    // LVGL's roller: the visible rows, each the font's line and the line space.
    expect(r.height).toBe(r.rows * (r.font.lineHeight + r.lineSpace));
    expect(r.rows).toBe(rows);
    return { height: r.height, line: r.font.lineHeight, lineSpace: r.lineSpace };
  };

  it('line-height is a multiple of the font\'s line: 1.25 built in, as a list always was', async () => {
    // The built-in list: 32 px, a quarter of the 39 px line between the rows.
    const plain = await start(config({ scenes: { rows } }), '', { fonts: true });
    expect(geometry(plain.emu)).toEqual({ height: 144, line: 39, lineSpace: 9 });
    expect(plain.emu.state.styles.list).toMatchObject({ lineHeight: 125 });

    // roomy: 20 px rows (a 24 px line) at 1.5: half a line between them.
    const { emu } = await start(config({ scenes: { rows, style: 'roomy' } }), '', { fonts: true });
    expect(geometry(emu)).toEqual({ height: 108, line: 24, lineSpace: 12 });
    // tight: 1 = no space; the font is the list's.
    emu.eval('fb.main.list.setStyle("scenes", "tight")');
    expect(geometry(emu)).toEqual({ height: 117, line: 39, lineSpace: 0 });
    // Another font without a line-height: a quarter of that font's line.
    emu.eval('fb.main.list.setStyle("scenes", "big")');
    expect(geometry(emu)).toEqual({ height: 216, line: 58, lineSpace: 14 });
    // The last line-height of the list wins, with the font the list ends up with.
    emu.eval('fb.main.list.setStyle("scenes", "tight roomy big")');
    expect(geometry(emu)).toEqual({ height: 261, line: 58, lineSpace: 29 });
    // "": the list of config.json again (roomy).
    emu.eval('fb.main.list.setStyle("scenes", "")');
    expect(geometry(emu)).toEqual({ height: 108, line: 24, lineSpace: 12 });
    // A list without a configured style comes back to the built-in geometry.
    plain.emu.eval('fb.main.list.setStyle("scenes", "roomy")');
    expect(geometry(plain.emu)).toEqual({ height: 108, line: 24, lineSpace: 12 });
    plain.emu.eval('fb.main.list.setStyle("scenes", "")');
    expect(geometry(plain.emu)).toEqual({ height: 144, line: 39, lineSpace: 9 });
  });

  it('"list" in config sets it for every list', async () => {
    const { emu } = await start(config({ scenes: { rows } }, { ...STYLES, list: { 'line-height': 2, 'font-size': 20 } }), '', { fonts: true });
    expect(geometry(emu)).toEqual({ height: 144, line: 24, lineSpace: 24 });
    expect(emu.state.styles.list).toMatchObject({ lineHeight: 200, px: 20 });
  });

  it('background-color: the rows\' behind the whole roller, "selected" behind the selected row, each with its opacity', async () => {
    const { emu } = await start(config({ scenes: { style: 'panel' } }));
    expect(look(emu).list).toEqual({ ...PLAIN.list, bg: '#0000ff', selectedBg: '#00ff00' });
    const shot = emu.screenshot('main');
    // Half opaque on the black display: half the colour; the selected row is drawn over the rows' background.
    expect(pixelsOf(shot, 0, 0, 255)).toBe(0);
    expect(pixelsOf(shot, 0, 0, 128)).toBeGreaterThan(3000);
    expect(pixelsOf(shot, 0, 255, 0)).toBe(0);
    expect(pixelsOf(shot, 0, 128, 64)).toBeGreaterThan(1000);
    // "#rrggbb" is opaque.
    const opaque = await start(config({ scenes: { style: 'picked' } }));
    expect(pixelsOf(opaque.emu.screenshot('main'), 0, 200, 82)).toBeGreaterThan(1000);
    // With another style the roller has no background of its own again.
    emu.eval('fb.main.list.setStyle("scenes", "alarm")');
    expect(look(emu).list).toEqual({ ...PLAIN.list, color: '#ff0000' });
    expect(pixelsOf(emu.screenshot('main'), 0, 0, 128)).toBe(0);
    // And from a script, on a list that had none.
    opaque.emu.eval('fb.main.list.setStyle("scenes", "panel")');
    expect(look(opaque.emu).list).toEqual({ ...PLAIN.list, bg: '#0000ff', selectedBg: '#00ff00' });
    expect(pixelsOf(opaque.emu.screenshot('main'), 0, 0, 128)).toBeGreaterThan(3000);
  });

  it('background-color has no effect on a button or a display item', async () => {
    const plain = await start(config());
    const { emu, rec } = await start(config({ light: { style: 'panel' }, blind: { style: 'panel' }, temp: { style: 'size32 panel' }, co2: { style: 'panel' } }));
    expect(rec.errors).toEqual([]);
    expect(lists(emu)).toMatchObject({ light: 'panel', temp: 'size32 panel' });
    for (const display of ['main', 'key3', 'key4']) {
      expect(emu.screenshot(display).data, display).toEqual(plain.emu.screenshot(display).data);
    }
    // From a script too.
    plain.emu.eval('fb.button("light").display.setStyle("panel"); fb.main.display.addStyle("co2", "panel")');
    for (const display of ['main', 'key3']) {
      expect(plain.emu.screenshot(display).data, display).toEqual(emu.screenshot(display).data);
    }
  });
});

describe('a style stays where it was put', () => {
  it('switching from one style to another takes the first away', async () => {
    const { emu } = await start(config({ co2: { style: 'alarm' }, blind: { style: 'alarm' }, scenes: { style: 'alarm' } }));
    expect(look(emu)).toMatchObject({
      co2: { value: '#ff0000 montserrat 14' }, blind: { label: '#ff0000 montserrat 32' }, list: { color: '#ff0000', font: 'montserrat 32' },
    });
    // alarm sets colours only, big a font and a size only: nothing of alarm may stay.
    emu.eval('fb.main.display.setStyle("co2", "big"); fb.button("blind").display.setStyle("big"); fb.main.list.setStyle("scenes", "big")');
    expect(look(emu)).toMatchObject({
      co2: { caption: PLAIN.co2.caption, value: '#ffffff montserrat_semibold 48', unit: '#999999 montserrat_semibold 48' },
      blind: { label: '#ffffff montserrat_semibold 48' },
      list: { color: '#808080', font: 'montserrat_semibold 48' },
    });
    expect(pixelsOf(emu.screenshot('main'), 255, 0, 0)).toBe(0);
    expect(pixelsOf(emu.screenshot('key4'), 255, 0, 0)).toBe(0);
    // And back: nothing of big stays.
    emu.eval('fb.main.display.setStyle("co2", "alarm"); fb.button("blind").display.setStyle("alarm"); fb.main.list.setStyle("scenes", "alarm")');
    expect(look(emu)).toMatchObject({
      co2: { value: '#ff0000 montserrat 14', unit: '#999999 montserrat 14' }, blind: { label: '#ff0000 montserrat 32' },
      list: { color: '#ff0000', font: 'montserrat 32' },
    });
  });

  it('a page change away and back keeps it: the boxes and the roller are built again', async () => {
    const { emu } = await start(config({ temp: { style: 'size32 alarm' }, scenes: { page: 1, style: 'small picked' } }),
      'fb.main.display.setStyle("co2", "big alarm"); fb.main.display.addStyle("temp", "center");');
    const first = look(emu);
    expect(first).toMatchObject({
      temp: { caption: '#ff8080 montserrat_semibold 14', value: '#ff0000 montserrat 32' },
      co2: { caption: '#ff8080 montserrat_semibold 14', value: '#ff0000 montserrat_semibold 48' },
      list: { font: 'montserrat 20', selectedBg: '#00c853' },
    });
    const shot = emu.screenshot('main').data.slice();

    emu.eval('fb.main.display.showPage(2)');
    emu.render();
    expect(emu.state.item('temp')!.box).toBeNull();
    expect(emu.state.item('co2')!.box).toBeNull();
    expect(emu.state.list('scenes')!.roller).toBeNull();

    emu.eval('fb.main.display.showPage(1)');
    expect(look(emu)).toEqual(first);
    expect(emu.screenshot('main').data).toEqual(shot);
  });

  it('a new text keeps it: the spans of a value, a caption, a unit and a button label are made again', async () => {
    const { emu } = await start(config({ temp: { style: 'size32 parts' }, blind: { style: 'loud' } }));
    emu.eval(`fb.main.display.setValue("temp", "1\\\\c0000ff2\\\\cffff003");
      fb.main.display.setLabel("temp", "In\\\\cff0000side"); fb.main.display.setUnit("temp", "K\\\\cffffffx");
      fb.button("blind").display.setLabel("Up\\\\c0000ffDown")`);
    expect(look(emu)).toMatchObject({
      // The spans without a colour of their own have the style's; every span has the style's size.
      temp: {
        value: '#00ff00 montserrat 32 | #0000ff montserrat 32 | #ffff00 montserrat 32',
        caption: '#00ffff montserrat_semibold 20 | #ff0000 montserrat_semibold 20',
        unit: '#ff00ff montserrat 10 | #ffffff montserrat 10',
      },
      blind: { label: '#ff0000 montserrat 20 | #0000ff montserrat 20' },
    });
    // Again, other spans; then one format (the button's label draws that itself).
    emu.eval(`fb.main.display.setValue("temp", "\\\\cff00009\\\\cffffff8"); fb.main.display.setValue("temp", "7\\\\c0000ff6");
      fb.button("blind").display.setLabel("Stop")`);
    expect(look(emu)).toMatchObject({
      temp: { value: '#00ff00 montserrat 32 | #0000ff montserrat 32' }, blind: { label: '#ff0000 montserrat 20' },
    });
    emu.eval('fb.button("blind").display.setLabel("A\\\\c00ff00B\\\\cffffffC")');
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 20 | #00ff00 montserrat 20 | #ffffff montserrat 20');
  });

  it('a box that is built again keeps it: a new position, an icon', async () => {
    const { emu } = await start(config({ co2: { style: 'big alarm' } }), '', { icons: true });
    const before = look(emu).co2;
    emu.eval('fb.main.display.setPosition("co2", 50, 60); fb.main.display.setIcon("co2", "sun")');
    expect(look(emu).co2).toEqual(before);
    expect(emu.state.item('co2')).toMatchObject({ x: 50, y: 60, style: 'big alarm', box: { icon: { name: 'sun', tint: '#ff0000' } } });
  });
});

describe('the padding of a display item follows the size its styles give', () => {
  it('a size of the item\'s own styles pads; the kind\'s 14 px does not', async () => {
    const pads = (emu: Emulator, name: string) => {
      emu.render();
      const box = emu.state.item(name)!.box!;
      return [box.captionPad, box.boxPad];
    };
    const { emu } = await start(config());
    // co2 has no style: the built-in 14 px gives no padding of its own (src/ui/box_logic.h).
    expect(pads(emu, 'co2')).toEqual([null, null]);
    // big = 48 px: step 4 (10 / 8 for a box with an underline).
    emu.eval('fb.main.display.setStyle("co2", "big")');
    expect(pads(emu, 'co2')).toEqual([10, 8]);
    // small = 20 px: the nearest step is 24 px (4 / 3).
    emu.eval('fb.main.display.setStyle("co2", "small")');
    expect(pads(emu, 'co2')).toEqual([4, 3]);
    // 14 px that a style of the item sets is step 1.
    emu.eval('fb.main.display.setStyle("co2", "tiny")');
    expect(pads(emu, 'co2')).toEqual([2, 1]);
    // A style without a size, and no style: none again.
    emu.eval('fb.main.display.setStyle("co2", "alarm")');
    expect(pads(emu, 'co2')).toEqual([null, null]);
    emu.eval('fb.main.display.setStyle("co2", "big"); fb.main.display.setStyle("co2", "")');
    expect(pads(emu, 'co2')).toEqual([null, null]);
    // temp: size32 from config, step 3.
    expect(pads(emu, 'temp')).toEqual([4, 8]);
  });
});

describe('a script restart', () => {
  it('restores every item\'s configured list and the values fb.style.set changed, the kinds\' included', async () => {
    const cfg = config({ temp: { style: 'size32 alarm' }, light: { style: 'big' }, scenes: { style: 'picked' } });
    const { emu, clock, rec } = await start(cfg);
    const configured = { look: look(emu), lists: lists(emu) };
    const idle = { main: emu.screenshot('main').data.slice(), key3: emu.screenshot('key3').data.slice(), key4: emu.screenshot('key4').data.slice() };
    const kinds = JSON.stringify(emu.state.styles);

    expect(message(emu, `
      fb.style.set("alarm", "color", "#00ff00"); fb.style.set("alarm", "title.color", "#00ffff");
      fb.style.set("picked", "selected.background-color", "#0000ff");
      fb.style.set("button", "color", "#ff00ff"); fb.style.set("button", "title.color", "#ff00ff");
      fb.style.set("displayitem", "color", "#ffff00"); fb.style.set("displayitem", "unit.color", "#ffff00");
      fb.style.set("displayitem", "title.color", "#ffff00");
      fb.style.set("list", "color", "#ffffff"); fb.style.set("list", "selected.color", "#ff0000");
      fb.main.display.setStyle("temp", "muted"); fb.main.display.addStyle("co2", "alarm");
      fb.button("light").display.removeStyle("big"); fb.button("blind").display.addStyle("big");
      fb.main.list.setStyle("scenes", "alarm");
    `)).toBe('ok undefined');
    expect(look(emu)).toEqual({
      light: { label: '#ff00ff montserrat 32', top: '#ff00ff montserrat_semibold 18' },
      blind: { label: '#ff00ff montserrat_semibold 48', top: '' },
      temp: { caption: '#ffff00 montserrat_semibold 14', value: '#808080 montserrat 14', unit: '#ffff00 montserrat 14' },
      co2: { caption: '#00ffff montserrat_semibold 14', value: '#00ff00 montserrat 14', unit: '#ffff00 montserrat 14' },
      list: { ...PLAIN.list, color: '#00ff00', selectedColor: '#ff0000' },
    });
    expect(JSON.stringify(emu.state.styles)).not.toBe(kinds);

    expect(emu.restart()).toEqual({ ok: true });
    clock.advance(100);
    expect(look(emu)).toEqual(configured.look);
    expect(lists(emu)).toEqual(configured.lists);
    // The kinds hold their config values again: equal to what they were, value by value.
    emu.render();
    expect(JSON.stringify(emu.state.styles)).toBe(kinds);
    // The same pixels as before the script touched anything.
    expect(emu.screenshot('main').data).toEqual(idle.main);
    expect(emu.screenshot('key3').data).toEqual(idle.key3);
    expect(emu.screenshot('key4').data).toEqual(idle.key4);
    // The styles have their config values again: an item that takes one now is red, not green.
    emu.eval('fb.main.display.setStyle("co2", "alarm"); fb.main.list.setStyle("scenes", "picked alarm")');
    expect(look(emu)).toMatchObject({ co2: { value: '#ff0000 montserrat 14', caption: '#ff8080 montserrat_semibold 14' }, list: { color: '#ff0000', selectedBg: '#00c853' } });
    // And a second round works as the first (the copy of the config values is made again).
    expect(message(emu, 'fb.style.set("button", "color", "#00ff00"); fb.style.set("alarm", "color", "#0000ff")')).toBe('ok undefined');
    expect(look(emu)).toMatchObject({ blind: { label: '#00ff00 montserrat 32' }, co2: { value: '#0000ff montserrat 14' } });
    expect(emu.restart()).toEqual({ ok: true });
    clock.advance(100);
    expect(look(emu)).toEqual(configured.look);
    expect(emu.screenshot('main').data).toEqual(idle.main);
    expect(rec.errors).toEqual([]);
  });

  it('an item a script made is gone, with its styles', async () => {
    const { emu, clock } = await start(config(), 'fb.main.display.create(50, 60, 40, 0, "big alarm", 0, "New", "u", 0, 1);');
    expect(emu.state.items.map(i => i.style)).toEqual(['size32', '', '', 'big alarm']);
    emu.stop();
    expect(emu.run('var nothing = 0;')).toEqual({ ok: true });
    clock.advance(100);
    expect(emu.state.items.map(i => i.style)).toEqual(['size32', '', '']);
  });
});

describe('a style the LVGL pool has no room for', () => {
  // The firmware adds a style to an object only while 4 KB + 128 B of the LVGL pool stay free.
  const LOW = 4096 + 64;
  /**
   * The firmware walks its widgets again "a little later": after 50 turns of its LVGL loop
   * (PendingRender.cpp), which on a device is a fraction of a second. Here a render is one turn.
   */
  const later = (emu: Emulator) => {
    for (let i = 0; i < 60; i++) emu.render();
  };

  it('a button keeps its look, font and size included, and takes the style when there is room again', async () => {
    const { emu, rec } = await start(config());
    expect(look(emu).blind.label).toBe(PLAIN.blind.label);
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    emu.eval('fb.button("blind").display.setStyle("loud")');
    // Not the style's size with the kind's colour.
    expect(look(emu).blind.label).toBe(PLAIN.blind.label);
    later(emu);
    expect(look(emu).blind.label).toBe(PLAIN.blind.label);
    expect(rec.logs.filter(line => /an item's styles were not added/.test(line))).toHaveLength(1);
    // The list is the one the script gave: only the drawing waits.
    expect(emu.state.key('blind')).toMatchObject({ style: 'loud', configStyle: '' });

    // Room again: the firmware tries again by itself, nothing else has to change.
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 20');
    expect(rec.errors).toEqual([]);
  });

  it('addStyle waits the same way; what the button carried stays until then', async () => {
    const { emu } = await start(config({ blind: { style: 'muted' } }));
    emu.render();
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    emu.eval('fb.button("blind").display.addStyle("loud")');
    expect(look(emu).blind.label).toBe('#808080 montserrat 32');
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 20');
  });

  it('a list keeps its look and takes the styles when there is room again', async () => {
    const { emu } = await start(config());
    emu.render();
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    emu.eval('fb.main.list.setStyle("scenes", "loud bold_row")');
    expect(look(emu).list).toEqual(PLAIN.list);
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu).list).toEqual({ ...PLAIN.list, color: '#ff0000', font: 'montserrat 20', selectedFont: 'montserrat_semibold 20' });
  });

  it('a display item keeps its look and takes the style when there is room again', async () => {
    const { emu } = await start(config());
    // Several formats: drawing the new text gives their labels back to the pool first.
    emu.eval('fb.main.display.setValue("co2", "\\\\cffffff6\\\\cffffff1\\\\cffffff2\\\\cffffff0\\\\cffffff0")');
    emu.render();
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    emu.eval('fb.main.display.setStyle("co2", "loud"); fb.main.display.setValue("co2", "612")');
    emu.render();
    // Drawn or not (the pool is low), never with the style's size and the kind's colour.
    const value = emu.state.item('co2')!.box!.value.runs;
    expect(value.every(r => r.px === 14 && r.color === '#ffffff')).toBe(true);
    expect([emu.state.item('co2')!.box!.captionPad, emu.state.item('co2')!.box!.boxPad]).toEqual([null, null]);
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu).co2).toMatchObject({ value: '#ff0000 montserrat 20', unit: '#999999 montserrat 20' });
    expect([emu.state.item('co2')!.box!.captionPad, emu.state.item('co2')!.box!.boxPad]).toEqual([4, 3]);
  });

  // One check for all the parts of an item: none of them gets the new list alone.
  const HALF = {
    // The value's block holds nothing for LVGL (a size: no room needed), the unit's does.
    size_unit: { 'font-size': 20, unit: { color: '#ff00ff' } },
    // The other way round.
    color_unit: { color: '#ff0000', unit: { 'font-size': 10 }, title: { 'font-size': 20 } },
    // The rows' block needs no room, the selected row's does; and the other way round.
    size_selected: { 'font-size': 20, selected: { color: '#0000ff', 'background-color': '#00c853' } },
    color_selected: { color: '#ff0000', selected: { 'font-family': 'montserrat_semibold' } },
  };
  const carriedBy = (emu: Emulator) => {
    emu.render();
    return {
      co2: emu.state.item('co2')!.box!.carried, list: emu.state.list('scenes')!.roller!.carried,
      blind: emu.state.key('blind')!.display!.carried,
    };
  };

  for (const style of ['size_unit', 'color_unit', 'parts'] as const) {
    it(`a display item with a unit never shows half a look: "${style}"`, async () => {
      const { emu } = await start(config({}, { ...STYLES, ...HALF }));
      emu.render();
      const pads = () => [emu.state.item('co2')!.box!.captionPad, emu.state.item('co2')!.box!.boxPad];
      emu.holdLvglMemory(emu.stats().lvglFree - LOW);
      emu.eval(`fb.main.display.setStyle("co2", "${style}")`);
      // Caption, value and unit all carry what they carried: not one of them the new list.
      expect(carriedBy(emu).co2).toEqual({ caption: '', value: '', unit: '' });
      expect(pads()).toEqual([null, null]);
      // The retries: whenever the three get the list, they get it together. (Drawing the
      // texts again gives their labels back to the pool, so a retry may find room.)
      for (let i = 0; i < 120; i++) {
        const c = carriedBy(emu).co2;
        expect([c.caption, c.unit], `walk ${i}`).toEqual([c.value, c.value]);
      }
      emu.holdLvglMemory(0);
      later(emu);
      expect(carriedBy(emu).co2).toEqual({ caption: style, value: style, unit: style });
      const wanted = await start(config({ co2: { style } }, { ...STYLES, ...HALF }));
      expect(look(emu).co2).toEqual(look(wanted.emu).co2);
      // And on to another list under a low pool: the three keep the first one, or (where
      // the new list needs no more room) take the new one, together.
      emu.holdLvglMemory(emu.stats().lvglFree - LOW);
      emu.eval('fb.main.display.setStyle("co2", "loud")');
      const c = carriedBy(emu).co2;
      expect([c.caption, c.unit]).toEqual([c.value, c.value]);
      emu.holdLvglMemory(0);
      later(emu);
      expect(carriedBy(emu).co2).toEqual({ caption: 'loud', value: 'loud', unit: 'loud' });
    });
  }

  for (const style of ['size_selected', 'color_selected', 'parts'] as const) {
    it(`a list with a "selected" block never shows half a look: "${style}"`, async () => {
      const { emu } = await start(config({}, { ...STYLES, ...HALF }));
      emu.render();
      emu.holdLvglMemory(emu.stats().lvglFree - LOW);
      emu.eval(`fb.main.list.setStyle("scenes", "${style}")`);
      expect(carriedBy(emu).list).toEqual({ rows: '', selected: '' });
      expect(look(emu).list).toEqual(PLAIN.list);
      later(emu);
      expect(look(emu).list).toEqual(PLAIN.list);
      emu.holdLvglMemory(0);
      later(emu);
      expect(carriedBy(emu).list).toEqual({ rows: style, selected: style });
      const wanted = await start(config({ scenes: { style } }, { ...STYLES, ...HALF }));
      expect(look(emu).list).toEqual(look(wanted.emu).list);
      // Back to no style needs no room: taking styles away always works.
      emu.holdLvglMemory(emu.stats().lvglFree - LOW);
      emu.eval('fb.main.list.setStyle("scenes", "")');
      expect(carriedBy(emu).list).toEqual({ rows: '', selected: '' });
      expect(look(emu).list).toEqual(PLAIN.list);
      emu.holdLvglMemory(0);
    });
  }

  it('a button\'s label and top label get a new list together', async () => {
    const { emu } = await start(config({}, { ...STYLES, ...HALF }));
    emu.render();
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    // "color_unit": the label's block needs room, the top label's (a size) does not.
    emu.eval('fb.button("light").display.setStyle("color_unit"); fb.button("blind").display.setStyle("color_unit")');
    expect(carriedBy(emu).blind).toEqual({ label: '', topLabel: '' });
    expect(look(emu).light).toEqual(PLAIN.light);
    emu.holdLvglMemory(0);
    later(emu);
    expect(carriedBy(emu).blind).toEqual({ label: 'color_unit', topLabel: 'color_unit' });
    expect(look(emu).light).toEqual({ label: '#ff0000 montserrat 32', top: '#ffd700 montserrat_semibold 20' });
  });

  it('a list that does not grow needs no reserve: two styles for one, also under a low pool', async () => {
    // The object's style list shrinks, but LVGL takes it apart and builds it again: the
    // firmware checks that the largest free block holds it, and no more than that.
    const { emu, rec } = await start(config({ blind: { style: 'muted spaced' } }));
    emu.render();
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    emu.eval('fb.button("blind").display.setStyle("loud")');
    expect(carriedBy(emu).blind).toEqual({ label: 'loud', topLabel: 'loud' });
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 20');
    // The other way round it grows, and waits.
    emu.eval('fb.button("blind").display.setStyle("muted spaced")');
    expect(carriedBy(emu).blind).toEqual({ label: 'loud', topLabel: 'loud' });
    emu.holdLvglMemory(0);
    later(emu);
    expect(carriedBy(emu).blind).toEqual({ label: 'muted spaced', topLabel: 'muted spaced' });
    expect(rec.errors).toEqual([]);
  });

  it('fb.style.set that adds a property is refused without room for it, and changes nothing', async () => {
    const { emu, rec } = await start(config({ blind: { style: 'muted' }, co2: { style: 'huge' }, scenes: { style: 'muted' } }));
    emu.render();
    const before = { look: look(emu), main: emu.screenshot('main').data.slice(), key: emu.screenshot('key4').data.slice() };
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    const held = emu.stats().lvglFree;
    for (const call of [
      'fb.style.set("muted", "opacity", 0.4)', 'fb.style.set("huge", "color", "#ff0000")',
      'fb.style.set("button", "letter-spacing", 4)', 'fb.style.set("list", "selected.text-decoration", "underline")',
    ]) {
      expect(message(emu, call), call).toBe('InternalError: out of memory');
    }
    rec.errors.length = 0;   // the four that were thrown on purpose
    // A value of a property the style holds needs no room; neither does an icon colour.
    expect(message(emu, 'fb.style.set("muted", "color", "#808080"); fb.style.set("muted", "icon.color", "#00ff00")')).toBe('ok undefined');
    emu.render();
    expect(emu.stats().lvglFree).toBe(held);
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu)).toEqual(before.look);
    expect(differ(emu.screenshot('main').data, before.main)).toBe('same');
    expect(differ(emu.screenshot('key4').data, before.key, 160)).toBe('same');
    // With room the same calls work.
    expect(message(emu, 'fb.style.set("huge", "color", "#ff0000")')).toBe('ok undefined');
    expect(look(emu).co2!.value).toBe('#ff0000 montserrat 48');
    expect(rec.errors).toEqual([]);
  });

  it('a block that got its first property: without room to put it on an item, the item has the look of its kind, whole, and takes its styles later', async () => {
    // "two": a size for the label and one for the top label. Nothing of it is on the labels.
    const two = { two: { 'font-size': 20, title: { 'font-size': 20 } } };
    const { emu, rec } = await start(config({ light: { style: 'two' } }, { ...STYLES, ...two }));
    emu.render();
    const light = () => { emu.render(); return emu.state.key('light')!.display!.carried; };
    expect(light()).toEqual({ label: 'two', topLabel: 'two' });
    expect(look(emu).light).toEqual({ label: '#ffffff montserrat 20', top: '#ffd700 montserrat_semibold 20' });
    // Room for one more property, so each call is accepted; not for the two blocks that the
    // display task then has to put on the button's labels.
    const want = 4096 + 128 + 96;
    let take = emu.stats().lvglFree - want;
    let free = emu.holdLvglMemory(take);
    for (let i = 0; i < 8 && Math.abs(free - want) > 8; i++) {
      take += free - want;
      free = emu.holdLvglMemory(take);
    }
    expect(Math.abs(free - want)).toBeLessThanOrEqual(8);
    expect(message(emu, 'fb.style.set("two", "color", "#ff0000"); fb.style.set("two", "title.color", "#00ff00")')).toBe('ok undefined');
    // Not the label in red with the top label still gold, and not the old list with a block
    // missing: no style of the list at all, and the record says so.
    expect(light()).toEqual({ label: '', topLabel: '' });
    expect(look(emu).light).toEqual(PLAIN.light);
    // The list itself is what it was: only the drawing waits.
    expect(lists(emu).light).toBe('two');
    later(emu);
    expect(light()).toEqual({ label: '', topLabel: '' });
    emu.holdLvglMemory(0);
    later(emu);
    expect(light()).toEqual({ label: 'two', topLabel: 'two' });
    expect(look(emu).light).toEqual({ label: '#ff0000 montserrat 20', top: '#00ff00 montserrat_semibold 20' });
    expect(rec.errors).toEqual([]);
  });

  it('a restart under a low pool still takes the added properties away: taking a style off an item needs no room', async () => {
    const { emu, clock, rec } = await start(config({ co2: { style: 'huge' }, blind: { style: 'huge' }, scenes: { style: 'small' } }));
    emu.render();
    const idle = { look: look(emu), blocks: emu.lvglUsedBlocks() };
    expect(message(emu, 'fb.style.set("huge", "color", "#ff0000"); fb.style.set("small", "color", "#00ff00"); fb.style.set("button", "opacity", 0.5)')).toBe('ok undefined');
    expect(look(emu)).toMatchObject({ blind: { label: '#ff0000 montserrat 48' }, list: { color: '#00ff00' } });
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    const held = emu.lvglUsedBlocks();
    expect(emu.restart()).toEqual({ ok: true });
    clock.advance(100);
    // The colours are gone from the blocks, and the blocks from the objects.
    expect(carriedBy(emu)).toEqual({
      co2: { caption: 'huge', value: 'huge', unit: 'huge' }, blind: { label: 'huge', topLabel: 'huge' }, list: { rows: 'small', selected: 'small' },
    });
    expect(look(emu).blind.label).toBe(idle.look.blind.label);
    expect(look(emu).list).toEqual(idle.look.list);
    // The two property blocks of "huge" and "small" went back to the pool.
    expect(emu.lvglUsedBlocks()).toBeLessThan(held);
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu)).toEqual(idle.look);
    expect(emu.lvglUsedBlocks()).toBe(idle.blocks);
    expect(rec.errors).toEqual([]);
  });

  it('fb.style.set: the styles themselves need no room; a text that has to be drawn again waits for it', async () => {
    const cfg = config({ blind: { style: 'alarm' }, co2: { style: 'alarm' } });
    const change = 'fb.style.set("alarm", "color", "#00ff00"); fb.style.set("button", "title.color", "#00ffff"); fb.style.set("list", "color", "#ffffff")';
    // What it looks like with room.
    const roomy = await start(cfg);
    expect(message(roomy.emu, change)).toBe('ok undefined');
    const wanted = look(roomy.emu);
    expect(wanted).toMatchObject({
      blind: { label: '#00ff00 montserrat 32' }, co2: { value: '#00ff00 montserrat 14' },
      light: { top: '#00ffff montserrat_semibold 18' }, list: { color: '#ffffff' },
    });

    const { emu, rec } = await start(cfg);
    emu.render();
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    expect(message(emu, change)).toBe('ok undefined');
    // The shared styles hold the same properties with other values: LVGL restyles what carries them.
    expect(look(emu)).toMatchObject({ list: { color: '#ffffff' } });
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu)).toEqual(wanted);
    expect(emu.screenshot('main').data).toEqual(roomy.emu.screenshot('main').data);
    expect(emu.screenshot('key4').data).toEqual(roomy.emu.screenshot('key4').data);
    expect(rec.errors).toEqual([]);
  });
});
