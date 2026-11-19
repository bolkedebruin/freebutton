import { describe, expect, it } from 'vitest';
import {
  fontChain, fontInstances, fontNames, fontStyles, fontTable, fontWarnings, findFace, hasFontTable, isSystemFontSrc, itemStyleList,
  slotFont, spanFont, styleNames, validateFonts, FACE_SYSTEM, FACE_SYSTEM_BOLD, SLOTS, SYSTEM_FONTS,
} from '../font-config';
import { DEFAULT_CONFIG_STYLES as DEFAULTS, StyleElement } from '../style-config';
import { TextSizeKind } from '../text-spans';

/** config.json with a "fonts" table. */
const withFonts = (fonts: unknown, rest: Record<string, unknown> = {}) => ({ fonts, ...rest });

describe('fontTable', () => {
  it('has the system fonts without a config; no fallback by default', () => {
    const t = fontTable({});
    expect(t.faces.map(f => [f.name, f.src, f.fallback, f.system])).toEqual([
      ['montserrat', 'medium.ttf', -1, true], ['montserrat_semibold', 'semibold.ttf', -1, true],
    ]);
    expect(SYSTEM_FONTS.map(f => f.name)).toEqual(['montserrat', 'montserrat_semibold']);
  });

  it('reads the top-level "fonts", not "style.fonts"', () => {
    expect(fontTable({ style: { fonts: [{ name: 'r', src: 'r.ttf' }] } }).faces).toHaveLength(2);
    expect(fontTable(withFonts([{ name: 'r', src: 'r.ttf' }])).faces).toHaveLength(3);
    expect(hasFontTable(withFonts([]))).toBe(true);
    expect(hasFontTable({ style: { fonts: [] } })).toBe(false);
    expect(hasFontTable({ fonts: {} })).toBe(false);
  });

  it('adds config faces after the system fonts; fallback absent or "" = none', () => {
    const t = fontTable(withFonts([
      { id: 1, name: 'mdi', src: 'icons.ttf' },
      { id: 2, name: 'mono', src: 'jb.ttf', fallback: '' },
      { name: 'sym', src: 'sym.otf', fallback: 1 },
    ]));
    expect(t.faces.slice(2).map(f => [f.index, f.name, f.id, f.fallback])).toEqual([
      [2, 'mdi', 1, -1], [3, 'mono', 2, -1], [4, 'sym', 0, 2],
    ]);
    expect(fontChain(t, 4)).toEqual([4, 2]);
  });

  it('a system font entry sets fallback / lineheight / id only; its src is ignored', () => {
    const t = fontTable(withFonts([
      { name: 'montserrat', src: 'inter.ttf', fallback: 'mdi', lineheight: 1.2, id: 9 },
      { name: 'montserrat_semibold', fallback: 'mdi' },
      { name: 'mdi', src: 'icons.ttf' },
    ]));
    expect(t.faces).toHaveLength(3);
    expect(t.faces[FACE_SYSTEM]).toMatchObject({ src: 'medium.ttf', configured: true, id: 9, fallback: 2, lineheight: 1.2 });
    expect(t.faces[FACE_SYSTEM_BOLD]).toMatchObject({ src: 'semibold.ttf', fallback: 2 });
    expect(fontChain(t, FACE_SYSTEM_BOLD)).toEqual([FACE_SYSTEM_BOLD, 2]);
  });

  it('cuts a chain at a cycle', () => {
    const t = fontTable(withFonts([{ name: 'montserrat', fallback: 'a' }, { name: 'a', src: 'a.ttf', fallback: 'montserrat' }]));
    expect(fontChain(t, FACE_SYSTEM)).toEqual([FACE_SYSTEM, 2]);
  });

  it('skips invalid and duplicate entries, keeps at most 8 faces', () => {
    const fonts = [
      { name: '1bad', src: 'a.ttf' }, { name: 'nosrc' }, { name: 'a', src: 'a.woff' }, { name: 'a', src: 'a.ttf', id: -1 },
      { name: 'a', src: 'a.ttf', id: 3 }, { name: 'a', src: 'b.ttf' }, { name: 'b', src: 'b.ttf', id: 3 },
      ...['c', 'd', 'e', 'f', 'g', 'h'].map(n => ({ name: n, src: `${n}.ttf` })),
    ];
    const t = fontTable(withFonts(fonts));
    expect(t.faces.map(f => f.name)).toEqual(['montserrat', 'montserrat_semibold', 'a', 'c', 'd', 'e', 'f', 'g']);
  });

  it('id 0 means no id (as on the device)', () => {
    expect(fontTable(withFonts([{ name: 'z', src: 'z.ttf', id: 0 }])).faces[2]).toMatchObject({ name: 'z', id: 0 });
  });

  it('an unknown fallback resolves to none; chains stop at 4 faces', () => {
    const t = fontTable(withFonts([
      { name: 'a', src: 'a.ttf', fallback: 'zz' },
      { name: 'b', src: 'b.ttf', fallback: 'c' }, { name: 'c', src: 'c.ttf', fallback: 'd' },
      { name: 'd', src: 'd.ttf', fallback: 'e' }, { name: 'e', src: 'e.ttf', fallback: 'montserrat' },
    ]));
    expect(t.faces[2]!.fallback).toBe(-1);
    expect(fontChain(t, 3).map(i => t.faces[i]!.name)).toEqual(['b', 'c', 'd', 'e']);
  });

  it('findFace by name or id', () => {
    const t = fontTable(withFonts([{ id: 4, name: 'x', src: 'x.ttf' }]));
    expect(findFace(t, 'x')).toBe(2);
    expect(findFace(t, 4)).toBe(2);
    expect(findFace(t, 'montserrat_semibold')).toBe(FACE_SYSTEM_BOLD);
    expect(findFace(t, 'icons')).toBe(-1);
    expect(findFace(t, 'medium')).toBe(-1);
    expect(findFace(t, 5)).toBe(-1);
    expect(findFace(t, '4')).toBe(-1);
  });

  it('system font files', () => {
    expect(isSystemFontSrc('medium.ttf')).toBe(true);
    expect(isSystemFontSrc('semibold.ttf')).toBe(true);
    expect(isSystemFontSrc('icons.ttf')).toBe(false);
  });
});

