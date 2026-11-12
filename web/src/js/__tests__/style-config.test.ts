// config.json "style" (style-config.ts): the same vectors as the firmware's
// test/test_style_logic (src/ui/style_logic.c), plus the JSON walk and its
// error messages.
import { describe, expect, it } from 'vitest';
import {
  configStyles, DECOR_NONE, DECOR_STRIKETHROUGH, DECOR_UNDERLINE, DEFAULT_STYLES, emptySheet, FACE_SYSTEM,
  FACE_SYSTEM_BOLD, isTitleElement, mergeStyle, parseStyle, parseStyleColor, resolveStyles, setStyleNumber,
  setStyleString, setStyleValue, styleDefaults, styleElement, styleItemPx, styleRole, styleRoleOf, StyleAlign,
  StyleElement, StyleRole, styleWarnings, type StyleProps,
} from '../style-config';
import type { FontResolver } from '../text-spans';

const E = StyleElement;

// A face table as fonts.c would have it: the system faces plus "mdi" (id 7).
const resolver: FontResolver = (name, isId, id) => {
  if (isId) return id === 7 ? 2 : -2;
  return ({ montserrat: FACE_SYSTEM, montserrat_semibold: FACE_SYSTEM_BOLD, mdi: 2 } as Record<string, number>)[name] ?? -2;
};

const text = (face: number, px: number, color: number): StyleProps => ({ face, px, color });

describe('defaults', () => {
  it('the defaults table (the look before "style")', () => {
    const d = styleDefaults();
    expect(d[E.KeyLabel]).toEqual(text(FACE_SYSTEM, 32, 0xffffff));
    expect(d[E.KeyTopLabel]).toEqual(text(FACE_SYSTEM_BOLD, 18, 0xffd700));
    expect(d[E.DisplayValue]).toEqual(text(FACE_SYSTEM, 14, 0xffffff));
    expect(d[E.DisplayUnit]).toEqual({ color: 0x999999 });
    expect(d[E.DisplayCaption]).toEqual(text(FACE_SYSTEM_BOLD, 14, 0xffd700));
    expect(d[E.List]).toEqual(text(FACE_SYSTEM, 32, 0x808080));
    expect(d[E.ListSelected]).toEqual({ color: 0x000000, bgColor: 0xffd700, bgOpa: 255 });
  });

  it('resolve without a style', () => {
    const d = styleDefaults();
    const r = resolveStyles(null);
    for (let el = 0; el < E.ListSelected; el++) expect(r[el]).toEqual(d[el]);
    // The selected row: the list's font and size, its own colours.
    expect(r[E.ListSelected]).toEqual({ face: FACE_SYSTEM, px: 32, color: 0x000000, bgColor: 0xffd700, bgOpa: 255 });
    expect(resolveStyles(emptySheet())).toEqual(r);
    expect(DEFAULT_STYLES).toEqual(r);
  });

  it('roles of elements', () => {
    expect([E.KeyLabel, E.KeyTopLabel, E.DisplayValue, E.DisplayUnit, E.DisplayCaption, E.List, E.ListSelected].map(styleRoleOf))
      .toEqual([StyleRole.Text, StyleRole.Title, StyleRole.Text, StyleRole.Text, StyleRole.Title, StyleRole.Text, StyleRole.Text]);
    expect(isTitleElement(E.KeyTopLabel)).toBe(true);
    expect(isTitleElement(E.List)).toBe(false);
  });
});

