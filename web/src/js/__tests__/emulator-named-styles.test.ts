/**
 * Named styles (config.json "style.styles", docs/STYLE.md) in the emulator:
 * the firmware's own config loader, cascade, script calls and restore, as the
 * virtual device shows them. The rules themselves are the firmware's
 * (src/ui/style_logic.c, tested natively); these tests are what a config and
 * a script get to see.
 */
import { describe, expect, it } from 'vitest';
import { configToModel } from '../config-model';
import type { Emulator, Screenshot } from '../emulator';
import { listSelectedFont, styleFont } from '../font-config';
import { StyleElement } from '../style-config';
import { BASIC_CONFIG, makeEmulator } from './emulator-helpers';
import { repoMedia } from './emulator-pixels';

const STYLES = {
  alarm: { text_color: '#ff0000' },
  muted: { text_color: '#808080' },
  big: { text_font: 'montserrat_semibold', text_size: 48 },
  picked: { bg_color: '#00c853', text_color: '#000000' },
  roomy: { text_size: 20, text_line_space: 12 },
  plain: {},
  // One of text_font / text_size: the part's font is a local one, built on the layer below.
  semi: { text_font: 'montserrat_semibold' },
  huge: { text_size: 48 },
  // A size and a colour: what a part shows when only half of a style arrives.
  loud: { text_color: '#ff0000', text_size: 20 },
};

/** BASIC_CONFIG's items, each with what `extra` adds to it (by name). */
function config(extra: Record<string, Record<string, unknown>> = {}, styles: Record<string, unknown> | null = STYLES) {
  const add = <T extends { name?: string }>(entries: readonly T[]) => entries.map(e => ({ ...e, ...(e.name ? extra[e.name] : {}) }));
  return {
    ...BASIC_CONFIG,
    ...(styles ? { style: { styles } } : {}),
    buttons: add(BASIC_CONFIG.buttons),
    displayitems: [
      ...add(BASIC_CONFIG.displayitems),
      { name: 'co2', displayitemid: 8, x: 10, y: 60, width: 40, page: 1, label: 'CO2', unit: 'ppm', ...extra.co2 },
    ],
    lists: add(BASIC_CONFIG.lists),
  };
}

/** What the device draws, per part: the colour and font of the text's runs. */
function look(emu: Emulator) {
  emu.render();
  const s = emu.state;
  const text = (t: { runs: { color: string; font: string; px: number }[] } | undefined) =>
    t?.runs.map(r => `${r.color} ${r.font} ${r.px}`).join(' | ') ?? null;
  const box = (name: string) => {
    const b = s.item(name)?.box;
    return b ? { caption: text(b.caption), value: text(b.value), unit: text(b.unit) } : null;
  };
  const roller = s.list('scenes')!.roller;
  return {
    light: { label: text(s.key('light')!.display!.label), top: text(s.key('light')!.display!.topLabel) },
    blind: { label: text(s.key('blind')!.display!.label) },
    temp: box('temp'),
    co2: box('co2'),
    list: roller && {
      color: roller.color, selectedBg: roller.selectedBg, selectedColor: roller.selectedColor,
      font: `${roller.font.font} ${roller.font.px}`, lineSpace: roller.lineSpace,
    },
  };
}

/** The font of a list's selected row, and of its other rows, as the device resolved them. */
function listFonts(emu: Emulator) {
  emu.render();
  const roller = emu.state.list('scenes')!.roller!;
  return { rows: `${roller.font.font} ${roller.font.px}`, selected: `${roller.selectedFont.font} ${roller.selectedFont.px}` };
}

/** Pixels of (about) one colour; the panel has 5/6/5 bits per channel. */
function pixelsOf(shot: Screenshot, r: number, g: number, b: number): number {
  let n = 0;
  for (let i = 0; i < shot.data.length; i += 4) {
    if (Math.abs(shot.data[i]! - r) <= 8 && Math.abs(shot.data[i + 1]! - g) <= 8 && Math.abs(shot.data[i + 2]! - b) <= 8) n++;
  }
  return n;
}

/** An emulator with values on the display items, so that value and unit are drawn. */
async function start(cfg: unknown, script = '', options: { fonts?: boolean } = {}) {
  // fonts: the repository's TrueType files. Without them LVGL's built-in font stands in
  // (one line height for every size), which is enough where only face and px are compared.
  const made = await makeEmulator({ config: cfg as never, ...(options.fonts ? { media: repoMedia() } : {}) });
  expect(made.emu.run(`fb.main.display.setValue("temp", "21"); fb.main.display.setValue("co2", "612");\n${script}`)).toEqual({ ok: true });
  made.clock.advance(100);
  return made;
}

const PLAIN = {
  light: { label: '#ffffff montserrat 32', top: '#ffd700 montserrat_semibold 18' },
  blind: { label: '#ffffff montserrat 32' },
  temp: { caption: '#ffd700 montserrat_semibold 14', value: '#ffffff montserrat 32', unit: '#999999 montserrat 32' },
  co2: { caption: '#ffd700 montserrat_semibold 14', value: '#ffffff montserrat 14', unit: '#999999 montserrat 14' },
  list: { color: '#808080', selectedBg: '#ffd700', selectedColor: '#000000', font: 'montserrat 32', lineSpace: 3 },
};

