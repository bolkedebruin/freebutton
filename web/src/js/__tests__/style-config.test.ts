/**
 * config.json "styles", the IDE's copy of the firmware's rules
 * (src/ui/style_logic.c, src/ui/box_logic.h, src/components/StyleRef.h and
 * the style loader of ConfigManager.cpp). The cases follow the native tests
 * (test/test_style_logic); mirror-firmware.test.ts compares the results
 * with the firmware itself in the emulator.
 */
import { describe, expect, it } from 'vitest';
import {
  boxPadding, boxTextAlign, configStyles, DECOR_LINE_THROUGH, DECOR_NONE, DECOR_UNDERLINE, DEFAULT_CONFIG_STYLES, iconStyleValue,
  lookupIconColor, lookupIconStyle, lookupPart, mergeStyle, paddingStep, parseItemStyle, parseScriptStyleList, parseStyleBackground,
  parseStyleColor, parseStyles, removedKey, removedKeyProblems, resolveStyles, scaledNumber, scriptSetStatus, setStyleNumber,
  setStyleString, setStyleValue, splitStyleProp, STYLE_ITEM_MAX, STYLE_MAX, STYLE_NAMED_MAX, STYLE_PROPERTIES, StyleAlign, styleBgOpa,
  styleColorHex, styleDefaults, StyleElement, styleElement, styleFollowFont, styleIconColor, styleItemPx, styleKind,
  styleLayersSet, styleLineSpace, styleListAdd, styleListNames, styleListRemove, styleLookup, stylePropScale,
  stylePropertyNames, styleSelectedRow, styleSettable, styleSubBlock, styleTextAlign, styleValueRule, scriptSetNoPartMessage,
  STYLE_SCRIPT_PROPERTIES, type StyleProps,
} from '../style-config';
import { TextAlign, type FontResolver } from '../text-spans';

/** montserrat 0, montserrat_semibold 1, "extra" 2 with id 7. */
const resolve: FontResolver = (name, isId, id) => {
  if (isId) return id === 7 ? 2 : -1;
  return ['montserrat', 'montserrat_semibold', 'extra'].indexOf(name);
};
const E = StyleElement;

describe('kinds, parts and the built-in look', () => {
  it('the reserved names are the kinds; the sub-blocks are title, unit, selected and icon', () => {
    expect(['button', 'displayitem', 'list', 'Button', 'key', ''].map(styleKind)).toEqual(['button', 'displayitem', 'list', null, null, null]);
    expect(['title', 'unit', 'selected', 'icon', 'main', 'color'].map(styleSubBlock)).toEqual(['title', 'unit', 'selected', 'icon', null, null]);
  });

  it('the parts a kind has', () => {
    expect(styleElement('button', 'main')).toBe(E.KeyLabel);
    expect(styleElement('button', 'title')).toBe(E.KeyTopLabel);
    expect(styleElement('button', 'unit')).toBe(-1);
    expect(styleElement('button', 'selected')).toBe(-1);
    expect(styleElement('displayitem', 'main')).toBe(E.DisplayValue);
    expect(styleElement('displayitem', 'title')).toBe(E.DisplayCaption);
    expect(styleElement('displayitem', 'unit')).toBe(E.DisplayUnit);
    expect(styleElement('displayitem', 'selected')).toBe(-1);
    expect(styleElement('list', 'main')).toBe(E.List);
    expect(styleElement('list', 'selected')).toBe(E.ListSelected);
    expect(styleElement('list', 'title')).toBe(-1);
  });

  it('the defaults: the look without "styles"', () => {
    const d = styleDefaults();
    expect(d[E.KeyLabel]).toEqual({ face: 0, px: 32, color: 0xffffff });
    expect(d[E.KeyTopLabel]).toEqual({ face: 1, px: 18, color: 0xffd700 });
    expect(d[E.DisplayValue]).toEqual({ face: 0, px: 14, color: 0xffffff });
    // The unit has no font of its own.
    expect(d[E.DisplayUnit]).toEqual({ color: 0x999999 });
    expect(d[E.DisplayCaption]).toEqual({ face: 1, px: 14, color: 0xffd700 });
    expect(d[E.List]).toEqual({ face: 0, px: 32, color: 0x808080, lineHeight: 125 });
    // The selected row has no font either; its background is opaque gold.
    expect(d[E.ListSelected]).toEqual({ color: 0x000000, bg: 0xffffd700 });
    expect(resolveStyles(null)).toEqual(d);
    expect(DEFAULT_CONFIG_STYLES.elements).toEqual(d);
    expect(DEFAULT_CONFIG_STYLES.named.size).toBe(0);
  });

  it('a kind\'s style changes its own part only: a title, the unit and the selected row take nothing from main', () => {
    const { styles } = parseStyles({
      styles: {
        button: { color: '#00ffff', 'font-size': 24 },
        displayitem: { 'font-size': 20, 'font-family': 'montserrat_semibold', color: '#112233' },
        list: { color: '#ffffff', 'font-size': 20 },
      },
    }, resolve);
    expect(styles.elements[E.KeyLabel]).toEqual({ face: 0, px: 24, color: 0x00ffff });
    expect(styles.elements[E.KeyTopLabel]).toEqual(styleDefaults()[E.KeyTopLabel]);
    expect(styles.elements[E.DisplayValue]).toEqual({ face: 1, px: 20, color: 0x112233 });
    expect(styles.elements[E.DisplayUnit]).toEqual({ color: 0x999999 });
    expect(styles.elements[E.DisplayCaption]).toEqual(styleDefaults()[E.DisplayCaption]);
    expect(styles.elements[E.List]).toEqual({ face: 0, px: 20, color: 0xffffff, lineHeight: 125 });
    expect(styles.elements[E.ListSelected]).toEqual({ color: 0x000000, bg: 0xffffd700 });
  });

  it('mergeStyle: the destination takes every property the source sets', () => {
    expect(mergeStyle({ color: 1, px: 14 }, { px: 20, opa: 0 })).toEqual({ color: 1, px: 20, opa: 0 });
  });

  it('the size of a "\\s" step in a text: 1..5, step 2 of a title = 18, else px', () => {
    expect([1, 2, 3, 4, 5].map(s => styleItemPx(s, false, 9))).toEqual([14, 24, 32, 48, 72]);
    expect([1, 2, 3].map(s => styleItemPx(s, true, 9))).toEqual([14, 18, 32]);
    expect([6, 40, 72, 0, 73, -1, 2.5].map(s => styleItemPx(s, false, 9))).toEqual([6, 40, 72, 9, 9, 9, 9]);
  });
});

