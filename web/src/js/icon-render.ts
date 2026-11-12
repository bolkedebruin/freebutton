/**
 * SVG -> RGBA pixels in the browser (the part of the icon pipeline that
 * needs a real canvas; icon-raster.ts encodes the result).
 *
 * Fit = contain, centred, on a transparent w x h canvas: the SVG root gets
 * width/height = the target size and preserveAspectRatio "xMidYMid meet",
 * so the browser's own SVG renderer does the fitting at full quality (no
 * bitmap scaling). A root without a viewBox gets one from its width/height
 * so it scales instead of being cropped.
 *
 * getImageData returns unpremultiplied RGBA, what encodeLvglBin expects.
 */

export class SvgError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'SvgError';
  }
}

const SVG_NS = 'http://www.w3.org/2000/svg';

function lengthPx(v: string | null): number | null {
  if (!v) return null;
  const m = /^\s*([0-9]*\.?[0-9]+(?:e[+-]?\d+)?)\s*(px)?\s*$/i.exec(v);
  if (!m) return null;
  const n = Number(m[1]);
  return Number.isFinite(n) && n > 0 ? n : null;
}

/**
 * The SVG text with its root sized for a w x h contain fit. Throws SvgError
 * when the text is not an SVG document.
 */
export function prepareSvg(svgText: string, w: number, h: number): string {
  const doc = new DOMParser().parseFromString(svgText, 'image/svg+xml');
  const root = doc.documentElement;
  if (!root || root.localName !== 'svg' || doc.getElementsByTagName('parsererror').length > 0) {
    throw new SvgError('not an SVG document');
  }
  if (!root.getAttribute('xmlns')) root.setAttribute('xmlns', SVG_NS);
  if (!root.getAttribute('viewBox')) {
    const vw = lengthPx(root.getAttribute('width'));
    const vh = lengthPx(root.getAttribute('height'));
    if (vw && vh) root.setAttribute('viewBox', `0 0 ${vw} ${vh}`);
  }
  root.setAttribute('width', String(w));
  root.setAttribute('height', String(h));
  root.setAttribute('preserveAspectRatio', 'xMidYMid meet');
  return new XMLSerializer().serializeToString(root);
}

type Canvas2D = CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;

function context(w: number, h: number): Canvas2D {
  if (typeof OffscreenCanvas !== 'undefined') {
    const ctx = new OffscreenCanvas(w, h).getContext('2d', { willReadFrequently: true });
    if (ctx) return ctx;
  }
  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  if (!ctx) throw new SvgError('no 2D canvas available');
  return ctx;
}

async function loadImage(svgText: string): Promise<HTMLImageElement> {
  const url = URL.createObjectURL(new Blob([svgText], { type: 'image/svg+xml' }));
  try {
    const img = new Image();
    img.decoding = 'async';
    const loaded = new Promise<void>((resolve, reject) => {
      img.onload = () => resolve();
      img.onerror = () => reject(new SvgError('the browser could not render this SVG'));
    });
    img.src = url;
    await loaded;
    return img;
  } finally {
    // The decoded image stays usable after the URL is revoked.
    URL.revokeObjectURL(url);
  }
}

/** Render an SVG at exactly w x h (contain, centred, transparent background). */
export async function rasterizeSvg(svgText: string, w: number, h: number): Promise<Uint8ClampedArray> {
  const img = await loadImage(prepareSvg(svgText, w, h));
  const ctx = context(w, h);
  ctx.clearRect(0, 0, w, h);
  ctx.drawImage(img, 0, 0, w, h);
  return ctx.getImageData(0, 0, w, h).data;
}

/** A data: URL of the prepared SVG (for the virtual device preview). */
export function svgDataUrl(svgText: string, w: number, h: number): string {
  return 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(prepareSvg(svgText, w, h));
}
