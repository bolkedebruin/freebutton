import { describe, expect, it } from 'vitest';
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
