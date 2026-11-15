/**
 * Workspace - the device's files and the file open in the editor.
 *
 * Talks to the device's S3 storage API (device-storage.ts) when it is
 * reachable ('device' mode); otherwise it falls back to the in-browser
 * virtual FS ('local' mode: dev without hardware, older firmware), so editing
 * keeps working and the UI says files are not on a device.
 *
 * The editable files are bucket `app`. The read-only buckets (examples, web,
 * fonts) are listed only on request (loadReadOnly, when the Assets panel
 * expands their group), cached per device, and never downloaded.
 *
 * Request discipline (the firmware has very little RAM): the `app` list is
 * fetched on start, on refresh, after save/create/delete, and when the REPL
 * connects to a different device -- never on a timer.
 *
 * Icons: SVG files can be uploaded from disk into `app` (uploadSvg, as
 * icons/<name>.svg); addIconToConfig then adds (after asking) the matching
 * entry to config.json "icons". After config.json is saved on a device -- and on
 * request (syncIcons) -- the icons it uses are rasterized and synced to
 * bucket `images` (icon-sync.ts). The sync only runs when the new or the
 * previous config uses icons, so configs without icons never touch the
 * bucket (older firmware has none).
 *
 * Fonts: .ttf/.otf files are uploaded from disk into bucket `fonts`
 * (uploadFont: checked first, ttf-check.ts; the device reloads its fonts on
 * every change of the bucket) and deleted from it (deleteFont; the system
 * fonts medium.ttf / semibold.ttf are refused); addFontToConfig /
 * removeFontFromConfig then offer the matching config.json "style.fonts"
 * entry. The previews draw with the device's fonts (font-loader.ts,
 * told about the device and every fonts listing here).
 *
 * State changes are published as `workspace-change` snapshots on the
 * app-state bus; progress/errors also go to the console pane.
 */
import { emit, on, type StorageUsage, type WorkspaceFile, type WorkspaceMode, type WorkspaceSnapshot } from './app-state';
import {
  CONFIG_KEY, MAIN_SCRIPT_KEY, MAIN_SCRIPT_TEMPLATE,
  canDeleteKey, fileKind, initialContentFor, isEditableKey, normalizeKey,
  iconNameForKey, resolveNewFileKey, svgUploadKey, utf8Length, validateBody, validateKey, MAX_SVG_BYTES, type FileKind,
  fontNameForKey, fontUploadKey,
} from './fs-rules';
import {
  S3Error, StorageUnavailableError, deleteObject, getObject, headObject, listObjects, putObject, rebootDevice,
  FONTS_BUCKET, IMAGES_BUCKET, type ListResult, type ReadOnlyBucket,
} from './device-storage';
import { checkTtf, MAX_FONT_BYTES } from './ttf-check';
import { fontWarnings, hasFontTable, isSystemFontSrc, SYSTEM_FONTS, validFontSrc } from './font-config';
import { fontLoader, wireFontLoader } from './font-loader';
import { iconSizeSlots, iconTable } from './icon-raster';
import { ICON_MANIFEST_KEY, readIconSources, setIconSources, syncIcons, type IconSyncDeps, type IconSyncResult } from './icon-sync';
import { prepareSvg, rasterizeSvg, SvgError } from './icon-render';
import { virtualFS } from './virtual-fs';
import { configToModel } from './config-model';
import { setConfig } from './device-config';
import { ConfigPreview } from './config-preview';
import { httpBaseFromWs } from './device-info';

/** What the workspace needs from the editor (injectable for tests). */
export interface EditorAdapter {
  isReady(): boolean;
  getCode(): string;
  open(content: string, kind: FileKind, opts: { isConfig: boolean }): void;
}

export interface WorkspaceDeps {
  editor: EditorAdapter;
  /** Ask the user (discard changes / delete). Defaults to window.confirm; the IDE passes its own dialog. */
  confirm?: (message: string) => boolean | Promise<boolean>;
  /** Used for POST /api/reboot only (storage goes through the S3 client). */
  fetchImpl?: typeof fetch;
  /** SVG -> RGBA (tests inject one; default: the browser canvas). */
  rasterize?: IconSyncDeps['rasterize'];
}

/** An SVG picked from disk (a File, or anything with a name and text()). */
export interface SvgUpload {
  name: string;
  text(): Promise<string>;
}

/** A font picked from disk (a File, or anything with a name, size and bytes). */
export interface FontUpload {
  name: string;
  size: number;
  arrayBuffer(): Promise<ArrayBuffer>;
}

/** Whether a parsed config uses icons at all (an icons table plus a size slot). */
function usesIcons(cfg: unknown): boolean {
  return iconTable(cfg).size > 0 && iconSizeSlots(cfg).length > 0;
}

interface Backend {
  exists(key: string): Promise<boolean>;
  read(key: string): Promise<string | null>;
  write(key: string, text: string): Promise<{ rebootRequired: boolean }>;
  remove(key: string): Promise<void>;
}