describe('named styles in config.json', () => {
  it('without "style.styles" nothing changes: the look, no fb.style, no style methods', async () => {
    const { emu, rec } = await start(config({}, null));
    expect(look(emu)).toEqual(PLAIN);
    expect(rec.errors).toEqual([]);
    const typeOf = (code: string) => emu.eval(`typeof ${code}`);
    expect(typeOf('fb.style')).toMatchObject({ ok: true, value: 'undefined' });
    for (const method of [
      'fb.key("light").display.setStyle', 'fb.key("light").display.setTopStyle', 'fb.main.display.setStyle',
      'fb.main.display.setLabelStyle', 'fb.main.list.setStyle', 'fb.main.list.setFocusStyle',
    ]) {
      expect(typeOf(method), method).toMatchObject({ ok: true, value: 'undefined' });
    }
    // The engine's own error for a call that is not there.
    const call = emu.eval('fb.main.display.setStyle("temp", "alarm")');
    expect(call.ok).toBe(false);
    expect(!call.ok && call.error.message).toMatch(/^TypeError: .*not a function/);
  });

  it('named styles that no item uses change nothing either, but the script calls exist', async () => {
    const { emu } = await start(config());
    expect(look(emu)).toEqual(PLAIN);
    expect(emu.eval('typeof fb.style + " " + typeof fb.style.set + " " + typeof fb.main.list.setFocusStyle'))
      .toMatchObject({ ok: true, value: 'object function function' });
  });

  it('a key: "style" on the label, "topstyle" on the top label', async () => {
    const { emu } = await start(config({ light: { style: 'big', topstyle: 'muted' }, blind: { style: 'alarm' } }));
    expect(look(emu)).toEqual({
      ...PLAIN,
      // Font and size from the style; the colour still the element's.
      light: { label: '#ffffff montserrat_semibold 48', top: '#808080 montserrat_semibold 18' },
      blind: { label: '#ff0000 montserrat 32' },
    });
    expect(emu.state.key('light')).toMatchObject({ font: { face: 1, size: 48 } });
    // The pixels: the red label is on the key's own display, and only there.
    expect(pixelsOf(emu.screenshot('key4'), 255, 0, 0)).toBeGreaterThan(20);
    expect(pixelsOf(emu.screenshot('key3'), 255, 0, 0)).toBe(0);
  });

  it('a display item: "style" on the value and the unit, "labelstyle" on the caption', async () => {
    const { emu } = await start(config({ temp: { style: 'alarm', labelstyle: 'muted' }, co2: { style: 'big', labelstyle: 'alarm' } }));
    expect(look(emu)).toEqual({
      ...PLAIN,
      // A named text_color colours the unit too.
      temp: { caption: '#808080 montserrat_semibold 14', value: '#ff0000 montserrat 32', unit: '#ff0000 montserrat 32' },
      // A named font is the value's and the unit's; the unit keeps its own colour.
      co2: {
        caption: '#ff0000 montserrat_semibold 14', value: '#ffffff montserrat_semibold 48', unit: '#999999 montserrat_semibold 48',
      },
    });
    expect(pixelsOf(emu.screenshot('main'), 255, 0, 0)).toBeGreaterThan(20);
    expect(pixelsOf((await start(config())).emu.screenshot('main'), 255, 0, 0)).toBe(0);
  });

  it('a list: "style" on the rows, "focusstyle" on the selected row (the only place for bg_color)', async () => {
    const { emu } = await start(config({ scenes: { style: 'alarm', focusstyle: 'picked' } }));
    expect(look(emu).list).toEqual({ ...PLAIN.list, color: '#ff0000', selectedBg: '#00c853', selectedColor: '#000000' });
    const shot = emu.screenshot('main');
    expect(pixelsOf(shot, 0, 200, 82)).toBeGreaterThan(1000);   // the green bar
    expect(pixelsOf(shot, 255, 215, 0)).toBeLessThan(400);      // no gold bar (the captions are gold)
  });

  it('a list: a named text_size and text_line_space replace the list\'s', async () => {
    const { emu } = await start(config({ scenes: { style: 'roomy' } }));
    expect(look(emu).list).toEqual({ ...PLAIN.list, font: 'montserrat 20', lineSpace: 12 });
  });

  it('an item field still wins over the named style', async () => {
    const { emu } = await start(config({
      // fontsize 2 = 24 px over the style's 48; the style's face stays.
      light: { style: 'big', fontsize: 2 },
      co2: { style: 'big', font: 'montserrat' },
      scenes: { style: 'alarm', focusstyle: 'picked', color: '0000ff', focuscolor: 'ff00ff', focustextcolor: 'ffffff', fontsize: 2 },
    }));
    const seen = look(emu);
    expect(seen.light.label).toBe('#ffffff montserrat_semibold 24');
    expect(seen.co2).toMatchObject({ value: '#ffffff montserrat 48', unit: '#999999 montserrat 48' });
    expect(seen.list).toMatchObject({ color: '#0000ff', selectedBg: '#ff00ff', selectedColor: '#ffffff', font: 'montserrat 24' });
  });

  // (The firmware logs a warning for each at boot; the IDE reports them from config.json: font-config.test.ts.)
  it('a field the device cannot use is ignored: unknown style, not a string, bg_* on a key or display item', async () => {
    const { emu } = await start(config({
      light: { style: 'nosuch', topstyle: 'picked' }, temp: { style: 'picked', labelstyle: 7 }, scenes: { style: 'picked' },
    }));
    expect(look(emu)).toEqual({ ...PLAIN, list: { ...PLAIN.list, color: '#000000' } });
  });

  it('bad names and a 17th style are left out; the rest loads', async () => {
    const many: Record<string, unknown> = { '1bad': { text_color: '#00ff00' }, 'has space': {}, notobject: 5 };
    for (let i = 1; i <= 17; i++) many[`s${i}`] = { text_color: i === 16 ? '#00ff00' : '#0000ff' };
    const { emu } = await start(config({ light: { style: 's16' }, blind: { style: 's17' } }, many));
    const seen = look(emu);
    expect(seen.light.label).toBe('#00ff00 montserrat 32');
    expect(seen.blind.label).toBe(PLAIN.blind.label);
    expect(emu.eval('fb.style.set("s17", "text_color", "#ffffff")')).toMatchObject({ ok: false, error: { message: 'ReferenceError: unknown style "s17"' } });
    expect(emu.eval('fb.style.set("1bad", "text_color", "#ffffff")')).toMatchObject({ ok: false, error: { message: 'ReferenceError: unknown style "1bad"' } });
    expect(emu.eval('fb.style.set("notobject", "text_color", "#ffffff")')).toMatchObject({ ok: false, error: { message: 'ReferenceError: unknown style "notobject"' } });
    expect(emu.eval('fb.style.set("s16", "text_color", "#ffffff")')).toMatchObject({ ok: true });
  });
});