describe('the property parser', () => {
  const str = (prop: string, value: string) => {
    const p: StyleProps = {};
    return [setStyleString(p, prop, value, resolve), p] as const;
  };
  const num = (prop: string, value: number) => {
    const p: StyleProps = {};
    return [setStyleNumber(p, prop, value, resolve), p] as const;
  };
  const any = (prop: string, value: unknown) => {
    const p: StyleProps = {};
    return [setStyleValue(p, prop, value, resolve), p] as const;
  };

  it('the property names are CSS\'s, in the firmware\'s order', () => {
    expect(STYLE_PROPERTIES).toEqual([
      'color', 'font-family', 'font-size', 'text-align', 'letter-spacing', 'text-decoration', 'opacity', 'line-height', 'background-color',
    ]);
  });

  it('how a number is passed: an integer, hundredths, or not at all', () => {
    expect(['font-size', 'letter-spacing', 'font-family'].map(stylePropScale)).toEqual([1, 1, 1]);
    expect(['opacity', 'line-height'].map(stylePropScale)).toEqual([100, 100]);
    expect(['color', 'text-align', 'text-decoration', 'background-color', 'font-weight', 'text_size', ''].map(stylePropScale))
      .toEqual([0, 0, 0, 0, 0, 0, 0]);
    expect(scaledNumber(20, 1)).toBe(20);
    expect(scaledNumber(2.5, 1)).toBeNull();
    expect(scaledNumber(1.25, 100)).toBe(125);
    expect(scaledNumber(0.29, 100)).toBe(29);       // 28.999999999999996 in binary
    expect(scaledNumber(0.333, 100)).toBeNull();    // more than two decimals
    expect(scaledNumber(NaN, 1)).toBeNull();
    expect(scaledNumber(Infinity, 1)).toBeNull();
    expect(scaledNumber(1e7, 1)).toBeNull();
    expect(scaledNumber(-3, 1)).toBe(-3);
  });

  it('color: "#rrggbb", the "#" required', () => {
    expect(str('color', '#ff8000')).toEqual(['ok', { color: 0xff8000 }]);
    expect(str('color', '#FF8000')).toEqual(['ok', { color: 0xff8000 }]);
    for (const bad of ['ff8000', 'red', '#ff80', '#ff800000', '#ff80zz', '', ' #ff8000']) {
      expect(str('color', bad), bad).toEqual(['bad-value', {}]);
    }
    expect(num('color', 5)).toEqual(['bad-value', {}]);
    expect(parseStyleColor('#000001')).toBe(1);
    expect(parseStyleColor(7)).toBeNull();
  });

  it('background-color: "#rrggbb" is opaque, "#rrggbbaa" has its opacity', () => {
    expect(str('background-color', '#102030')).toEqual(['ok', { bg: 0xff102030 }]);
    expect(str('background-color', '#10203080')).toEqual(['ok', { bg: 0x80102030 }]);
    expect(str('background-color', '#10203000')).toEqual(['ok', { bg: 0x00102030 }]);
    expect(str('background-color', '#ffffffff')).toEqual(['ok', { bg: 0xffffffff }]);
    for (const bad of ['102030', '#1020', '#1020300', '#102030800', 'blue']) expect(str('background-color', bad), bad).toEqual(['bad-value', {}]);
    expect(parseStyleBackground('#0000ff80')).toBe(0x800000ff);
    expect(styleBgOpa(0x800000ff)).toBe(0x80);
    expect(styleBgOpa(0xff102030)).toBe(255);
    expect(styleColorHex(0x80102030)).toBe('#102030');
  });

  it('font-family: a font name or the id of a "fonts" entry', () => {
    expect(str('font-family', 'montserrat_semibold')).toEqual(['ok', { face: 1 }]);
    expect(str('font-family', 'extra')).toEqual(['ok', { face: 2 }]);
    expect(str('font-family', 'comic')).toEqual(['unknown-font', {}]);
    expect(str('font-family', '')).toEqual(['bad-value', {}]);
    expect(num('font-family', 7)).toEqual(['ok', { face: 2 }]);
    expect(num('font-family', 8)).toEqual(['unknown-font', {}]);
    expect(num('font-family', 0)).toEqual(['unknown-font', {}]);   // ids start at 1
    expect(any('font-family', 7.5)).toEqual(['bad-value', {}]);
    // Without a resolver no font is known.
    expect(setStyleString({}, 'font-family', 'montserrat', null)).toBe('unknown-font');
  });

  it('font-size: an integer 6..72', () => {
    expect(num('font-size', 6)).toEqual(['ok', { px: 6 }]);
    expect(num('font-size', 72)).toEqual(['ok', { px: 72 }]);
    expect(num('font-size', 5)).toEqual(['bad-value', {}]);
    expect(num('font-size', 73)).toEqual(['bad-value', {}]);
    expect(any('font-size', 20.5)).toEqual(['bad-value', {}]);
    expect(str('font-size', '20')).toEqual(['bad-value', {}]);
    // The steps 1..5 of the old "fontsize" are no sizes.
    expect(num('font-size', 3)).toEqual(['bad-value', {}]);
  });

  it('text-align and text-decoration: words; both orders of the two lines', () => {
    expect(str('text-align', 'left')).toEqual(['ok', { align: StyleAlign.Left }]);
    expect(str('text-align', 'center')).toEqual(['ok', { align: StyleAlign.Center }]);
    expect(str('text-align', 'right')).toEqual(['ok', { align: StyleAlign.Right }]);
    expect(str('text-align', 'justify')).toEqual(['bad-value', {}]);
    expect(str('text-align', 'Center')).toEqual(['bad-value', {}]);
    expect(str('text-decoration', 'none')).toEqual(['ok', { decor: DECOR_NONE }]);
    expect(str('text-decoration', 'underline')).toEqual(['ok', { decor: DECOR_UNDERLINE }]);
    expect(str('text-decoration', 'line-through')).toEqual(['ok', { decor: DECOR_LINE_THROUGH }]);
    expect(str('text-decoration', 'underline line-through')).toEqual(['ok', { decor: 3 }]);
    expect(str('text-decoration', 'line-through underline')).toEqual(['ok', { decor: 3 }]);
    for (const bad of ['strikethrough', 'blink', 'underline  line-through', 'underline underline']) {
      expect(str('text-decoration', bad), bad).toEqual(['bad-value', {}]);
    }
    expect(any('text-align', 1)).toEqual(['bad-value', {}]);
  });

  it('letter-spacing: an integer -100..100', () => {
    expect(num('letter-spacing', -100)).toEqual(['ok', { letterSpace: -100 }]);
    expect(num('letter-spacing', 100)).toEqual(['ok', { letterSpace: 100 }]);
    expect(num('letter-spacing', 0)).toEqual(['ok', { letterSpace: 0 }]);
    expect(num('letter-spacing', 101)).toEqual(['bad-value', {}]);
    expect(num('letter-spacing', -101)).toEqual(['bad-value', {}]);
    expect(any('letter-spacing', 2.5)).toEqual(['bad-value', {}]);
  });

  it('opacity: 0..1 with at most two decimals, stored 0..255', () => {
    expect(any('opacity', 0)).toEqual(['ok', { opa: 0 }]);
    expect(any('opacity', 1)).toEqual(['ok', { opa: 255 }]);
    expect(any('opacity', 0.5)).toEqual(['ok', { opa: 128 }]);
    expect(any('opacity', 0.25)).toEqual(['ok', { opa: 64 }]);
    expect(any('opacity', 0.01)).toEqual(['ok', { opa: 3 }]);
    expect(any('opacity', 0.99)).toEqual(['ok', { opa: 252 }]);
    expect(any('opacity', 1.01)).toEqual(['bad-value', {}]);
    expect(any('opacity', -0.1)).toEqual(['bad-value', {}]);
    expect(any('opacity', 0.333)).toEqual(['bad-value', {}]);
    expect(any('opacity', '0.5')).toEqual(['bad-value', {}]);
    expect(any('opacity', 128)).toEqual(['bad-value', {}]);
  });

  it('line-height: 0.8..3.0 with at most two decimals, stored as hundredths', () => {
    expect(any('line-height', 0.8)).toEqual(['ok', { lineHeight: 80 }]);
    expect(any('line-height', 3)).toEqual(['ok', { lineHeight: 300 }]);
    expect(any('line-height', 1.25)).toEqual(['ok', { lineHeight: 125 }]);
    expect(any('line-height', 1)).toEqual(['ok', { lineHeight: 100 }]);
    expect(any('line-height', 0.79)).toEqual(['bad-value', {}]);
    expect(any('line-height', 3.01)).toEqual(['bad-value', {}]);
    expect(any('line-height', 1.255)).toEqual(['bad-value', {}]);
    expect(any('line-height', '1.5')).toEqual(['bad-value', {}]);
  });

  it('a name that is no property is unknown, whatever its value; other JSON types are bad values', () => {
    for (const name of ['font-weight', 'font-style', 'text_color', 'text_size', 'bg_color', 'Color', 'title.color', '']) {
      for (const value of ['#ffffff', 5, 0.5, true, null, {}, []]) expect(any(name, value), `${name} ${String(value)}`).toEqual(['unknown', {}]);
    }
    for (const name of STYLE_PROPERTIES) {
      for (const value of [true, false, null, {}, ['#ffffff']]) expect(any(name, value), `${name} ${String(value)}`).toEqual(['bad-value', {}]);
    }
  });

  it('an "icon" block takes "color" only', () => {
    expect(iconStyleValue('color', '#ffff00')).toBe(0xffff00);
    expect(iconStyleValue('color', '#000000')).toBe(0);
    expect(iconStyleValue('color', 'ffff00')).toBe('bad-value');
    expect(iconStyleValue('color', 5)).toBe('bad-value');
    expect(iconStyleValue('font-size', 20)).toBe('unknown');
    expect(iconStyleValue('opacity', 0.5)).toBe('unknown');
  });

  it('the property names a block sets', () => {
    expect(stylePropertyNames({ px: 20, color: 1, bg: 2, lineHeight: 100 })).toEqual(['color', 'font-size', 'line-height', 'background-color']);
    expect(stylePropertyNames({})).toEqual([]);
  });
});

