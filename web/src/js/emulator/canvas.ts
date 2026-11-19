/**
 * Shows a display of the virtual device on a canvas: the pixels LVGL drew in
 * the wasm, one canvas pixel per device pixel. Framework-free; scale the
 * canvas with CSS (image-rendering: pixelated keeps the pixels square).
 *
 *   const detach = attachCanvas(emu, 'main', canvas);
 *   ...
 *   detach();
 *
 * The whole frame is drawn once; after that only the rectangle of each
 * 'frame' event. Nothing is drawn while no frame arrives.
 */

/** The part of CanvasRenderingContext2D this needs (a test passes a fake). */
export interface CanvasContextLike {
  createImageData(width: number, height: number): { data: Uint8ClampedArray; width: number; height: number };
  putImageData(image: { data: Uint8ClampedArray; width: number; height: number }, dx: number, dy: number): void;
}

export interface CanvasLike {
  width: number;
  height: number;
  getContext(kind: '2d'): CanvasContextLike | null;
}

export interface FrameRect {
  /** The display's id ("main", "key3"). */
  display: string;
  x: number;
  y: number;
  width: number;
  height: number;
}

/** What attachCanvas needs of an Emulator. */
export interface FrameSource {
  displays(): ReadonlyArray<{ id: string; width: number; height: number }>;
  readPixels(display: string, rect?: { x: number; y: number; width: number; height: number }): Uint8ClampedArray;
  on(event: 'frame', listener: (frame: FrameRect) => void): () => void;
}

/**
 * Draws `display` on `canvas` now and after every frame. Sets the canvas to
 * the display's size in device pixels. Returns the function that stops it.
 * Throws for a display the device does not have.
 */
export function attachCanvas(emu: FrameSource, display: string, canvas: CanvasLike): () => void {
  const info = emu.displays().find(d => d.id === display);
  if (!info) throw new Error(`the virtual device has no display "${display}"`);
  if (canvas.width !== info.width) canvas.width = info.width;
  if (canvas.height !== info.height) canvas.height = info.height;
  const context = canvas.getContext('2d');
  if (!context) throw new Error('canvas: no 2d context');

  const draw = (x: number, y: number, width: number, height: number): void => {
    if (width <= 0 || height <= 0) return;
    const image = context.createImageData(width, height);
    image.data.set(emu.readPixels(display, { x, y, width, height }));
    context.putImageData(image, x, y);
  };
  draw(0, 0, info.width, info.height);
  return emu.on('frame', frame => {
    if (frame.display === display) draw(frame.x, frame.y, frame.width, frame.height);
  });
}