describe('the font of each part of an item', () => {
  const cfg = withFonts([{ id: 3, name: 'roboto', src: 'roboto.ttf' }], {
    styles: {
      displayitem: { 'font-size': 24, title: { 'font-family': 'roboto' } },
      list: { selected: { 'font-size': 40 } },
      big: { 'font-family': 'montserrat_semibold', 'font-size': 48 },
      semi: { 'font-family': 'montserrat_semibold' },
      small: { 'font-size': 20 },
      byid: { 'font-family': 3 },
      parts: { title: { 'font-size': 20 }, unit: { 'font-size': 10 }, selected: { 'font-family': 'roboto' } },
    },
  });
  const styles = fontStyles(cfg);

  it('the slots: a kind and a part, with that part\'s element', () => {
    expect(SLOTS.value).toEqual({ kind: 'displayitem', part: 'main', element: StyleElement.DisplayValue });
    expect(SLOTS.unit.element).toBe(StyleElement.DisplayUnit);
    expect(SLOTS.caption.element).toBe(StyleElement.DisplayCaption);
    expect(SLOTS.button.element).toBe(StyleElement.KeyLabel);
    expect(SLOTS.buttonTop.element).toBe(StyleElement.KeyTopLabel);
    expect(SLOTS.list.element).toBe(StyleElement.List);
    expect(SLOTS.listSelected.element).toBe(StyleElement.ListSelected);
  });

  it('without styles: the built-in look', () => {
    expect(slotFont(DEFAULTS, SLOTS.value)).toEqual({ face: FACE_SYSTEM, px: 14 });
    expect(slotFont(DEFAULTS, SLOTS.unit)).toEqual({ face: FACE_SYSTEM, px: 14 });
    expect(slotFont(DEFAULTS, SLOTS.caption)).toEqual({ face: FACE_SYSTEM_BOLD, px: 14 });
    expect(slotFont(DEFAULTS, SLOTS.button)).toEqual({ face: FACE_SYSTEM, px: 32 });
    expect(slotFont(DEFAULTS, SLOTS.buttonTop)).toEqual({ face: FACE_SYSTEM_BOLD, px: 18 });
    expect(slotFont(DEFAULTS, SLOTS.list)).toEqual({ face: FACE_SYSTEM, px: 32 });
    expect(slotFont(DEFAULTS, SLOTS.listSelected)).toEqual({ face: FACE_SYSTEM, px: 32 });
  });

  it('the kind\'s style, then the item\'s styles: face and size each from the last that sets it', () => {
    expect(slotFont(styles, SLOTS.value)).toEqual({ face: 0, px: 24 });
    expect(slotFont(styles, SLOTS.caption)).toEqual({ face: 2, px: 14 });
    expect(slotFont(styles, SLOTS.value, ['big'])).toEqual({ face: 1, px: 48 });
    expect(slotFont(styles, SLOTS.value, ['big', 'small'])).toEqual({ face: 1, px: 20 });
    expect(slotFont(styles, SLOTS.value, ['small', 'big'])).toEqual({ face: 1, px: 48 });
    expect(slotFont(styles, SLOTS.value, ['semi'])).toEqual({ face: 1, px: 24 });
    expect(slotFont(styles, SLOTS.value, ['byid'])).toEqual({ face: 2, px: 24 });
    // A main block does not reach the title.
    expect(slotFont(styles, SLOTS.caption, ['big'])).toEqual({ face: 2, px: 14 });
    expect(slotFont(styles, SLOTS.caption, ['parts'])).toEqual({ face: 2, px: 20 });
    expect(slotFont(styles, SLOTS.button, ['small'])).toEqual({ face: 0, px: 20 });
    expect(slotFont(styles, SLOTS.buttonTop, ['small', 'parts'])).toEqual({ face: 1, px: 20 });
  });

  it('the unit: what its "unit" blocks do not set is the value\'s', () => {
    expect(slotFont(styles, SLOTS.unit)).toEqual({ face: 0, px: 24 });
    expect(slotFont(styles, SLOTS.unit, ['big'])).toEqual({ face: 1, px: 48 });
    expect(slotFont(styles, SLOTS.unit, ['big', 'parts'])).toEqual({ face: 1, px: 10 });
    const own = fontStyles({ styles: { displayitem: { unit: { 'font-family': 'montserrat_semibold', 'font-size': 9 } }, big: { 'font-size': 48 } } });
    expect(slotFont(own, SLOTS.unit, ['big'])).toEqual({ face: 1, px: 9 });
  });

  it('the selected row: what its "selected" blocks do not set is the rows\'', () => {
    expect(slotFont(styles, SLOTS.listSelected)).toEqual({ face: 0, px: 40 });
    expect(slotFont(styles, SLOTS.listSelected, ['big'])).toEqual({ face: 1, px: 40 });
    expect(slotFont(styles, SLOTS.listSelected, ['small', 'parts'])).toEqual({ face: 2, px: 40 });
    expect(slotFont(DEFAULTS, SLOTS.listSelected, [])).toEqual({ face: 0, px: 32 });
    expect(slotFont(fontStyles({ styles: { small: { 'font-size': 20 } } }), SLOTS.listSelected, ['small'])).toEqual({ face: 0, px: 20 });
  });

  it('itemStyleList: the styles the device gives an entry', () => {
    expect(itemStyleList({ style: 'big small' }, styles)).toEqual(['big', 'small']);
    expect(itemStyleList({ style: 'big nosuch list big' }, styles)).toEqual(['big']);
    expect(itemStyleList({}, styles)).toEqual([]);
    expect(itemStyleList({ style: 5 }, styles)).toEqual([]);
  });

  it('spanFont: step 2 on a title = 18 px whatever the font', () => {
    const base = { face: 1, px: 14 };
    const span = (sizeKind: TextSizeKind, size: number, font = -1) => ({ font, sizeKind, size });
    expect(spanFont(base, SLOTS.caption, span(TextSizeKind.Step, 2))).toEqual({ face: 1, px: 18 });
    expect(spanFont(base, SLOTS.buttonTop, span(TextSizeKind.Step, 2, 0))).toEqual({ face: 0, px: 18 });
    expect(spanFont(base, SLOTS.value, span(TextSizeKind.Step, 2))).toEqual({ face: 1, px: 24 });
    expect(spanFont(base, SLOTS.value, span(TextSizeKind.Step, 7))).toEqual({ face: 1, px: 14 });
    expect(spanFont(base, SLOTS.value, span(TextSizeKind.Px, 40))).toEqual({ face: 1, px: 40 });
    expect(spanFont(base, SLOTS.value, span(TextSizeKind.Base, 0, 2))).toEqual({ face: 2, px: 14 });
  });
});

