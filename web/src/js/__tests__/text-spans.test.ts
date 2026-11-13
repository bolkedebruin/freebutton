import { describe, expect, it } from 'vitest';
import {
  parseTextSpans, spanPx, spanColorHex, TEXT_FONT_BASE, TEXT_FONT_UNKNOWN,
  TextAlign, TextSizeKind, TextValign, type FontResolver, type TextSpan,
} from '../text-spans';

const FACES = ['medium', 'semibold', 'icons', 'mono'];
const IDS: Record<number, number> = { 7: 3 };
const resolve: FontResolver = (name, isId, id) => {
  if (isId) return IDS[id] ?? TEXT_FONT_UNKNOWN;
  const i = FACES.indexOf(name);
  return i >= 0 ? i : TEXT_FONT_UNKNOWN;
};

function span(offset: number, len: number, over: Partial<TextSpan> = {}): TextSpan {
  return {
    offset, len, font: TEXT_FONT_BASE, sizeKind: TextSizeKind.Base, size: 0,
    color: null, align: TextAlign.Keep, valign: TextValign.Keep, ...over,
  };
}

const text = (s: string) => parseTextSpans(s, resolve).text;

describe('parseTextSpans: plain text', () => {
  it('empty input gives no spans', () => {
    expect(parseTextSpans('')).toEqual({ text: '', spans: [], truncated: false });
  });

  it('text without escapes is one base span', () => {
    expect(parseTextSpans('21.5 °C')).toEqual({ text: '21.5 °C', spans: [span(0, 7)], truncated: false });
  });

  it('keeps astral characters (MDI glyphs) as they are', () => {
    const r = parseTextSpans('\u{F0D43}x');
    expect(r.text).toBe('\u{F0D43}x');
    expect(r.spans).toEqual([span(0, 3)]);
  });
});

describe('parseTextSpans: backslash', () => {
  it('\\\\ is a literal backslash', () => {
    expect(text('a\\\\b')).toBe('a\\b');
    expect(text('\\\\\\\\')).toBe('\\\\');
  });

  it('a trailing backslash is literal', () => {
    expect(text('abc\\')).toBe('abc\\');
    expect(text('\\')).toBe('\\');
  });

  it('an unknown escape keeps the backslash and the character', () => {
    expect(text('\\q')).toBe('\\q');
    expect(text('\\n\\x')).toBe('\\n\\x');
  });

  it('after a malformed escape scanning resumes right after the backslash', () => {
    // "\c12" is malformed, then "\\" is an escape.
    expect(text('\\c12\\\\')).toBe('\\c12\\');
    // "\s" + "\s1": the second one applies.
    const r = parseTextSpans('\\s\\s1x');
    expect(r.text).toBe('\\sx');
    expect(r.spans).toEqual([span(0, 2), span(2, 1, { sizeKind: TextSizeKind.Step, size: 1 })]);
  });
});

describe('parseTextSpans: \\f fonts', () => {
  it('switches to a named font', () => {
    const r = parseTextSpans('a\\f{mono}b', resolve);
    expect(r.text).toBe('ab');
    expect(r.spans).toEqual([span(0, 1), span(1, 1, { font: 3 })]);
  });

  it('switches by id (all digits)', () => {
    let seen: [string, boolean, number] | null = null;
    const r = parseTextSpans('\\f{7}x', (n, isId, id) => { seen = [n, isId, id]; return resolve(n, isId, id); });
    expect(seen).toEqual(['7', true, 7]);
    expect(r.spans).toEqual([span(0, 1, { font: 3 })]);
  });

  it('\\f{} goes back to the item font', () => {
    const r = parseTextSpans('\\f{icons}a\\f{}b', resolve);
    expect(r.spans).toEqual([span(0, 1, { font: 2 }), span(1, 1)]);
  });

  it('an unknown name or id gives TEXT_FONT_UNKNOWN', () => {
    expect(parseTextSpans('\\f{nope}a', resolve).spans).toEqual([span(0, 1, { font: TEXT_FONT_UNKNOWN })]);
    expect(parseTextSpans('\\f{99}a', resolve).spans).toEqual([span(0, 1, { font: TEXT_FONT_UNKNOWN })]);
  });

  it('without a resolver every font is unknown', () => {
    expect(parseTextSpans('\\f{medium}a').spans).toEqual([span(0, 1, { font: TEXT_FONT_UNKNOWN })]);
  });

  it('a huge id saturates at INT32_MAX (as text_spans.c)', () => {
    const calls: [boolean, number][] = [];
    parseTextSpans('\\f{99999999999}a\\f{007}b', (_n, isId, id) => { calls.push([isId, id]); return TEXT_FONT_UNKNOWN; });
    expect(calls).toEqual([[true, 0x7fffffff], [true, 7]]);
  });

  it('a long name is still a name (no length limit in the parser)', () => {
    expect(parseTextSpans(`\\f{${'a'.repeat(30)}}x`, resolve)).toEqual({
      text: 'x', spans: [span(0, 1, { font: TEXT_FONT_UNKNOWN })], truncated: false,
    });
  });

  it('malformed \\f is literal', () => {
    expect(text('\\fmono')).toBe('\\fmono');
    expect(text('\\f{mono')).toBe('\\f{mono');
    expect(text('\\f')).toBe('\\f');
    // Only [A-Za-z0-9_] before the brace.
    expect(text('\\f{my-font}x')).toBe('\\f{my-font}x');
    expect(text('\\f{a b}x')).toBe('\\f{a b}x');
    expect(text('\\f{mo-no}x')).toBe('\\f{mo-no}x');
    // Digits then letters: a name (unknown here), not literal.
    expect(parseTextSpans('\\f{7a}a', resolve).spans).toEqual([span(0, 1, { font: TEXT_FONT_UNKNOWN })]);
    expect(text('\\f{é}x')).toBe('\\f{é}x');
    expect(text('\\f{a\\\\}x')).toBe('\\f{a\\}x');
  });

  it('an unknown font does not merge with the item font', () => {
    const r = parseTextSpans('a\\f{zz}b', resolve);
    expect(r.spans).toEqual([span(0, 1), span(1, 1, { font: TEXT_FONT_UNKNOWN })]);
  });
});