/**
 * HTTP base of the device for a REPL connection URL: "" (same origin) when
 * the REPL connected back to the origin that served the IDE.
 */
export function deviceHttpBase(wsUrl: string, origin: string = typeof location !== 'undefined' ? location.origin : ''): string {
  const base = httpBaseFromWs(wsUrl.trim()).replace(/\/+$/, '');
  return base === origin ? '' : base;
}

export function describeError(err: unknown): string {
  if (err instanceof S3Error || err instanceof StorageUnavailableError) return err.message;
  return err instanceof Error ? err.message : String(err);
}

export const LOCAL_MESSAGE = 'No device storage reachable: files are kept in this browser session only.';

function toFiles(result: ListResult): WorkspaceFile[] {
  return result.objects
    .map(o => ({ key: o.key, size: o.size, ...(o.lastModified ? { lastModified: o.lastModified } : {}) }))
    .sort((a, b) => a.key.localeCompare(b.key));
}

function displayPath(key: string): string {
  return '/' + key;
}

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

export class Workspace {
  private mode: WorkspaceMode = 'probing';
  private message = '';
  private files: WorkspaceFile[] = [];
  private busy = false;
  private currentKey = MAIN_SCRIPT_KEY;
  private dirty = false;
  private isNew = false;
  private readOnly: Partial<Record<ReadOnlyBucket, WorkspaceFile[]>> = {};
  // Icon sync state: the running sync, whether another one was asked for
  // meanwhile (with the newest config text), and whether the stored config
  // uses icons (then a save without icons still cleans the bucket).
  private iconSync: Promise<IconSyncResult | null> | null = null;
  private iconSyncNext: string | null | undefined = undefined;
  private storedUsesIcons = false;
  /** The open file could not be read: the editor does not hold its text, so it must not be saved. */
  private unread = false;
  private storage: StorageUsage | null = null;

  private base = '';
  private deps: WorkspaceDeps | null = null;
  // Doc-change events caused by our own openDocument() must not mark dirty.
  private suppressDirty = false;
  // Bumped on every user edit; lets save() tell whether the user kept typing
  // while the PUT was in flight.
  private revision = 0;
  private unsubs: Array<() => void> = [];
  private started: Promise<void> | null = null;
  // Unsaved config.json edits re-render the device preview (never stored).
  private preview = new ConfigPreview(() =>
    this.currentKey === CONFIG_KEY && this.deps?.editor.isReady() ? this.deps.editor.getCode() : null);

  // ---- public API --------------------------------------------------------

  snapshot(): WorkspaceSnapshot {
    return {
      mode: this.mode,
      message: this.message,
      files: this.files,
      busy: this.busy,
      currentKey: this.currentKey,
      dirty: this.dirty,
      isNew: this.isNew,
      readOnly: this.readOnly,
      iconSyncing: this.iconSync !== null,
      storage: this.storage ?? undefined,
    };
  }

  /** The icon sync in flight (or null): tests and callers can await it. */
  get pendingIconSync(): Promise<IconSyncResult | null> | null {
    return this.iconSync;
  }

  get deviceBase(): string {
    return this.base;
  }

  /**
   * Wire to the editor and the connection lifecycle, then probe storage and
   * open scripts/main.js. Idempotent; resolves when the first load is done.
   */
  start(deps: WorkspaceDeps): Promise<void> {
    if (this.started) return this.started;
    this.deps = deps;
    this.unsubs.push(
      on('editor-doc-change', () => {
        if (this.suppressDirty) return;
        this.revision++;
        if (this.currentKey === CONFIG_KEY) this.preview.schedule();
        if (!this.dirty) {
          this.dirty = true;
          this.publish();
        }
      }),
      on('save-request', () => { void this.save(); }),
      on('connection-change', (e) => {
        if (!e.detail.connected || !e.detail.url) return;
        const base = deviceHttpBase(e.detail.url);
        if (base === this.base) return; // same device (incl. auto-reconnects)
        this.base = base;
        this.readOnly = {};
        void this.reload();
      }),
    );
    this.started = this.waitForEditor().then(() => this.reload());
    return this.started;
  }

  /** Test helper / teardown. */
  stop(): void {
    this.unsubs.forEach(fn => fn());
    this.unsubs = [];
    this.started = null;
    this.preview.reset();
    this.deps = null;
    this.mode = 'probing';
    this.message = '';
    this.files = [];
    this.busy = false;
    this.currentKey = MAIN_SCRIPT_KEY;
    this.dirty = false;
    this.isNew = false;
    this.readOnly = {};
    this.base = '';
    this.revision = 0;
    this.iconSync = null;
    this.iconSyncNext = undefined;
    this.storedUsesIcons = false;
    this.unread = false;
    this.storage = null;
  }

