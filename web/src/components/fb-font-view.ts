import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on, type WorkspaceFile, type WorkspaceSnapshot } from '../js/app-state.js';
import { workspace } from '../js/workspace.js';
import { FONT_PX } from '../js/config-model.js';
import { isSystemFontSrc } from '../js/font-config.js';
import { fontNameForKey } from '../js/fs-rules.js';
import { formatFileSize } from '../js/virtual-fs.js';
import { FONTS_BUCKET, getObjectBytes } from '../js/device-storage.js';
import { fontCodepoints, missingChars } from '../js/font-cmap.js';

/** Glyphs shown of a font that has none of the sample's characters (an icon font). */
const GLYPH_LIMIT = 240;

const DEFAULT_SAMPLE = 'Living room 21.5 °C';
type LoadState = 'loading' | 'ready' | 'failed';

/** CSS family for the specimen of a fonts-bucket key (own namespace: not the previews' "fb-" faces). */
function specimenFamily(key: string): string {
  return 'fbspec-' + key.replace(/[^A-Za-z0-9]/g, '_');
}

/**
 * Main-pane view of the device's fonts (bucket `fonts`), to compare them:
 * every font draws the same sample text, which can be changed, at one of the
 * sizes the device uses, white on the display's background. Only what the
 * font itself has is drawn: characters it lacks are listed, not drawn with
 * another font, and a font with none of the sample's characters (an icon
 * font) shows its own glyphs. Each row has the
 * name config.json refers to the font by, "Add to config.json" and Delete
 * (not for the system fonts). The files are downloaded once per session, one
 * after the other. Every font takes the height its specimen needs; the view
 * scrolls (the cards do not shrink to fit it).
 */
@customElement('fb-font-view')
export class FbFontView extends LitElement {
  createRenderRoot() { return this; }

  @state() private _ws: WorkspaceSnapshot = workspace.snapshot();
  @state() private _sample = DEFAULT_SAMPLE;
  @state() private _px: number = 32;
  @state() private _states: Record<string, LoadState> = {};
  private _unsub?: () => void;
  /** key -> "size:mtime" of the registered face (a re-upload loads again). */
  private _versions = new Map<string, string>();
  private _faces = new Map<string, FontFace>();
  /** key -> code points the font has (null: its cmap could not be read). */
  private _glyphs = new Map<string, Set<number> | null>();
  private _queue: Promise<void> = Promise.resolve();

  connectedCallback() {
    super.connectedCallback();
    this._ws = workspace.snapshot();
    this._unsub = on('workspace-change', (e) => { this._ws = e.detail; this._loadFonts(); });
    void workspace.loadReadOnly(['fonts']).then(() => this._loadFonts());
    this._loadFonts();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsub?.();
  }

  private get _fonts(): WorkspaceFile[] {
    return this._ws.readOnly?.fonts ?? [];
  }

  private _loadFonts() {
    if (typeof FontFace === 'undefined' || typeof document === 'undefined' || !document.fonts) return;
    for (const font of this._fonts) {
      const version = `${font.size}:${font.lastModified?.getTime() ?? ''}`;
      if (this._versions.get(font.key) === version) continue;
      this._versions.set(font.key, version);
      this._states = { ...this._states, [font.key]: 'loading' };
      // One download at a time: the device serves one large file well, not five.
      this._queue = this._queue.then(() => this._loadOne(font.key, version));
    }
  }

  private async _loadOne(key: string, version: string) {
    try {
      const got = await getObjectBytes(workspace.deviceBase, key, FONTS_BUCKET);
      if (!got) throw new Error('not found');
      const cps = fontCodepoints(got.bytes);
      this._glyphs.set(key, cps ? new Set(cps) : null);
      const face = new FontFace(specimenFamily(key), got.bytes.slice().buffer as ArrayBuffer);
      await face.load();
      const old = this._faces.get(key);
      if (old) document.fonts.delete(old);
      document.fonts.add(face);
      this._faces.set(key, face);
      if (this._versions.get(key) === version) this._states = { ...this._states, [key]: 'ready' };
    } catch {
      if (this._versions.get(key) === version) this._states = { ...this._states, [key]: 'failed' };
    }
  }

  private async _delete(key: string) {
    if (await workspace.deleteFont(key)) await workspace.removeFontFromConfig(key);
  }

  private _renderSpecimen(key: string) {
    const style = `font-family: '${specimenFamily(key)}'; font-size: ${this._px}px;`;
    const sample = this._sample || DEFAULT_SAMPLE;
    const has = this._glyphs.get(key);
    // The font's characters are unknown: draw the sample as the browser does.
    if (!has) {
      return html`<div class="text-white whitespace-nowrap leading-[1.5]" data-testid="font-specimen" style=${style}>${sample}</div>`;
    }
    const missing = missingChars(sample, has);
    const wanted = new Set([...sample].filter(ch => !/\s/.test(ch)));
    if (wanted.size > 0 && missing.length === wanted.size) {
      // None of the sample: an icon font. Show what it does have.
      const own = [...has].filter(cp => cp > 0x20 && !(cp >= 0x7f && cp <= 0xa0));
      return html`
        <div class="text-[11px] text-gray-200 mb-3 font-sans" data-testid="font-glyph-note">
          This font has none of the sample's characters. Its own glyphs (${own.length}${own.length > GLYPH_LIMIT ? `, the first ${GLYPH_LIMIT} shown` : ''}):
        </div>
        <div class="text-white flex flex-wrap gap-x-3 gap-y-2 leading-[1.5]" data-testid="font-glyphs" style=${style}>
          ${own.slice(0, GLYPH_LIMIT).map(cp => html`<span title="U+${cp.toString(16).toUpperCase().padStart(4, '0')}">${String.fromCodePoint(cp)}</span>`)}
        </div>
      `;
    }
    return html`
      <div class="text-white whitespace-nowrap leading-[1.5]" data-testid="font-specimen" style=${style}>${[...sample].map(ch =>
        missing.includes(ch) ? html`<span class="opacity-25" style="font-family: sans-serif" title="Not in this font">□</span>` : ch)}</div>
      ${missing.length ? html`
        <div class="text-[11px] text-status-orange mt-2 font-sans" data-testid="font-missing">Not in this font: ${missing.join(' ')}</div>
      ` : ''}
    `;
  }