describe('validateFonts', () => {
  it('accepts a good table, styles and style lists', () => {
    expect(validateFonts(withFonts([
      { id: 1, name: 'roboto', src: 'roboto.ttf', fallback: 'mdi', lineheight: 1.2 },
      { id: 2, name: 'mono', src: 'jetbrains-mono.otf', fallback: '' },
      { name: 'mdi', src: 'icons.ttf' },
      { name: 'montserrat', fallback: 'mdi' },
      { name: 'montserrat_semibold', fallback: 'mdi', lineheight: 1.1 },
    ], {
      styles: {
        button: { 'font-size': 28, title: { 'font-family': 'mono', 'font-size': 16 } },
        big: { 'font-family': 'roboto', 'font-size': 40, title: { 'font-family': 2 } },
        alarm: { color: '#ff0000', icon: { color: '#ff0000' } },
        rows: { 'font-family': 1, 'line-height': 1.5, 'background-color': '#00000080', selected: { 'background-color': '#ffffff' } },
      },
      displayitems: [{ style: 'big alarm' }, { style: '' }, {}],
      buttons: [{ position: 3, style: 'alarm' }],
      lists: [{ name: 'l', style: 'rows  alarm' }],
    }))).toEqual([]);
  });

  it('a src on a system font is no error (a warning: fontWarnings)', () => {
    expect(validateFonts(withFonts([{ name: 'montserrat', src: 'inter.ttf' }]))).toEqual([]);
    expect(fontWarnings(withFonts([{ name: 'montserrat', src: 'inter.ttf' }]), null)).toEqual([
      expect.stringContaining('fonts[0]: "montserrat" is a system font (medium.ttf): its "src" is ignored'),
    ]);
  });

  it('entry format errors', () => {
    const p = validateFonts(withFonts([
      'x', {}, { name: '9a', src: 'a.ttf' }, { name: 'b', src: 'b.woff2' }, { name: 'c' },
      { name: 'd', src: 'd.ttf', id: 0 }, { name: 'e', src: 'e.ttf', id: 1.5 },
      { name: 'f', src: 'f.ttf', fallback: 7.5 }, { name: 'g', src: 'g.ttf', lineheight: 3 },
      { name: 'h', src: `${'h'.repeat(93)}.ttf` }, { name: 'i', src: 'a..b.ttf' }, { name: 'j', src: 'x.TTF' },
    ]));
    expect(p).toEqual([
      'fonts[0] must be an object',
      'fonts[1].name is required',
      expect.stringContaining('fonts[2].name "9a" is not a valid name'),
      expect.stringContaining('fonts[3].src must be a key'),
      expect.stringContaining('fonts[4].src is required'),
      'fonts[5].id must be an integer >= 1',
      'fonts[6].id must be an integer >= 1',
      expect.stringContaining('fonts[7].fallback must be'),
      'fonts[8].lineheight must be a number 0.8..2 (em)',
      expect.stringContaining('fonts[9].src must be a key'),
      expect.stringContaining('fonts[10].src must be a key'),
      expect.stringContaining('fonts[11].src must be a key'),
      expect.stringContaining('"fonts" adds 9 fonts'),
    ]);
  });

  it('"fonts" must be an array', () => {
    expect(validateFonts({ fonts: {} })[0]).toContain('"fonts" must be an array');
  });

  it('duplicate names and ids', () => {
    expect(validateFonts(withFonts([
      { id: 1, name: 'a', src: 'a.ttf' }, { id: 1, name: 'b', src: 'b.ttf' }, { name: 'a', src: 'c.ttf' },
      { name: 'montserrat', fallback: 'a' }, { name: 'montserrat', fallback: '' },
    ]))).toEqual([
      'fonts[1].id 1 is already used by font "a" (font ids must be unique)',
      'fonts[2].name "a" is already used (font names must be unique)',
      'fonts[4].name "montserrat": the system font is configured a second time',
    ]);
  });

  it('unknown fallback by name and id', () => {
    expect(validateFonts(withFonts([
      { name: 'a', src: 'a.ttf', fallback: 'nope' }, { name: 'b', src: 'b.ttf', fallback: 42 },
      { name: 'montserrat', fallback: 'icons' },
    ]))).toEqual([
      'font "montserrat": fallback "icons" is not a font',
      'font "a": fallback "nope" is not a font',
      'font "b": fallback id 42 is not a font',
    ]);
  });

  it('cycles, reported once', () => {
    expect(validateFonts(withFonts([
      { name: 'a', src: 'a.ttf', fallback: 'b' }, { id: 5, name: 'b', src: 'b.ttf', fallback: 'a' },
    ]))).toEqual(['fonts: fallback cycle a -> b -> a']);
    expect(validateFonts(withFonts([{ name: 'a', src: 'a.ttf', fallback: 'a' }]))).toEqual(['fonts: fallback cycle a -> a']);
    expect(validateFonts(withFonts([{ name: 'montserrat', fallback: 'a' }, { name: 'a', src: 'a.ttf', fallback: 'montserrat' }])))
      .toEqual(['fonts: fallback cycle montserrat -> a -> montserrat']);
  });

  it('chain longer than 4', () => {
    const p = validateFonts(withFonts([
      { name: 'a', src: 'a.ttf', fallback: 'b' }, { name: 'b', src: 'b.ttf', fallback: 'c' },
      { name: 'c', src: 'c.ttf', fallback: 'd' }, { name: 'd', src: 'd.ttf', fallback: 'montserrat' },
    ]));
    expect(p).toEqual(['font "a": fallback chain a -> b -> c -> d -> montserrat has 5 fonts (at most 4)']);
  });

  it('more than 6 added faces (system font entries do not count)', () => {
    const fonts = ['a', 'b', 'c', 'd', 'e', 'f'].map(n => ({ name: n, src: `${n}.ttf` }));
    expect(validateFonts(withFonts([...fonts, { name: 'montserrat', fallback: 'a' }]))).toEqual([]);
    expect(validateFonts(withFonts([...fonts, { name: 'g', src: 'g.ttf' }]))[0]).toContain('"fonts" adds 7 fonts');
  });

  it('the items\' "style": an unknown style, a reserved name, a name that is none, a fifth, not a string', () => {
    const cfg = {
      styles: { a: {}, b: {}, c: {}, d: {}, e: {} },
      buttons: [{ position: 1, style: 'a nosuch' }, { position: 2, style: 'button a' }, { position: 3, style: 7 }],
      displayitems: [{ style: 'a b c d e' }, { style: '1bad' }, { style: 'a a a a a b' }],
      lists: [{ name: 'l', style: 'list displayitem' }],
      // An image takes no style.
      images: [{ name: 'i', style: 'nosuch' }],
    };
    expect(validateFonts(cfg)).toEqual([
      'buttons[0].style: unknown style "nosuch", left out',
      'buttons[1].style: "button" is the style of a kind and cannot be named by an item, left out',
      'buttons[2].style: is not a string of style names, ignored',
      'displayitems[0].style: more than 4 styles, "e" left out',
      'displayitems[1].style: "1bad" is not a style name, left out',
      'lists[0].style: "list" is the style of a kind and cannot be named by an item, left out',
      'lists[0].style: "displayitem" is the style of a kind and cannot be named by an item, left out',
    ]);
    // Without "styles" every name is unknown.
    expect(validateFonts({ buttons: [{ position: 1, style: 'alarm' }] })).toEqual(['buttons[0].style: unknown style "alarm", left out']);
  });

  it('includes the problems inside "styles"; a style left out is an unknown one', () => {
    expect(validateFonts({
      styles: { '9x': {}, ok: { 'font-weight': 700, 'font-family': 'comic', color: 'red' } },
      displayitems: [{ style: '9x ok' }],
    })).toEqual([
      'styles: "9x" is not a style name (letters, digits, _; not starting with a digit; max 24), skipped',
      'styles.ok: unknown property "font-weight", ignored (there is no font-weight: a bold face is another font, chosen with "font-family")',
      'styles.ok.font-family: unknown font, ignored ("comic" is not montserrat, montserrat_semibold or a "fonts" entry)',
      'styles.ok.color: bad value, ignored (it takes a colour "#rrggbb")',
      'displayitems[0].style: "9x" is not a style name, left out',
    ]);
  });

  it('the keys the firmware no longer reads, each once, with what replaces it', () => {
    expect(validateFonts({
      style: { fonts: [], text: { text_color: '#ffffff' } },
      buttons: [{ position: 1, font: 'montserrat', fontsize: 3, topfont: 1, topfontsize: 2, topstyle: 'a', topLabel: 'T' }],
      displayitems: [{ fontsize: 2, labelfont: 'x', labelfontsize: 1, labelstyle: 'a', iconcolor: '#ffffff' }],
      lists: [{ name: 'l', color: 'ffffff', focuscolor: 'ffffff', focustextcolor: '000000', focusstyle: 'a', fontsize: 2 }],
      images: [{ name: 'i', color: '#ffffff' }],
    })).toEqual([
      '"style" is no longer read: use the top-level "fonts" and "styles"',
      '"font" is no longer read: use a style with "font-family" (buttons[0])',
      '"fontsize" is no longer read: use a style with "font-size" (buttons[0] and 2 more)',
      '"topfont" is no longer read: use a style with "title": {"font-family"} (buttons[0])',
      '"topfontsize" is no longer read: use a style with "title": {"font-size"} (buttons[0])',
      '"topstyle" is no longer read: use a style with a "title" block (buttons[0])',
      '"topLabel" is no longer read: use "toplabel" (buttons[0])',
      '"labelfont" is no longer read: use a style with "title": {"font-family"} (displayitems[0])',
      '"labelfontsize" is no longer read: use a style with "title": {"font-size"} (displayitems[0])',
      '"labelstyle" is no longer read: use a style with a "title" block (displayitems[0])',
      '"iconcolor" is no longer read: use a style with "icon": {"color"} (displayitems[0])',
      '"color" is no longer read: use a style with "color" (lists[0])',
      '"focuscolor" is no longer read: use a style with "selected": {"background-color"} (lists[0])',
      '"focustextcolor" is no longer read: use a style with "selected": {"color"} (lists[0])',
      '"focusstyle" is no longer read: use a style with a "selected" block (lists[0])',
    ]);
  });
});