  /** Re-list files (Assets "refresh"), and the read-only buckets already loaded. */
  async refresh(): Promise<void> {
    const loaded = Object.keys(this.readOnly) as ReadOnlyBucket[];
    await this.withBusy(async () => {
      await this.probe();
      if (this.mode !== 'device') {
        this.readOnly = {};
        return;
      }
      for (const bucket of loaded) await this.listReadOnly(bucket);
    });
  }

  /**
   * Text of a user file without opening it in the editor (the image view
   * shows an SVG). Null when it does not exist or cannot be read.
   */
  async readText(rawKey: string): Promise<string | null> {
    try {
      return await this.backend().read(normalizeKey(rawKey));
    } catch (err) {
      this.note(`Could not read ${displayPath(normalizeKey(rawKey))}: ${describeError(err)}`, 'error');
      return null;
    }
  }

  /**
   * List read-only buckets not listed yet (device mode only; one request
   * each, sequentially). Contents are never downloaded.
   */
  async loadReadOnly(buckets: readonly ReadOnlyBucket[]): Promise<void> {
    const missing = buckets.filter(b => !this.readOnly[b]);
    if (this.mode !== 'device' || missing.length === 0) return;
    await this.withBusy(async () => {
      for (const bucket of missing) await this.listReadOnly(bucket);
    });
  }

  /** Open a file in the editor. Returns false if it was not opened. */
  async open(rawKey: string, opts: { force?: boolean } = {}): Promise<boolean> {
    const key = normalizeKey(rawKey);
    if (!isEditableKey(key)) {
      this.note(`${displayPath(key)} is not editable here`, 'warn');
      return false;
    }
    if (!opts.force && key !== this.currentKey && !(await this.confirmDiscard())) return false;

    return this.withBusy(async () => {
      let text: string | null;
      try {
        text = await this.backend().read(key);
      } catch (err) {
        this.note(`Could not open ${displayPath(key)}: ${describeError(err)}`, 'error');
        // The editor does not hold this file's text: a save would replace the
        // stored file with whatever is in the editor (an empty editor at start).
        if (key === this.currentKey) this.unread = true;
        return false;
      }
      let isNew = false;
      if (text === null) {
        if (key !== MAIN_SCRIPT_KEY) {
          this.note(`${displayPath(key)} not found`, 'error');
          return false;
        }
        // The boot script does not exist yet: start from a template; Save creates it.
        text = MAIN_SCRIPT_TEMPLATE;
        isNew = true;
      }
      this.load(key, text, isNew);
      return true;
    });
  }

  /**
   * Save the open file (validated first: path rules, size, JSON for .json).
   * Returns true when stored. config.json also refreshes the button preview
   * and, on a device, emits `reboot-suggested` (config is applied at boot).
   */
  async save(): Promise<boolean> {
    const editor = this.deps?.editor;
    if (!editor?.isReady()) return false;
    const key = this.currentKey;
    const text = editor.getCode();

    if (this.unread) {
      this.note(`Not saved: ${displayPath(key)} could not be read, so the editor does not hold its text. Open the file again (or reload the page) first.`, 'error');
      return false;
    }
    const problem = validateKey(key) ?? validateBody(key, text);
    if (problem) {
      this.note(`Not saved: ${problem}`, 'error');
      return false;
    }

    return this.withBusy(async () => {
      const rev = this.revision;
      try {
        await this.backend().write(key, text);
      } catch (err) {
        this.note(`Save of ${displayPath(key)} failed: ${describeError(err)}`, 'error');
        return false;
      }
      this.dirty = this.revision !== rev;
      this.isNew = false;
      const where = this.mode === 'device' ? 'on device' : 'in browser memory (no device)';
      this.note(`Saved ${displayPath(key)} (${utf8Length(text)} bytes) ${where}`, 'result');
      await this.listQuietly();

      if (key === CONFIG_KEY) {
        this.configStored(text);
        // Saved: the stored config is the preview now (unless typing went on).
        if (!this.dirty) this.preview.reset();
        // The firmware reads config.json only at boot (the device also flags
        // this with x-freebutton-reboot-required), so always offer a reboot.
        if (this.mode === 'device') emit('reboot-suggested', { key });
      }
      return true;
    });
  }

  /** Create a new file from a user-typed name and open it. */
  async create(name: string): Promise<boolean> {
    const key = resolveNewFileKey(name);
    const problem = validateKey(key);
    if (problem) {
      this.note(`Cannot create "${name}": ${problem}`, 'error');
      return false;
    }
    if (this.files.some(f => f.key === key)) return this.open(key);
    if (!(await this.confirmDiscard())) return false;

    const created = await this.withBusy(async () => {
      const content = initialContentFor(key);
      try {
        // The list may be stale: never clobber a file that already exists.
        if (await this.backend().exists(key)) return 'exists' as const;
        await this.backend().write(key, content);
      } catch (err) {
        this.note(`Create ${displayPath(key)} failed: ${describeError(err)}`, 'error');
        return false;
      }
      this.note(`Created ${displayPath(key)}`, 'info');
      await this.listQuietly();
      this.load(key, content, false);
      return true;
    });
    if (created === 'exists') {
      await this.listQuietly();
      return this.open(key, { force: true });
    }
    return created;
  }