  private _renderFont(font: WorkspaceFile) {
    const system = isSystemFontSrc(font.key);
    const st = this._states[font.key];
    const name = fontNameForKey(font.key);
    return html`
      <div class="shrink-0 rounded-xl border border-border-default overflow-hidden" data-font-key=${font.key}>
        <div class="flex items-center gap-3 px-4 py-2 bg-white/5">
          <div class="flex-1 min-w-0">
            <div class="text-xs text-text-default font-mono truncate">${font.key}
              ${system ? html`<span class="ml-2 px-1.5 py-0.5 rounded bg-white/10 text-[10px] text-gray-100 font-sans" title="Built into the device: it cannot be deleted">system</span>` : ''}
            </div>
            <div class="text-[10px] text-gray-200">${formatFileSize(font.size)}${system ? '' : html` · in config.json: <span class="font-mono">"${name}"</span>`}</div>
          </div>
          ${system ? '' : html`
            <button class="px-2.5 py-1 rounded-md text-[11px] bg-white/5 text-text-default hover:bg-white/10 disabled:opacity-40"
              title='Add { "name": "${name}", "src": "${font.key}" } to config.json "style.fonts" (asks first)'
              data-testid="font-add" ?disabled=${this._ws.busy}
              @click=${() => workspace.addFontToConfig(font.key)}>Add to config.json</button>
            <button class="px-2.5 py-1 rounded-md text-[11px] bg-status-red/15 text-status-red hover:bg-status-red/25 disabled:opacity-40"
              data-testid="delete-font" ?disabled=${this._ws.busy}
              @click=${() => this._delete(font.key)}>Delete</button>
          `}
        </div>
        <div class="bg-[rgb(30,30,30)] px-4 py-4 overflow-x-auto overflow-y-hidden custom-scrollbar scroll-chain">
          ${st === 'failed'
            ? html`<div class="text-xs text-status-red">This font could not be loaded for the preview.</div>`
            : st !== 'ready'
              ? html`<div class="text-xs text-gray-200">Loading…</div>`
              : this._renderSpecimen(font.key)}
        </div>
      </div>
    `;
  }

  render() {
    const ws = this._ws;
    const fonts = this._fonts;
    return html`
      <div class="h-full overflow-y-auto custom-scrollbar pane-scroll p-6 flex flex-col gap-4" data-testid="font-view">
        <div class="shrink-0 flex flex-wrap items-center gap-3">
          <h2 class="text-sm font-semibold text-text-default mr-auto">Fonts on the device</h2>
          <label class="flex items-center gap-2 text-xs text-gray-100">
            Sample
            <input type="text" data-testid="font-sample" spellcheck="false"
              class="w-64 bg-white/5 border border-white/10 rounded-lg px-2 py-1.5 text-xs text-text-default outline-none focus:border-purple-80"
              .value=${this._sample}
              @input=${(e: Event) => { this._sample = (e.target as HTMLInputElement).value; }} />
          </label>
          <div class="flex items-center gap-1" role="group" aria-label="Size in pixels on the device">
            <span class="text-xs text-gray-100 mr-1">Size</span>
            ${FONT_PX.map(px => html`
              <button class="px-2 py-1 rounded-md text-xs font-mono transition-colors ${px === this._px ? 'bg-white/90 text-bg-default' : 'bg-white/5 text-gray-100 hover:bg-white/10'}"
                aria-pressed=${px === this._px ? 'true' : 'false'} data-font-px=${px}
                @click=${() => { this._px = px; }}>${px}</button>
            `)}
          </div>
        </div>
        ${ws.mode !== 'device'
          ? html`<div class="text-xs text-gray-100" data-testid="font-view-empty">Fonts are stored on the device. Connect to a device to see them.</div>`
          : fonts.length === 0
            ? html`<div class="text-xs text-gray-200" data-testid="font-view-empty">${ws.readOnly?.fonts ? 'No fonts on the device.' : 'Reading the font list…'}</div>`
            : fonts.map(f => this._renderFont(f))}
        <div class="shrink-0 text-[11px] leading-relaxed text-gray-200 max-w-[70ch]">
          Sizes are the device's pixels (the previews on the right are scaled down). Upload .ttf / .otf files with
          the + in the Fonts panel. The device reloads its fonts on every change.
        </div>
      </div>
    `;
  }
}