describe('the IDE computes the same fonts as the firmware (font-config.ts against the emulator)', () => {
  const STYLE = {
    fonts: [{ name: 'extra', src: 'semibold.ttf' }],
    display: { unit: { text_size: 10 } },
    styles: {
      face: { text_font: 'extra' }, size: { text_size: 20 }, both: { text_font: 'montserrat_semibold', text_size: 40 },
      colour: { text_color: '#ff0000' }, bar: { bg_color: '#00ff00', text_size: 18 },
    },
  };
  const CASES: Record<string, Record<string, Record<string, unknown>>> = {
    'no named style': {},
    'a style with a font only': { light: { style: 'face', topstyle: 'face' }, temp: { style: 'face', labelstyle: 'face' }, co2: { style: 'face' }, scenes: { style: 'face' } },
    'a style with a size only': { light: { style: 'size', topstyle: 'size' }, temp: { style: 'size', labelstyle: 'size' }, co2: { style: 'size' }, scenes: { style: 'size' } },
    'a style with both': { light: { style: 'both', topstyle: 'both' }, temp: { style: 'both', labelstyle: 'both' }, co2: { style: 'both' }, scenes: { style: 'both' } },
    'a style without a font': { light: { style: 'colour' }, temp: { style: 'colour' }, co2: { style: 'colour', labelstyle: 'colour' }, scenes: { style: 'colour' } },
    'item fields over a style': {
      light: { style: 'both', fontsize: 2, topstyle: 'both', topfontsize: 2 }, temp: { style: 'both', labelstyle: 'both', labelfontsize: 2 },
      co2: { style: 'both', font: 'extra', fontsize: 30 }, scenes: { style: 'both', fontsize: 2 },
    },
    'a style the part cannot use': { light: { style: 'bar' }, temp: { style: 'bar' }, co2: { labelstyle: 'nosuch' }, scenes: { style: 'bar', focusstyle: 'bar' } },
    // The selected row: a focus style with one of font / size is built on the rows' font, whichever layer gives that.
    'a focus style with a font only, on an item fontsize': { scenes: { fontsize: 2, focusstyle: 'face' } },
    'a focus style with a size only, on an item font': { scenes: { font: 'extra', focusstyle: 'size' } },
    'a focus style with a size only, on an item font and fontsize': { scenes: { font: 'extra', fontsize: 40, focusstyle: 'size' } },
    'a focus style with a font only, on a rows style with a size': { scenes: { style: 'size', focusstyle: 'face' } },
    'a focus style with a size only, on a rows style with both': { scenes: { style: 'both', focusstyle: 'size' } },
    'a focus style with both, on an item font': { scenes: { font: 'extra', fontsize: 2, focusstyle: 'both' } },
    'a focus style without a font, on an item font': { scenes: { font: 'extra', fontsize: 2, focusstyle: 'colour' } },
  };
  // "style.list.selected" with a font of its own: its face and px, both, whatever the rows have.
  const OWN_CASES: Record<string, Record<string, unknown>> = {
    'list.selected with its own size': {},
    'list.selected with its own size, an item font on the rows': { font: 'extra', fontsize: 2 },
    'list.selected with its own size, a focus style with a font only': { fontsize: 2, focusstyle: 'face' },
    'list.selected with its own size, a focus style with a size only': { font: 'extra', focusstyle: 'size' },
    'list.selected with its own size, a focus style with both': { focusstyle: 'both' },
  };
  for (const [name, scenes] of Object.entries(OWN_CASES)) {
    it(name, async () => {
      const cfg = { ...config({ scenes }, null), style: { ...STYLE, list: { selected: { text_size: 26 } } } };
      const { emu } = await start(cfg);
      emu.render();
      const model = configToModel(cfg);
      const list = model.pages[0]!.lists![0]!;
      const roller = emu.state.list('scenes')!.roller!;
      const selected = listSelectedFont(list.font!, model.styles, list.focusStyle);
      expect(`${model.fonts.faces[list.font!.face]!.name} ${list.font!.px}`).toBe(`${roller.font.font} ${roller.font.px}`);
      expect(`${model.fonts.faces[selected.face]!.name} ${selected.px}`).toBe(`${roller.selectedFont.font} ${roller.selectedFont.px}`);
    });
  }
  for (const [name, extra] of Object.entries(CASES)) {
    it(name, async () => {
      const cfg = { ...config(extra, null), style: STYLE };
      const { emu } = await start(cfg);
      emu.render();
      const model = configToModel(cfg);
      const face = (index: number) => model.fonts.faces[index]!.name;
      const want = (f: { face: number; px: number } | undefined) => f && `${face(f.face)} ${f.px}`;
      const got = (t: { font: { font: string; px: number } | null } | undefined) => t?.font && `${t.font.font} ${t.font.px}`;
      const button = (key: string) => model.pages[0]!.buttons.find(b => b.name === key)!;
      const item = (key: string) => model.pages[0]!.displayItems.find(i => i.name === key)!;
      const list = model.pages[0]!.lists![0]!;
      const s = emu.state;
      expect({
        // A key without font or style fields has no font of its own in the model: the element style's.
        label: want(button('light').labelFont ?? styleFont(model.styles, StyleElement.KeyLabel)),
        top: want(button('light').topFont ?? styleFont(model.styles, StyleElement.KeyTopLabel)),
        temp: [want(item('temp').valueFont), want(item('temp').unitFont), want(item('temp').labelFont)],
        co2: [want(item('co2').valueFont), want(item('co2').unitFont), want(item('co2').labelFont)],
        list: want(list.font),
        selected: want(listSelectedFont(list.font!, model.styles, list.focusStyle)),
      }).toEqual({
        label: got(s.key('light')!.display!.label), top: got(s.key('light')!.display!.topLabel),
        temp: [got(s.item('temp')!.box!.value), got(s.item('temp')!.box!.unit), got(s.item('temp')!.box!.caption)],
        co2: [got(s.item('co2')!.box!.value), got(s.item('co2')!.box!.unit), got(s.item('co2')!.box!.caption)],
        list: got(s.list('scenes')!.roller!),
        selected: got({ font: s.list('scenes')!.roller!.selectedFont }),
      });
      // The list's colours as the model gives them to the preview.
      expect({ color: list.color, focusColor: list.focusColor, focusTextColor: list.focusTextColor }).toEqual({
        color: s.list('scenes')!.roller!.color, focusColor: s.list('scenes')!.roller!.selectedBg,
        focusTextColor: s.list('scenes')!.roller!.selectedColor,
      });
    });
  }
});

