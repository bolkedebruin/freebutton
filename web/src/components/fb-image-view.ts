import { LitElement, html } from 'lit';
import { customElement, property, state } from 'lit/decorators.js';
import { emit, on, type WorkspaceSnapshot } from '../js/app-state.js';
import { workspace } from '../js/workspace.js';
import { canDeleteKey, fileKind, iconNameForKey } from '../js/fs-rules.js';
import { formatFileSize } from '../js/virtual-fs.js';
import { getObjectBytes } from '../js/device-storage.js';
import { decodeScreenshot, drawScreenshot, isScreenshotKey, saveCanvasPng, SCREENSHOT_KEY, type Screenshot } from '../js/screenshot.js';

const ICON_SIZES = [24, 32, 48, 96];

/**
 * Main-pane view of the images, and of one asset that is not text. Without a
 * file (the Images entry of the left bar) it shows every SVG on the device
 * as a tile, or how to add the first one. With a file:
 *   an SVG icon  drawn at the sizes the device uses, on the display's
 *                background, with the name config.json refers to it by;
 *   a screenshot file a tool left on the device  decoded and shown;
 *   anything else  what it is and that it can only be deleted.
 * Every file can be deleted from here.
 */
@customElement('fb-image-view')
export class FbImageView extends LitElement {
  createRenderRoot() { return this; }

  @property() fileKey = '';
  @state() private _ws: WorkspaceSnapshot = workspace.snapshot();
  @state() private _svg: string | null = null;
  @state() private _shot: Screenshot | null = null;
  @state() private _loading = false;
  @state() private _error = '';
  private _unsub?: () => void;
  private _loadedKey = '';
  /** SVG text per key for the overview tiles (read once, one after the other). */
  @state() private _thumbs: Record<string, string | null> = {};
  private _thumbQueue: Promise<void> = Promise.resolve();

  connectedCallback() {
    super.connectedCallback();
    this._ws = workspace.snapshot();
    this._unsub = on('workspace-change', (e) => { this._ws = e.detail; });
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsub?.();
  }

  protected willUpdate() {
    if (this.fileKey !== this._loadedKey) void this._load(this.fileKey);
    if (!this.fileKey) this._loadThumbs();
  }

  private get _svgFiles() {
    return this._ws.files.filter(f => fileKind(f.key) === 'svg');
  }

  private _loadThumbs() {
    for (const f of this._svgFiles) {
      if (f.key in this._thumbs) continue;
      this._thumbs = { ...this._thumbs, [f.key]: null };
      this._thumbQueue = this._thumbQueue.then(async () => {
        const text = await workspace.readText(f.key).catch(() => null);
        if (text !== null) this._thumbs = { ...this._thumbs, [f.key]: text };
      });
    }
  }

  private _renderOverview() {
    const files = this._svgFiles;
    return html`
      <div class="h-full overflow-y-auto custom-scrollbar pane-scroll p-6 flex flex-col gap-5" data-testid="image-overview">
        <h2 class="text-sm font-semibold text-text-default">Images${files.length ? html` · ${files.length}` : ''}</h2>
        ${files.length === 0 ? html`
          <div class="text-xs leading-relaxed text-gray-100 max-w-[60ch]" data-testid="image-overview-empty">
            No images yet. Upload SVG files with the + in the Images panel; they are stored as
            <span class="font-mono">icons/&lt;name&gt;.svg</span>. Name them in config.json
            <span class="font-mono">"icons"</span> to use them on a key, display item or image.
          </div>
        ` : html`
          <div class="flex flex-wrap gap-3">
            ${files.map(f => {
              const text = this._thumbs[f.key];
              return html`
                <button class="flex flex-col items-center gap-2 p-3 w-28 rounded-lg border border-white/5 bg-[rgb(30,30,30)] hover:border-white/20"
                  title="/${f.key}" data-image-key=${f.key}
                  @click=${() => emit('main-view', { view: 'image', key: f.key })}>
                  ${text
                    ? html`<img src="data:image/svg+xml;charset=utf-8,${encodeURIComponent(text)}" width="48" height="48" alt="" style="width:48px;height:48px" />`
                    : html`<span class="w-12 h-12 rounded bg-white/5"></span>`}
                  <span class="w-full text-[10px] text-gray-100 font-mono truncate">${iconNameForKey(f.key)}</span>
                </button>
              `;
            })}
          </div>
        `}
      </div>
    `;
  }

  protected updated() {
    const canvas = this.querySelector<HTMLCanvasElement>('[data-testid="image-canvas"]');
    if (canvas && this._shot) drawScreenshot(canvas, this._shot);
  }

