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
 * State changes are published as `workspace-change` snapshots on the
 * app-state bus; progress/errors also go to the console pane.
 */
import { emit, on, type WorkspaceFile, type WorkspaceMode, type WorkspaceSnapshot } from './app-state';
import {
  CONFIG_KEY, MAIN_SCRIPT_KEY, MAIN_SCRIPT_TEMPLATE,
  canDeleteKey, fileKind, initialContentFor, isEditableKey, normalizeKey,
  resolveNewFileKey, utf8Length, validateBody, validateKey, type FileKind,
} from './fs-rules';
import {
  S3Error, StorageUnavailableError, deleteObject, getObject, headObject, listObjects, putObject, rebootDevice,
  type ListResult, type ReadOnlyBucket,
} from './device-storage';
import { virtualFS } from './virtual-fs';
import { parseConfigText } from './config-model';
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
  /** Ask the user (discard changes / delete). Defaults to window.confirm. */
  confirm?: (message: string) => boolean;
  /** Used for POST /api/reboot only (storage goes through the S3 client). */
  fetchImpl?: typeof fetch;
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

export class Workspace {
  private mode: WorkspaceMode = 'probing';
  private message = '';
  private files: WorkspaceFile[] = [];
  private busy = false;
  private currentKey = MAIN_SCRIPT_KEY;
  private dirty = false;
  private isNew = false;
  private readOnly: Partial<Record<ReadOnlyBucket, WorkspaceFile[]>> = {};

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
    };
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
    if (!opts.force && key !== this.currentKey && !this.confirmDiscard()) return false;

    return this.withBusy(async () => {
      let text: string | null;
      try {
        text = await this.backend().read(key);
      } catch (err) {
        this.note(`Could not open ${displayPath(key)}: ${describeError(err)}`, 'error');
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
        this.applyConfig(text);
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
    if (!this.confirmDiscard()) return false;

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
    if (!opts.skipConfirm && !this.ask(`Delete ${displayPath(key)}${this.mode === 'device' ? ' from the device' : ''}?`)) return false;

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
    if (removed && key === this.currentKey) {
      // The open file is gone: drop its buffer and go back to the boot script.
      await this.open(MAIN_SCRIPT_KEY, { force: true });
    }
    return removed;
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

  private async reload(): Promise<void> {
    await this.withBusy(async () => {
      await this.probe();
      if (this.files.some(f => f.key === CONFIG_KEY)) {
        try {
          const text = await this.backend().read(CONFIG_KEY);
          if (text !== null) this.applyConfig(text);
        } catch (err) {
          this.note(`Could not read config.json: ${describeError(err)}`, 'warn');
        }
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
    } catch (err) {
      this.message = `File list failed: ${describeError(err)}`;
    }
  }

  private async listReadOnly(bucket: ReadOnlyBucket): Promise<void> {
    try {
      const files = toFiles(await listObjects(this.base, { bucket }));
      this.readOnly = { ...this.readOnly, [bucket]: files };
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

  private applyConfig(text: string): void {
    const model = parseConfigText(text);
    if (model) setConfig(model.pages, model.connectors, model.names);
  }

  private confirmDiscard(): boolean {
    if (!this.dirty) return true;
    return this.ask(`Discard unsaved changes to ${displayPath(this.currentKey)}?`);
  }

  private ask(message: string): boolean {
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