  /** Delete a file (after confirmation). config.json and other buckets' keys are refused. */
  async remove(rawKey: string, opts: { skipConfirm?: boolean } = {}): Promise<boolean> {
    const key = normalizeKey(rawKey);
    if (!canDeleteKey(key)) {
      this.note(`${displayPath(key)} cannot be deleted`, 'warn');
      return false;
    }
    if (!opts.skipConfirm && !(await this.ask(`Delete ${displayPath(key)}${this.mode === 'device' ? ' from the device' : ''}?`))) return false;

    const removed = await this.withBusy(async () => {
      try {
        await this.backend().remove(key);
      } catch (err) {
        this.note(`Delete ${displayPath(key)} failed: ${describeError(err)}`, 'error');
        return false;
      }
      this.note(`Deleted ${displayPath(key)}`, 'info');
      await this.listQuietly();
      return true;
    });
    if (removed && key.endsWith('.svg') && this.mode === 'device' && this.storedUsesIcons) void this.syncIcons();
    if (removed && key === this.currentKey) {
      // The open file is gone: drop its buffer and go back to the boot script.
      await this.open(MAIN_SCRIPT_KEY, { force: true });
    }
    return removed;
  }

  /**
   * Upload an SVG from disk to icons/<name>.svg (overwrites). Returns the
   * key, or null when refused. The SVG must parse; it is stored unchanged.
   */
  async uploadSvg(file: SvgUpload): Promise<string | null> {
    const key = svgUploadKey(file.name);
    if (!key) {
      this.note(`${file.name} is not an .svg file`, 'error');
      return null;
    }
    const problem = validateKey(key);
    if (problem) {
      this.note(`Cannot upload ${file.name}: ${problem}`, 'error');
      return null;
    }
    let text: string;
    try {
      text = await file.text();
    } catch (err) {
      this.note(`Cannot read ${file.name}: ${describeError(err)}`, 'error');
      return null;
    }
    const bytes = utf8Length(text);
    if (bytes > MAX_SVG_BYTES) {
      this.note(`Cannot upload ${file.name}: ${bytes} bytes > ${MAX_SVG_BYTES}`, 'error');
      return null;
    }
    try {
      prepareSvg(text, 1, 1);
    } catch (err) {
      const why = err instanceof SvgError ? err.message : describeError(err);
      this.note(`Cannot upload ${file.name}: ${why}`, 'error');
      return null;
    }
    return this.withBusy(async () => {
      try {
        await this.backend().write(key, text);
      } catch (err) {
        this.note(`Upload of ${displayPath(key)} failed: ${describeError(err)}`, 'error');
        return null;
      }
      this.note(`Uploaded ${displayPath(key)} (${bytes} bytes). Use it in config.json: "icons": {"<name>": "${displayPath(key)}"}`, 'result');
      await this.listQuietly();
      // A replaced SVG that config.json already uses: the device needs new rasters.
      if (this.mode === 'device' && this.storedUsesIcons) void this.syncIcons();
      return key;
    });
  }

  /**
   * Add or update the config.json "icons" entry for an uploaded SVG key
   * (name = the file stem, iconNameForKey), after asking. The file is
   * rewritten as JSON with its own indentation (key order kept; config.json
   * has no comments). Skipped while config.json has unsaved edits. Returns
   * true when config.json now has the entry.
   */
  async addIconToConfig(svgKey: string): Promise<boolean> {
    const name = iconNameForKey(svgKey);
    const path = displayPath(svgKey);
    const snippet = `"icons": {"${name}": "${path}"}`;
    if (this.currentKey === CONFIG_KEY && this.dirty) {
      this.note(`config.json has unsaved changes: add ${snippet} yourself`, 'warn');
      return false;
    }
    let text: string | null;
    try {
      text = await this.backend().read(CONFIG_KEY);
    } catch (err) {
      this.note(`Could not read config.json: ${describeError(err)}`, 'warn');
      return false;
    }
    let cfg: unknown;
    try {
      cfg = JSON.parse(text ?? '{}');
    } catch {
      this.note(`config.json is not valid JSON: add ${snippet} yourself`, 'warn');
      return false;
    }
    if (typeof cfg !== 'object' || cfg === null || Array.isArray(cfg)) return false;
    const o = cfg as Record<string, unknown>;
    if ('icons' in o && (typeof o.icons !== 'object' || o.icons === null || Array.isArray(o.icons))) {
      this.note(`config.json "icons" is not an object: add ${snippet} yourself`, 'warn');
      return false;
    }
    const icons = (o.icons ?? {}) as Record<string, unknown>;
    if (icons[name] === path) return true;
    const question = name in icons
      ? `Change icon "${name}" in config.json from ${JSON.stringify(icons[name])} to ${path}?`
      : `Add icon "${name}" (${path}) to config.json "icons"?`;
    if (!(await this.ask(question))) return false;

    o.icons = { ...icons, [name]: path };
    const indent = /\n([ \t]+)"/.exec(text ?? '')?.[1] ?? '  ';
    const next = JSON.stringify(o, null, indent) + '\n';
    const problem = validateBody(CONFIG_KEY, next);
    if (problem) {
      this.note(`Not added to config.json: ${problem}`, 'error');
      return false;
    }
    return this.withBusy(async () => {
      try {
        await this.backend().write(CONFIG_KEY, next);
      } catch (err) {
        this.note(`Save of /config.json failed: ${describeError(err)}`, 'error');
        return false;
      }
      this.note(`config.json: icon "${name}" = ${path}`, 'result');
      await this.listQuietly();
      if (this.currentKey === CONFIG_KEY && !this.dirty) this.load(CONFIG_KEY, next, false);
      // The firmware does not use the "icons" table itself (only the
      // rasters), so no reboot is needed for it.
      this.configStored(next);
      return true;
    });
  }