describe('cascade', () => {
  it('role then element', () => {
    const s = emptySheet();
    s.role[StyleRole.Text] = { color: 0x00ff00, letterSpace: 3 };
    s.element[E.KeyLabel] = { color: 0xff0000 };
    const r = resolveStyles(s);
    // The element beats the role; the role's other properties stay.
    expect(r[E.KeyLabel]).toEqual({ face: FACE_SYSTEM, px: 32, color: 0xff0000, letterSpace: 3 });
    // The role beats the defaults on every "text" element.
    expect(r[E.DisplayValue]).toEqual({ face: FACE_SYSTEM, px: 14, color: 0x00ff00, letterSpace: 3 });
    expect(r[E.List]).toEqual({ face: FACE_SYSTEM, px: 32, color: 0x00ff00, letterSpace: 3 });
    expect(r[E.DisplayUnit]!.color).toBe(0x00ff00);
    // "title" elements are untouched.
    expect(r[E.KeyTopLabel]).toEqual(text(FACE_SYSTEM_BOLD, 18, 0xffd700));
    expect(r[E.DisplayCaption]).toEqual(text(FACE_SYSTEM_BOLD, 14, 0xffd700));
  });

  it('text_font from the role, text_size from the element', () => {
    const s = emptySheet();
    s.role[StyleRole.Title] = { face: 3 };
    s.element[E.KeyTopLabel] = { px: 24 };
    const r = resolveStyles(s);
    expect(r[E.KeyTopLabel]).toEqual(text(3, 24, 0xffd700));
    expect(r[E.DisplayCaption]).toEqual(text(3, 14, 0xffd700));
    expect(r[E.KeyLabel]).toEqual(text(FACE_SYSTEM, 32, 0xffffff));
  });

  it('the unit takes its font and size from its element only', () => {
    const s = emptySheet();
    s.role[StyleRole.Text] = { face: 3, px: 20, align: StyleAlign.Right };
    let r = resolveStyles(s);
    // The role's font reaches the value, not the unit (it takes the value's).
    expect(r[E.DisplayValue]).toEqual({ face: 3, px: 20, color: 0xffffff, align: StyleAlign.Right });
    expect(r[E.DisplayUnit]).toEqual({ color: 0x999999, align: StyleAlign.Right });
    // The unit element sets a size of its own (and only that) ...
    s.element[E.DisplayUnit] = { px: 12 };
    r = resolveStyles(s);
    expect(r[E.DisplayUnit]).toEqual({ px: 12, color: 0x999999, align: StyleAlign.Right });
    // ... and a font.
    s.element[E.DisplayUnit] = { px: 12, face: FACE_SYSTEM_BOLD };
    r = resolveStyles(s);
    expect(r[E.DisplayUnit]).toEqual({ face: FACE_SYSTEM_BOLD, px: 12, color: 0x999999, align: StyleAlign.Right });
  });

  it('list.selected inherits the list', () => {
    const s = emptySheet();
    s.element[E.List] = { px: 24, color: 0x00ff00, letterSpace: 2 };
    s.role[StyleRole.Text] = { face: 3 };
    let r = resolveStyles(s);
    expect(r[E.List]).toEqual({ face: 3, px: 24, color: 0x00ff00, letterSpace: 2 });
    // The selected row: the list's text properties, its own colour and bg.
    expect(r[E.ListSelected]).toEqual({ face: 3, px: 24, color: 0x000000, letterSpace: 2, bgColor: 0xffd700, bgOpa: 255 });
    // list.selected's own properties win.
    s.element[E.ListSelected] = { bgColor: 0x123456, px: 28, color: 0xffffff };
    r = resolveStyles(s);
    expect(r[E.ListSelected]).toEqual({ face: 3, px: 28, color: 0xffffff, letterSpace: 2, bgColor: 0x123456, bgOpa: 255 });
    expect(r[E.List]!.px).toBe(24);
  });

  it('bg stays on list.selected', () => {
    const s = emptySheet();
    s.role[StyleRole.Text] = { bgColor: 0x111111 };
    s.element[E.List] = { bgOpa: 10 };
    const r = resolveStyles(s);
    expect(r[E.KeyLabel]!.bgColor).toBeUndefined();
    expect(r[E.List]!.bgOpa).toBeUndefined();
    expect(r[E.ListSelected]).toMatchObject({ bgColor: 0xffd700, bgOpa: 255 });
  });

  it('merge', () => {
    const a: StyleProps = { color: 1, px: 14 };
    mergeStyle(a, { color: 2, decor: DECOR_UNDERLINE, lineSpace: -4, opa: 128 });
    expect(a).toEqual({ color: 2, px: 14, decor: DECOR_UNDERLINE, lineSpace: -4, opa: 128 });
  });
});