describe('named styles from a script', () => {
  it('setStyle / setTopStyle / setLabelStyle / setFocusStyle give a part a style; "" takes it away', async () => {
    const { emu, rec } = await start(config({ temp: { style: 'alarm' } }), `
      var k = fb.key("light").display;
      k.setStyle("big"); k.setTopStyle("muted");
      print(fb.main.display.setStyle("co2", "alarm"), fb.main.display.setLabelStyle(8, "muted"), fb.main.display.setStyle(99, "alarm"));
      fb.main.display.setStyle("temp", "");
      fb.main.list.setStyle("scenes", "alarm"); fb.main.list.setFocusStyle(0, "picked");
    `);
    expect(rec.errors).toEqual([]);
    expect(rec.console).toEqual(['true true false']);
    expect(look(emu)).toEqual({
      ...PLAIN,
      light: { label: '#ffffff montserrat_semibold 48', top: '#808080 montserrat_semibold 18' },
      // The configured style is gone: the element's look.
      temp: PLAIN.temp,
      co2: { caption: '#808080 montserrat_semibold 14', value: '#ff0000 montserrat 14', unit: '#ff0000 montserrat 14' },
      list: { ...PLAIN.list, color: '#ff0000', selectedBg: '#00c853' },
    });
    // And back: naming the configured style again, "" on the others.
    emu.eval('k.setStyle(""); k.setTopStyle(""); fb.main.display.setStyle("temp", "alarm"); fb.main.display.setStyle("co2", "");'
      + 'fb.main.display.setLabelStyle("co2", ""); fb.main.list.setStyle("scenes", ""); fb.main.list.setFocusStyle("scenes", "")');
    expect(look(emu)).toEqual({ ...PLAIN, temp: { ...PLAIN.temp, value: '#ff0000 montserrat 32', unit: '#ff0000 montserrat 32' } });
  });

  it('a display item a script creates takes a style like any other', async () => {
    const { emu } = await start(config(), `
      var id = fb.main.display.create(50, 60, 40, 0, 1, 0, "New", "u", 0, 1);
      fb.main.display.setValue(id, "5"); fb.main.display.setStyle(id, "alarm"); fb.main.display.setLabelStyle(id, "muted");
      print(id);
    `);
    emu.render();
    const box = emu.state.items.find(i => i.label === 'New')!.box!;
    expect(box.value.runs[0]).toMatchObject({ text: '5', color: '#ff0000' });
    expect(box.unit.runs[0]).toMatchObject({ color: '#ff0000' });
    expect(box.caption.runs[0]).toMatchObject({ color: '#808080' });
  });

  it('fb.style.set recolours every user at once, markup spans included', async () => {
    const { emu, rec } = await start(config({
      temp: { style: 'alarm' }, co2: { labelstyle: 'alarm' }, blind: { style: 'alarm' }, light: { topstyle: 'alarm' },
      scenes: { style: 'alarm', focusstyle: 'picked' },
    }), `
      fb.main.display.setValue("temp", "2\\\\c0000ff1\\\\cffff00 C");
      fb.key("blind").display.setLabel("Bl\\\\c0000ffind");
    `);
    expect(rec.errors).toEqual([]);
    const before = look(emu);
    // The first span has the style's colour, the others their own.
    expect(before.temp!.value).toBe('#ff0000 montserrat 32 | #0000ff montserrat 32 | #ffff00 montserrat 32');
    expect(before.blind.label).toBe('#ff0000 montserrat 32 | #0000ff montserrat 32');
    expect(before).toMatchObject({
      temp: { unit: '#ff0000 montserrat 32' }, co2: { caption: '#ff0000 montserrat_semibold 14' },
      light: { top: '#ff0000 montserrat_semibold 18' }, list: { color: '#ff0000', selectedBg: '#00c853' },
    });
    expect(pixelsOf(emu.screenshot('main'), 0, 255, 0)).toBe(0);

    expect(emu.eval('fb.style.set("alarm", "text_color", "#00ff00"); fb.style.set("picked", "bg_color", "#ff00ff");'
      + 'fb.style.set("picked", "text_color", "ffffff")')).toMatchObject({ ok: true });
    const after = look(emu);
    expect(after.temp!.value).toBe('#00ff00 montserrat 32 | #0000ff montserrat 32 | #ffff00 montserrat 32');
    expect(after.blind.label).toBe('#00ff00 montserrat 32 | #0000ff montserrat 32');
    expect(after).toMatchObject({
      temp: { unit: '#00ff00 montserrat 32' }, co2: { caption: '#00ff00 montserrat_semibold 14' },
      light: { top: '#00ff00 montserrat_semibold 18' },
      list: { color: '#00ff00', selectedBg: '#ff00ff', selectedColor: '#ffffff' },
    });
    // Nothing else moved.
    expect(after.co2!.value).toBe(PLAIN.co2.value);
    expect(after.light.label).toBe(PLAIN.light.label);
    const shot = emu.screenshot('main');
    expect(pixelsOf(shot, 0, 255, 0)).toBeGreaterThan(50);
    expect(pixelsOf(shot, 255, 0, 0)).toBe(0);
    expect(pixelsOf(shot, 255, 0, 255)).toBeGreaterThan(1000);
    const key = emu.screenshot('key4');
    expect(pixelsOf(key, 0, 255, 0)).toBeGreaterThan(5);
    expect(pixelsOf(key, 255, 0, 0)).toBe(0);
  });

  it('fb.style.set takes numbers too: text_line_space of a list style', async () => {
    const { emu } = await start(config({ scenes: { style: 'roomy' } }));
    expect(emu.eval('fb.style.set("roomy", "text_line_space", 2)')).toMatchObject({ ok: true });
    expect(look(emu).list).toMatchObject({ lineSpace: 2, font: 'montserrat 20' });
  });

  it('the error types: ReferenceError for an unknown style, TypeError for the rest', async () => {
    const { emu } = await start(config({ temp: { style: 'alarm' } }));
    const message = (code: string): string => {
      const result = emu.eval(code);
      return result.ok ? `ok ${result.value}` : result.error.message;
    };
    const before = look(emu);
    // Unknown style.
    expect(message('fb.key("light").display.setStyle("nosuch")')).toBe('ReferenceError: unknown style "nosuch"');
    expect(message('fb.key("light").display.setTopStyle("nosuch")')).toBe('ReferenceError: unknown style "nosuch"');
    expect(message('fb.main.display.setStyle("temp", "nosuch")')).toBe('ReferenceError: unknown style "nosuch"');
    expect(message('fb.main.display.setLabelStyle("temp", "Alarm")')).toBe('ReferenceError: unknown style "Alarm"');
    expect(message('fb.main.list.setStyle("scenes", "nosuch")')).toBe('ReferenceError: unknown style "nosuch"');
    expect(message('fb.main.list.setFocusStyle("scenes", "nosuch")')).toBe('ReferenceError: unknown style "nosuch"');
    expect(message('fb.style.set("nosuch", "text_color", "#ffffff")')).toBe('ReferenceError: unknown style "nosuch"');
    // Unknown item or list: as setFont.
    expect(message('fb.main.display.setStyle("nope", "alarm")')).toBe('ReferenceError: unknown display item "nope"');
    expect(message('fb.main.list.setStyle("nolist", "alarm")')).toBe('ReferenceError: unknown list "nolist"');
    // Not a string.
    expect(message('fb.key("light").display.setStyle(3)')).toMatch(/^TypeError: .*requires a style name \(string\)/);
    expect(message('fb.main.display.setStyle("temp")')).toMatch(/^TypeError: /);
    expect(message('fb.main.display.setStyle("temp", 3)')).toMatch(/^TypeError: .*requires a style name \(string\)/);
    expect(message('fb.main.list.setFocusStyle("scenes", null)')).toMatch(/^TypeError: .*requires a style name \(string\)/);
    // A style with bg_* on a key or a display item; a list takes it on both parts.
    expect(message('fb.key("light").display.setStyle("picked")')).toMatch(/^TypeError: .*style "picked" sets a background, which only a list has/);
    expect(message('fb.key("light").display.setTopStyle("picked")')).toMatch(/^TypeError: .*sets a background/);
    expect(message('fb.main.display.setStyle("temp", "picked")')).toMatch(/^TypeError: .*sets a background/);
    expect(message('fb.main.display.setLabelStyle("temp", "picked")')).toMatch(/^TypeError: .*sets a background/);
    // fb.style.set: a property the style does not have, the font and the size, an unknown property, a bad value.
    expect(message('fb.style.set("alarm", "text_opa", 128)')).toBe('TypeError: fb.style.set(): the style does not set "text_opa" in config.json');
    expect(message('fb.style.set("plain", "text_color", "#ffffff")')).toBe('TypeError: fb.style.set(): the style does not set "text_color" in config.json');
    expect(message('fb.style.set("big", "text_font", "montserrat")')).toBe('TypeError: fb.style.set(): "text_font" cannot be changed by a script');
    expect(message('fb.style.set("big", "text_size", 20)')).toBe('TypeError: fb.style.set(): "text_size" cannot be changed by a script');
    expect(message('fb.style.set("alarm", "colour", "#ffffff")')).toBe('TypeError: fb.style.set(): unknown property "colour"');
    expect(message('fb.style.set("alarm", "text_color", "red")')).toBe('TypeError: fb.style.set(): not a valid value for "text_color"');
    expect(message('fb.style.set("alarm", "text_color", 5)')).toBe('TypeError: fb.style.set(): not a valid value for "text_color"');
    expect(message('fb.style.set("roomy", "text_line_space", 101)')).toBe('TypeError: fb.style.set(): not a valid value for "text_line_space"');
    expect(message('fb.style.set("alarm", "text_color", true)')).toBe('TypeError: fb.style.set(): the value must be a string or a number');
    expect(message('fb.style.set("alarm", "text_color")')).toMatch(/^TypeError: fb\.style\.set\(\) requires/);
    // None of it changed anything.
    expect(look(emu)).toEqual(before);
  });

  it('a script restart restores the configured styles and their values', async () => {
    const cfg = config({ temp: { style: 'alarm' }, light: { style: 'big' }, scenes: { focusstyle: 'picked' } });
    const { emu, clock, rec } = await start(cfg);
    const configured = look(emu);
    const idle = emu.screenshot('main');
    expect(configured).toMatchObject({ temp: { value: '#ff0000 montserrat 32' }, light: { label: '#ffffff montserrat_semibold 48' } });

    expect(emu.eval(`
      fb.style.set("alarm", "text_color", "#00ff00"); fb.style.set("picked", "bg_color", "#0000ff");
      fb.main.display.setStyle("temp", "muted"); fb.main.display.setLabelStyle("temp", "alarm"); fb.main.display.setStyle("co2", "alarm");
      fb.key("light").display.setStyle(""); fb.key("light").display.setTopStyle("alarm"); fb.key("blind").display.setStyle("big");
      fb.main.list.setStyle("scenes", "alarm"); fb.main.list.setFocusStyle("scenes", "");
    `)).toMatchObject({ ok: true });
    const changed = look(emu);
    expect(changed).toMatchObject({
      temp: { caption: '#00ff00 montserrat_semibold 14', value: '#808080 montserrat 32' },
      co2: { value: '#00ff00 montserrat 14' },
      light: { label: PLAIN.light.label, top: '#00ff00 montserrat_semibold 18' },
      blind: { label: '#ffffff montserrat_semibold 48' },
      list: { color: '#00ff00', selectedBg: '#ffd700' },
    });

    expect(emu.restart()).toEqual({ ok: true });
    clock.advance(100);
    expect(look(emu)).toEqual(configured);
    // The same pixels as before the script touched anything.
    expect(emu.screenshot('main').data).toEqual(idle.data);
    // The style has its config value again: a part that takes it now is red, not green.
    emu.eval('fb.main.display.setStyle("co2", "alarm")');
    expect(look(emu).co2!.value).toBe('#ff0000 montserrat 14');
    expect(rec.errors).toEqual([]);
  });
});