describe('config.json "styles"', () => {
  const parse = (styles: unknown) => parseStyles({ styles }, resolve, 'montserrat, montserrat_semibold or a "fonts" entry');

  it('no "styles": the built-in look, no problem', () => {
    expect(parseStyles({}, resolve)).toMatchObject({ problems: [] });
    expect(parseStyles({ styles: null }, resolve).problems).toEqual([]);
    expect(configStyles({}, resolve)).toEqual(DEFAULT_CONFIG_STYLES);
    expect(parse([]).problems).toEqual(['"styles" is not an object, ignored']);
    expect(parse('big').problems).toEqual(['"styles" is not an object, ignored']);
  });

  it('a style has the blocks its keys give it: main when a key is no sub-block, a sub-block when it is an object with a key', () => {
    const { styles, problems } = parse({
      alarm: { color: '#ff0000', title: { color: '#ff8080' } },
      parts: {
        title: { 'font-size': 20 }, unit: { color: '#ff00ff' }, selected: { 'background-color': '#00c853' }, icon: { color: '#ffff00' },
      },
      empty: {},
      hollow: { title: {}, unit: {}, selected: {}, icon: {} },
      // A key that is no property still makes a main block: an empty one.
      odd: { colour: '#ffffff' },
    });
    expect([...styles.named.keys()]).toEqual(['alarm', 'parts', 'empty', 'hollow', 'odd']);
    expect(styles.named.get('alarm')).toEqual({ main: { color: 0xff0000 }, title: { color: 0xff8080 } });
    expect(styles.named.get('parts')).toEqual({
      title: { px: 20 }, unit: { color: 0xff00ff }, selected: { bg: 0xff00c853 }, icon: 0xffff00,
    });
    expect(styles.named.get('empty')).toEqual({});
    expect(styles.named.get('hollow')).toEqual({});
    expect(styles.named.get('odd')).toEqual({ main: {} });
    expect(problems).toEqual([
      'styles.odd: unknown property "colour", ignored (the properties are color, font-family, font-size, text-align, letter-spacing, text-decoration, opacity, line-height, background-color)',
    ]);
  });

  it('the reserved styles go into the kinds; they are no styles an item can name', () => {
    const { styles, sheet, problems } = parse({
      button: { 'font-size': 24, title: { color: '#ffffff' }, icon: { color: '#0000ff' } },
      displayitem: { unit: { color: '#00ff00', 'font-size': 10 }, icon: { color: '#00ffff' } },
      list: { 'line-height': 2, 'background-color': '#00000080', selected: { 'background-color': '#ff00ff', 'font-family': 'extra' } },
      big: { 'font-size': 48 },
    });
    expect(problems).toEqual([]);
    expect([...styles.named.keys()]).toEqual(['big']);
    expect(styles.elements[E.KeyLabel]).toEqual({ face: 0, px: 24, color: 0xffffff });
    expect(styles.elements[E.KeyTopLabel]).toEqual({ face: 1, px: 18, color: 0xffffff });
    expect(styles.elements[E.DisplayUnit]).toEqual({ color: 0x00ff00, px: 10 });
    expect(styles.elements[E.List]).toEqual({ face: 0, px: 32, color: 0x808080, lineHeight: 200, bg: 0x80000000 });
    expect(styles.elements[E.ListSelected]).toEqual({ color: 0, bg: 0xffff00ff, face: 2 });
    expect(styles.kindIcon).toEqual({ button: 0x0000ff, displayitem: 0x00ffff });
    // The sheet holds what config sets, without the built-in values.
    expect(sheet.element[E.KeyLabel]).toEqual({ px: 24 });
    expect(sheet.element[E.DisplayValue]).toEqual({});
  });

  it('a sub-block a kind does not have, and one that is no object', () => {
    const { styles, problems } = parse({
      button: { unit: { color: '#ffffff' }, selected: { color: '#ffffff' }, title: 5 },
      displayitem: { selected: { color: '#ffffff' } },
      list: { title: { color: '#ffffff' }, unit: { color: '#ffffff' }, icon: { color: '#ffffff' } },
      alarm: { color: '#ff0000', title: 'red', unit: [], icon: 7, selected: null },
      // In a style of the table every sub-block loads: which kind uses it is not known here.
      any: { unit: { color: '#ffffff' }, selected: { color: '#ffffff' }, title: { color: '#ffffff' } },
    });
    expect(problems).toEqual([
      'styles.button.title is not an object, ignored',
      'styles.button.unit: a button has no such part, ignored',
      'styles.button.selected: a button has no such part, ignored',
      'styles.displayitem.selected: a displayitem has no such part, ignored',
      'styles.list.title: a list has no such part, ignored',
      'styles.list.unit: a list has no such part, ignored',
      'styles.list.icon: a list has no such part, ignored',
      'styles.alarm.title is not an object, ignored',
      'styles.alarm.unit is not an object, ignored',
      'styles.alarm.icon is not an object, ignored',
    ]);
    expect(styles.elements).toEqual(styleDefaults());
    expect(styles.kindIcon).toEqual({});
    expect(styles.named.get('alarm')).toEqual({ main: { color: 0xff0000 } });
    expect(Object.keys(styles.named.get('any')!)).toEqual(['title', 'unit', 'selected']);
  });

  it('bad names, blocks that are no objects and the 30th style are left out; the rest loads', () => {
    const many: Record<string, unknown> = { '1bad': { color: '#00ff00' }, 'has space': {}, notobject: 5, button: 5, [`x${'y'.repeat(24)}`]: {} };
    for (let i = 1; i <= STYLE_NAMED_MAX + 2; i++) many[`s${i}`] = { color: '#0000ff' };
    many['2late'] = {};
    const { styles, problems } = parse(many);
    expect(STYLE_MAX).toBe(32);
    expect(STYLE_NAMED_MAX).toBe(29);
    expect(styles.named.size).toBe(29);
    expect(styles.named.has('s29')).toBe(true);
    expect(styles.named.has('s30')).toBe(false);
    expect(styles.named.has('notobject')).toBe(false);
    // JSON.parse keeps the file's order for these keys; the walk ends at the 30th style.
    expect(problems).toEqual([
      'styles.button is not an object, ignored',
      'styles: "1bad" is not a style name (letters, digits, _; not starting with a digit; max 24), skipped',
      'styles: "has space" is not a style name (letters, digits, _; not starting with a digit; max 24), skipped',
      'styles.notobject is not an object, skipped',
      'styles: "xyyyyyyyyyyyyyyyyyyyyyyyy" is not a style name (letters, digits, _; not starting with a digit; max 24), skipped',
      'styles: more than 32 styles, "s30" and the rest are left out',
    ]);
  });

  it('a bad property or value is left out, with the firmware\'s line and what the IDE can add; the rest of the block loads', () => {
    const { styles, problems } = parse({
      mixed: {
        color: 'red', 'font-size': 5, 'font-weight': 700, 'font-style': 'italic', 'text-align': 'justify', opacity: 2, 'letter-spacing': 2,
        'font-family': 'comic', text_color: '#00ff00', caption: { color: '#ffffff' },
        title: { color: '#00ffff', 'line-height': 9, 'font-family': 9 },
        icon: { color: 'yellow', size: 20 },
      },
      short: { color: '00ff00', 'background-color': '#12345' },
    });
    expect(styles.named.get('mixed')).toEqual({ main: { letterSpace: 2 }, title: { color: 0x00ffff } });
    expect(styles.named.get('short')).toEqual({ main: {} });
    expect(problems).toEqual([
      'styles.mixed.color: bad value, ignored (it takes a colour "#rrggbb")',
      'styles.mixed.font-size: bad value, ignored (it takes an integer 6..72 (px))',
      'styles.mixed: unknown property "font-weight", ignored (there is no font-weight: a bold face is another font, chosen with "font-family")',
      'styles.mixed: unknown property "font-style", ignored (there is no font-style: an italic face is another font, chosen with "font-family")',
      'styles.mixed.text-align: bad value, ignored (it takes "left", "center" or "right")',
      'styles.mixed.opacity: bad value, ignored (it takes a number 0..1 with at most two decimals)',
      'styles.mixed.font-family: unknown font, ignored ("comic" is not montserrat, montserrat_semibold or a "fonts" entry)',
      'styles.mixed: unknown property "text_color", ignored (the properties are color, font-family, font-size, text-align, letter-spacing, text-decoration, opacity, line-height, background-color)',
      'styles.mixed: unknown property "caption", ignored ("caption" is not a part: title, unit, selected, icon)',
      'styles.mixed.title.line-height: bad value, ignored (it takes a number 0.8..3 with at most two decimals (times the font\'s line height))',
      'styles.mixed.title.font-family: unknown font, ignored (9 is not montserrat, montserrat_semibold or a "fonts" entry)',
      'styles.mixed.icon.color: bad value, ignored (it takes a colour "#rrggbb")',
      'styles.mixed.icon: unknown property "size", ignored (an "icon" block takes "color" only)',
      'styles.short.color: bad value, ignored (it takes a colour "#rrggbb")',
      'styles.short.background-color: bad value, ignored (it takes a colour "#rrggbb" or "#rrggbbaa")',
    ]);
  });

  it('a part inside a part is an unknown property there: a part cannot hold another part', () => {
    const { styles, problems } = parse({
      nested: { title: { color: '#00ffff', unit: { color: '#ffffff' }, icon: { color: '#ffffff' }, caption: { color: '#ffffff' } } },
      button: { title: { selected: { color: '#ffffff' }, title: 5 } },
    });
    expect(styles.named.get('nested')).toEqual({ title: { color: 0x00ffff } });
    expect(problems).toEqual([
      'styles.button.title: unknown property "selected", ignored (a part cannot hold another part)',
      'styles.button.title: unknown property "title", ignored (a part cannot hold another part)',
      'styles.nested.title: unknown property "unit", ignored (a part cannot hold another part)',
      'styles.nested.title: unknown property "icon", ignored (a part cannot hold another part)',
      // A name that is no part at all keeps its own hint.
      'styles.nested.title: unknown property "caption", ignored ("caption" is not a part: title, unit, selected, icon)',
    ]);
  });
});