describe('parseTextSpans: \\s sizes', () => {
  it('legacy steps \\s1..\\s5', () => {
    for (let s = 1; s <= 5; s++) {
      expect(parseTextSpans(`\\s${s}x`).spans).toEqual([span(0, 1, { sizeKind: TextSizeKind.Step, size: s })]);
    }
  });

  it('\\s0 goes back to the item size', () => {
    const r = parseTextSpans('\\s3a\\s0b');
    expect(r.spans).toEqual([span(0, 1, { sizeKind: TextSizeKind.Step, size: 3 }), span(1, 1)]);
  });

  it('\\s6..\\s9 are steps 6..9 (drawn at 14 px)', () => {
    for (let s = 6; s <= 9; s++) {
      const r = parseTextSpans(`\\s${s}x`);
      expect(r.spans).toEqual([span(0, 1, { sizeKind: TextSizeKind.Step, size: s })]);
      expect(spanPx(r.spans[0]!, 40)).toBe(14);
    }
  });

  it('\\sN takes a single digit', () => {
    expect(parseTextSpans('\\s12').spans).toEqual([span(0, 1, { sizeKind: TextSizeKind.Step, size: 1 })]);
    expect(text('\\s12')).toBe('2');
  });

  it('\\s{px} within 6..72', () => {
    expect(parseTextSpans('21\\s{20}°C').spans).toEqual([span(0, 2), span(2, 2, { sizeKind: TextSizeKind.Px, size: 20 })]);
    expect(parseTextSpans('\\s{6}x').spans[0]).toMatchObject({ sizeKind: TextSizeKind.Px, size: 6 });
    expect(parseTextSpans('\\s{72}x').spans[0]).toMatchObject({ sizeKind: TextSizeKind.Px, size: 72 });
    expect(parseTextSpans('\\s{040}x').spans[0]).toMatchObject({ sizeKind: TextSizeKind.Px, size: 40 });
  });

  it('\\s{px} out of range or malformed is literal', () => {
    for (const bad of ['\\s{5}', '\\s{0}', '\\s{73}', '\\s{100}', '\\s{}', '\\s{a}', '\\s{4 }', '\\s{-8}', '\\s{20', '\\s', '\\sx', '\\s{0040}']) {
      const r = parseTextSpans(bad + 'x');
      expect(r.text).toBe(bad + 'x');
      expect(r.spans).toEqual([span(0, bad.length + 1)]);
    }
  });
});

describe('parseTextSpans: \\c colour', () => {
  it('6 hex digits, either case', () => {
    expect(parseTextSpans('\\cFF8000x').spans).toEqual([span(0, 1, { color: 0xff8000 })]);
    expect(parseTextSpans('\\cff8000x').spans).toEqual([span(0, 1, { color: 0xff8000 })]);
    expect(spanColorHex(0x00ff00)).toBe('#00ff00');
  });

  it('black is a colour (not "the item colour")', () => {
    expect(parseTextSpans('a\\c000000b').spans).toEqual([span(0, 1), span(1, 1, { color: 0 })]);
  });

  it('fewer than 6 hex digits is literal', () => {
    expect(text('\\c12345')).toBe('\\c12345');
    expect(text('\\c12345g')).toBe('\\c12345g');
    expect(text('\\c')).toBe('\\c');
  });
});