  private async _load(key: string) {
    this._loadedKey = key;
    this._svg = null;
    this._shot = null;
    this._error = '';
    if (!key) return;
    const kind = fileKind(key);
    if (kind !== 'svg' && !isScreenshotKey(key)) return;
    this._loading = true;
    try {
      if (kind === 'svg') {
        const text = await workspace.readText(key);
        if (this._loadedKey !== key) return;
        if (text === null) this._error = 'The file could not be read.';
        else this._svg = text;
      } else {
        const got = await getObjectBytes(workspace.deviceBase, key);
        if (this._loadedKey !== key) return;
        this._shot = got ? decodeScreenshot(SCREENSHOT_KEY.exec(key)![1], got.bytes) : null;
        if (!this._shot) this._error = 'This is not a readable screenshot file.';
      }
    } catch (err) {
      if (this._loadedKey === key) this._error = err instanceof Error ? err.message : String(err);
    } finally {
      if (this._loadedKey === key) this._loading = false;
    }
  }

  private async _delete() {
    if (await workspace.remove(this.fileKey)) emit('main-view', { view: fileKind(this.fileKey) === 'svg' ? 'image' : 'editor' });
  }

  private _savePng() {
    const canvas = this.querySelector<HTMLCanvasElement>('[data-testid="image-canvas"]');
    if (canvas && this._shot) saveCanvasPng(canvas, `${this._shot.name}.png`);
  }

  private _renderSvg(text: string) {
    const src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(text)}`;
    const name = iconNameForKey(this.fileKey);
    return html`
      <div class="flex flex-col gap-6">
        <div class="flex flex-wrap items-end gap-6">
          ${ICON_SIZES.map(size => html`
            <figure class="flex flex-col items-center gap-2">
              <div class="bg-[rgb(30,30,30)] rounded-lg p-3 border border-white/5">
                <img src=${src} width=${size} height=${size} alt="${name} at ${size} px" style="width:${size}px;height:${size}px" />
              </div>
              <figcaption class="text-[10px] text-gray-200 font-mono">${size} px</figcaption>
            </figure>
          `)}
        </div>
        <div class="text-xs leading-relaxed text-gray-100 max-w-[60ch]">
          Use it in config.json: add <span class="font-mono text-text-default">"${name}": "${this.fileKey}"</span> to
          <span class="font-mono">"icons"</span>, then set <span class="font-mono text-text-default">"icon": "${name}"</span>
          on a key, display item or image. The device draws a raster of it at the size the item asks for.
        </div>
      </div>
    `;
  }

  private _renderBody() {
    if (this._loading) return html`<div class="text-xs text-gray-200">Loading…</div>`;
    if (this._error) return html`<div class="text-xs text-status-red" role="alert">${this._error}</div>`;
    if (this._svg !== null) return this._renderSvg(this._svg);
    if (this._shot) {
      return html`
        <div class="flex flex-col items-start gap-3">
          <canvas data-testid="image-canvas" class="rounded-lg border border-white/10 max-w-full" style="image-rendering: pixelated;"></canvas>
          <div class="text-[10px] text-gray-200 font-mono">${this._shot.width} × ${this._shot.height} px</div>
          <div class="text-xs leading-relaxed text-gray-100 max-w-[60ch]">
            A screenshot file left on the device. The Screenshot button in the Device view keeps nothing on the device.
          </div>
        </div>
      `;
    }
    return html`
      <div class="text-xs leading-relaxed text-gray-100 max-w-[60ch]" data-testid="image-unknown">
        The IDE cannot show this type of file. It opens .js, .json and .txt files as text and .svg files as icons.
        ${canDeleteKey(this.fileKey) ? 'If nothing on the device uses the file, delete it to free the space.' : ''}
      </div>
    `;
  }

  render() {
    if (!this.fileKey) return this._renderOverview();
    const file = this._ws.files.find(f => f.key === this.fileKey);
    return html`
      <div class="h-full overflow-y-auto custom-scrollbar pane-scroll p-6 flex flex-col gap-5" data-testid="image-view">
        <div class="flex items-center gap-3">
          <div class="flex-1 min-w-0">
            <h2 class="text-sm font-semibold text-text-default font-mono truncate">/${this.fileKey}</h2>
            <div class="text-[11px] text-gray-200">${file ? formatFileSize(file.size) : ''}</div>
          </div>
          ${this._shot ? html`
            <button class="px-3 py-1.5 rounded-md text-xs bg-white/5 text-text-default hover:bg-white/10"
              data-testid="image-save" @click=${() => this._savePng()}>Save as PNG</button>
          ` : ''}
          ${canDeleteKey(this.fileKey) ? html`
            <button class="px-3 py-1.5 rounded-md text-xs bg-status-red/15 text-status-red hover:bg-status-red/25"
              data-testid="image-delete" @click=${() => this._delete()}>Delete</button>
          ` : ''}
        </div>
        ${this._renderBody()}
      </div>
    `;
  }
}