  /**
   * Upload a .ttf/.otf from disk to bucket `fonts` (overwrites; the device
   * reloads its fonts). Device mode only. The file is checked first
   * (ttf-check.ts: refused with the reason, warnings go to the console).
   * Returns the key, or null when refused.
   */
  async uploadFont(file: FontUpload): Promise<string | null> {
    const key = fontUploadKey(file.name);
    if (!key) {
      this.note(`${file.name} is not a .ttf or .otf font`, 'error');
      return null;
    }
    if (this.mode !== 'device') {
      this.note(`Cannot upload ${file.name}: fonts are stored on a device only (no device storage)`, 'error');
      return null;
    }
    if (file.size > MAX_FONT_BYTES) {
      this.note(`Cannot upload ${file.name}: ${file.size} bytes > ${MAX_FONT_BYTES} (2 MiB)`, 'error');
      return null;
    }
    let bytes: Uint8Array;
    try {
      bytes = new Uint8Array(await file.arrayBuffer());
    } catch (err) {
      this.note(`Cannot read ${file.name}: ${describeError(err)}`, 'error');
      return null;
    }
    const check = checkTtf(bytes);
    if (!check.ok) {
      this.note(`Cannot upload ${file.name}: ${check.error}`, 'error');
      return null;
    }
    return this.withBusy(async () => {
      try {
        await putObject(this.base, key, bytes, FONTS_BUCKET);
      } catch (err) {
        this.note(`Upload of fonts/${key} failed: ${describeError(err)}`, 'error');
        return null;
      }
      this.note(`Uploaded fonts/${key} (${bytes.length} bytes); the device reloads its fonts`, 'result');
      for (const w of check.warnings) this.note(`fonts/${key}: ${w}`, 'warn');
      await this.listReadOnly(FONTS_BUCKET);
      return key;
    });
  }

  /** Delete a font from bucket `fonts` (after confirmation). Device mode only. */
  async deleteFont(key: string, opts: { skipConfirm?: boolean } = {}): Promise<boolean> {
    if (this.mode !== 'device' || !validFontSrc(key)) return false;
    if (isSystemFontSrc(key)) {
      const name = SYSTEM_FONTS.find(f => f.src === key)!.name;
      this.note(`fonts/${key} is the system font "${name}": it cannot be deleted (upload a new version to replace it)`, 'warn');
      return false;
    }
    if (!opts.skipConfirm && !(await this.ask(`Delete fonts/${key} from the device?`))) return false;
    return this.withBusy(async () => {
      try {
        await deleteObject(this.base, key, FONTS_BUCKET);
      } catch (err) {
        this.note(`Delete of fonts/${key} failed: ${describeError(err)}`, 'error');
        return false;
      }
      this.note(`Deleted fonts/${key}`, 'info');
      await this.listReadOnly(FONTS_BUCKET);
      return true;
    });
  }

  /**
   * Add a config.json "style.fonts" entry for an uploaded font key (name =
   * the file stem, fontNameForKey), after asking. An entry with that src
   * already, or a system font file, = nothing to do. Same rules as
   * addIconToConfig (skipped while config.json has unsaved edits). The
   * firmware reads "style" at boot, so a reboot is offered.
   */
  async addFontToConfig(key: string): Promise<boolean> {
    if (isSystemFontSrc(key)) return true;
    const name = fontNameForKey(key);
    const snippet = `"style": {"fonts": [{"name": "${name}", "src": "${key}"}]}`;
    const edited = await this.editConfig(snippet, async (o) => {
      if ('style' in o && !isRecord(o.style)) return { skip: `config.json "style" is not an object: add ${snippet} yourself` };
      const style = (o.style ?? {}) as Record<string, unknown>;
      if ('fonts' in style && !Array.isArray(style.fonts)) {
        return { skip: `config.json "style.fonts" is not an array: add ${snippet} yourself` };
      }
      const fonts = (style.fonts ?? []) as unknown[];
      if (fonts.some(e => isRecord(e) && e.src === key)) return { done: true };
      const same = fonts.find(e => isRecord(e) && e.name === name) as Record<string, unknown> | undefined;
      if (same) {
        if (!(await this.ask(`Change font "${name}" in config.json from ${JSON.stringify(same.src)} to ${key}?`))) return { done: false };
        o.style = { ...style, fonts: fonts.map(e => (e === same ? { ...same, src: key } : e)) };
      } else {
        if (!(await this.ask(`Add font "${name}" (${key}) to config.json "style.fonts"?`))) return { done: false };
        const next = [...fonts, { name, src: key }];
        // A new "fonts" goes first in "style"; an existing one keeps its place.
        o.style = 'fonts' in style ? { ...style, fonts: next } : { fonts: next, ...style };
      }
      return { changed: `config.json: font "${name}" = ${key}` };
    });
    return edited;
  }