describe('a list\'s selected row with a focus style that sets one of text_font / text_size', () => {
  it('keeps the rows\' size: an item "fontsize" under a focus style with a font only', async () => {
    // (fontsize 2 = 24 px: the list style's own size is 32, which would hide the difference.)
    const { emu } = await start(config({ scenes: { fontsize: 2, focusstyle: 'semi' } }));
    expect(listFonts(emu)).toEqual({ rows: 'montserrat 24', selected: 'montserrat_semibold 24' });
  });

  it('keeps the rows\' face: an item "font" under a focus style with a size only', async () => {
    const { emu } = await start(config({ scenes: { font: 'montserrat_semibold', focusstyle: 'huge' } }));
    expect(listFonts(emu)).toEqual({ rows: 'montserrat_semibold 32', selected: 'montserrat_semibold 48' });
  });

  it('follows the rows\' named style', async () => {
    const { emu } = await start(config({ scenes: { style: 'roomy', focusstyle: 'semi' } }));
    expect(listFonts(emu)).toEqual({ rows: 'montserrat 20', selected: 'montserrat_semibold 20' });
    // A rows style with both: its face stays under a focus size, its size under a focus face.
    emu.eval('fb.main.list.setStyle("scenes", "big"); fb.main.list.setFocusStyle("scenes", "huge")');
    expect(listFonts(emu)).toEqual({ rows: 'montserrat_semibold 48', selected: 'montserrat_semibold 48' });
    emu.eval('fb.main.list.setStyle("scenes", "roomy")');
    expect(listFonts(emu)).toEqual({ rows: 'montserrat 20', selected: 'montserrat 48' });
  });

  it('follows a script\'s setFont and setStyle, as it does without a focus style', async () => {
    const { emu } = await start(config({ scenes: { focusstyle: 'semi' } }));
    expect(listFonts(emu)).toEqual({ rows: 'montserrat 32', selected: 'montserrat_semibold 32' });
    emu.eval('fb.main.list.setFont("scenes", "montserrat", 2)');
    expect(listFonts(emu)).toEqual({ rows: 'montserrat 24', selected: 'montserrat_semibold 24' });
    emu.eval('fb.main.list.setFocusStyle("scenes", "huge"); fb.main.list.setFont("scenes", "montserrat_semibold", 2)');
    expect(listFonts(emu)).toEqual({ rows: 'montserrat_semibold 24', selected: 'montserrat_semibold 48' });
    // Without a focus style the selected row is the rows' again.
    emu.eval('fb.main.list.setFocusStyle("scenes", "")');
    expect(listFonts(emu)).toEqual({ rows: 'montserrat_semibold 24', selected: 'montserrat_semibold 24' });
  });
});

