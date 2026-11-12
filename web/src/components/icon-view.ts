import { html, nothing } from 'lit';
import { iconPreviewUrl } from '../js/icon-sync.js';

/**
 * An icon of config.json "icons" in the virtual device, w x h CSS pixels
 * (the SVG fitted like the device's raster). With a colour it is drawn as
 * a tinted mask, like a single-colour raster on the device. Before the SVG
 * has been read from the device: a dashed placeholder.
 */
export function iconView(name: string | undefined, w: number, h: number, color?: string, scale = 1) {
  if (!name) return nothing;
  const cw = Math.max(1, Math.round(w * scale));
  const ch = Math.max(1, Math.round(h * scale));
  const url = iconPreviewUrl(name, w, h);
  const size = `width: ${cw}px; height: ${ch}px;`;
  if (!url) {
    return html`<span class="inline-block shrink-0 border border-dashed border-white/40 rounded-[2px]"
      data-icon=${name} data-icon-missing title="icon ${name} (SVG not loaded)" style=${size}></span>`;
  }
  if (color) {
    const mask = `mask-image: url("${url}"); -webkit-mask-image: url("${url}"); mask-size: 100% 100%; -webkit-mask-size: 100% 100%;`;
    return html`<span class="inline-block shrink-0" data-icon=${name} title="icon ${name}"
      style="${size} background-color: ${color}; ${mask}"></span>`;
  }
  return html`<img class="inline-block shrink-0" data-icon=${name} alt=${name} title="icon ${name}" src=${url} style=${size} />`;
}