  /**
   * Remove the config.json "style.fonts" entries whose src is `key` (a
   * deleted font), after asking.
   */
  async removeFontFromConfig(key: string): Promise<boolean> {
    return this.editConfig(`the "style.fonts" entry with "src": "${key}"`, async (o) => {
      const style = isRecord(o.style) ? o.style : null;
      if (!style || !Array.isArray(style.fonts)) return { done: true };
      const fonts = style.fonts as unknown[];
      const keep = fonts.filter(e => !(isRecord(e) && e.src === key));
      if (keep.length === fonts.length) return { done: true };
      const names = fonts.filter(e => !keep.includes(e)).map(e => `"${(e as Record<string, unknown>).name}"`).join(', ');
      if (!(await this.ask(`Remove font ${names} (${key}) from config.json "style.fonts"?`))) return { done: false };
      style.fonts = keep;
      return { changed: `config.json: font ${names} removed` };
    });
  }

  /**
   * Read config.json, let `change` edit the parsed object, validate and
   * store it (with its own indentation) and show it. `what` names the edit
   * in "do it yourself" messages.
   */
  private async editConfig(
    what: string,
    change: (o: Record<string, unknown>) => Promise<{ done: boolean } | { skip: string } | { changed: string }>,
  ): Promise<boolean> {
    if (this.currentKey === CONFIG_KEY && this.dirty) {
      this.note(`config.json has unsaved changes: edit ${what} yourself`, 'warn');
      return false;
    }
    let text: string | null;
    try {
      text = await this.backend().read(CONFIG_KEY);
    } catch (err) {
      this.note(`Could not read config.json: ${describeError(err)}`, 'warn');
      return false;
    }
    let cfg: unknown;
    try {
      cfg = JSON.parse(text ?? '{}');
    } catch {
      this.note(`config.json is not valid JSON: edit ${what} yourself`, 'warn');
      return false;
    }
    if (typeof cfg !== 'object' || cfg === null || Array.isArray(cfg)) return false;
    const o = cfg as Record<string, unknown>;
    const r = await change(o);
    if ('skip' in r) {
      this.note(r.skip, 'warn');
      return false;
    }
    if ('done' in r) return r.done;
    const indent = /\n([ \t]+)"/.exec(text ?? '')?.[1] ?? '  ';
    const next = JSON.stringify(o, null, indent) + '\n';
    const problem = validateBody(CONFIG_KEY, next);
    if (problem) {
      this.note(`config.json not changed: ${problem}`, 'error');
      return false;
    }
    return this.withBusy(async () => {
      try {
        await this.backend().write(CONFIG_KEY, next);
      } catch (err) {
        this.note(`Save of /config.json failed: ${describeError(err)}`, 'error');
        return false;
      }
      this.note(r.changed, 'result');
      await this.listQuietly();
      if (this.currentKey === CONFIG_KEY && !this.dirty) this.load(CONFIG_KEY, next, false);
      this.configStored(next);
      // The firmware reads config.json (and its "style") at boot.
      if (this.mode === 'device') emit('reboot-suggested', { key: CONFIG_KEY });
      return true;
    });
  }

  /**
   * Rasterize the icons of config.json (`cfgText`, else the stored one) and
   * sync them to bucket `images`. Runs one at a time: a request while one
   * runs is queued (the newest text wins). Device mode only.
   */
  syncIcons(cfgText?: string): Promise<IconSyncResult | null> {
    if (this.iconSync) {
      this.iconSyncNext = cfgText ?? null;
      return this.iconSync;
    }
    const run = async (text: string | undefined): Promise<IconSyncResult | null> => {
      let result: IconSyncResult | null = null;
      try {
        result = await this.runIconSync(text);
      } catch (err) {
        this.note(`Icon sync failed: ${describeError(err)}`, 'error');
      }
      if (this.iconSyncNext !== undefined) {
        const next = this.iconSyncNext;
        this.iconSyncNext = undefined;
        return run(next ?? undefined);
      }
      return result;
    };
    this.iconSync = run(cfgText).finally(() => {
      this.iconSync = null;
      this.publish();
    });
    this.publish();
    return this.iconSync;
  }

