import { describe, expect, it } from 'vitest';
import { fontNamedStyles, itemUnitFont, listSelectedFont, namedStyleOf, styleNames } from '../font-config';
import { DEFAULT_STYLES as DEFAULTS, StyleElement } from '../style-config';
import {
  fontChain, fontInstances, fontNames, fontsizePx, fontStyles, fontTable, fontWarnings, findFace, hasFontTable,
  isSystemFontSrc, slotFont, spanFont, unitFont, validateFonts, FACE_SYSTEM, FACE_SYSTEM_BOLD, SLOTS, SYSTEM_FONTS,
} from '../font-config';
import { TextSizeKind } from '../text-spans';

/** config.json with a "style.fonts" table. */
const withFonts = (fonts: unknown, rest: Record<string, unknown> = {}) => ({ style: { fonts }, ...rest });

describe('fontTable', () => {
  it('has the system fonts without a config; no fallback by default', () => {
    const t = fontTable({});
    expect(t.faces.map(f => [f.name, f.src, f.fallback, f.system])).toEqual([
      ['montserrat', 'medium.ttf', -1, true], ['montserrat_semibold', 'semibold.ttf', -1, true],
    ]);
    expect(SYSTEM_FONTS.map(f => f.name)).toEqual(['montserrat', 'montserrat_semibold']);
  });

  it('reads style.fonts, not a top-level "fonts"', () => {
    expect(fontTable({ fonts: [{ name: 'r', src: 'r.ttf' }] }).faces).toHaveLength(2);
    expect(fontTable(withFonts([{ name: 'r', src: 'r.ttf' }])).faces).toHaveLength(3);
    expect(hasFontTable(withFonts([]))).toBe(true);
    expect(hasFontTable({ fonts: [] })).toBe(false);
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

describe('sizes', () => {
  it('fontsizePx: steps, px, fallback', () => {
    expect([1, 2, 3, 4, 5].map(s => fontsizePx(s, 0))).toEqual([14, 24, 32, 48, 72]);
    expect(fontsizePx(6, 0)).toBe(6);
    expect(fontsizePx(72, 0)).toBe(72);
    expect(fontsizePx(73, 9)).toBe(9);
    expect(fontsizePx(0, 9)).toBe(9);
    expect(fontsizePx(2.5, 9)).toBe(9);
    expect(fontsizePx(undefined, 9)).toBe(9);
    expect(fontsizePx('40', 9)).toBe(40);
  });

  it('slotFont: the item layer over the style defaults', () => {
    const t = fontTable(withFonts([{ id: 1, name: 'roboto', src: 'r.ttf' }]));
    const f = (face: number, px: number, configured = false) => ({ face, px, configured });
    expect(slotFont(t, {}, SLOTS.value)).toEqual(f(FACE_SYSTEM, 14));
    expect(slotFont(t, { fontsize: 3 }, SLOTS.value)).toEqual(f(FACE_SYSTEM, 32));
    expect(slotFont(t, { fontsize: 2 }, SLOTS.value)).toEqual(f(FACE_SYSTEM, 24));
    expect(slotFont(t, {}, SLOTS.caption)).toEqual(f(FACE_SYSTEM_BOLD, 14));
    // Title step 2 = 18 px (as before "style").
    expect(slotFont(t, { labelfontsize: 2 }, SLOTS.caption)).toEqual(f(FACE_SYSTEM_BOLD, 18));
    expect(slotFont(t, { labelfontsize: 3 }, SLOTS.caption)).toEqual(f(FACE_SYSTEM_BOLD, 32));
    expect(slotFont(t, {}, SLOTS.button)).toEqual(f(FACE_SYSTEM, 32));
    expect(slotFont(t, {}, SLOTS.buttonTop)).toEqual(f(FACE_SYSTEM_BOLD, 18));
    expect(slotFont(t, { topfontsize: 2 }, SLOTS.buttonTop)).toEqual(f(FACE_SYSTEM_BOLD, 18));
    expect(slotFont(t, {}, SLOTS.list)).toEqual(f(FACE_SYSTEM, 32));
    expect(slotFont(t, { font: 'roboto', fontsize: 40 }, SLOTS.value)).toEqual(f(2, 40, true));
    expect(slotFont(t, { font: 1, fontsize: 2 }, SLOTS.value)).toEqual(f(2, 24, true));
    expect(slotFont(t, { labelfont: 'roboto' }, SLOTS.caption)).toEqual(f(2, 14, true));
    expect(slotFont(t, { labelfont: 'roboto', labelfontsize: 2 }, SLOTS.caption)).toEqual(f(2, 18, true));
    expect(slotFont(t, { topfont: 1, topfontsize: 2 }, SLOTS.buttonTop)).toEqual(f(2, 18, true));
    expect(slotFont(t, { topfont: 'montserrat', topfontsize: 20 }, SLOTS.buttonTop)).toEqual(f(FACE_SYSTEM, 20, true));
    expect(slotFont(t, { fontsize: 40 }, SLOTS.value)).toEqual(f(FACE_SYSTEM, 40));
    // An unknown font keeps the style's face; 0 / out of range keep its size.
    expect(slotFont(t, { font: 'nope', fontsize: 0 }, SLOTS.value)).toEqual(f(FACE_SYSTEM, 14));
    expect(slotFont(t, { fontsize: 99 }, SLOTS.button)).toEqual(f(FACE_SYSTEM, 32));
  });

  it('slotFont: the style gives what the item does not set', () => {
    const cfg = withFonts([{ name: 'roboto', src: 'r.ttf' }], {});
    (cfg.style as Record<string, unknown>).text = { text_font: 'roboto' };
    (cfg.style as Record<string, unknown>).key = { label: { text_size: 40 } };
    const t = fontTable(cfg);
    const styles = fontStyles(cfg, t);
    expect(slotFont(t, {}, SLOTS.button, styles)).toEqual({ face: 2, px: 40, configured: false });
    expect(slotFont(t, { fontsize: 20 }, SLOTS.button, styles)).toEqual({ face: 2, px: 20, configured: false });
    expect(slotFont(t, { font: 'montserrat' }, SLOTS.button, styles)).toEqual({ face: FACE_SYSTEM, px: 40, configured: true });
    // The title role is untouched.
    expect(slotFont(t, {}, SLOTS.buttonTop, styles)).toEqual({ face: FACE_SYSTEM_BOLD, px: 18, configured: false });
  });

  it('unitFont: the value\'s unless style.display.unit sets font or size', () => {
    const value = { face: 3, px: 40, configured: true };
    expect(unitFont(value)).toEqual(value);
    const t = fontTable({});
    const styles = fontStyles({ style: { display: { unit: { text_size: 20 } } } }, t);
    expect(unitFont(value, styles)).toEqual({ face: 3, px: 20, configured: true });
    const styles2 = fontStyles({ style: { display: { unit: { text_font: 'montserrat_semibold' } } } }, t);
    expect(unitFont(value, styles2)).toEqual({ face: FACE_SYSTEM_BOLD, px: 40, configured: true });
  });

  it('spanFont: step 2 on a title = 18 px whatever the font', () => {
    const base = { face: FACE_SYSTEM_BOLD, px: 14, configured: false };
    const step = (size: number, font = -1) => ({ font, sizeKind: TextSizeKind.Step, size });
    expect(spanFont(base, SLOTS.caption, step(2))).toEqual({ face: FACE_SYSTEM_BOLD, px: 18 });
    expect(spanFont(base, SLOTS.caption, step(3))).toEqual({ face: FACE_SYSTEM_BOLD, px: 32 });
    expect(spanFont(base, SLOTS.caption, step(7))).toEqual({ face: FACE_SYSTEM_BOLD, px: 14 });
    expect(spanFont(base, SLOTS.caption, step(2, 4))).toEqual({ face: 4, px: 18 });
    expect(spanFont({ face: 4, px: 20, configured: true }, SLOTS.buttonTop, step(2))).toEqual({ face: 4, px: 18 });
    expect(spanFont(base, SLOTS.value, step(2))).toEqual({ face: FACE_SYSTEM_BOLD, px: 24 });
    expect(spanFont(base, SLOTS.caption, { font: -1, sizeKind: TextSizeKind.Px, size: 30 })).toEqual({ face: FACE_SYSTEM_BOLD, px: 30 });
    expect(spanFont(base, SLOTS.caption, { font: 4, sizeKind: TextSizeKind.Base, size: 0 })).toEqual({ face: 4, px: 14 });
  });
});

describe('validateFonts', () => {
  it('accepts a good table and references', () => {
    expect(validateFonts(withFonts([
      { id: 1, name: 'roboto', src: 'roboto.ttf', fallback: 'mdi', lineheight: 1.2 },
      { id: 2, name: 'mono', src: 'jetbrains-mono.otf', fallback: '' },
      { name: 'mdi', src: 'icons.ttf' },
      { name: 'montserrat', fallback: 'mdi' },
      { name: 'montserrat_semibold', fallback: 'mdi', lineheight: 1.1 },
    ], {
      displayitems: [{ font: 'roboto', fontsize: 40, labelfont: 2, labelfontsize: 1 }],
      buttons: [{ position: 3, font: 1, fontsize: 28, topfont: 'mono', topfontsize: 16 }],
      lists: [{ name: 'l', font: 'montserrat_semibold', fontsize: 72 }],
    }))).toEqual([]);
  });

  it('a src on a system font is no error (a warning: fontWarnings)', () => {
    expect(validateFonts(withFonts([{ name: 'montserrat', src: 'inter.ttf' }]))).toEqual([]);
    expect(fontWarnings(withFonts([{ name: 'montserrat', src: 'inter.ttf' }]), null)).toEqual([
      expect.stringContaining('style.fonts[0]: "montserrat" is a system font (medium.ttf): its "src" is ignored'),
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
      'style.fonts[0] must be an object',
      'style.fonts[1].name is required',
      expect.stringContaining('style.fonts[2].name "9a" is not a valid name'),
      expect.stringContaining('style.fonts[3].src must be a key'),
      expect.stringContaining('style.fonts[4].src is required'),
      'style.fonts[5].id must be an integer >= 1',
      'style.fonts[6].id must be an integer >= 1',
      expect.stringContaining('style.fonts[7].fallback must be'),
      'style.fonts[8].lineheight must be a number 0.8..2 (em)',
      expect.stringContaining('style.fonts[9].src must be a key'),
      expect.stringContaining('style.fonts[10].src must be a key'),
      expect.stringContaining('style.fonts[11].src must be a key'),
      expect.stringContaining('"style.fonts" adds 9 fonts'),
    ]);
  });

  it('"style.fonts" must be an array; a top-level "fonts" is not checked', () => {
    expect(validateFonts({ style: { fonts: {} } })[0]).toContain('"style.fonts" must be an array');
    expect(validateFonts({ fonts: {} })).toEqual([]);
  });

  it('duplicate names and ids', () => {
    expect(validateFonts(withFonts([
      { id: 1, name: 'a', src: 'a.ttf' }, { id: 1, name: 'b', src: 'b.ttf' }, { name: 'a', src: 'c.ttf' },
      { name: 'montserrat', fallback: 'a' }, { name: 'montserrat', fallback: '' },
    ]))).toEqual([
      'style.fonts[1].id 1 is already used by font "a" (font ids must be unique)',
      'style.fonts[2].name "a" is already used (font names must be unique)',
      'style.fonts[4].name "montserrat": the system font is configured a second time',
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
    ]))).toEqual(['style.fonts: fallback cycle a -> b -> a']);
    expect(validateFonts(withFonts([{ name: 'a', src: 'a.ttf', fallback: 'a' }]))).toEqual(['style.fonts: fallback cycle a -> a']);
    expect(validateFonts(withFonts([{ name: 'montserrat', fallback: 'a' }, { name: 'a', src: 'a.ttf', fallback: 'montserrat' }])))
      .toEqual(['style.fonts: fallback cycle montserrat -> a -> montserrat']);
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
    expect(validateFonts(withFonts([...fonts, { name: 'g', src: 'g.ttf' }]))[0]).toContain('"style.fonts" adds 7 fonts');
  });

  it('item references and sizes', () => {
    expect(validateFonts(withFonts([{ id: 3, name: 'x', src: 'x.ttf' }], {
      displayitems: [{ font: 'y', labelfont: 4, fontsize: 0, labelfontsize: 73 }],
      mqttdisplays: [{ font: true }],
      buttons: [{ position: 1, font: 'x', topfont: 'montserrat_semibold', fontsize: 2.5, topfontsize: '18' }],
      lists: [{ font: 'montserrat', fontsize: 40 }, { fontsize: 80 }, { font: 'medium' }],
    }))).toEqual([
      expect.stringContaining('displayitems[0].font "y" is not a font (montserrat, montserrat_semibold or a "style.fonts" entry)'),
      expect.stringContaining('displayitems[0].labelfont 4 is not a font'),
      expect.stringContaining('displayitems[0].fontsize must be an integer 1..72'),
      expect.stringContaining('displayitems[0].labelfontsize must be an integer 1..72'),
      'mqttdisplays[0].font must be a font name or id',
      expect.stringContaining('buttons[0].fontsize must be an integer 1..72'),
      expect.stringContaining('buttons[0].topfontsize must be an integer 1..72'),
      expect.stringContaining('lists[2].font "medium" is not a font'),
    ]);
  });

  it('includes the style errors', () => {
    expect(validateFonts({ style: { key: { label: { text_color: 'red', text_font: 'nope' } } } })).toEqual([
      'style.key.label.text_color must be a colour "#rrggbb"',
      'style.key.label.text_font "nope" is not a font (montserrat, montserrat_semibold or a "style.fonts" entry)',
    ]);
  });
});

describe('fontInstances and warnings', () => {
  it('counts the system fonts of the firmware screens', () => {
    expect([...fontInstances({})].sort()).toEqual(['montserrat@14', 'montserrat_semibold@14']);
  });

  it('expands item fonts by their chains', () => {
    const cfg = withFonts([
      { name: 'mdi', src: 'icons.ttf' },
      { name: 'montserrat_semibold', fallback: 'mdi' },
      { name: 'mono', src: 'm.ttf', fallback: '' }, { name: 'r', src: 'r.ttf', fallback: 'mono' },
    ], {
      displayitems: [
        { fontsize: 3, label: 'Temp' },
        { font: 'r', fontsize: 40, unit: '\\s{20}°C', labelfontsize: 2 },
      ],
      buttons: [{ position: 1, label: 'a', toplabel: 'b' }, { position: 2, label: 'c' }],
      lists: [{ name: 'l', font: 'mono', fontsize: 5 }],
    });
    expect([...fontInstances(cfg)].sort()).toEqual([
      'mdi@14', 'mdi@18',
      'mono@20', 'mono@40', 'mono@72',
      'montserrat@14', 'montserrat@32',
      'montserrat_semibold@14', 'montserrat_semibold@18',
      'r@20', 'r@40',
    ]);
  });

  it('the style\'s fonts and sizes count; a unit with its own size; list.selected with its own font', () => {
    const cfg = {
      style: {
        fonts: [{ name: 'r', src: 'r.ttf' }],
        text: { text_font: 'r' },
        key: { label: { text_size: 40 } },
        display: { unit: { text_size: 10 } },
        list: { selected: { text_font: 'montserrat_semibold' } },
      },
      displayitems: [{ unit: 'x' }],
      buttons: [{ position: 1, label: 'a' }],
      lists: [{ name: 'l' }],
    };
    expect([...fontInstances(cfg)].sort()).toEqual([
      'montserrat@14', 'montserrat_semibold@14', 'montserrat_semibold@32', 'r@10', 'r@14', 'r@32', 'r@40',
    ]);
  });

  it('caption escapes: \\s2 on a title in its own font = 18 px', () => {
    const s = fontInstances({ displayitems: [{ label: 'a\\s2b\\s3c' }] });
    expect(s.has('montserrat_semibold@18')).toBe(true);
    expect(s.has('montserrat_semibold@32')).toBe(true);
    expect(s.has('montserrat_semibold@24')).toBe(false);
  });

  it('warns about missing bucket files and too many instances', () => {
    const cfg = withFonts([{ name: 'r', src: 'r.ttf' }, { name: 'mdi', src: 'icons.ttf' }], {
      displayitems: Array.from({ length: 30 }, (_, i) => ({ font: 'r', fontsize: 10 + i })),
    });
    const w = fontWarnings(cfg, new Set(['medium.ttf', 'semibold.ttf', 'icons.ttf']));
    expect(w).toEqual([
      expect.stringContaining('font "r": r.ttf is not in'),
      expect.stringContaining('font instances'),
    ]);
    expect(fontWarnings(withFonts([{ name: 'r', src: 'r.ttf' }]), null)).toEqual([]);
    // The system fonts are not reported (always present).
    expect(fontWarnings({ style: {} }, new Set())).toEqual([]);
  });

  it('warns about no "style" and a top-level "fonts"', () => {
    expect(fontWarnings({}, null)).toEqual([expect.stringContaining('config.json has no "style"')]);
    expect(fontWarnings({ style: {}, fonts: [] }, null)).toEqual([expect.stringContaining('move "fonts" into "style"')]);
  });

  it('warns about icons drawn by a system font without a fallback', () => {
    const icon = '\u{F0335}';
    const cfg = {
      style: {},
      buttons: [{ position: 1, label: `${icon} Lamp` }, { position: 2, label: 'x', toplabel: '\\u{F0D43}' }],
      displayitems: [{ label: 'Temp', unit: icon }],
      lists: [{ name: 'l', items: ['a', icon] }],
    };
    expect(fontWarnings(cfg, null)).toEqual([
      'font "montserrat" has no fallback: the icons in buttons[0].label (and 2 more) show as boxes. '
        + 'Add {"name": "montserrat", "fallback": "<icon font>"} to "style.fonts".',
      expect.stringContaining('font "montserrat_semibold" has no fallback: the icons in buttons[1].toplabel show as boxes'),
    ]);
    // With fallbacks, or icons in a config font: no warning.
    const ok = {
      style: { fonts: [{ name: 'mdi', src: 'icons.ttf' }, { name: 'montserrat', fallback: 'mdi' }, { name: 'montserrat_semibold', fallback: 'mdi' }] },
      buttons: cfg.buttons, displayitems: cfg.displayitems, lists: cfg.lists,
    };
    expect(fontWarnings(ok, null)).toEqual([]);
    expect(fontWarnings({ style: { fonts: [{ name: 'mdi', src: 'icons.ttf' }] }, buttons: [{ position: 1, label: `\\f{mdi}${icon}` }] }, null)).toEqual([]);
  });

  it('fontNames lists the system fonts, config names and ids', () => {
    expect(fontNames(withFonts([{ id: 2, name: 'r', src: 'r.ttf' }]))).toEqual({ fonts: ['montserrat', 'montserrat_semibold', 'r'], fontIds: [2] });
  });
});

describe('named styles ("style.styles") in the font cascade and the validation', () => {
  const E = StyleElement;
  const cfg = (rest: Record<string, unknown> = {}, style: Record<string, unknown> = {}) => ({
    style: {
      fonts: [{ name: 'r', src: 'r.ttf' }],
      styles: {
        face: { text_font: 'r' }, size: { text_size: 20 }, both: { text_font: 'montserrat_semibold', text_size: 40 },
        colour: { text_color: '#ff0000' }, bar: { bg_color: '#00ff00' }, barOpa: { bg_opa: 9, text_size: 18 }, empty: {},
      },
      ...style,
    },
    ...rest,
  });
  const fontOf = (c: Record<string, unknown>, raw: Record<string, unknown>, slot: keyof typeof SLOTS) => {
    const t = fontTable(c);
    const f = slotFont(t, raw, SLOTS[slot], fontStyles(c, t), fontNamedStyles(c, t));
    return `${t.faces[f.face]!.name} ${f.px}`;
  };

  it('namedStyleOf: the style a field names, as the device takes it', () => {
    const named = fontNamedStyles(cfg());
    expect(namedStyleOf({ style: 'size' }, 'style', E.KeyLabel, named)).toEqual({ px: 20 });
    expect(namedStyleOf({ style: 'empty' }, 'style', E.KeyLabel, named)).toEqual({});
    for (const raw of [{}, { style: '' }, { style: 'nosuch' }, { style: 'Size' }, { style: 3 }, { style: null }, { style: ['size'] }]) {
      expect(namedStyleOf(raw, 'style', E.KeyLabel, named), JSON.stringify(raw)).toBeUndefined();
    }
    // bg_*: a list only.
    expect(namedStyleOf({ style: 'bar' }, 'style', E.DisplayValue, named)).toBeUndefined();
    expect(namedStyleOf({ topstyle: 'barOpa' }, 'topstyle', E.KeyTopLabel, named)).toBeUndefined();
    expect(namedStyleOf({ style: 'bar' }, 'style', E.List, named)).toEqual({ bgColor: 0x00ff00 });
    expect(namedStyleOf({ focusstyle: 'bar' }, 'focusstyle', E.ListSelected, named)).toEqual({ bgColor: 0x00ff00 });
  });

  it('slotFont: face = item, else named, else element; px likewise', () => {
    const c = cfg();
    // key label: element montserrat 32
    expect(fontOf(c, {}, 'button')).toBe('montserrat 32');
    expect(fontOf(c, { style: 'colour' }, 'button')).toBe('montserrat 32');
    expect(fontOf(c, { style: 'face' }, 'button')).toBe('r 32');
    expect(fontOf(c, { style: 'size' }, 'button')).toBe('montserrat 20');
    expect(fontOf(c, { style: 'both' }, 'button')).toBe('montserrat_semibold 40');
    expect(fontOf(c, { style: 'both', fontsize: 2 }, 'button')).toBe('montserrat_semibold 24');
    expect(fontOf(c, { style: 'both', font: 'r' }, 'button')).toBe('r 40');
    expect(fontOf(c, { style: 'both', font: 'r', fontsize: 9 }, 'button')).toBe('r 9');
    // each slot reads its own field
    expect(fontOf(c, { style: 'both' }, 'buttonTop')).toBe('montserrat_semibold 18');
    expect(fontOf(c, { topstyle: 'both' }, 'buttonTop')).toBe('montserrat_semibold 40');
    // a title's step 2 is 18 px, over a named size too
    expect(fontOf(c, { topstyle: 'both', topfontsize: 2 }, 'buttonTop')).toBe('montserrat_semibold 18');
    expect(fontOf(c, { labelstyle: 'size' }, 'caption')).toBe('montserrat_semibold 20');
    expect(fontOf(c, { style: 'size' }, 'caption')).toBe('montserrat_semibold 14');
    expect(fontOf(c, { style: 'face' }, 'value')).toBe('r 14');
    expect(fontOf(c, { style: 'size' }, 'list')).toBe('montserrat 20');
    // a style the part cannot use, or that is not there: as without the field
    expect(fontOf(c, { style: 'barOpa' }, 'button')).toBe('montserrat 32');
    expect(fontOf(c, { style: 'barOpa' }, 'list')).toBe('montserrat 18');
    expect(fontOf(c, { style: 'nosuch' }, 'button')).toBe('montserrat 32');
    // no named styles given: the element's
    expect(slotFont(fontTable(c), { style: 'both' }, SLOTS.button)).toEqual({ face: FACE_SYSTEM, px: 32, configured: false });
  });

  it('the unit: the item, else the named style, else display.unit where it sets one, else the value\'s', () => {
    const unit = (c: Record<string, unknown>, raw: Record<string, unknown>) => {
      const t = fontTable(c);
      const styles = fontStyles(c, t);
      const named = fontNamedStyles(c, t);
      const u = itemUnitFont(t, raw, slotFont(t, raw, SLOTS.value, styles, named), styles, named);
      return `${t.faces[u.face]!.name} ${u.px}`;
    };
    const plain = cfg();
    const own = cfg({}, { display: { unit: { text_font: 'r', text_size: 10 } } });
    expect(unit(plain, {})).toBe('montserrat 14');
    expect(unit(plain, { style: 'both' })).toBe('montserrat_semibold 40');
    expect(unit(own, {})).toBe('r 10');
    // the named style goes over display.unit, property by property
    expect(unit(own, { style: 'both' })).toBe('montserrat_semibold 40');
    expect(unit(own, { style: 'size' })).toBe('r 20');
    expect(unit(own, { style: 'face' })).toBe('r 10');
    expect(unit(own, { style: 'colour' })).toBe('r 10');
    // the item's font / fontsize are the value's and the unit's
    expect(unit(own, { fontsize: 3 })).toBe('r 32');
    expect(unit(own, { font: 'montserrat' })).toBe('montserrat 10');
    expect(unit(own, { style: 'both', font: 'montserrat', fontsize: 2 })).toBe('montserrat 24');
    // unitFont itself, as the preview calls it
    const value = { face: 0, px: 24, configured: false };
    expect(unitFont(value, DEFAULTS)).toEqual(value);
    expect(unitFont(value, DEFAULTS, { px: 40 })).toEqual({ face: 0, px: 40, configured: false });
    expect(unitFont(value, DEFAULTS, { face: 1, px: 40 }, { size: true })).toEqual({ face: 1, px: 24, configured: true });
  });

  // The firmware's rule (style_logic_list_selected_font, test/test_style_logic): the same cases.
  it('listSelectedFont: the rows\' font, list.selected\'s own over it, the focus style\'s font / size on top', () => {
    const font = { face: 0, px: 32, configured: false };
    expect(listSelectedFont(font)).toEqual(font);
    expect(listSelectedFont(font, DEFAULTS, { color: 1 })).toEqual(font);
    expect(listSelectedFont(font, DEFAULTS, { px: 18 })).toEqual({ ...font, px: 18 });
    expect(listSelectedFont(font, DEFAULTS, { face: 1, px: 18 })).toEqual({ ...font, face: 1, px: 18 });
    // The rows have an item font (face 2 at 24 px): a focus style with one of the two keeps the other.
    const rows = { face: 2, px: 24, configured: true };
    expect(listSelectedFont(rows)).toEqual(rows);
    expect(listSelectedFont(rows, DEFAULTS, { face: 5 })).toEqual({ ...rows, face: 5 });
    expect(listSelectedFont(rows, DEFAULTS, { px: 48 })).toEqual({ ...rows, px: 48 });
    expect(listSelectedFont(rows, DEFAULTS, { face: 5, px: 48 })).toEqual({ ...rows, face: 5, px: 48 });
    // list.selected with a size of its own keeps its font: face and px both, whatever the rows have.
    const styles = fontStyles({ style: { list: { selected: { text_size: 40 } } } });
    expect(listSelectedFont(font, styles)).toEqual({ ...font, px: 40 });
    expect(listSelectedFont(rows, styles)).toEqual({ ...rows, face: 0, px: 40 });
    expect(listSelectedFont(font, styles, { px: 18 })).toEqual({ ...font, px: 18 });
    expect(listSelectedFont(rows, styles, { px: 18 })).toEqual({ ...rows, face: 0, px: 18 });
    expect(listSelectedFont(rows, styles, { face: 5 })).toEqual({ ...rows, face: 5, px: 40 });
    // list.selected that names the list's own font and size has none of its own.
    const same = fontStyles({ style: { list: { text_size: 30, selected: { text_size: 30 } } } });
    expect(listSelectedFont(rows, same)).toEqual(rows);
  });

  it('validateFonts: the six references must name a style of "style.styles"', () => {
    const good = cfg({
      buttons: [{ position: 1, style: 'both', topstyle: 'colour' }, { position: 2, style: '', topstyle: 'empty' }],
      displayitems: [{ style: 'size', labelstyle: 'face' }],
      lists: [{ name: 'l', style: 'bar', focusstyle: 'barOpa' }, { name: 'm', style: 'colour', focusstyle: 'colour' }],
    });
    expect(validateFonts(good)).toEqual([]);

    const bad = cfg({
      buttons: [{ position: 1, style: 'nosuch', topstyle: 'Colour' }],
      mqttbuttons: [{ position: 2, style: 3 }],
      displayitems: [{ style: ['size'], labelstyle: 'missing' }],
      lists: [{ name: 'l', style: 'gone', focusstyle: true }],
    });
    const have = '("style.styles" has face, size, both, colour, bar, barOpa, empty)';
    expect(validateFonts(bad)).toEqual([
      'displayitems[0].style must be the name of a "style.styles" entry',
      `displayitems[0].labelstyle "missing" is not a named style ${have}`,
      `buttons[0].style "nosuch" is not a named style ${have}`,
      `buttons[0].topstyle "Colour" is not a named style ${have}`,
      'mqttbuttons[0].style must be the name of a "style.styles" entry',
      `lists[0].style "gone" is not a named style ${have}`,
      'lists[0].focusstyle must be the name of a "style.styles" entry',
    ]);
    // Without "style.styles" every reference is one to an undefined style.
    expect(validateFonts({ style: {}, buttons: [{ position: 1, style: 'alarm' }] })).toEqual([
      'buttons[0].style "alarm" is not a named style (config.json has no "style.styles")',
    ]);
    expect(validateFonts({ lists: [{ name: 'l', focusstyle: 'picked' }] })).toEqual([
      'lists[0].focusstyle "picked" is not a named style (config.json has no "style.styles")',
    ]);
  });

  it('validateFonts: a style that sets bg_color / bg_opa on a key or a display item', () => {
    const c = cfg({
      buttons: [{ position: 1, style: 'bar', topstyle: 'barOpa' }],
      displayitems: [{ style: 'bar', labelstyle: 'bar' }],
      lists: [{ name: 'l', style: 'bar', focusstyle: 'barOpa' }],
    });
    expect(validateFonts(c)).toEqual([
      expect.stringMatching(/^displayitems\[0\]\.style "bar": the style sets bg_color, which only a list can use .*the device ignores the field$/),
      expect.stringMatching(/^displayitems\[0\]\.labelstyle "bar": the style sets bg_color, which only a list can use/),
      expect.stringMatching(/^buttons\[0\]\.style "bar": the style sets bg_color, which only a list can use/),
      expect.stringMatching(/^buttons\[0\]\.topstyle "barOpa": the style sets bg_opa, which only a list can use/),
    ]);
  });

  it('validateFonts: includes the problems inside "style.styles"; a style left out is an undefined one', () => {
    const styles: Record<string, unknown> = { '2bad': {}, a: { text_color: 'red', nope: 1 } };
    for (let i = 1; i <= 16; i++) styles[`s${i}`] = {};
    expect(validateFonts({ style: { styles }, buttons: [{ position: 1, style: '2bad', topstyle: 's16' }] })).toEqual([
      expect.stringContaining('style.styles: "2bad" is not a valid style name'),
      'style.styles.a.text_color must be a colour "#rrggbb"',
      expect.stringContaining('style.styles.a.nope is not a style property'),
      'style.styles has 17 named styles; the device holds 16 ("s16" and the rest are left out)',
      expect.stringContaining('buttons[0].style "2bad" is not a named style ("style.styles" has a, s1, '),
      expect.stringContaining('buttons[0].topstyle "s16" is not a named style'),
    ]);
  });

  it('fontInstances: a named style with a font and a size counts, used or not; the others where an item uses them', () => {
    const unused = cfg({ buttons: [{ position: 1, label: 'a' }] });
    // "both" (montserrat_semibold 40): the device creates its font at boot. "face", "size" and
    // "barOpa" set one of the two: no font until a part uses them.
    expect([...fontInstances(unused)].sort()).toEqual(['montserrat@14', 'montserrat@32', 'montserrat_semibold@14', 'montserrat_semibold@40']);
    expect([...fontInstances({ ...unused, style: { ...(unused as { style: object }).style, styles: {} } })].sort())
      .toEqual(['montserrat@14', 'montserrat@32', 'montserrat_semibold@14']);
    // With a fallback chain the style's font counts once per face of the chain.
    expect([...fontInstances({
      style: { fonts: [{ name: 'a', src: 'a.ttf', fallback: 'montserrat' }], styles: { s: { text_font: 'a', text_size: 21 } } },
    })].sort()).toEqual(['a@21', 'montserrat@14', 'montserrat@21', 'montserrat_semibold@14']);
    const used = cfg({
      buttons: [{ position: 1, label: 'a', toplabel: 't', style: 'both', topstyle: 'size' }],
      displayitems: [{ label: 'c', unit: 'u', style: 'face', labelstyle: 'both' }],
      lists: [{ name: 'l', style: 'size', focusstyle: 'barOpa' }],
    });
    expect([...fontInstances(used)].sort()).toEqual([
      'montserrat@14', 'montserrat@18', 'montserrat@20', 'montserrat_semibold@14', 'montserrat_semibold@20',
      'montserrat_semibold@40', 'r@14',
    ]);
    // A style a key cannot use adds nothing.
    expect(fontInstances(cfg({ buttons: [{ position: 1, label: 'a', style: 'barOpa' }] }))).toEqual(fontInstances(unused));
  });

  it('styleNames: each named style with the properties it sets', () => {
    expect(styleNames(cfg())).toEqual({
      face: ['text_font'], size: ['text_size'], both: ['text_font', 'text_size'], colour: ['text_color'], bar: ['bg_color'],
      barOpa: ['text_size', 'bg_opa'], empty: [],
    });
    expect(styleNames({})).toEqual({});
    expect(styleNames({ style: { styles: {} } })).toEqual({});
  });

  it('styleNames: a style may have the name of an Object.prototype member; a name that is no style is not found', () => {
    // As config.json gives it: JSON.parse makes "__proto__" an own property.
    const config = JSON.parse('{"style":{"styles":{"__proto__":{"text_color":"#ff0000"},"constructor":{"text_opa":9},"toString":{}}}}');
    const names = styleNames(config);
    expect(Object.keys(names).sort()).toEqual(['__proto__', 'constructor', 'toString']);
    expect(names['__proto__']).toEqual(['text_color']);
    expect(names.constructor).toEqual(['text_opa']);
    expect(names.toString).toEqual([]);
    // No inherited members: a lookup of any other name gives nothing.
    const plain = styleNames({ style: { styles: { alarm: { text_color: '#ff0000' } } } });
    for (const name of ['constructor', 'toString', 'hasOwnProperty', '__proto__', 'valueOf']) expect(plain[name], name).toBeUndefined();
    expect(Object.keys(plain)).toEqual(['alarm']);
  });
});