describe('the style list of an item', () => {
  const named = new Set(['alarm', 'muted', 'big', 'small', 'tiny']);

  it('a name of Object.prototype is a style only when "styles" defines it', () => {
    // Not defined: unknown, like any other name (the lookup is by the styles' own names).
    for (const name of ['constructor', '__proto__', 'toString', 'hasOwnProperty']) {
      expect(parseItemStyle(name, named), name).toEqual({ list: [], problems: [`unknown style "${name}", left out`] });
      expect(parseScriptStyleList(`alarm ${name}`, named), name).toEqual({ ok: false, error: 'unknown', name });
    }
    // Defined (JSON.parse makes "__proto__" an own key, as in a config.json).
    const { styles, problems } = parseStyles(JSON.parse('{"styles": {"constructor": {"color": "#ff0000"}, "__proto__": {"font-size": 20}}}'), resolve);
    expect(problems).toEqual([]);
    expect([...styles.named.keys()]).toEqual(['constructor', '__proto__']);
    expect(parseItemStyle('__proto__ constructor', styles.named)).toEqual({ list: ['__proto__', 'constructor'], problems: [] });
    expect(parseScriptStyleList('constructor', styles.named)).toEqual({ ok: true, list: ['constructor'] });
    expect(lookupPart(styles, 'button', 'main', ['constructor', '__proto__']).props).toMatchObject({ color: 0xff0000, px: 20 });
    expect(Object.keys(styleSettable(styles))).toEqual(['button', 'displayitem', 'list', 'constructor', '__proto__']);
  });

  it('names are separated by one or more spaces', () => {
    expect(styleListNames('big alarm')).toEqual(['big', 'alarm']);
    expect(styleListNames('  big    alarm  ')).toEqual(['big', 'alarm']);
    expect(styleListNames('')).toEqual([]);
    expect(styleListNames('   ')).toEqual([]);
    // Only a space separates: a tab is part of a name.
    expect(styleListNames('big\talarm')).toEqual(['big\talarm']);
  });

  it('add: at the end; a name the list has moves there; a fifth is refused', () => {
    expect(styleListAdd([], 'a')).toEqual(['a']);
    expect(styleListAdd(['a', 'b'], 'c')).toEqual(['a', 'b', 'c']);
    expect(styleListAdd(['a', 'b', 'c'], 'a')).toEqual(['b', 'c', 'a']);
    expect(styleListAdd(['a', 'b'], 'b')).toEqual(['a', 'b']);
    expect(STYLE_ITEM_MAX).toBe(4);
    expect(styleListAdd(['a', 'b', 'c', 'd'], 'e')).toBeNull();
    expect(styleListAdd(['a', 'b', 'c', 'd'], 'b')).toEqual(['a', 'c', 'd', 'b']);
    expect(styleListRemove(['a', 'b', 'c'], 'b')).toEqual(['a', 'c']);
    expect(styleListRemove(['a'], 'x')).toEqual(['a']);
  });

  it('config.json: a name that cannot be used is left out, the others stay', () => {
    expect(parseItemStyle('big alarm', named)).toEqual({ list: ['big', 'alarm'], problems: [] });
    expect(parseItemStyle(undefined, named)).toEqual({ list: [], problems: [] });
    expect(parseItemStyle(null, named)).toEqual({ list: [], problems: [] });
    expect(parseItemStyle('', named)).toEqual({ list: [], problems: [] });
    expect(parseItemStyle(7, named)).toEqual({ list: [], problems: ['is not a string of style names, ignored'] });
    expect(parseItemStyle(['big'], named).problems).toEqual(['is not a string of style names, ignored']);
    expect(parseItemStyle('button alarm', named)).toEqual({
      list: ['alarm'], problems: ['"button" is the style of a kind and cannot be named by an item, left out'],
    });
    expect(parseItemStyle('nosuch muted 1bad Alarm', named)).toEqual({
      list: ['muted'],
      problems: ['unknown style "nosuch", left out', '"1bad" is not a style name, left out', 'unknown style "Alarm", left out'],
    });
    expect(parseItemStyle('big alarm muted small tiny', named)).toEqual({
      list: ['big', 'alarm', 'muted', 'small'], problems: ['more than 4 styles, "tiny" left out'],
    });
    expect(parseItemStyle('list alarm displayitem', named).list).toEqual(['alarm']);
  });

  it('a name given twice counts once, at its last place', () => {
    expect(parseItemStyle('alarm muted alarm', named)).toEqual({ list: ['muted', 'alarm'], problems: [] });
    expect(parseItemStyle('big big big big big muted', named)).toEqual({ list: ['big', 'muted'], problems: [] });
  });

  it('a script: the first name that cannot be used ends it, with the error the device throws', () => {
    expect(parseScriptStyleList('big alarm', named)).toEqual({ ok: true, list: ['big', 'alarm'] });
    expect(parseScriptStyleList('', named)).toEqual({ ok: true, list: [] });
    expect(parseScriptStyleList('alarm   small', named)).toEqual({ ok: true, list: ['alarm', 'small'] });
    expect(parseScriptStyleList('alarm list', named)).toEqual({ ok: false, error: 'reserved', name: 'list' });
    expect(parseScriptStyleList('alarm Alarm', named)).toEqual({ ok: false, error: 'unknown', name: 'Alarm' });
    // A name that is no style name is an unknown style.
    expect(parseScriptStyleList('1bad', named)).toEqual({ ok: false, error: 'unknown', name: '1bad' });
    expect(parseScriptStyleList('alarm muted big small tiny', named)).toEqual({ ok: false, error: 'too-many', name: 'tiny' });
    expect(parseScriptStyleList('alarm muted alarm big small alarm', named)).toEqual({ ok: true, list: ['muted', 'big', 'small', 'alarm'] });
  });
});