describe('fontInstances and warnings', () => {
  /** The fonts every device has from boot: the firmware screens and the looks of the kinds. */
  const BOOT = ['montserrat@14', 'montserrat_semibold@14', 'montserrat@32', 'montserrat_semibold@18'];

  it('counts the fonts of the firmware screens and of the kinds', () => {
    expect([...fontInstances({})]).toEqual(BOOT);
    // A kind's own size replaces the built-in one.
    expect([...fontInstances({ styles: { button: { 'font-size': 20 }, list: { 'font-size': 20 } } })])
      .toEqual(['montserrat@14', 'montserrat_semibold@14', 'montserrat@20', 'montserrat_semibold@18']);
  });

  it('expands every font by its fallback chain', () => {
    const s = fontInstances(withFonts([
      { name: 'mdi', src: 'icons.ttf' }, { name: 'roboto', src: 'roboto.ttf', fallback: 'mdi' }, { name: 'montserrat', fallback: 'mdi' },
    ], {
      styles: { big: { 'font-family': 'roboto', 'font-size': 40 }, small: { 'font-size': 20 } },
      displayitems: [{ style: 'big', label: 'T', unit: 'C' }, { style: 'small' }],
    }));
    expect([...s].sort()).toEqual([
      'mdi@14', 'mdi@20', 'mdi@32', 'mdi@40', 'montserrat@14', 'montserrat@20', 'montserrat@32', 'montserrat_semibold@14',
      'montserrat_semibold@18', 'roboto@40',
    ]);
  });

  it('a style part with a font and a size counts, used or not; one of the two only where an item uses it', () => {
    const cfg = {
      styles: {
        unused: { 'font-family': 'montserrat_semibold', 'font-size': 60, title: { 'font-family': 'montserrat', 'font-size': 11 } },
        half: { 'font-size': 50 }, parts: { unit: { 'font-size': 10 }, selected: { 'font-size': 12 } },
      },
    };
    expect([...fontInstances(cfg)]).toEqual([...BOOT, 'montserrat_semibold@60', 'montserrat@11']);
    expect([...fontInstances({ ...cfg, buttons: [{ position: 1, style: 'half', toplabel: 'T' }, { position: 2, style: 'half' }] })])
      .toEqual([...BOOT, 'montserrat_semibold@60', 'montserrat@11', 'montserrat@50']);
    expect([...fontInstances({ ...cfg, displayitems: [{ style: 'parts', unit: 'C' }], lists: [{ name: 'l', style: 'parts' }] })])
      .toEqual([...BOOT, 'montserrat_semibold@60', 'montserrat@11', 'montserrat@10', 'montserrat@12']);
  });

  it('caption and unit escapes: \\s2 on a title = 18 px; a font switch', () => {
    const s = fontInstances(withFonts([{ name: 'r', src: 'r.ttf' }], {
      displayitems: [{ label: 'a\\s2b\\f{r}c', unit: '\\s{9}x' }],
    }));
    expect(s.has('montserrat_semibold@18')).toBe(true);
    expect(s.has('r@18')).toBe(true);
    expect(s.has('montserrat@9')).toBe(true);
    expect(s.has('montserrat_semibold@24')).toBe(false);
  });

  it('warns about missing bucket files and too many instances', () => {
    const styles: Record<string, unknown> = {};
    for (let i = 0; i < 25; i++) styles[`s${i}`] = { 'font-family': 'r', 'font-size': 10 + i };
    const cfg = withFonts([{ name: 'r', src: 'r.ttf' }, { name: 'mdi', src: 'icons.ttf' }], { styles });
    const w = fontWarnings(cfg, new Set(['medium.ttf', 'semibold.ttf', 'icons.ttf']));
    expect(w).toEqual([
      expect.stringContaining('font "r": r.ttf is not in'),
      'about 29 font instances (font x size, fallbacks included) are needed; the device holds 24, the rest draw the built-in font. Use fewer sizes or fonts.',
    ]);
    expect(fontWarnings(withFonts([{ name: 'r', src: 'r.ttf' }]), null)).toEqual([]);
    // The system fonts are not reported (always present); a config without "fonts" and "styles" is the built-in look.
    expect(fontWarnings({}, new Set())).toEqual([]);
  });

  it('warns about icons drawn by a system font without a fallback', () => {
    const icon = '\u{F0335}';
    const cfg = {
      buttons: [{ position: 1, label: `${icon} Lamp` }, { position: 2, label: 'x', toplabel: '\\u{F0D43}' }],
      displayitems: [{ label: 'Temp', unit: icon }],
      lists: [{ name: 'l', items: ['a', icon] }],
    };
    expect(fontWarnings(cfg, null)).toEqual([
      'font "montserrat" has no fallback: the icons in buttons[0].label (and 2 more) show as boxes. '
        + 'Add {"name": "montserrat", "fallback": "<icon font>"} to "fonts".',
      expect.stringContaining('font "montserrat_semibold" has no fallback: the icons in buttons[1].toplabel show as boxes'),
    ]);
    // With fallbacks, or icons in a config font: no warning.
    const ok = {
      fonts: [{ name: 'mdi', src: 'icons.ttf' }, { name: 'montserrat', fallback: 'mdi' }, { name: 'montserrat_semibold', fallback: 'mdi' }],
      buttons: cfg.buttons, displayitems: cfg.displayitems, lists: cfg.lists,
    };
    expect(fontWarnings(ok, null)).toEqual([]);
    expect(fontWarnings({ fonts: [{ name: 'mdi', src: 'icons.ttf' }], buttons: [{ position: 1, label: `\\f{mdi}${icon}` }] }, null)).toEqual([]);
    // The font a style gives the part is the one that draws.
    const styled = { ...ok, fonts: [{ name: 'mdi', src: 'icons.ttf' }], styles: { icons: { 'font-family': 'mdi', title: { 'font-family': 'mdi' }, unit: { 'font-family': 'mdi' } } } };
    expect(fontWarnings({ ...styled, buttons: cfg.buttons.map(b => ({ ...b, style: 'icons' })), displayitems: [{ unit: icon, style: 'icons' }], lists: [{ name: 'l', items: [icon], style: 'icons' }] }, null)).toEqual([]);
  });

  it('notes a property that has no effect where it is used: background-color and line-height', () => {
    const cfg = {
      styles: {
        button: { 'background-color': '#000000', title: { 'background-color': '#000000' } },
        displayitem: { 'line-height': 1.5, unit: { 'background-color': '#000000' } },
        list: { 'background-color': '#000000', 'line-height': 2, selected: { 'background-color': '#ffffff' } },
        panel: { 'background-color': '#0000ff80', selected: { 'background-color': '#00ff0080' } },
        roomy: { 'line-height': 1.5, title: { 'line-height': 1.5 } },
        unused: { 'background-color': '#ffffff' },
      },
      buttons: [{ position: 1, style: 'panel roomy' }, { position: 2, style: 'panel' }],
      displayitems: [{ style: 'roomy' }, { style: 'panel roomy' }],
      // A list draws both.
      lists: [{ name: 'l', style: 'panel roomy' }],
    };
    expect(fontWarnings(cfg, null)).toEqual([
      'styles.button: "background-color" has no effect on a button (a background is drawn for a list only)',
      'styles.button.title: "background-color" has no effect on a button (a background is drawn for a list only)',
      'styles.displayitem: "line-height" has no effect on a display item (it applies to lists and button labels)',
      'styles.displayitem.unit: "background-color" has no effect on a display item (a background is drawn for a list only)',
      'styles.panel: "background-color" has no effect on a button (buttons[0] uses the style; a background is drawn for a list only)',
      'styles.roomy: "line-height" has no effect on a display item (displayitems[0] uses the style; it applies to lists and button labels)',
      'styles.roomy.title: "line-height" has no effect on a display item (displayitems[0] uses the style; it applies to lists and button labels)',
      'styles.panel: "background-color" has no effect on a display item (displayitems[1] uses the style; a background is drawn for a list only)',
    ]);
    // Notes, not problems: the config can be saved.
    expect(validateFonts(cfg)).toEqual([]);
  });

  it('fontNames lists the system fonts, config names and ids', () => {
    expect(fontNames(withFonts([{ id: 2, name: 'r', src: 'r.ttf' }]))).toEqual({ fonts: ['montserrat', 'montserrat_semibold', 'r'], fontIds: [2] });
  });

  it('styleNames: every style, the kinds included, with the properties fb.style.set can set for it', () => {
    const names = styleNames({ styles: { alarm: { color: '#ff0000', title: { color: '#ff8080' } }, constructor: { opacity: 0.5 } } });
    expect(Object.keys(names)).toEqual(['button', 'displayitem', 'list', 'alarm', 'constructor']);
    const six = ['color', 'text-align', 'letter-spacing', 'text-decoration', 'opacity', 'background-color'];
    // Every property but font-family, font-size and line-height, in each block the style has; an icon colour.
    expect(names.alarm).toEqual([...six, ...six.map(n => `title.${n}`), 'icon.color']);
    expect(names.constructor).toEqual([...six, 'icon.color']);
    expect(names.list).toContain('selected.background-color');
    expect(names.list).not.toContain('icon.color');
    expect(names.list).not.toContain('font-size');
    // A name that is no style is not found, also one of Object.prototype.
    expect(Object.prototype.hasOwnProperty.call(names, 'toString')).toBe(false);
    expect(names.toString).toBeUndefined();
    expect(Object.keys(styleNames({}))).toEqual(['button', 'displayitem', 'list']);
  });
});