  /** POST /api/reboot (device mode only). */
  async reboot(): Promise<boolean> {
    if (this.mode !== 'device') return false;
    try {
      await rebootDevice(this.base, this.deps?.fetchImpl);
      this.note('Device is rebooting...', 'info');
      return true;
    } catch (err) {
      this.note(`Reboot failed: ${describeError(err)}`, 'error');
      return false;
    }
  }

  // ---- internals ---------------------------------------------------------

  /** config.json `text` was stored: show it, and sync icons when it (or the previous one) uses them. */
  private configStored(text: string): void {
    const cfg = this.applyConfig(text);
    const wantsSync = usesIcons(cfg) || this.storedUsesIcons;
    this.storedUsesIcons = usesIcons(cfg);
    if (this.mode === 'device' && wantsSync) void this.syncIcons(text);
    void this.checkFonts(cfg);
  }

  /**
   * Font and style warnings of a stored config (console, font-config
   * fontWarnings): files missing from bucket `fonts` (listed once when the
   * config has a "style.fonts" table), too many font instances, no "style".
   */
  private async checkFonts(cfg: unknown): Promise<void> {
    if (cfg === null) return;
    const hasFonts = hasFontTable(cfg);
    if (hasFonts && this.mode === 'device' && !this.readOnly.fonts) await this.listReadOnly(FONTS_BUCKET);
    const listed = this.readOnly.fonts;
    const keys = hasFonts && this.mode === 'device' && listed ? new Set(listed.map(f => f.key)) : null;
    for (const w of fontWarnings(cfg, keys)) emit('console-line', { text: `config.json: ${w}`, type: 'warn' });
  }

  private async runIconSync(cfgText: string | undefined): Promise<IconSyncResult | null> {
    if (this.mode !== 'device') {
      this.note('Icons are synced to a device only (no device storage)', 'warn');
      return null;
    }
    const text = cfgText ?? await this.backend().read(CONFIG_KEY);
    if (text === null) {
      this.note('Icons not synced: no config.json on the device', 'warn');
      return null;
    }
    let cfg: unknown;
    try {
      cfg = JSON.parse(text);
    } catch {
      this.note('Icons not synced: config.json is not valid JSON', 'error');
      return null;
    }
    const base = this.base;
    const log = (t: string, type: 'info' | 'warn' | 'error' | 'result' | 'log' | 'input') => {
      emit('console-line', { text: t, type });
    };
    const result = await syncIcons(cfg, {
      readSvg: async (key) => (await getObject(base, key))?.text ?? null,
      listImages: async () => (await listObjects(base, { bucket: IMAGES_BUCKET })).objects,
      putImage: async (key, bytes) => { await putObject(base, key, bytes, IMAGES_BUCKET); },
      deleteImage: (key) => deleteObject(base, key, IMAGES_BUCKET),
      rasterize: this.deps?.rasterize ?? rasterizeSvg,
      log,
      readManifest: async () => (await getObject(base, ICON_MANIFEST_KEY))?.text ?? null,
      writeManifest: async (text) => { await putObject(base, ICON_MANIFEST_KEY, text); },
    });
    if (!result.ok) {
      this.message = result.noBucket
        ? 'Icons not synced: this firmware has no "images" storage bucket (update the device firmware)'
        : 'Icon sync had errors (see console)';
      this.publish();
    }
    return result;
  }

  private async reload(): Promise<void> {
    let cfg: unknown = null;
    await this.withBusy(async () => {
      await this.probe();
      // Previews draw with the device's fonts (when the browser can).
      wireFontLoader();
      void fontLoader.setDevice(this.mode === 'device' ? this.base : null);
      if (this.files.some(f => f.key === CONFIG_KEY)) {
        try {
          const text = await this.backend().read(CONFIG_KEY);
          if (text !== null) cfg = this.applyConfig(text);
        } catch (err) {
          this.note(`Could not read config.json: ${describeError(err)}`, 'warn');
        }
      }
      this.storedUsesIcons = usesIcons(cfg);
      // The SVGs for the virtual device preview (a few small reads).
      if (this.mode === 'device' && iconTable(cfg).size > 0) {
        const backend = this.backend();
        setIconSources(await readIconSources(cfg, (key) => backend.read(key)));
      }
    });
    // Keep unsaved edits; otherwise show what this storage has.
    if (!this.dirty) {
      const opened = await this.open(this.currentKey, { force: true });
      if (!opened && this.currentKey !== MAIN_SCRIPT_KEY) await this.open(MAIN_SCRIPT_KEY, { force: true });
    }
  }

