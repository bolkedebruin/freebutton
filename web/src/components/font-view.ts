import { html, nothing } from 'lit';
import type { ItemFont } from '../js/app-state.js';
import { getFontTable } from '../js/device-config.js';
import { fontResolver, spanFont, type FontSlot, type FontTable } from '../js/font-config.js';
import { fontCss } from '../js/font-loader.js';
import { DECOR_STRIKETHROUGH, DECOR_UNDERLINE, StyleAlign, styleColorHex, type StyleProps } from '../js/style-config.js';
import { parseTextSpans, spanColorHex, TextAlign } from '../js/text-spans.js';

/**
 * Text of the virtual device in the device's fonts (font-loader.ts): the
 * face's fallback chain as font-family ("fb-roboto", "fb-icons", ...), the
 * device px times the preview's scale.
 */

function cssPx(n: number): string {
  return `${Math.round(n * 100) / 100}px`;
}

/** Inline style for a text in `font` (device px x `scale`). */
export function fontStyle(font: ItemFont, scale: number, table: FontTable = getFontTable()): string {
  const css = fontCss(table, font.face);
  const lh = css.lineHeight !== undefined ? ` line-height: ${css.lineHeight};` : '';
  return `font-family: ${css.family}; font-weight: ${css.weight}; font-size: ${cssPx(font.px * scale)};${lh}`;
}

/** A style colour with its text_opa / bg_opa as CSS. */
export function styleColorCss(color: number, opa?: number): string {
  if (opa === undefined || opa >= 255) return styleColorHex(color);
  return `rgba(${(color >> 16) & 255}, ${(color >> 8) & 255}, ${color & 255}, ${Math.round(opa / 255 * 1000) / 1000})`;
}

const STYLE_ALIGN_CSS: Record<number, string> = {
  [StyleAlign.Left]: 'left', [StyleAlign.Center]: 'center', [StyleAlign.Right]: 'right',
};

/**
 * Inline style for the text properties of a resolved style (style-config)
 * other than font and size: text_color with text_opa, text_align,
 * text_letter_space (device px x `scale`), text_decor. text_line_space is
 * not drawn (one-line texts).
 */
export function textStyleCss(p: Readonly<StyleProps> | undefined, scale: number): string {
  if (!p) return '';
  let css = '';
  if (p.color !== undefined) css += `color: ${styleColorCss(p.color, p.opa)}; `;
  if (p.align !== undefined && STYLE_ALIGN_CSS[p.align]) css += `text-align: ${STYLE_ALIGN_CSS[p.align]}; `;
  if (p.letterSpace) css += `letter-spacing: ${cssPx(p.letterSpace * scale)}; `;
  if (p.decor) {
    const lines = [p.decor & DECOR_UNDERLINE ? 'underline' : '', p.decor & DECOR_STRIKETHROUGH ? 'line-through' : ''];
    css += `text-decoration: ${lines.filter(Boolean).join(' ')}; `;
  }
  return css.trimEnd();
}

const ALIGN_CSS: Record<number, string> = {
  [TextAlign.Left]: 'left', [TextAlign.Center]: 'center', [TextAlign.Right]: 'right',
};

/**
 * Display text with markup (text-spans.ts: \f font, \s size, \c colour,
 * \a alignment), starting in `base`: one <span> per run, like the device's
 * one label per span. Plain text stays one span in `base`.
 */
export function richText(text: string, base: ItemFont, slot: FontSlot, scale: number, table: FontTable = getFontTable()) {
  if (!text) return nothing;
  if (!text.includes('\\')) return html`<span style=${fontStyle(base, scale, table)}>${text}</span>`;
  const { text: out, spans } = parseTextSpans(text, fontResolver(table));
  let align: string | undefined;
  for (const s of spans) if (s.align !== TextAlign.Keep) align = ALIGN_CSS[s.align];
  return html`<span data-rich style=${align ? `display: block; text-align: ${align};` : ''}>${spans.map(s => {
    const f = spanFont(base, slot, s);
    const color = s.color !== null ? ` color: ${spanColorHex(s.color)};` : '';
    return html`<span style="${fontStyle({ ...f, configured: true }, scale, table)}${color}">${out.slice(s.offset, s.offset + s.len)}</span>`;
  })}</span>`;
}
