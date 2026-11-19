/**
 * A 2D canvas for happy-dom, which has none: it keeps the pixels put on it
 * (RGBA, rows top to bottom), enough for the virtual device's displays.
 *
 *   const restore = installFakeCanvas();
 *   ... canvasPixels(canvas) ...
 *   restore();
 */
interface Kept {
  width: number;
  height: number;
  data: Uint8ClampedArray;
  /** drawImage calls: the source and its rectangle. */
  drawn: Array<{ source: unknown; sx: number; sy: number; sw: number; sh: number }>;
}

const kept = new WeakMap<HTMLCanvasElement, Kept>();

function keptOf(canvas: HTMLCanvasElement): Kept {
  let k = kept.get(canvas);
  if (!k || k.width !== canvas.width || k.height !== canvas.height) {
    k = { width: canvas.width, height: canvas.height, data: new Uint8ClampedArray(canvas.width * canvas.height * 4), drawn: k?.drawn ?? [] };
    kept.set(canvas, k);
  }
  return k;
}

function context(canvas: HTMLCanvasElement) {
  return {
    canvas,
    createImageData: (width: number, height: number) => ({ width, height, data: new Uint8ClampedArray(width * height * 4) }),
    putImageData(image: { data: Uint8ClampedArray; width: number; height: number }, dx: number, dy: number) {
      const k = keptOf(canvas);
      for (let row = 0; row < image.height; row++) {
        const y = dy + row;
        if (y < 0 || y >= k.height) continue;
        const width = Math.min(image.width, k.width - dx);
        k.data.set(image.data.subarray(row * image.width * 4, (row * image.width + width) * 4), (y * k.width + dx) * 4);
      }
    },
    getImageData(x: number, y: number, width: number, height: number) {
      const k = keptOf(canvas);
      const data = new Uint8ClampedArray(width * height * 4);
      for (let row = 0; row < height; row++) {
        const from = ((y + row) * k.width + x) * 4;
        data.set(k.data.subarray(from, from + width * 4), row * width * 4);
      }
      return { width, height, data };
    },
    drawImage(source: unknown, sx: number, sy: number, sw: number, sh: number) {
      keptOf(canvas).drawn.push({ source, sx, sy, sw, sh });
    },
  };
}

/** Gives every canvas a 2D context; returns the function that takes it away again. */
export function installFakeCanvas(): () => void {
  const proto = HTMLCanvasElement.prototype as unknown as { getContext: unknown; toDataURL: unknown };
  const before = { getContext: proto.getContext, toDataURL: proto.toDataURL };
  proto.getContext = function (this: HTMLCanvasElement, kind: string) {
    return kind === '2d' ? context(this) : null;
  };
  proto.toDataURL = function (this: HTMLCanvasElement) {
    return `data:image/png;fake,${this.width}x${this.height}`;
  };
  return () => {
    proto.getContext = before.getContext;
    proto.toDataURL = before.toDataURL;
  };
}

/** The pixels a canvas shows. */
export function canvasPixels(canvas: HTMLCanvasElement): Uint8ClampedArray {
  return keptOf(canvas).data;
}

/** What was drawn on a canvas with drawImage. */
export function canvasDrawn(canvas: HTMLCanvasElement): Kept['drawn'] {
  return keptOf(canvas).drawn;
}