describe('item layer', () => {
  it('styleItemPx', () => {
    expect(styleItemPx(1, false, 99)).toBe(14);
    expect(styleItemPx(2, false, 99)).toBe(24);
    expect(styleItemPx(2, true, 99)).toBe(18); // a title's step 2
    expect(styleItemPx(1, true, 99)).toBe(14);
    expect(styleItemPx(3, true, 99)).toBe(32);
    expect(styleItemPx(4, false, 99)).toBe(48);
    expect(styleItemPx(5, true, 99)).toBe(72);
    expect(styleItemPx(6, true, 99)).toBe(6);
    expect(styleItemPx(40, false, 99)).toBe(40);
    expect(styleItemPx(72, false, 99)).toBe(72);
    expect(styleItemPx(0, true, 18)).toBe(18);
    expect(styleItemPx(73, false, 32)).toBe(32);
    expect(styleItemPx(-1, false, 32)).toBe(32);
    expect(styleItemPx(2.5, false, 32)).toBe(32);
    expect(styleItemPx(NaN, false, 32)).toBe(32);
  });
});

describe('lookup', () => {
  it('elements and roles', () => {
    expect(styleElement('key', 'label')).toBe(E.KeyLabel);
    expect(styleElement('key', 'toplabel')).toBe(E.KeyTopLabel);
    expect(styleElement('display', 'value')).toBe(E.DisplayValue);
    expect(styleElement('display', 'unit')).toBe(E.DisplayUnit);
    expect(styleElement('display', 'caption')).toBe(E.DisplayCaption);
    expect(styleElement('list', null)).toBe(E.List);
    expect(styleElement('list', 'selected')).toBe(E.ListSelected);
    expect(styleElement('key', null)).toBe(-1);
    expect(styleElement('display', null)).toBe(-1);
    expect(styleElement('key', 'value')).toBe(-1);
    expect(styleElement('list', 'label')).toBe(-1);
    expect(styleElement('button', 'label')).toBe(-1);
    expect(styleElement('key', 'toString')).toBe(-1);
    expect(styleRole('text')).toBe(StyleRole.Text);
    expect(styleRole('title')).toBe(StyleRole.Title);
    expect(styleRole('Text')).toBe(-1);
    expect(styleRole('')).toBe(-1);
  });
});

