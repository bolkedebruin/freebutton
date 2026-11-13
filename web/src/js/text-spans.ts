/**
 * Display text markup -> spans (pure). The same grammar and semantics as
 * the firmware's src/ui/text_spans.h (text_parse in text_spans.c); keep the
 * two in sync. The previews of the virtual device and the instance estimate
 * (font-config.ts) use it.
 *
 * Grammar (a backslash starts an escape; anything malformed is kept as
 * literal text: the backslash is emitted and scanning resumes right after it):
 *   \\            a literal backslash
 *   \f{name}      font for the following text: a config font name or a
 *   \f{12}        config font id (all digits); the name is [A-Za-z0-9_]*
 *                 (anything else before the "}" is malformed); unknown ->
 *                 TEXT_FONT_UNKNOWN (the renderer draws the item's font).
 *                 \f{} = the item's font. A huge id saturates at INT32_MAX.
 *   \s{40}        size in px (FONT_PX_MIN..FONT_PX_MAX, else literal)
 *   \s1 .. \s5    legacy size step (14/24/32/48/72 px); \s0 = the item's
 *                 size; \s6 .. \s9 = step 6..9 (drawn at 14 px, as before)
 *   \cRRGGBB      colour (exactly 6 hex digits)
 *   \al \ac \ar   text alignment (left, centre, right)
 *   \vt \vm \vb   vertical alignment (top, middle, bottom)
 *   \uXXXX        the codepoint (exactly 4 hex digits)
 *   \u{X...}      the codepoint (1..6 hex digits, <= 0x10FFFF); NUL and
 *                 surrogates are literal. e.g. \u{F0D43} for an MDI icon
 *   \i{name}      image reference: parsed and dropped (not drawn yet)
 *
 * The C side works on UTF-8 bytes; here offsets and lengths are UTF-16
 * indexes into `text`. The \i name limit (1..63) counts UTF-8 bytes, as on
 * the device.
 */

export const FONT_PX_MIN = 6;
export const FONT_PX_MAX = 72;
/** Legacy size steps 1..5 -> px (fonts_px). */
export const STEP_PX = [14, 24, 32, 48, 72] as const;

/** Span font: the item's font. */
export const TEXT_FONT_BASE = -1;
/** Span font: \f{x} named a font that does not exist. */
export const TEXT_FONT_UNKNOWN = -2;

export enum TextSizeKind { Base = 0, Step = 1, Px = 2 }
export enum TextAlign { Keep = 0, Left = 1, Center = 2, Right = 3 }
export enum TextValign { Keep = 0, Top = 1, Middle = 2, Bottom = 3 }

export interface TextSpan {
  /** Into `text` (UTF-16 code units). */
  offset: number;
  /** Never 0. */
  len: number;
  /** Face index from the resolver, TEXT_FONT_BASE or TEXT_FONT_UNKNOWN. */
  font: number;
  sizeKind: TextSizeKind;
  /** Step (1..9) or px, per sizeKind; 0 for Base. */
  size: number;
  /** 0xRRGGBB, or null = the item's colour. */
  color: number | null;
  align: TextAlign;
  valign: TextValign;
}

/**
 * Resolves \f{...}: `isId` when the name is all digits (then `id` holds the
 * number). Returns a face index >= 0 or TEXT_FONT_UNKNOWN.
 */
export type FontResolver = (name: string, isId: boolean, id: number) => number;

export interface TextParseResult {
  text: string;
  spans: TextSpan[];
  /** More spans were needed than maxSpans: the last span ran to the end of the text. */
  truncated: boolean;
}

const IMAGE_NAME_MAX = 63;
const INT32_MAX = 0x7fffffff;

function utf8Bytes(s: string): number {
  let n = 0;
  for (const ch of s) {
    const c = ch.codePointAt(0)!;
    n += c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
  }
  return n;
}

const isHex = (c: string | undefined): boolean => c !== undefined && /^[0-9A-Fa-f]$/.test(c);
const isDigit = (c: string | undefined): boolean => c !== undefined && c >= '0' && c <= '9';

function validCodepoint(cp: number): boolean {
  return cp > 0 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
}

/** The text between `{` at `open` and the first `}` after it; null when there is no `}`. */
function braced(input: string, open: number): { body: string; end: number } | null {
  if (input[open] !== '{') return null;
  const close = input.indexOf('}', open + 1);
  if (close < 0) return null;
  return { body: input.slice(open + 1, close), end: close + 1 };
}

/**
 * Parse display text markup. `resolve` maps \f{} names/ids to faces (null:
 * every \f{x} is TEXT_FONT_UNKNOWN).
 */