describe('a named style stays where it was put', () => {
  it('switching from one style to another takes the first away', async () => {
    const { emu } = await start(config({ co2: { style: 'alarm' }, blind: { style: 'alarm' }, scenes: { style: 'alarm' } }));
    expect(look(emu)).toMatchObject({
      co2: { value: '#ff0000 montserrat 14', unit: '#ff0000 montserrat 14' }, blind: { label: '#ff0000 montserrat 32' },
      list: { color: '#ff0000', font: 'montserrat 32' },
    });
    // alarm sets a colour only, big a font and a size only: nothing of alarm may stay.
    emu.eval('fb.main.display.setStyle("co2", "big"); fb.key("blind").display.setStyle("big"); fb.main.list.setStyle("scenes", "big")');
    expect(look(emu)).toMatchObject({
      co2: { value: '#ffffff montserrat_semibold 48', unit: '#999999 montserrat_semibold 48' },
      blind: { label: '#ffffff montserrat_semibold 48' },
      list: { color: '#808080', font: 'montserrat_semibold 48' },
    });
    expect(pixelsOf(emu.screenshot('main'), 255, 0, 0)).toBe(0);
    expect(pixelsOf(emu.screenshot('key4'), 255, 0, 0)).toBe(0);
    // And back: nothing of big stays.
    emu.eval('fb.main.display.setStyle("co2", "alarm"); fb.key("blind").display.setStyle("alarm"); fb.main.list.setStyle("scenes", "alarm")');
    expect(look(emu)).toMatchObject({
      co2: { value: '#ff0000 montserrat 14', unit: '#ff0000 montserrat 14' }, blind: { label: '#ff0000 montserrat 32' },
      list: { color: '#ff0000', font: 'montserrat 32' },
    });
  });

  it('a page change away and back keeps it: the boxes and the roller are built again', async () => {
    const { emu } = await start(config({
      temp: { style: 'alarm', labelstyle: 'muted' }, scenes: { page: 1, style: 'roomy', focusstyle: 'picked' },
    }), 'fb.main.display.setStyle("co2", "big"); fb.main.display.setLabelStyle("co2", "alarm");');
    const first = look(emu);
    expect(first).toMatchObject({
      temp: { caption: '#808080 montserrat_semibold 14', value: '#ff0000 montserrat 32', unit: '#ff0000 montserrat 32' },
      co2: { caption: '#ff0000 montserrat_semibold 14', value: '#ffffff montserrat_semibold 48' },
      list: { font: 'montserrat 20', lineSpace: 12, selectedBg: '#00c853' },
    });
    const shot = emu.screenshot('main');

    emu.eval('fb.main.display.showPage(2)');
    emu.render();
    expect(emu.state.item('temp')!.box).toBeNull();
    expect(emu.state.item('co2')!.box).toBeNull();
    expect(emu.state.list('scenes')!.roller).toBeNull();

    emu.eval('fb.main.display.showPage(1)');
    expect(look(emu)).toEqual(first);
    expect(emu.screenshot('main').data).toEqual(shot.data);
  });

  it('a new text keeps it: the spans of a value, a caption, a unit and a key label are made again', async () => {
    const { emu } = await start(config({ temp: { style: 'alarm', labelstyle: 'muted' }, blind: { style: 'loud' } }));
    emu.eval(`fb.main.display.setValue("temp", "1\\\\c0000ff2\\\\cffff003");
      fb.main.display.setLabel("temp", "In\\\\c00ff00side"); fb.main.display.setUnit("temp", "K\\\\cffffffx");
      fb.key("blind").display.setLabel("Up\\\\c0000ffDown")`);
    expect(look(emu)).toMatchObject({
      // The spans without a colour of their own have the style's; every span has the style's size.
      temp: {
        value: '#ff0000 montserrat 32 | #0000ff montserrat 32 | #ffff00 montserrat 32',
        caption: '#808080 montserrat_semibold 14 | #00ff00 montserrat_semibold 14',
        unit: '#ff0000 montserrat 32 | #ffffff montserrat 32',
      },
      blind: { label: '#ff0000 montserrat 20 | #0000ff montserrat 20' },
    });
    // Again, other spans; then one format (the key's label draws that itself).
    emu.eval(`fb.main.display.setValue("temp", "\\\\c00ff009\\\\cffffff8"); fb.main.display.setValue("temp", "7\\\\c0000ff6");
      fb.key("blind").display.setLabel("Stop")`);
    expect(look(emu)).toMatchObject({
      temp: { value: '#ff0000 montserrat 32 | #0000ff montserrat 32' }, blind: { label: '#ff0000 montserrat 20' },
    });
    emu.eval('fb.key("blind").display.setLabel("A\\\\c00ff00B\\\\cffffffC")');
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 20 | #00ff00 montserrat 20 | #ffffff montserrat 20');
  });

  it('fb.style.set on a style nobody uses: a part that takes the style afterwards shows the new value', async () => {
    const { emu, rec } = await start(config());
    expect(emu.eval('fb.style.set("alarm", "text_color", "#00ff00"); fb.style.set("roomy", "text_line_space", 5)')).toMatchObject({ ok: true });
    expect(look(emu)).toEqual(PLAIN);
    emu.eval('fb.main.display.setStyle("co2", "alarm"); fb.key("blind").display.setStyle("alarm"); fb.main.list.setStyle("scenes", "roomy")');
    expect(look(emu)).toMatchObject({
      co2: { value: '#00ff00 montserrat 14', unit: '#00ff00 montserrat 14' }, blind: { label: '#00ff00 montserrat 32' },
      list: { font: 'montserrat 20', lineSpace: 5 },
    });
    expect(rec.errors).toEqual([]);
  });
});

