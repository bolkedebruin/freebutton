import { LitElement, html, nothing } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { emit, on, type WorkspaceSnapshot } from '../js/app-state.js';
import { workspace } from '../js/workspace.js';
import { setDeviceInfoVisible } from '../js/device-info.js';
import { formatFileSize } from '../js/virtual-fs.js';
import { captureScreenshots, drawScreenshot, saveCanvasPng, type Screenshot } from '../js/screenshot.js';
import { CameraIcon } from '../icons/index.js';
import { updateDevice, updateFileProblem, type UpdateKind, type UpdateProgress } from '../js/device-update.js';
import { askConfirm } from './fb-dialog.js';
import './fb-device-info.js';

/** Who polls the device info for this view (device-info.ts). */
const INFO_WATCHER = 'device-view';

/**
 * Main-pane Device view (the Device button of the nav bar): what the device
 * reports (network, firmware, free memory), what is stored on it, and the
 * actions on the device itself: take a screenshot of its displays, reboot,
 * change the access credentials, install new firmware or a new filesystem
 * image (with the upload and the restart shown as they happen).
 *
 * Screenshot: the device repaints its displays and writes the frames to its
 * storage (a frame does not fit its RAM); the IDE downloads them, deletes
 * them from the device at once and shows them here. Save as PNG stores one
 * on this computer. Nothing stays on the device.
 */
@customElement('fb-device-view')
export class FbDeviceView extends LitElement {
  createRenderRoot() { return this; }

  @state() private _ws: WorkspaceSnapshot = workspace.snapshot();
  @state() private _shots: Screenshot[] = [];
  @state() private _capturing = false;
  @state() private _shotError = '';
  @state() private _shotTime = '';
  @state() private _rebooting = false;
  /** The update in progress, or its last result, with what it installs. */
  @state() private _fw: UpdateProgress | null = null;
  @state() private _fwName = '';
  @state() private _fwKind: UpdateKind = 'firmware';
  private _unsub?: () => void;

  connectedCallback() {
    super.connectedCallback();
    this._ws = workspace.snapshot();
    this._unsub = on('workspace-change', (e) => { this._ws = e.detail; });
    setDeviceInfoVisible(true, INFO_WATCHER);
    void workspace.loadReadOnly(['fonts', 'web']);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsub?.();
    setDeviceInfoVisible(false, INFO_WATCHER);
  }

  protected updated() {
    this.querySelectorAll<HTMLCanvasElement>('canvas[data-shot]').forEach(canvas => {
      const shot = this._shots.find(s => s.name === canvas.dataset.shot);
      if (shot && canvas.dataset.drawn !== this._shotTime) {
        drawScreenshot(canvas, shot);
        canvas.dataset.drawn = this._shotTime;
      }
    });
  }

  private async _screenshot() {
    if (this._capturing) return;
    this._capturing = true;
    this._shotError = '';
    try {
      this._shots = await captureScreenshots(workspace.deviceBase);
      this._shotTime = new Date().toLocaleTimeString();
    } catch (err) {
      this._shotError = err instanceof Error ? err.message : String(err);
    } finally {
      this._capturing = false;
      // The file list may have shown the temporary files for a moment.
      void workspace.refresh();
    }
  }

  private _save(name: string) {
    const canvas = this.querySelector<HTMLCanvasElement>(`canvas[data-shot="${name}"]`);
    if (canvas) saveCanvasPng(canvas, `${name}.png`);
  }

  private async _reboot() {
    if (!(await askConfirm('Reboot the device?\nIt is back in a few seconds; scripts start again.', 'Reboot'))) return;
    this._rebooting = true;
    await workspace.reboot();
    this._rebooting = false;
  }

  private _renderStorage() {
    const ws = this._ws;
    const sum = (files: { size: number }[] | undefined) => (files ? formatFileSize(files.reduce((n, f) => n + f.size, 0)) : '…');
    const rows: [string, string, string][] = [
      ['Your files', `${ws.files.length}`, sum(ws.files)],
      ['Fonts', ws.readOnly?.fonts ? `${ws.readOnly.fonts.length}` : '…', sum(ws.readOnly?.fonts)],
      ['IDE (this page)', ws.readOnly?.web ? `${ws.readOnly.web.length}` : '…', sum(ws.readOnly?.web)],
    ];
    const st = ws.storage;
    const space: [string, string][] = [];
    if (st?.fs) space.push(['Filesystem', `${formatFileSize(st.fs.used)} used · ${formatFileSize(Math.max(0, st.fs.total - st.fs.used))} free`]);
    if (st?.media) space.push(['Media store (fonts, images)', `${formatFileSize(st.media.used)} used · ${formatFileSize(st.media.free)} free`]);
    return html`
      <div class="flex flex-col gap-1.5" data-testid="device-storage">
        ${rows.map(([label, count, size]) => html`
          <div class="flex items-center justify-between gap-3 text-[11px]">
            <span class="text-gray-200">${label}</span>
            <span class="text-white font-mono">${count} files · ${size}</span>
          </div>
        `)}
        ${space.map(([label, text]) => html`
          <div class="flex items-center justify-between gap-3 text-[11px]" data-testid="device-space">
            <span class="text-gray-200">${label}</span>
            <span class="text-white font-mono">${text}</span>
          </div>
        `)}
      </div>
    `;
  }

