/**
 * The media of the virtual device: the files of the device's media store
 * (bucket `fonts`: TrueType files; bucket `images`: LVGL rasters), which the
 * firmware in the wasm reads to draw text and images.
 *
 * The wasm asks for a file while it runs and cannot wait, so a MediaCache
 * holds what the page's MediaSource answered. A source may answer later (a
 * fetch): the file is then missing for now, and when it arrives the firmware
 * is told as after an upload to the device (fonts are read again 1.5 s after
 * the last change, images at once). Emulator.create() first asks for
 * everything the config makes the firmware open and waits for it, so the
 * first frame already has its fonts.
 *
 * One cache can serve many emulators (a new one per config edit): a file is
 * fetched once.
 */
import { MEDIA_BUCKETS, type MediaBucket } from './host';

export { MEDIA_BUCKETS, type MediaBucket };

export interface MediaSource {
  /** The file's bytes, or null when the bucket has no such key. May answer later. */
  read(bucket: MediaBucket, key: string): Uint8Array | null | Promise<Uint8Array | null>;
}

export type MediaListener = (bucket: MediaBucket, key: string) => void;

const id = (bucket: MediaBucket, key: string): string => `${bucket}/${key}`;

export class MediaCache {
  private readonly files = new Map<string, Uint8Array | null>();
  private readonly waiting = new Map<string, Promise<void>>();
  private readonly listeners = new Set<MediaListener>();
  /** Whether the source ever answered with a promise: only then a config needs asking ahead. */
  private async = false;
  /** Configs whose files are all here (Emulator.create asks ahead once per config). */
  private readonly settledFor = new Set<string>();
  /** Counts clear(): an answer to a question from before it is not kept. */
  private cleared = 0;

  constructor(private readonly source: MediaSource | null = null) {}

  /** The file, null = the bucket has none, undefined = asked for and not here yet. */
  get(bucket: MediaBucket, key: string): Uint8Array | null | undefined {
    const name = id(bucket, key);
    if (this.files.has(name)) return this.files.get(name) ?? null;
    if (this.waiting.has(name)) return undefined;
    if (!this.source) {
      this.files.set(name, null);
      return null;
    }
    let answer: Uint8Array | null | Promise<Uint8Array | null>;
    try {
      answer = this.source.read(bucket, key);
    } catch {
      answer = null;
    }
    if (!(answer instanceof Promise)) {
      this.files.set(name, answer ?? null);
      return answer ?? null;
    }
    this.async = true;
    const asked = this.cleared;
    const arrived = answer.then(
      data => { this.arrive(bucket, key, data ?? null, asked); },
      () => { this.arrive(bucket, key, null, asked); },
    );
    this.waiting.set(name, arrived);
    return undefined;
  }

  private arrive(bucket: MediaBucket, key: string, data: Uint8Array | null, asked: number): void {
    const name = id(bucket, key);
    this.waiting.delete(name);
    // Asked before a clear() (another device): not an answer for now, the next use asks again.
    if (asked !== this.cleared) {
      this.settledFor.clear();
      return;
    }
    this.files.set(name, data);
    // A file that turned out missing changes nothing: it was missing before.
    if (data) this.tell(bucket, key);
  }

  private tell(bucket: MediaBucket, key: string): void {
    for (const listener of [...this.listeners]) listener(bucket, key);
  }

  /** How many files were asked for and are not here yet. */
  get pending(): number {
    return this.waiting.size;
  }

  /** Resolves when everything asked for so far is here. */
  async settle(): Promise<void> {
    while (this.waiting.size) await Promise.all([...this.waiting.values()]);
  }

  /**
   * The page has a new version of a file (an upload, a newly rendered icon),
   * or null: it is gone. Emulators on this cache show it as the device does
   * after an upload.
   */
  set(bucket: MediaBucket, key: string, data: Uint8Array | null): void {
    this.files.set(id(bucket, key), data);
    this.settledFor.clear();
    this.tell(bucket, key);
  }

  /** Forgets what the source answered (all buckets, or one): the next use asks again. */
  clear(bucket?: MediaBucket): void {
    for (const name of [...this.files.keys()]) {
      if (!bucket || name.startsWith(`${bucket}/`)) this.files.delete(name);
    }
    this.settledFor.clear();
    this.cleared++;
  }

  /** Forgets what the source answered for one file (it was missing, the source may have it now): the next use asks again. */
  forget(bucket: MediaBucket, key: string): void {
    this.files.delete(id(bucket, key));
    this.settledFor.clear();
  }

  /** Calls listener when a file arrives or changes; returns the function that stops it. */
  subscribe(listener: MediaListener): () => void {
    this.listeners.add(listener);
    return () => { this.listeners.delete(listener); };
  }

  /** Bytes of the files held. */
  get bytes(): number {
    let total = 0;
    for (const data of this.files.values()) total += data?.length ?? 0;
    return total;
  }

  /** @internal Whether Emulator.create() has to ask ahead for this config. */
  needsAsking(configText: string): boolean {
    return this.source !== null && (this.async || this.files.size === 0) && !this.settledFor.has(configText);
  }

  /** @internal */
  noteSettled(configText: string): void {
    if (this.settledFor.size > 32) this.settledFor.clear();
    this.settledFor.add(configText);
  }
}

/** Width and height of an icon raster key ("ico-<name>-<w>x<h>.bin", the firmware's media_image_icon_key). */
function iconRasterSize(key: string): { width: number; height: number } | null {
  const match = /^ico-.+-(\d{1,4})x(\d{1,4})\.bin$/.exec(key);
  if (!match) return null;
  const width = Number(match[1]);
  const height = Number(match[2]);
  return width > 0 && height > 0 && width <= 1024 && height <= 1024 ? { width, height } : null;
}

/**
 * For a page that knows which rasters exist but does not have their pixels:
 * an icon raster of the right size with nothing in it (LVGL .bin, format A8,
 * coverage 0). The firmware lays it out as the real one and draws nothing.
 * No fonts: text is drawn in the firmware's built-in font, as on a device
 * without font files.
 */
export function placeholderMedia(exists: (key: string) => boolean = () => true): MediaSource {
  return {
    read: (bucket, key) => {
      if (bucket !== 'images' || !exists(key)) return null;
      const size = iconRasterSize(key);
      if (!size) return null;
      const bytes = new Uint8Array(12 + size.width * size.height);
      const view = new DataView(bytes.buffer);
      view.setUint8(0, 0x19);                 // LV_IMAGE_HEADER_MAGIC
      view.setUint8(1, 0x0e);                 // LV_COLOR_FORMAT_A8
      view.setUint16(4, size.width, true);
      view.setUint16(6, size.height, true);
      view.setUint16(8, size.width, true);    // stride
      return bytes;
    },
  };
}