describe('the geometry that follows a named style', () => {
  it('a list: the roller is as high as its rows, with a named font and after fb.style.set of text_line_space', async () => {
    const rows = 3;
    const geometry = (emu: Emulator) => {
      emu.render();
      const r = emu.state.list('scenes')!.roller!;
      // LVGL's roller: the visible rows, each the font's line and the line space.
      expect(r.height).toBe(r.rows * (r.font.lineHeight + r.lineSpace));
      expect(r.rows).toBe(rows);
      return { height: r.height, line: r.font.lineHeight, lineSpace: r.lineSpace };
    };
    // The list style: 32 px, a quarter of the line as line space.
    const plain = await start(config({ scenes: { rows } }), '', { fonts: true });
    expect(geometry(plain.emu)).toEqual({ height: 144, line: 39, lineSpace: 9 });

    // roomy: 20 px rows, 12 px between them.
    const { emu } = await start(config({ scenes: { rows, style: 'roomy' } }), '', { fonts: true });
    expect(geometry(emu)).toEqual({ height: 108, line: 24, lineSpace: 12 });
    expect(emu.eval('fb.style.set("roomy", "text_line_space", 2)')).toMatchObject({ ok: true });
    expect(geometry(emu)).toEqual({ height: 78, line: 24, lineSpace: 2 });
    expect(emu.eval('fb.style.set("roomy", "text_line_space", 30)')).toMatchObject({ ok: true });
    expect(geometry(emu)).toEqual({ height: 162, line: 24, lineSpace: 30 });
    // Another style from a script, with a 48 px font and no line space: a quarter of that line again.
    emu.eval('fb.main.list.setStyle("scenes", "big")');
    expect(geometry(emu)).toEqual({ height: 216, line: 58, lineSpace: 14 });
    // Without the style: as the plain list.
    emu.eval('fb.main.list.setStyle("scenes", "")');
    expect(geometry(emu)).toEqual({ height: 144, line: 39, lineSpace: 9 });
  });

  it('a display item: the padding goes with a named text_size as with the style\'s size', async () => {
    const pads = (emu: Emulator, name: string) => {
      emu.render();
      const box = emu.state.item(name)!.box!;
      return [box.captionPad, box.boxPad];
    };
    // co2 has no "fontsize": display.value's 14 px gives no padding of its own (src/ui/box_logic.h).
    const { emu } = await start(config({ temp: { style: 'big' } }));
    expect(pads(emu, 'co2')).toEqual([null, null]);
    // big = 48 px: step 4 (10 / 8 for a box with an underline).
    emu.eval('fb.main.display.setStyle("co2", "big")');
    expect(pads(emu, 'co2')).toEqual([10, 8]);
    // roomy = 20 px: the nearest step is 24 px (4 / 3).
    emu.eval('fb.main.display.setStyle("co2", "roomy")');
    expect(pads(emu, 'co2')).toEqual([4, 3]);
    // A style without a size, and no style: none again.
    emu.eval('fb.main.display.setStyle("co2", "alarm")');
    expect(pads(emu, 'co2')).toEqual([null, null]);
    emu.eval('fb.main.display.setStyle("co2", "big"); fb.main.display.setStyle("co2", "")');
    expect(pads(emu, 'co2')).toEqual([null, null]);
    // The item's own "fontsize" (temp: 3) is not changed by the style's size.
    expect(pads(emu, 'temp')).toEqual([4, 8]);
  });
});