  private get _updating(): boolean {
    return this._fw?.phase === 'uploading' || this._fw?.phase === 'applying';
  }

  private async _updatePicked(kind: UpdateKind, e: Event) {
    const input = e.target as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file || this._updating) return;
    this._fwName = file.name;
    this._fwKind = kind;
    const problem = await updateFileProblem(kind, file);
    if (problem) { this._fw = { phase: 'failed', message: problem }; return; }
    const what = `${file.name} (${formatFileSize(file.size)})`;
    const ok = await askConfirm(kind === 'firmware'
      ? `Install ${what} as the firmware of this device?\nThe device restarts. Your files and config.json stay as they are.`
      : `Replace the filesystem of this device with ${what}?\nThis replaces EVERY file on it: config.json, your scripts, images and the IDE. The device restarts, and scripts/main.js must be deployed again. Fonts are not affected.`,
      kind === 'firmware' ? 'Install' : 'Replace all files');
    if (!ok) return;
    await updateDevice(workspace.deviceBase, kind, file, (p) => { this._fw = p; });
    void workspace.refresh();
  }

  private _renderUpdate() {
    const online = this._ws.mode === 'device';
    const fw = this._fw;
    const percent = fw?.phase === 'uploading' ? fw.percent ?? 0 : 100;
    const isFs = this._fwKind === 'filesystem';
    const label = !fw ? ''
      : fw.phase === 'uploading' ? `Uploading ${this._fwName} · ${percent}%`
      : fw.phase === 'applying' ? `Applying: the device restarts ${isFs ? 'with the new filesystem' : 'into the new firmware'}…`
      : fw.phase === 'done' ? `Updated. ${fw.message ?? ''}${isFs ? ' Reload this page to load the IDE from it.' : ''}`
      : `Not updated. ${fw.message ?? ''}`;
    const button = (kind: UpdateKind, text: string, hint: string) => html`
      <input type="file" accept=".bin,application/octet-stream" class="hidden" data-testid="${kind}-input"
        @change=${(e: Event) => this._updatePicked(kind, e)} />
      <button class="px-3 py-1.5 rounded-md text-xs bg-white/5 text-text-default hover:bg-white/10 disabled:opacity-40"
        data-testid="update-${kind}" ?disabled=${!online || this._updating}
        title=${online ? hint : 'Connect to a device first'}
        @click=${() => this.querySelector<HTMLInputElement>(`[data-testid="${kind}-input"]`)?.click()}>
        ${this._updating && this._fwKind === kind ? 'Updating…' : text}</button>
    `;
    return html`
      <section class="rounded-xl border border-border-default p-4 flex flex-col gap-3" data-testid="device-update">
        <div class="flex flex-wrap items-center gap-3">
          <div class="flex-1 min-w-[16rem]">
            <h3 class="text-xs font-semibold text-text-default">Update</h3>
            <div class="text-[11px] text-gray-200">
              Firmware: a firmware.bin; your files stay. Filesystem: a littlefs.bin; it replaces every file on the device, the IDE included.
            </div>
          </div>
          ${button('firmware', 'Update firmware…', 'Choose a firmware.bin and install it')}
          ${button('filesystem', 'Update filesystem…', 'Choose a littlefs.bin: it replaces every file on the device')}
        </div>
        ${fw ? html`
          <div class="flex flex-col gap-1.5" data-testid="update-progress" data-phase=${fw.phase}>
            ${this._updating ? html`
              <div class="h-1.5 rounded-full bg-white/10 overflow-hidden" role="progressbar"
                aria-valuemin="0" aria-valuemax="100" aria-valuenow=${fw.phase === 'uploading' ? percent : nothing}>
                <div class="h-full bg-purple-90 ${fw.phase === 'applying' ? 'animate-pulse' : ''}" style="width: ${percent}%; transition: width 200ms;"></div>
              </div>
            ` : ''}
            <div class="text-xs ${fw.phase === 'failed' ? 'text-status-red' : fw.phase === 'done' ? 'text-status-green' : 'text-gray-100'}"
              role="status">${label}</div>
          </div>
        ` : ''}
      </section>
    `;
  }

  private _renderScreenshots() {
    const online = this._ws.mode === 'device';
    return html`
      <section class="rounded-xl border border-border-default p-4 flex flex-col gap-3" data-testid="device-screenshots">
        <div class="flex items-center gap-3">
          <div class="flex-1 min-w-0">
            <h3 class="text-xs font-semibold text-text-default">Screenshot</h3>
            <div class="text-[11px] text-gray-200">
              ${this._shotTime ? `Taken at ${this._shotTime}. Not kept on the device.` : 'What the displays show now. Not kept on the device.'}
            </div>
          </div>
          <button class="px-3 py-1.5 rounded-md text-xs bg-purple-90 text-white hover:bg-purple-active disabled:opacity-40 flex items-center gap-2"
            data-testid="take-screenshot" ?disabled=${!online || this._capturing}
            title=${online ? 'Take a screenshot of every display' : 'Connect to a device first'}
            @click=${() => this._screenshot()}>
            <span class="w-4 h-4 flex items-center justify-center [&>svg]:w-4 [&>svg]:h-4">${CameraIcon()}</span>
            ${this._capturing ? 'Taking screenshot…' : this._shots.length ? 'Take again' : 'Take screenshot'}
          </button>
        </div>
        ${this._shotError ? html`<div class="text-xs text-status-red" role="alert" data-testid="screenshot-error">${this._shotError}</div>` : ''}
        ${this._shots.length ? html`
          <div class="flex flex-wrap items-start gap-4">
            ${this._shots.map(shot => html`
              <figure class="flex flex-col gap-2" data-testid="screenshot">
                <canvas data-shot=${shot.name} class="rounded-lg border border-white/10 max-w-full" style="image-rendering: pixelated;"></canvas>
                <figcaption class="flex items-center justify-between gap-3">
                  <span class="text-[10px] text-gray-200 font-mono">${shot.name} · ${shot.width} × ${shot.height}</span>
                  <button class="px-2.5 py-1 rounded-md text-[11px] bg-white/5 text-text-default hover:bg-white/10"
                    data-testid="save-screenshot" @click=${() => this._save(shot.name)}>Save as PNG</button>
                </figcaption>
              </figure>
            `)}
          </div>
        ` : ''}
      </section>
    `;
  }

  render() {
    const online = this._ws.mode === 'device';
    return html`
      <div class="h-full overflow-y-auto custom-scrollbar pane-scroll p-6 flex flex-col gap-4" data-testid="device-view">
        <div class="flex flex-wrap items-center gap-2">
          <h2 class="text-sm font-semibold text-text-default mr-auto">Device</h2>
          <button class="px-3 py-1.5 rounded-md text-xs bg-white/5 text-text-default hover:bg-white/10"
            data-testid="device-access" @click=${() => emit('open-settings')}>Access credentials…</button>
          <button class="px-3 py-1.5 rounded-md text-xs bg-white/5 text-text-default hover:bg-white/10 disabled:opacity-40"
            data-testid="device-reboot" ?disabled=${!online || this._rebooting}
            @click=${() => this._reboot()}>${this._rebooting ? 'Rebooting…' : 'Reboot'}</button>
        </div>
        ${online ? '' : html`
          <div class="text-xs text-gray-100 rounded-md bg-white/5 px-3 py-2" data-testid="device-offline">
            No device reachable. Enter its address at the top right and connect.
          </div>
        `}
        ${this._renderScreenshots()}
        ${this._renderUpdate()}
        <div class="grid gap-4" style="grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));">
          <section class="rounded-xl border border-border-default p-4 flex flex-col gap-3">
            <h3 class="text-xs font-semibold text-text-default">Status</h3>
            <fb-device-info showName></fb-device-info>
          </section>
          <section class="rounded-xl border border-border-default p-4 flex flex-col gap-3">
            <h3 class="text-xs font-semibold text-text-default">Storage</h3>
            ${this._renderStorage()}
            <div class="text-[11px] leading-relaxed text-gray-200">
              The device name, WiFi, MQTT brokers and time server are in config.json; the device reads it at boot.
            </div>
            <button class="self-start px-3 py-1.5 rounded-md text-xs bg-white/5 text-text-default hover:bg-white/10"
              data-testid="device-open-config"
              @click=${async () => { if (await workspace.open('config.json')) emit('main-view', { view: 'editor' }); }}>Open config.json</button>
          </section>
        </div>
      </div>
    `;
  }
}