describe('the lookup', () => {
  const { styles } = parseStyles({
    styles: {
      displayitem: { unit: { color: '#00ff00' }, icon: { color: '#00ffff' } },
      alarm: { color: '#ff0000', title: { color: '#ff8080' } },
      muted: { color: '#808080' },
      big: { 'font-family': 'montserrat_semibold', 'font-size': 48 },
      semi: { 'font-family': 'montserrat_semibold' },
      small: { 'font-size': 20 },
      tiny: { 'font-size': 14 },
      parts: { unit: { 'font-size': 10 }, selected: { 'font-family': 'extra' }, icon: { color: '#ffff00' } },
      tint: { icon: { color: '#00ff00' } },
    },
  }, resolve);

  it('each property from the last style that sets it, else the kind, else built in', () => {
    const kind = { color: 1, px: 14, face: 0 };
    expect(styleLookup(kind, [])).toEqual(kind);
    expect(styleLookup(kind, [{ color: 2 }, undefined, { px: 20 }, { color: 3 }])).toEqual({ color: 3, px: 20, face: 0 });
    expect(lookupPart(styles, 'displayitem', 'main', ['alarm', 'muted']).props).toMatchObject({ color: 0x808080, px: 14, face: 0 });
    expect(lookupPart(styles, 'displayitem', 'main', ['muted', 'alarm']).props).toMatchObject({ color: 0xff0000 });
    // muted has no "title": the title is alarm's.
    expect(lookupPart(styles, 'displayitem', 'title', ['alarm', 'muted']).props).toMatchObject({ color: 0xff8080, face: 1, px: 14 });
    expect(lookupPart(styles, 'button', 'title', ['muted']).props).toEqual(styleDefaults()[E.KeyTopLabel]);
    // A name that is no style contributes nothing.
    expect(lookupPart(styles, 'button', 'main', ['nosuch']).props).toEqual(styleDefaults()[E.KeyLabel]);
  });

  it('face and size are looked up separately', () => {
    expect(lookupPart(styles, 'button', 'main', ['big', 'small']).props).toMatchObject({ face: 1, px: 20 });
    expect(lookupPart(styles, 'button', 'main', ['small', 'big']).props).toMatchObject({ face: 1, px: 48 });
    expect(lookupPart(styles, 'list', 'main', ['semi', 'small']).props).toMatchObject({ face: 1, px: 20, lineHeight: 125 });
  });

  it('a part the kind does not have: nothing', () => {
    expect(lookupPart(styles, 'button', 'unit', ['parts'])).toEqual({ props: {}, ownSize: false });
    expect(lookupPart(styles, 'list', 'title', ['alarm'])).toEqual({ props: {}, ownSize: false });
  });

  it('whether one of the item\'s own styles sets the size', () => {
    expect(lookupPart(styles, 'displayitem', 'main', []).ownSize).toBe(false);
    expect(lookupPart(styles, 'displayitem', 'main', ['alarm']).ownSize).toBe(false);
    expect(lookupPart(styles, 'displayitem', 'main', ['alarm', 'tiny']).ownSize).toBe(true);
    expect(styleLayersSet([{ color: 1 }, undefined], ['px'])).toBe(false);
    expect(styleLayersSet([{ color: 1 }, { px: 14 }], ['px', 'face'])).toBe(true);
  });

  it('the unit and the selected row follow another part\'s font where their own blocks set none', () => {
    expect(styleFollowFont({}, 1, 48)).toEqual({ face: 1, px: 48 });
    expect(styleFollowFont({ px: 10 }, 1, 48)).toEqual({ face: 1, px: 10 });
    expect(styleFollowFont({ face: 2 }, 1, 48)).toEqual({ face: 2, px: 48 });
    // The unit's colour is its own: a main colour does not colour it.
    expect(lookupPart(styles, 'displayitem', 'unit', ['alarm']).props).toEqual({ color: 0x00ff00 });
    expect(lookupPart(styles, 'displayitem', 'unit', ['parts']).props).toEqual({ color: 0x00ff00, px: 10 });
    expect(lookupPart(styles, 'list', 'selected', ['alarm', 'parts']).props).toEqual({ color: 0, bg: 0xffffd700, face: 2 });
  });

  it('the selected row of a list: the rows\' opacity, letter-spacing and text-decoration unless "selected" sets them', () => {
    const selected: StyleProps = { color: 0, bg: 0xffffd700 };
    const rows: StyleProps = { face: 0, px: 32, color: 0x808080, lineHeight: 125 };
    // Nothing set: nothing taken over.
    expect(styleSelectedRow(selected, rows)).toEqual(selected);
    const styled: StyleProps = { ...rows, opa: 128, letterSpace: 4, decor: DECOR_UNDERLINE, align: StyleAlign.Right, bg: 0xff112233 };
    // Those three, and never the colour, the background, the alignment or the font (styleFollowFont does that).
    expect(styleSelectedRow(selected, styled)).toEqual({ color: 0, bg: 0xffffd700, opa: 128, letterSpace: 4, decor: DECOR_UNDERLINE });
    // What "selected" sets wins, property by property; 0 and "none" are values too.
    expect(styleSelectedRow({ ...selected, opa: 255, decor: DECOR_NONE }, styled)).toEqual({
      color: 0, bg: 0xffffd700, opa: 255, decor: DECOR_NONE, letterSpace: 4,
    });
    expect(styleSelectedRow({ ...selected, letterSpace: 0 }, styled).letterSpace).toBe(0);
    // The arguments are not changed.
    expect(selected).toEqual({ color: 0, bg: 0xffffd700 });
  });

  it('text-align: the style\'s, a \\a in the text wins', () => {
    expect(styleTextAlign({})).toBe(0);
    expect(styleTextAlign({ align: StyleAlign.Right })).toBe(StyleAlign.Right);
    const span = (align: TextAlign) => ({ align });
    expect(boxTextAlign([], StyleAlign.Center)).toBe(StyleAlign.Center);
    expect(boxTextAlign([span(TextAlign.Keep)], 0)).toBe(0);
    expect(boxTextAlign([span(TextAlign.Keep), span(TextAlign.Right)], StyleAlign.Center)).toBe(StyleAlign.Right);
    expect(boxTextAlign([span(TextAlign.Left), span(TextAlign.Center), span(TextAlign.Keep)], StyleAlign.Right)).toBe(StyleAlign.Center);
  });

  it('line-height: (line-height - 1) lines of the font; 1.25 is a quarter of the line', () => {
    expect(styleLineSpace(39, 125)).toBe(9);     // 39 / 4
    expect(styleLineSpace(24, 150)).toBe(12);
    expect(styleLineSpace(39, 100)).toBe(0);
    expect(styleLineSpace(58, 125)).toBe(14);
    expect(styleLineSpace(58, 150)).toBe(29);
    expect(styleLineSpace(24, 200)).toBe(24);
    // Under one line: negative, truncated towards zero as C does.
    expect(styleLineSpace(39, 80)).toBe(-7);
  });

  it('the icon colour: the last of the item\'s styles with one, else the kind\'s, else the main colour', () => {
    expect(styleIconColor(undefined, [])).toBeNull();
    expect(styleIconColor(5, [undefined])).toBe(5);
    expect(styleIconColor(5, [7, undefined, 9, undefined])).toBe(9);
    expect(styleIconColor(undefined, [0])).toBe(0);
    expect(lookupIconStyle(styles, 'button', ['alarm'])).toBeNull();
    expect(lookupIconColor(styles, 'button', ['alarm'])).toBe(0xff0000);
    expect(lookupIconColor(styles, 'button', [])).toBe(0xffffff);
    expect(lookupIconColor(styles, 'button', ['alarm', 'tint'])).toBe(0x00ff00);
    expect(lookupIconColor(styles, 'displayitem', ['muted'])).toBe(0x00ffff);
    expect(lookupIconColor(styles, 'displayitem', ['tint', 'parts'])).toBe(0xffff00);
  });
});