describe('fb.style.set: what it refuses', () => {
  it('no style name, a value that is no string or number, a NUL character', async () => {
    const { emu } = await start(config({ temp: { style: 'alarm' } }));
    const message = (code: string): string => {
      const result = emu.eval(code);
      return result.ok ? `ok ${result.value}` : result.error.message;
    };
    const before = look(emu);
    expect(message('fb.style.set("", "text_color", "#00ff00")')).toBe('TypeError: fb.style.set() requires a style name, a property name and a value');
    expect(message('fb.style.set(7, "text_color", "#00ff00")')).toMatch(/^TypeError: fb\.style\.set\(\) requires a style name \(string\)/);
    expect(message('fb.style.set()')).toMatch(/^TypeError: /);
    expect(message('fb.style.set("alarm", 5, "#00ff00")')).toBe('TypeError: fb.style.set() requires a style name, a property name and a value');
    for (const value of ['true', 'false', 'null', 'undefined', '{}', '["#00ff00"]', 'function () {}']) {
      expect(message(`fb.style.set("alarm", "text_color", ${value})`), value).toBe('TypeError: fb.style.set(): the value must be a string or a number');
    }
    // Not an integer.
    expect(message('fb.style.set("roomy", "text_line_space", 2.5)')).toBe('TypeError: fb.style.set(): not a valid value for "text_line_space"');
    expect(message('fb.style.set("roomy", "text_line_space", NaN)')).toBe('TypeError: fb.style.set(): not a valid value for "text_line_space"');
    // A C string ends at a NUL: what comes after it must not be dropped silently.
    expect(message('fb.style.set("alarm", "text_color\\0x", "#00ff00")')).toBe('TypeError: fb.style.set(): the property name has a NUL character in it');
    expect(message('fb.style.set("alarm", "text_color", "#00ff00\\0zz")')).toBe('TypeError: fb.style.set(): the value has a NUL character in it');
    expect(message('fb.style.set("alarm\\0x", "text_color", "#00ff00")')).toBe('ReferenceError: unknown style "alarm"');
    expect(look(emu)).toEqual(before);
  });
});

describe('setStyle on a key that config.json does not list', () => {
  it('throws InternalError, as setFont does: the key has no Button to style', async () => {
    // Key 4 (the bar's right key) has no entry in "buttons": no name, reached through fb.bars.
    const cfg = config();
    const { emu } = await start({ ...cfg, buttons: cfg.buttons.filter(b => b.position !== 4) });
    const message = (code: string): string => {
      const result = emu.eval(code);
      return result.ok ? `ok ${result.value}` : result.error.message;
    };
    expect(message('fb.bars[0].right.display.setStyle("alarm")')).toBe('InternalError: failed to set style for button 4');
    expect(message('fb.bars[0].right.display.setTopStyle("alarm")')).toBe('InternalError: failed to set style for button 4');
    expect(message('fb.bars[0].right.display.setStyle("")')).toBe('InternalError: failed to set style for button 4');
    // The argument is checked first, as on a key with a Button.
    expect(message('fb.bars[0].right.display.setStyle("nosuch")')).toBe('ReferenceError: unknown style "nosuch"');
    expect(message('fb.bars[0].right.display.setStyle("picked")')).toMatch(/^TypeError: .*sets a background/);
    // The key beside it is in the config.
    expect(message('fb.bars[0].left.display.setStyle("alarm")')).toBe('ok undefined');
    emu.render();
    expect(emu.state.key('light')!.display!.label.runs).toMatchObject([{ color: '#ff0000', font: 'montserrat', px: 32 }]);
  });
});

describe('a named style the LVGL pool has no room for', () => {
  // The firmware adds a named style to an object only while 4 KB + 128 B of the LVGL pool stay free.
  const LOW = 4096 + 64;
  /**
   * The firmware walks its widgets again "a little later": after 50 turns of its LVGL loop
   * (PendingRender.cpp), which on a device is a fraction of a second. Here a render is one turn.
   */
  const later = (emu: Emulator) => {
    for (let i = 0; i < 60; i++) emu.render();
  };

  it('a key shows without it, font and size included, and takes it when there is room again', async () => {
    const { emu, rec } = await start(config());
    expect(look(emu).blind.label).toBe(PLAIN.blind.label);
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    emu.eval('fb.key("blind").display.setStyle("loud")');
    // Not the style's size with the element's colour.
    expect(look(emu).blind.label).toBe(PLAIN.blind.label);
    later(emu);
    expect(look(emu).blind.label).toBe(PLAIN.blind.label);
    expect(rec.logs.filter(line => /named style was not added/.test(line))).toHaveLength(1);

    // Room again: the firmware tries again by itself, nothing else has to change.
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu).blind.label).toBe('#ff0000 montserrat 20');
    expect(rec.errors).toEqual([]);
  });

  it('a list shows without it and takes it when there is room again', async () => {
    const { emu } = await start(config());
    emu.render();
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    emu.eval('fb.main.list.setStyle("scenes", "loud"); fb.main.list.setFocusStyle("scenes", "semi")');
    expect(look(emu).list).toEqual(PLAIN.list);
    expect(listFonts(emu)).toEqual({ rows: 'montserrat 32', selected: 'montserrat 32' });
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu).list).toEqual({ ...PLAIN.list, color: '#ff0000', font: 'montserrat 20' });
    expect(listFonts(emu)).toEqual({ rows: 'montserrat 20', selected: 'montserrat_semibold 20' });
  });

  it('a display item shows without it and takes it when there is room again', async () => {
    const { emu } = await start(config());
    // Several formats: drawing the new text gives their labels back to the pool first.
    emu.eval('fb.main.display.setValue("co2", "\\\\cffffff6\\\\cffffff1\\\\cffffff2\\\\cffffff0\\\\cffffff0")');
    emu.render();
    emu.holdLvglMemory(emu.stats().lvglFree - LOW);
    emu.eval('fb.main.display.setStyle("co2", "loud"); fb.main.display.setValue("co2", "612")');
    emu.render();
    // Drawn or not (the pool is low), never with the style's size and the element's colour.
    const value = emu.state.item('co2')!.box!.value.runs;
    expect(value.every(r => r.px === 14 && r.color === '#ffffff')).toBe(true);
    expect([emu.state.item('co2')!.box!.captionPad, emu.state.item('co2')!.box!.boxPad]).toEqual([null, null]);
    emu.holdLvglMemory(0);
    later(emu);
    expect(look(emu).co2).toMatchObject({ value: '#ff0000 montserrat 20', unit: '#ff0000 montserrat 20' });
    expect([emu.state.item('co2')!.box!.captionPad, emu.state.item('co2')!.box!.boxPad]).toEqual([4, 3]);
  });
});