describe('parseTextSpans: alignment', () => {
  it('\\al \\ac \\ar and \\vt \\vm \\vb', () => {
    expect(parseTextSpans('\\alx').spans[0]!.align).toBe(TextAlign.Left);
    expect(parseTextSpans('\\acx').spans[0]!.align).toBe(TextAlign.Center);
    expect(parseTextSpans('\\arx').spans[0]!.align).toBe(TextAlign.Right);
    expect(parseTextSpans('\\vtx').spans[0]!.valign).toBe(TextValign.Top);
    expect(parseTextSpans('\\vmx').spans[0]!.valign).toBe(TextValign.Middle);
    expect(parseTextSpans('\\vbx').spans[0]!.valign).toBe(TextValign.Bottom);
  });

  it('other letters are literal', () => {
    expect(text('\\axy')).toBe('\\axy');
    expect(text('\\vx')).toBe('\\vx');
    expect(text('\\a')).toBe('\\a');
    expect(text('\\v')).toBe('\\v');
  });
});

describe('parseTextSpans: \\u codepoints', () => {
  it('\\uXXXX', () => {
    expect(text('\\u00b0C')).toBe('°C');
    expect(text('\\u20AC')).toBe('€');
    expect(text('\\u00410')).toBe('A0');
  });

  it('\\u{...} reaches the astral planes (MDI)', () => {
    expect(text('\\u{F0D43}')).toBe('\u{F0D43}');
    expect(text('\\u{41}')).toBe('A');
    expect(text('\\u{10FFFF}')).toBe('\u{10FFFF}');
    expect(text('\\u{0f0d43}')).toBe('\u{F0D43}');
  });

  it('invalid codepoints and malformed forms are literal', () => {
    for (const bad of ['\\u{110000}', '\\u{D800}', '\\u{dfff}', '\\u{0}', '\\u0000', '\\uD83D', '\\u{}',
      '\\u{1234567}', '\\u{F0D43', '\\u12', '\\u12g4', '\\u', '\\u{g}']) {
      expect(text(bad)).toBe(bad);
    }
  });

  it('a decoded codepoint takes the current attributes', () => {
    const r = parseTextSpans('\\f{icons}\\u{F0D43}', resolve);
    expect(r.text).toBe('\u{F0D43}');
    expect(r.spans).toEqual([span(0, 2, { font: 2 })]);
  });
});

describe('parseTextSpans: \\i images', () => {
  it('\\i{name} is dropped', () => {
    expect(parseTextSpans('a\\i{rain}b')).toEqual({ text: 'ab', spans: [span(0, 2)], truncated: false });
  });

  it('\\i without a name or brace is literal', () => {
    expect(text('\\i{}')).toBe('\\i{}');
    expect(text('\\irain')).toBe('\\irain');
    expect(text('\\i{rain')).toBe('\\i{rain');
    expect(text(`\\i{${'a'.repeat(64)}}`)).toBe(`\\i{${'a'.repeat(64)}}`);
    expect(text(`\\i{${'a'.repeat(63)}}`)).toBe('');
  });
});

describe('parseTextSpans: spans', () => {
  it('escapes without text in between give no empty span', () => {
    expect(parseTextSpans('\\s2\\s3\\cff0000x').spans).toEqual([span(0, 1, { sizeKind: TextSizeKind.Step, size: 3, color: 0xff0000 })]);
    expect(parseTextSpans('x\\s3').spans).toEqual([span(0, 1)]);
  });

  it('equal attributes merge across escapes', () => {
    expect(parseTextSpans('a\\s0b\\f{}c\\\\d').spans).toEqual([span(0, 5)]);
    expect(parseTextSpans('a\\s3b\\s0c').spans).toEqual([
      span(0, 1), span(1, 1, { sizeKind: TextSizeKind.Step, size: 3 }), span(2, 1),
    ]);
  });

  it('step 2 and 24 px are different spans', () => {
    expect(parseTextSpans('\\s2a\\s{24}b').spans).toHaveLength(2);
  });

  it('a full example', () => {
    const r = parseTextSpans('\\f{mono}12:30 \\f{icons}\\u{F0D43}\\s{20}\\cff0000!', resolve);
    expect(r.text).toBe('12:30 \u{F0D43}!');
    expect(r.spans).toEqual([
      span(0, 6, { font: 3 }),
      span(6, 2, { font: 2 }),
      span(8, 1, { font: 2, sizeKind: TextSizeKind.Px, size: 20, color: 0xff0000 }),
    ]);
  });

  it('maxSpans: the last span runs to the end', () => {
    const r = parseTextSpans('a\\s2b\\s3c', null, 2);
    expect(r.truncated).toBe(true);
    expect(r.spans).toEqual([span(0, 1), span(1, 2, { sizeKind: TextSizeKind.Step, size: 2 })]);
    expect(parseTextSpans('a\\s2b', null, 2).truncated).toBe(false);
  });
});

describe('spanPx', () => {
  it('maps base, steps and px', () => {
    expect(spanPx({ sizeKind: TextSizeKind.Base, size: 0 }, 40)).toBe(40);
    expect(spanPx({ sizeKind: TextSizeKind.Step, size: 1 }, 40)).toBe(14);
    expect(spanPx({ sizeKind: TextSizeKind.Step, size: 5 }, 40)).toBe(72);
    expect(spanPx({ sizeKind: TextSizeKind.Px, size: 33 }, 40)).toBe(33);
  });
});