  /** List files; decides between device storage and the local fallback. */
  private async probe(): Promise<void> {
    try {
      const result = await listObjects(this.base);
      this.mode = 'device';
      this.message = '';
      this.files = toFiles(result);
      this.noteUsage(result.usage);
    } catch (err) {
      if (err instanceof StorageUnavailableError) {
        if (this.mode !== 'local') {
          emit('console-line', { text: `No device storage (${err.message}); files are kept in browser memory only.`, type: 'warn' });
        }
        this.mode = 'local';
        this.message = LOCAL_MESSAGE;
        this.files = this.localList();
      } else {
        // The device answered with an S3 error: storage exists but failed.
        this.mode = 'device';
        this.message = err instanceof S3Error && err.code === 'NoSuchBucket'
          ? 'File list failed: this firmware has no "app" storage bucket (update the device firmware)'
          : `File list failed: ${describeError(err)}`;
        emit('console-line', { text: this.message, type: 'error' });
      }
    }
  }

  /** Re-list after a mutation; failures only update the message. */
  private async listQuietly(): Promise<void> {
    if (this.mode === 'local') {
      this.files = this.localList();
      return;
    }
    try {
      const result = await listObjects(this.base);
      this.files = toFiles(result);
      this.noteUsage(result.usage);
    } catch (err) {
      this.message = `File list failed: ${describeError(err)}`;
    }
  }

  /** Keep the space a listing reported (each store's numbers are replaced by its own listings only). */
  private noteUsage(usage: StorageUsage | undefined): void {
    if (usage) this.storage = { ...this.storage, ...usage };
  }

  private async listReadOnly(bucket: ReadOnlyBucket): Promise<void> {
    try {
      const result = await listObjects(this.base, { bucket });
      const files = toFiles(result);
      this.readOnly = { ...this.readOnly, [bucket]: files };
      if (bucket === FONTS_BUCKET) void fontLoader.setListing(result.objects);
      this.noteUsage(result.usage);
    } catch (err) {
      this.note(`Listing bucket ${bucket} failed: ${describeError(err)}`, 'warn');
    }
  }

  private localList(): WorkspaceFile[] {
    return virtualFS.getFileList()
      .map(f => ({ key: f.name, size: f.size }))
      .sort((a, b) => a.key.localeCompare(b.key));
  }

  private backend(): Backend {
    if (this.mode === 'local') {
      return {
        exists: async (key) => virtualFS.getFile(key) !== undefined,
        read: async (key) => virtualFS.readText(key),
        write: async (key, text) => {
          if (!virtualFS.writeText(key, text)) throw new Error('browser file store is full');
          return { rebootRequired: false };
        },
        remove: async (key) => { virtualFS.deleteFile(key); },
      };
    }
    return {
      exists: (key) => headObject(this.base, key),
      read: async (key) => (await getObject(this.base, key))?.text ?? null,
      write: (key, text) => putObject(this.base, key, text),
      remove: (key) => deleteObject(this.base, key),
    };
  }

  private load(key: string, text: string, isNew: boolean): void {
    this.unread = false;
    this.suppressDirty = true;
    try {
      this.deps?.editor.open(text, fileKind(key), { isConfig: key === CONFIG_KEY });
    } finally {
      this.suppressDirty = false;
    }
    this.currentKey = key;
    this.dirty = false;
    this.isNew = isNew;
    // A (re)opened file: unsaved config.json edits are gone, show the stored config.
    this.preview.reset();
  }

  /** Show a config.json text on the virtual device; returns it parsed (null if invalid). */
  private applyConfig(text: string): unknown {
    let parsed: unknown;
    try {
      parsed = JSON.parse(text);
    } catch {
      return null;
    }
    const model = configToModel(parsed);
    setConfig(model.pages, model.connectors, model.names, model.fonts, model.styles);
    return parsed;
  }

  private async confirmDiscard(): Promise<boolean> {
    if (!this.dirty) return true;
    return this.ask(`Discard unsaved changes to ${displayPath(this.currentKey)}?`);
  }

  private async ask(message: string): Promise<boolean> {
    const fn = this.deps?.confirm ?? ((m: string) => (typeof window !== 'undefined' && typeof window.confirm === 'function' ? window.confirm(m) : true));
    return fn(message);
  }

  /**
   * Mirror an outcome to the console; problems also stay visible in Assets
   * until the next successful operation.
   */
  private note(text: string, type: 'info' | 'warn' | 'error' | 'result'): void {
    this.message = type === 'error' || type === 'warn' ? text : (this.mode === 'local' ? LOCAL_MESSAGE : '');
    emit('console-line', { text, type });
    this.publish();
  }

  private async withBusy<T>(fn: () => Promise<T>): Promise<T> {
    this.busy = true;
    this.publish();
    try {
      return await fn();
    } finally {
      this.busy = false;
      this.publish();
    }
  }

  private publish(): void {
    emit('workspace-change', this.snapshot());
  }

  private waitForEditor(): Promise<void> {
    if (this.deps?.editor.isReady()) return Promise.resolve();
    return new Promise(resolve => {
      const off = on('editor-ready', () => { off(); resolve(); });
    });
  }
}

export const workspace = new Workspace();