describe('the padding of a display item', () => {
  it('the step: the nearest of 14 / 24 / 32 / 48 / 72, a tie to the smaller', () => {
    expect([14, 15, 18, 19, 20, 24, 27, 28, 29, 32, 39, 40, 41, 48, 59, 60, 61, 72].map(px => paddingStep(px, true)))
      .toEqual([1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5]);
  });

  it('none under 14 px, and none at 14 px that no style of the item set', () => {
    expect([6, 10, 13].map(px => paddingStep(px, true))).toEqual([0, 0, 0]);
    expect(paddingStep(14, false)).toBe(0);
    expect(paddingStep(14, true)).toBe(1);
    // A size from "displayitem" other than 14 pads.
    expect(paddingStep(24, false)).toBe(2);
    expect(paddingStep(73, true)).toBe(0);
  });

  it('the paddings of a step, by boxtype', () => {
    expect([0, 1, 2, 3, 4, 5, 6].map(step => boxPadding(step, 0))).toEqual([
      null, { captionPad: 2, boxPad: 1 }, { captionPad: 4, boxPad: 3 }, { captionPad: 4, boxPad: 8 },
      { captionPad: 10, boxPad: 8 }, { captionPad: 14, boxPad: 12 }, null,
    ]);
    expect([1, 2, 3, 4, 5].map(step => boxPadding(step, 1))).toEqual([
      { captionPad: 2, boxPad: 1 }, { captionPad: 4, boxPad: 3 }, { captionPad: 0, boxPad: 8 },
      { captionPad: 6, boxPad: 8 }, { captionPad: 6, boxPad: 12 },
    ]);
  });
});