describe('properties', () => {
  const str = (p: StyleProps, prop: string, v: string, bg = false) => setStyleString(p, prop, v, bg, resolver);
  const num = (p: StyleProps, prop: string, v: number, bg = false) => setStyleNumber(p, prop, v, bg, resolver);

  it('valid properties', () => {
    const p: StyleProps = {};
    expect(str(p, 'text_font', 'montserrat_semibold')).toBe('ok');
    expect(p.face).toBe(FACE_SYSTEM_BOLD);
    expect(num(p, 'text_font', 7)).toBe('ok'); // a config font id
    expect(p.face).toBe(2);
    expect(num(p, 'text_size', 6)).toBe('ok');
    expect(num(p, 'text_size', 72)).toBe('ok');
    expect(p.px).toBe(72);
    expect(str(p, 'text_color', '#ffd700')).toBe('ok');
    expect(p.color).toBe(0xffd700);
    expect(num(p, 'text_opa', 0)).toBe('ok');
    expect(num(p, 'text_opa', 255)).toBe('ok');
    expect(p.opa).toBe(255);
    expect(str(p, 'text_align', 'left')).toBe('ok');
    expect(p.align).toBe(StyleAlign.Left);
    expect(str(p, 'text_align', 'right')).toBe('ok');
    expect(str(p, 'text_align', 'center')).toBe('ok');
    expect(p.align).toBe(StyleAlign.Center);
    expect(num(p, 'text_letter_space', -100)).toBe('ok');
    expect(p.letterSpace).toBe(-100);
    expect(num(p, 'text_line_space', 100)).toBe('ok');
    expect(p.lineSpace).toBe(100);
    expect(str(p, 'text_decor', 'underline strikethrough')).toBe('ok');
    expect(p.decor).toBe(DECOR_UNDERLINE | DECOR_STRIKETHROUGH);
    expect(str(p, 'text_decor', 'strikethrough')).toBe('ok');
    expect(p.decor).toBe(DECOR_STRIKETHROUGH);
    expect(str(p, 'text_decor', 'none')).toBe('ok');
    expect(p.decor).toBe(DECOR_NONE);
    expect(Object.keys(p).sort()).toEqual(['align', 'color', 'decor', 'face', 'letterSpace', 'lineSpace', 'opa', 'px']);
    // bg on list.selected
    const s: StyleProps = {};
    expect(str(s, 'bg_color', '000000', true)).toBe('ok');
    expect(num(s, 'bg_opa', 128, true)).toBe('ok');
    expect(s).toEqual({ bgColor: 0, bgOpa: 128 });
  });

  it('errors leave the props unchanged', () => {
    const p: StyleProps = { px: 20, color: 0xabcdef };
    const before = { ...p };
    // unknown property
    expect(str(p, 'color', '#ffffff')).toBe('unknown');
    expect(num(p, 'fontsize', 3)).toBe('unknown');
    expect(str(p, 'text_colour', '#ffffff')).toBe('unknown');
    // bg outside list.selected
    expect(str(p, 'bg_color', '#ffffff')).toBe('unknown');
    expect(num(p, 'bg_opa', 255)).toBe('unknown');
    // bad colours
    expect(str(p, 'text_color', '#fff')).toBe('bad-value');
    expect(str(p, 'text_color', 'red')).toBe('bad-value');
    expect(str(p, 'text_color', '#gg0000')).toBe('bad-value');
    expect(str(p, 'bg_color', '#1234567', true)).toBe('bad-value');
    // sizes out of range
    expect(num(p, 'text_size', 5)).toBe('bad-value');
    expect(num(p, 'text_size', 73)).toBe('bad-value');
    expect(num(p, 'text_size', 0)).toBe('bad-value');
    expect(num(p, 'text_size', 20.5)).toBe('bad-value');
    // other ranges
    expect(num(p, 'text_opa', 256)).toBe('bad-value');
    expect(num(p, 'text_opa', -1)).toBe('bad-value');
    expect(num(p, 'bg_opa', 300, true)).toBe('bad-value');
    expect(num(p, 'text_letter_space', 101)).toBe('bad-value');
    expect(num(p, 'text_line_space', -101)).toBe('bad-value');
    // bad keywords
    expect(str(p, 'text_align', 'middle')).toBe('bad-value');
    expect(str(p, 'text_align', 'Left')).toBe('bad-value');
    expect(str(p, 'text_decor', 'overline')).toBe('bad-value');
    // wrong JSON type
    expect(str(p, 'text_size', '32')).toBe('bad-value');
    expect(num(p, 'text_color', 0xffffff)).toBe('bad-value');
    expect(num(p, 'text_align', 1)).toBe('bad-value');
    expect(setStyleValue(p, 'text_color', null, false, resolver)).toBe('bad-value');
    expect(setStyleValue(p, 'text_colour', true, false, resolver)).toBe('unknown');
    // fonts
    expect(str(p, 'text_font', 'roboto')).toBe('unknown-font');
    expect(str(p, 'text_font', 'medium')).toBe('unknown-font'); // the old built-in name
    expect(num(p, 'text_font', 8)).toBe('unknown-font');
    expect(num(p, 'text_font', 0)).toBe('unknown-font');
    expect(str(p, 'text_font', '')).toBe('bad-value');
    expect(p).toEqual(before);
  });

  it('parse colour', () => {
    expect(parseStyleColor('#ffd700')).toBe(0xffd700);
    expect(parseStyleColor('808080')).toBe(0x808080);
    expect(parseStyleColor('ABCDEF')).toBe(0xabcdef);
    expect(parseStyleColor('#000000')).toBe(0);
    for (const bad of ['', '#', '#fff', '12345', '1234567', '##123456', '#12345g', ' 123456', '0x123456', null, 0xffffff]) {
      expect(parseStyleColor(bad)).toBeNull();
    }
  });
});