export function parseTextSpans(
  input: string,
  resolve: FontResolver | null = null,
  maxSpans = Infinity,
): TextParseResult {
  let text = '';
  const spans: TextSpan[] = [];
  let truncated = false;

  let font = TEXT_FONT_BASE;
  let sizeKind = TextSizeKind.Base;
  let size = 0;
  let color: number | null = null;
  let align = TextAlign.Keep;
  let valign = TextValign.Keep;

  const append = (s: string): void => {
    if (s.length === 0) return;
    const last = spans[spans.length - 1];
    if (last && last.font === font && last.sizeKind === sizeKind && last.size === size
        && last.color === color && last.align === align && last.valign === valign) {
      last.len += s.length;
    } else if (spans.length >= maxSpans) {
      // No room: the last span runs to the end of the text.
      truncated = true;
      if (last) last.len += s.length;
    } else {
      spans.push({ offset: text.length, len: s.length, font, sizeKind, size, color, align, valign });
    }
    text += s;
  };

  let i = 0;
  const n = input.length;
  while (i < n) {
    const bs = input.indexOf('\\', i);
    if (bs < 0) {
      append(input.slice(i));
      break;
    }
    if (bs > i) append(input.slice(i, bs));
    i = bs;
    const e = input[i + 1];
    // Set by a well-formed escape: where scanning continues.
    let next = -1;

    switch (e) {
      case '\\':
        append('\\');
        next = i + 2;
        break;
      case 'f': {
        const b = braced(input, i + 2);
        if (!b) break;
        if (b.body === '') {
          font = TEXT_FONT_BASE;
          next = b.end;
          break;
        }
        if (!/^[A-Za-z0-9_]+$/.test(b.body)) break;
        const isId = /^[0-9]+$/.test(b.body);
        let id = 0;
        // As text_spans.c: saturates at INT32_MAX.
        if (isId) for (const ch of b.body) id = id > (INT32_MAX - 9) / 10 ? INT32_MAX : id * 10 + Number(ch);
        const face = resolve ? resolve(b.body, isId, id) : TEXT_FONT_UNKNOWN;
        font = face >= 0 ? face : TEXT_FONT_UNKNOWN;
        next = b.end;
        break;
      }
      case 's': {
        const d = input[i + 2];
        if (isDigit(d)) {
          const step = Number(d);
          if (step === 0) { sizeKind = TextSizeKind.Base; size = 0; } else { sizeKind = TextSizeKind.Step; size = step; }
          next = i + 3;
          break;
        }
        const b = braced(input, i + 2);
        if (!b || !/^[0-9]{1,3}$/.test(b.body)) break;
        const v = Number(b.body);
        if (v < FONT_PX_MIN || v > FONT_PX_MAX) break;
        sizeKind = TextSizeKind.Px;
        size = v;
        next = b.end;
        break;
      }
      case 'c': {
        const hex = input.slice(i + 2, i + 8);
        if (!/^[0-9A-Fa-f]{6}$/.test(hex)) break;
        color = parseInt(hex, 16);
        next = i + 8;
        break;
      }
      case 'a': {
        const a = input[i + 2];
        const v = a === 'l' ? TextAlign.Left : a === 'c' ? TextAlign.Center : a === 'r' ? TextAlign.Right : null;
        if (v === null) break;
        align = v;
        next = i + 3;
        break;
      }
      case 'v': {
        const a = input[i + 2];
        const v = a === 't' ? TextValign.Top : a === 'm' ? TextValign.Middle : a === 'b' ? TextValign.Bottom : null;
        if (v === null) break;
        valign = v;
        next = i + 3;
        break;
      }
      case 'u': {
        let cp = -1;
        let end = -1;
        if (input[i + 2] === '{') {
          const b = braced(input, i + 2);
          if (b && /^[0-9A-Fa-f]{1,6}$/.test(b.body)) {
            cp = parseInt(b.body, 16);
            end = b.end;
          }
        } else {
          const hex = input.slice(i + 2, i + 6);
          if (hex.length === 4 && [...hex].every(isHex)) {
            cp = parseInt(hex, 16);
            end = i + 6;
          }
        }
        if (!validCodepoint(cp)) break;
        append(String.fromCodePoint(cp));
        next = end;
        break;
      }
      case 'i': {
        const b = braced(input, i + 2);
        if (!b || b.body === '' || utf8Bytes(b.body) > IMAGE_NAME_MAX) break;
        next = b.end;
        break;
      }
      default:
        break;
    }

    if (next < 0) {
      // Malformed (or a trailing backslash): the backslash is literal.
      append('\\');
      next = i + 1;
    }
    i = next;
  }
  return { text, spans, truncated };
}

/**
 * Pixel size of a span: Base -> `basePx`, Step 1..5 -> STEP_PX, steps 6..9
 * -> 14 (as the firmware draws them), Px -> the value.
 */
export function spanPx(span: Pick<TextSpan, 'sizeKind' | 'size'>, basePx: number): number {
  if (span.sizeKind === TextSizeKind.Px) return span.size;
  if (span.sizeKind === TextSizeKind.Step) return STEP_PX[span.size - 1] ?? STEP_PX[0];
  return basePx;
}

/** "#rrggbb" of a span colour. */
export function spanColorHex(color: number): string {
  return '#' + color.toString(16).padStart(6, '0');
}