describe('fb.style.set', () => {
  it('a dotted property names a part', () => {
    expect(splitStyleProp('color')).toEqual({ part: 'main', name: 'color' });
    expect(splitStyleProp('title.color')).toEqual({ part: 'title', name: 'color' });
    expect(splitStyleProp('unit.font-size')).toEqual({ part: 'unit', name: 'font-size' });
    expect(splitStyleProp('selected.background-color')).toEqual({ part: 'selected', name: 'background-color' });
    expect(splitStyleProp('icon.color')).toEqual({ part: 'icon', name: 'color' });
    // A prefix that is no part: the whole string, which is then no property.
    expect(splitStyleProp('main.color')).toEqual({ part: 'main', name: 'main.color' });
    expect(splitStyleProp('title.')).toEqual({ part: 'title', name: '' });
  });

  const { styles } = parseStyles({
    styles: {
      button: { opacity: 1, icon: { color: '#0000ff' } },
      list: { 'background-color': '#000000' },
      alarm: { color: '#ff0000', title: { color: '#ff8080' } },
      big: { 'font-family': 'montserrat', 'font-size': 48, 'line-height': 1.5 },
      spaced: { 'letter-spacing': 3, opacity: 0.5, 'text-decoration': 'underline', 'text-align': 'left' },
      picked: { selected: { 'background-color': '#00c853', color: '#0000ff' } },
      tint: { icon: { color: '#00ff00' } },
      empty: {},
    },
  }, resolve);
  const has = styleSettable(styles);
  const SIX = ['color', 'text-align', 'letter-spacing', 'text-decoration', 'opacity', 'background-color'];
  const block = (part: string) => SIX.map(n => `${part}.${n}`);

  it('what fb.style.set can set: every property but the fixed three, in each block the style has', () => {
    expect(STYLE_SCRIPT_PROPERTIES).toEqual(SIX);
    // A style of the table: the blocks config.json gives it, and an icon colour always.
    expect(has.alarm).toEqual([...SIX, ...block('title'), 'icon.color']);
    expect(has.big).toEqual([...SIX, 'icon.color']);
    expect(has.picked).toEqual([...block('selected'), 'icon.color']);
    expect(has.tint).toEqual(['icon.color']);
    expect(has.empty).toEqual(['icon.color']);
    // A kind: all its parts; a list draws no icon.
    expect(has.button).toEqual([...SIX, ...block('title'), 'icon.color']);
    expect(has.displayitem).toEqual([...SIX, ...block('title'), ...block('unit'), 'icon.color']);
    expect(has.list).toEqual([...SIX, ...block('selected')]);
    // No prototype: "constructor" is no style.
    expect(has.constructor).toBeUndefined();
  });

  it('a property the block does not set is added; never the font, the size or the line height', () => {
    expect(scriptSetStatus(has.alarm!, 'color', '#00ff00')).toBe('ok');
    expect(scriptSetStatus(has.alarm!, 'title.color', '#00ff00')).toBe('ok');
    // Not set in config.json: added.
    expect(scriptSetStatus(has.alarm!, 'opacity', 0.5)).toBe('ok');
    expect(scriptSetStatus(has.alarm!, 'title.text-decoration', 'underline')).toBe('ok');
    expect(scriptSetStatus(has.big!, 'color', '#ff0000')).toBe('ok');
    expect(scriptSetStatus(has.big!, 'background-color', '#ff000080')).toBe('ok');
    expect(scriptSetStatus(has.big!, 'font-family', 'montserrat')).toBe('fixed');
    expect(scriptSetStatus(has.big!, 'font-size', 20)).toBe('fixed');
    expect(scriptSetStatus(has.big!, 'line-height', 2)).toBe('fixed');
    // Fixed whether the style sets it or not, and whether it has the block or not.
    expect(scriptSetStatus(has.alarm!, 'title.font-size', 30)).toBe('fixed');
    expect(scriptSetStatus(has.alarm!, 'unit.font-size', 30)).toBe('fixed');
    expect(scriptSetStatus(has.picked!, 'selected.background-color', '#ff00ff80')).toBe('ok');
    // The icon colour: every style of the table has the place for one.
    expect(scriptSetStatus(has.tint!, 'icon.color', '#ffffff')).toBe('ok');
    expect(scriptSetStatus(has.alarm!, 'icon.color', '#ffffff')).toBe('ok');
    expect(scriptSetStatus(has.empty!, 'icon.color', '#ffffff')).toBe('ok');
  });

  it('a block the style does not have cannot be made', () => {
    expect(scriptSetStatus(has.alarm!, 'unit.color', '#ffffff')).toBe('no-part');
    expect(scriptSetStatus(has.alarm!, 'selected.opacity', 0.5)).toBe('no-part');
    expect(scriptSetStatus(has.big!, 'title.color', '#ffffff')).toBe('no-part');
    // The main block is there when the style sets a property of its own.
    expect(scriptSetStatus(has.picked!, 'color', '#ffffff')).toBe('no-part');
    expect(scriptSetStatus(has.empty!, 'color', '#ffffff')).toBe('no-part');
    expect(scriptSetStatus(has.tint!, 'opacity', 0.5)).toBe('no-part');
    // The device's words for it.
    expect(scriptSetNoPartMessage('alarm', 'unit.color')).toBe('fb.style.set(): the style has no "unit" block in config.json');
    expect(scriptSetNoPartMessage('picked', 'color')).toBe('fb.style.set(): the style has no main block in config.json');
    expect(scriptSetNoPartMessage('button', 'unit.color')).toBe('fb.style.set(): the style of this kind has no "unit" part');
    expect(scriptSetNoPartMessage('list', 'icon.color')).toBe('fb.style.set(): the style of this kind has no "icon" part');
  });

  it('a name that is no property', () => {
    for (const name of ['text_color', 'font-weight', 'main.color', 'title.colour', 'colour', 'icon.opacity', 'icon.size', 'title.', '']) {
      expect(scriptSetStatus(has.alarm!, name, '#ffffff'), name).toBe('unknown');
    }
    // Unknown before anything else: also in a block the style does not have.
    expect(scriptSetStatus(has.alarm!, 'unit.colour', '#ffffff')).toBe('unknown');
    expect(scriptSetStatus(has.list!, 'icon.size', 5)).toBe('unknown');
    // Names of Object.prototype are no properties.
    for (const name of ['constructor', 'toString', '__proto__', 'title.constructor', 'hasOwnProperty']) {
      expect(scriptSetStatus(has.alarm!, name, '#ffffff'), name).toBe('unknown');
      expect(styleValueRule(name), name).toBe('a valid value');
    }
  });

  it('the value, as config.json takes it', () => {
    expect(scriptSetStatus(has.alarm!, 'color', 'red')).toBe('bad-value');
    expect(scriptSetStatus(has.alarm!, 'color', 'ff0000')).toBe('bad-value');
    expect(scriptSetStatus(has.alarm!, 'color', '#ff000080')).toBe('bad-value');
    expect(scriptSetStatus(has.alarm!, 'color', 5)).toBe('bad-value');
    expect(scriptSetStatus(has.spaced!, 'opacity', 1)).toBe('ok');
    expect(scriptSetStatus(has.spaced!, 'opacity', 1.5)).toBe('bad-value');
    expect(scriptSetStatus(has.spaced!, 'opacity', 0.333)).toBe('bad-value');
    expect(scriptSetStatus(has.spaced!, 'opacity', '0.5')).toBe('bad-value');
    expect(scriptSetStatus(has.spaced!, 'letter-spacing', 12)).toBe('ok');
    expect(scriptSetStatus(has.spaced!, 'letter-spacing', 2.5)).toBe('bad-value');
    expect(scriptSetStatus(has.spaced!, 'letter-spacing', NaN)).toBe('bad-value');
    expect(scriptSetStatus(has.spaced!, 'letter-spacing', 101)).toBe('bad-value');
    expect(scriptSetStatus(has.spaced!, 'text-decoration', 'line-through underline')).toBe('ok');
    expect(scriptSetStatus(has.spaced!, 'text-decoration', 'blink')).toBe('bad-value');
    expect(scriptSetStatus(has.spaced!, 'text-align', 'right')).toBe('ok');
    expect(scriptSetStatus(has.alarm!, 'opacity', 0.333)).toBe('bad-value');
    expect(scriptSetStatus(has.tint!, 'icon.color', 5)).toBe('bad-value');
    expect(scriptSetStatus(has.tint!, 'icon.color', 'green')).toBe('bad-value');
    // Without a value the property and the block are checked.
    expect(scriptSetStatus(has.alarm!, 'color', null)).toBe('ok');
    expect(scriptSetStatus(has.alarm!, 'opacity', null)).toBe('ok');
    expect(scriptSetStatus(has.alarm!, 'unit.opacity', null)).toBe('no-part');
  });

  it('the order of the checks: the name, the fixed three, the block, the value', () => {
    // A number that is none for the property does not hide that the property is fixed ...
    expect(scriptSetStatus(has.big!, 'font-size', 14.5)).toBe('fixed');
    expect(scriptSetStatus(has.big!, 'font-size', 2.5)).toBe('fixed');
    expect(scriptSetStatus(has.big!, 'line-height', 1.234)).toBe('fixed');
    expect(scriptSetStatus(has.big!, 'font-family', NaN)).toBe('fixed');
    expect(scriptSetStatus(has.big!, 'font-size', 'large')).toBe('fixed');
    // ... nor that the name is no property, nor that the block is not there.
    expect(scriptSetStatus(has.big!, 'font-weight', 14.5)).toBe('unknown');
    expect(scriptSetStatus(has.big!, 'title.font-size', 14.5)).toBe('fixed');
    expect(scriptSetStatus(has.big!, 'title.opacity', 0.333)).toBe('no-part');
    expect(scriptSetStatus(has.big!, 'title.color', 'red')).toBe('no-part');
    expect(scriptSetStatus(has.list!, 'icon.color', 5)).toBe('no-part');
    expect(scriptSetStatus(has.big!, 'opacity', 0.333)).toBe('bad-value');
  });

  it('the kinds: every property of every part the kind has, held or not', () => {
    expect(scriptSetStatus(has.button!, 'color', '#00ff00')).toBe('ok');
    expect(scriptSetStatus(has.button!, 'title.color', '#00ff00')).toBe('ok');
    expect(scriptSetStatus(has.button!, 'opacity', 0.5)).toBe('ok');
    expect(scriptSetStatus(has.button!, 'icon.color', '#ff00ff')).toBe('ok');
    expect(scriptSetStatus(has.displayitem!, 'unit.color', '#00ff00')).toBe('ok');
    // Not held by the kind: added.
    expect(scriptSetStatus(has.displayitem!, 'opacity', 0.5)).toBe('ok');
    expect(scriptSetStatus(has.displayitem!, 'background-color', '#000000')).toBe('ok');
    expect(scriptSetStatus(has.displayitem!, 'icon.color', '#ff00ff')).toBe('ok');
    expect(scriptSetStatus(has.button!, 'title.opacity', 0.5)).toBe('ok');
    // A part the kind does not have.
    expect(scriptSetStatus(has.button!, 'unit.color', '#ffffff')).toBe('no-part');
    expect(scriptSetStatus(has.button!, 'selected.color', '#ffffff')).toBe('no-part');
    expect(scriptSetStatus(has.list!, 'title.color', '#ffffff')).toBe('no-part');
    expect(scriptSetStatus(has.list!, 'icon.color', '#ffffff')).toBe('no-part');
    expect(scriptSetStatus(has.list!, 'background-color', '#0000ff')).toBe('ok');
    expect(scriptSetStatus(has.list!, 'selected.background-color', '#ffffff80')).toBe('ok');
    for (const kind of ['button', 'displayitem', 'list']) {
      expect(scriptSetStatus(has[kind]!, 'font-size', 20)).toBe('fixed');
      expect(scriptSetStatus(has[kind]!, 'line-height', 2)).toBe('fixed');
      expect(scriptSetStatus(has[kind]!, 'colour', '#ffffff')).toBe('unknown');
      expect(scriptSetStatus(has[kind]!, 'color', 'white')).toBe('bad-value');
    }
  });
});