describe('parseStyle (config.json "style")', () => {
  const PLAN_STYLE = {
    fonts: [
      { id: 1, name: 'mdi', src: 'icons.ttf' },
      { name: 'montserrat', fallback: 'mdi' },
      { name: 'montserrat_semibold', fallback: 'mdi' },
    ],
    text: { text_font: 'montserrat', text_color: '#ffffff' },
    title: { text_font: 'montserrat_semibold', text_color: '#ffd700' },
    key: { label: { text_size: 32 }, toplabel: { text_size: 18 } },
    display: { value: { text_size: 14 }, unit: { text_color: '#999999' }, caption: { text_size: 14 } },
    list: { text_size: 32, text_color: '#808080', selected: { bg_color: '#ffd700', text_color: '#000000' } },
  };

  it('the plan\'s example "style" resolves to the defaults', () => {
    const parsed = parseStyle({ style: PLAN_STYLE }, resolver);
    expect(parsed.errors).toEqual([]);
    expect(parsed.present).toBe(true);
    expect(resolveStyles(parsed.sheet)).toEqual(DEFAULT_STYLES);
  });

  it('fills roles and elements', () => {
    const { sheet, errors } = parseStyle({ style: {
      title: { text_color: '00ff00', text_font: 7 },
      key: { label: { text_size: 40, text_align: 'left' } },
      list: { text_opa: 128, selected: { bg_opa: 100 } },
    } }, resolver);
    expect(errors).toEqual([]);
    expect(sheet.role[StyleRole.Title]).toEqual({ color: 0x00ff00, face: 2 });
    expect(sheet.element[E.KeyLabel]).toEqual({ px: 40, align: StyleAlign.Left });
    expect(sheet.element[E.List]).toEqual({ opa: 128 });
    expect(sheet.element[E.ListSelected]).toEqual({ bgOpa: 100 });
    const r = configStyles({ style: { title: { text_color: '#00ff00' } } }, resolver);
    expect(r[E.KeyTopLabel]!.color).toBe(0x00ff00);
    expect(r[E.DisplayCaption]!.color).toBe(0x00ff00);
  });

  it('no style / not an object', () => {
    expect(parseStyle({}, resolver)).toEqual({ sheet: emptySheet(), errors: [], present: false });
    expect(parseStyle({ style: [] }, resolver).errors).toEqual([expect.stringContaining('"style" must be an object')]);
  });

  it('errors: unknown section, element and property; bad values; unknown fonts; bg outside selected', () => {
    const { sheet, errors } = parseStyle({ style: {
      fonts: 'ignored here',
      txt: {},
      text: { text_colour: '#fff', text_size: 80, text_color: '#00ff00' },
      title: 'gold',
      key: { label: { text_font: 'roboto' }, bottom: {} },
      display: { caption: { bg_color: '#000000' }, value: { text_align: 'middle', text_decor: 'overline', text_opa: 300 } },
      list: { selected: { bg_opa: 'full', text_letter_space: 200 }, text_font: 0 },
    } }, resolver, 'montserrat, montserrat_semibold or a "style.fonts" entry');
    expect(errors).toEqual([
      'style.txt is not a style section (fonts, text, title, key, display, list)',
      expect.stringContaining('style.text.text_colour is not a style property (text_font, text_size, text_color,'),
      'style.text.text_size must be an integer 6..72 (px)',
      expect.stringContaining('style.title must be an object of style properties'),
      'style.key.label.text_font "roboto" is not a font (montserrat, montserrat_semibold or a "style.fonts" entry)',
      'style.key.bottom is not an element of "key" (label, toplabel)',
      'style.display.caption.bg_color: bg_color and bg_opa are allowed in style.list.selected only',
      'style.display.value.text_align must be "left", "center" or "right"',
      'style.display.value.text_decor must be "none", "underline", "strikethrough" or "underline strikethrough"',
      'style.display.value.text_opa must be an integer 0..255',
      expect.stringContaining('style.list.text_font 0 is not a font'),
      'style.list.selected.bg_opa must be an integer 0..255',
      'style.list.selected.text_letter_space must be an integer -100..100 (px)',
    ]);
    // What the device keeps: the valid properties.
    expect(sheet.role[StyleRole.Text]).toEqual({ color: 0x00ff00 });
    expect(sheet.element[E.KeyLabel]).toEqual({});
  });

  it('"key" / "display" / "list.selected" must be objects', () => {
    expect(parseStyle({ style: { key: [], display: 1, list: { selected: 'x' } } }, resolver).errors).toEqual([
      'style.key must be an object of elements (label, toplabel)',
      'style.display must be an object of elements (value, unit, caption)',
      expect.stringContaining('style.list.selected must be an object'),
    ]);
  });
});

describe('styleWarnings', () => {
  it('no style; a top-level "fonts"', () => {
    expect(styleWarnings({ style: {} })).toEqual([]);
    expect(styleWarnings({})).toEqual([expect.stringContaining('no "style"')]);
    expect(styleWarnings({ style: {}, fonts: [] })).toEqual([expect.stringContaining('move "fonts" into "style"')]);
    expect(styleWarnings(null)).toEqual([]);
  });
});