describe('keys the config no longer has', () => {
  it('a removed key and what to use instead, by where it is', () => {
    expect(removedKey('top', 'style')).toBe('use the top-level "fonts" and "styles"');
    expect(removedKey('top', 'mqttbuttons')).toBe('use "buttons"');
    expect(removedKey('top', 'mqttdisplays')).toBe('use "displayitems"');
    expect(removedKey('top', 'mqttbrokers')).toBe('use "brokers"');
    for (const scope of ['button', 'displayitem', 'list'] as const) {
      expect(removedKey(scope, 'font')).toBe('use a style with "font-family"');
      expect(removedKey(scope, 'fontsize')).toBe('use a style with "font-size"');
    }
    expect(removedKey('button', 'topfont')).toBe('use a style with "title": {"font-family"}');
    expect(removedKey('button', 'topfontsize')).toBe('use a style with "title": {"font-size"}');
    expect(removedKey('button', 'topstyle')).toBe('use a style with a "title" block');
    expect(removedKey('button', 'topLabel')).toBe('use "toplabel"');
    expect(removedKey('button', 'leds')).toBe('put the topics in "topics"; the topics under "leds" are no longer subscribed');
    expect(removedKey('button', 'buttonuid')).toBe('"position" and "page" say where the button is');
    expect(removedKey('button', 'id')).toBe('use "position"');
    expect(removedKey('displayitem', 'labelfont')).toBe('use a style with "title": {"font-family"}');
    expect(removedKey('displayitem', 'labelfontsize')).toBe('use a style with "title": {"font-size"}');
    expect(removedKey('displayitem', 'labelstyle')).toBe('use a style with a "title" block');
    expect(removedKey('displayitem', 'iconcolor')).toBe('use a style with "icon": {"color"}');
    expect(removedKey('list', 'color')).toBe('use a style with "color"');
    expect(removedKey('list', 'focuscolor')).toBe('use a style with "selected": {"background-color"}');
    expect(removedKey('list', 'focustextcolor')).toBe('use a style with "selected": {"color"}');
    expect(removedKey('list', 'focusstyle')).toBe('use a style with a "selected" block');
    // The key was an alias: with "autoDiscovery": false in an old config, discovery is ON now.
    expect(removedKey('broker', 'autoDiscovery')).toBe('use "hassdiscovery"; Home Assistant discovery is on unless "hassdiscovery" is false');
  });

  it('the same key elsewhere is not a removed one', () => {
    expect(removedKey('top', 'fontsize')).toBeNull();
    expect(removedKey('displayitem', 'color')).toBeNull();
    expect(removedKey('button', 'color')).toBeNull();
    expect(removedKey('displayitem', 'id')).toBeNull();
    expect(removedKey('list', 'topfont')).toBeNull();
    expect(removedKey('button', 'style')).toBeNull();
    expect(removedKey('displayitem', 'toplabel')).toBeNull();
    expect(removedKey('top', 'buttons')).toBeNull();
  });

  it('a config: each key once, as the device logs it, with where it is', () => {
    expect(removedKeyProblems({
      style: { fonts: [] },
      mqttbuttons: [{ fontsize: 3 }],
      buttons: [{ position: 1, fontsize: 3, topLabel: 'x' }, { position: 2, fontsize: 2, id: 2 }],
      displayitems: [{ fontsize: 2, iconcolor: '#ff0000', color: '#ffffff' }],
      lists: [{ name: 'l', color: 'ff0000', fontsize: 2 }],
      // "color" is valid on an image; the entries of other arrays are not looked at.
      images: [{ name: 'i', color: '#ff0000', fontsize: 2 }],
      brokers: [{ autoDiscovery: true }],
    })).toEqual([
      '"style" is no longer read: use the top-level "fonts" and "styles"',
      '"mqttbuttons" is no longer read: use "buttons"',
      '"fontsize" is no longer read: use a style with "font-size" (buttons[0] and 3 more)',
      '"topLabel" is no longer read: use "toplabel" (buttons[0])',
      '"id" is no longer read: use "position" (buttons[1])',
      '"iconcolor" is no longer read: use a style with "icon": {"color"} (displayitems[0])',
      '"color" is no longer read: use a style with "color" (lists[0])',
      '"autoDiscovery" is no longer read: use "hassdiscovery"; Home Assistant discovery is on unless "hassdiscovery" is false (brokers[0])',
    ]);
    expect(removedKeyProblems({ buttons: [{ position: 1, style: 'big' }], fonts: [], styles: {} })).toEqual([]);
    expect(removedKeyProblems(null)).toEqual([]);
  });

  it('by depth, as the device finds them: also in a section that is an object where an array belongs', () => {
    // The device reports the keys at depth 3 under the four sections, where an object and
    // an array each count one level: the members of an object's member objects too.
    expect(removedKeyProblems({ buttons: { a: { font: 1, label: 'x' }, b: { font: 2, topfont: 3 } } })).toEqual([
      '"font" is no longer read: use a style with "font-family" (buttons.a and 1 more)',
      '"topfont" is no longer read: use a style with "title": {"font-family"} (buttons.b)',
    ]);
    expect(removedKeyProblems({ brokers: { main: { autoDiscovery: false } } })).toEqual([
      '"autoDiscovery" is no longer read: use "hassdiscovery"; Home Assistant discovery is on unless "hassdiscovery" is false (brokers.main)',
    ]);
    // Not at depth 3: a key of the section itself, an object deeper down, an entry that is no object.
    expect(removedKeyProblems({ buttons: { font: 1 } })).toEqual([]);
    expect(removedKeyProblems({ buttons: [[{ font: 1 }]] })).toEqual([]);
    expect(removedKeyProblems({ buttons: { a: [{ font: 1 }] } })).toEqual([]);
    expect(removedKeyProblems({ buttons: [{ topics: [{ font: 1 }] }] })).toEqual([]);
    expect(removedKeyProblems({ buttons: 'font', lists: 7, displayitems: null })).toEqual([]);
    // Another section: not looked at, whatever its shape.
    expect(removedKeyProblems({ images: { a: { font: 1 } }, core: { a: { fontsize: 1 } } })).toEqual([]);
  });
});
