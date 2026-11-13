import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { on } from '../js/app-state.js';
import { XIcon } from '../icons/index.js';
import {
  clearCredentials, copyText, deviceOrigin, formatFbcred, getCredentials, normalizeCredentials, parseFbcred,
  setCredentials, takeImportedCredentials, validateCredentials,
} from '../js/auth.js';
import { workspace } from '../js/workspace.js';

const INPUT_CLASS = 'w-full bg-white/5 border border-white/10 rounded-lg px-2 py-1.5 text-xs text-text-default font-mono outline-none focus:border-purple-80';
const BUTTON_CLASS = 'px-3 py-1.5 rounded-md text-xs transition-colors disabled:opacity-50';

@customElement('fb-settings-modal')
export class FbSettingsModal extends LitElement {
  createRenderRoot() { return this; }

  @state() private _open = false;
  @state() private _rippleEnabled = true;

  // Device access (SigV4 credentials, src/js/auth.ts)
  @state() private _akid = '';
  @state() private _secret = '';
  @state() private _showSecret = false;
  @state() private _fbcred = '';
  /** Why Settings opened by itself (the device refused a request). */
  @state() private _authAlert = '';
  @state() private _authStatus: { text: string; error: boolean } | null = null;
  /**
   * Show the stored credentials as a summary instead of the form. After Save
   * this REMOVES the submitted form from the page, which is what a browser's
   * password manager takes as a finished sign-in: it offers to save right
   * then, with the dialog still open (and not only when the dialog closes).
   */
  @state() private _showSummary = false;

  private _unsubs: Array<() => void> = [];

  connectedCallback() {
    super.connectedCallback();
    this._rippleEnabled = document.body.style.animation !== 'none';
    this._unsubs.push(
      on('open-settings', () => { this._openSettings(); }),
      on('auth-required', (e) => {
        this._openSettings();
        this._authAlert = e.detail.message || 'The device requires access credentials.';
        // The stored ones were refused: go straight to the fields.
        this._showSummary = false;
      }),
    );
    // Opened from the device's QR link: the credentials are stored already;
    // show them so they can be copied or saved in the password manager.
    const imported = takeImportedCredentials();
    if (imported) {
      this._openSettings();
      this._akid = imported.accessKeyId;
      this._secret = imported.secretAccessKey;
      // The form, so that Save can hand them to the password manager.
      this._showSummary = false;
      this._authStatus = {
        text: 'Received from the device and stored in this browser. Save puts them in your password manager; Copy copies them as FBCRED text.',
        error: false,
      };
    }
  }

  private async _copyCredentials() {
    const creds = normalizeCredentials(this._akid, this._secret);
    if (validateCredentials(creds)) { this._authStatus = { text: 'Nothing to copy: enter valid credentials first.', error: true }; return; }
    const ok = await copyText(formatFbcred(creds));
    this._authStatus = ok
      ? { text: 'Copied as FBCRED:<access key id>:<secret>.', error: false }
      : { text: 'This browser did not allow copying; select the fields and copy them by hand.', error: true };
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._unsubs.forEach(fn => fn());
    this._unsubs = [];
  }

  /** Origin of the device the IDE talks to (same origin unless the REPL URL points elsewhere). */
  private get _deviceOrigin(): string {
    return deviceOrigin(workspace.deviceBase);
  }

  private _openSettings() {
    if (!this._open) {
      const stored = getCredentials(this._deviceOrigin);
      this._akid = stored?.accessKeyId ?? '';
      this._secret = stored?.secretAccessKey ?? '';
      this._showSecret = false;
      this._fbcred = '';
      this._authStatus = null;
      this._authAlert = '';
      this._showSummary = !!stored;
    }
    this._open = true;
  }

  private _close() {
    this._open = false;
    this._authAlert = '';
  }

  private _onFbcredInput(value: string) {
    this._fbcred = value;
    if (!value.trim()) { this._authStatus = null; return; }
    const creds = parseFbcred(value);
    if (creds) {
      this._akid = creds.accessKeyId;
      this._secret = creds.secretAccessKey;
      this._authStatus = { text: 'Read the access key id and secret. Save to use them.', error: false };
    } else {
      this._authStatus = { text: 'Not a valid FBCRED:<access key id>:<secret> string.', error: true };
    }
  }

  private _onAkidInput(value: string) {
    // Pasting the whole FBCRED string into the first field works too.
    if (/^\s*FBCRED:/i.test(value)) { this._onFbcredInput(value); return; }
    this._akid = value;
  }

  private _saveCredentials() {
    const creds = normalizeCredentials(this._akid, this._secret);
    const error = validateCredentials(creds);
    if (error) { this._authStatus = { text: error, error: true }; return; }
    try {
      setCredentials(creds, this._deviceOrigin);
    } catch (err) {
      this._authStatus = { text: err instanceof Error ? err.message : String(err), error: true };
      return;
    }
    this._akid = creds.accessKeyId;
    this._secret = creds.secretAccessKey;
    this._fbcred = '';
    this._authAlert = '';
    this._authStatus = { text: 'Saved in this browser. Requests to the device are now signed.', error: false };
    this._showSummary = true;
    void this._offerToPasswordManager(creds.accessKeyId, creds.secretAccessKey);
  }

  /**
   * Chromium: ask the password manager directly (Credential Management API).
   * Other browsers offer to save when the submitted form disappears, which
   * the summary does. Failing or declining changes nothing for the IDE.
   */
  private async _offerToPasswordManager(id: string, password: string) {
    const Ctor = (window as unknown as { PasswordCredential?: new (data: { id: string; password: string; name?: string }) => Credential }).PasswordCredential;
    if (!Ctor || !navigator.credentials?.store) return;
    try {
      await navigator.credentials.store(new Ctor({ id, password, name: `FreeButton ${this._deviceOrigin}` }));
    } catch {
      // Not allowed here (insecure context, user setting): the form path remains.
    }
  }

  private _clearCredentials() {
    clearCredentials(this._deviceOrigin);
    this._akid = '';
    this._secret = '';
    this._fbcred = '';
    this._showSummary = false;
    this._authStatus = { text: 'Credentials removed from this browser.', error: false };
  }

  private _renderDeviceAccess() {
    const origin = this._deviceOrigin;
    const stored = getCredentials(origin);
    return html`
      <section class="flex flex-col gap-2" data-testid="device-access">
        <div>
          <div class="text-sm text-text-default">Device access</div>
          <div class="text-xs text-gray-200 mt-0.5" data-testid="device-access-state">
            ${stored ? html`Signing requests to ${origin} as <span class="font-mono">${stored.accessKeyId}</span>`
                     : html`No credentials stored for ${origin}`}
          </div>
        </div>

        ${this._authAlert ? html`
          <div class="text-xs leading-snug rounded-md px-2 py-1.5 bg-status-red/10 text-status-red" role="alert"
            data-testid="device-access-alert">${this._authAlert}</div>
        ` : ''}

        ${this._showSummary && stored ? html`
          <div class="flex items-center gap-2 rounded-lg bg-white/5 px-3 py-2" data-testid="credentials-summary">
            <span class="w-2 h-2 rounded-full bg-status-green shrink-0" aria-hidden="true"></span>
            <span class="flex-1 min-w-0 text-xs text-text-default truncate">Access key <span class="font-mono">${stored.accessKeyId}</span></span>
            <button type="button" data-testid="credentials-edit"
              class="${BUTTON_CLASS} text-gray-100 bg-white/5 hover:bg-white/10"
              @click=${() => { this._showSummary = false; this._authStatus = null; }}>Change</button>
            <button type="button" data-testid="credentials-copy"
              class="${BUTTON_CLASS} text-gray-100 bg-white/5 hover:bg-white/10"
              @click=${() => this._copyCredentials()}>Copy</button>
            <button type="button" data-testid="credentials-clear"
              class="${BUTTON_CLASS} text-status-red hover:bg-white/10"
              @click=${() => this._clearCredentials()}>Remove</button>
          </div>
        ` : html`
        <label class="flex flex-col gap-1">
          <span class="text-xs text-gray-100">Paste FBCRED (from the QR code)</span>
          <input type="text" data-testid="fbcred-input" class=${INPUT_CLASS} autocomplete="off" spellcheck="false"
            placeholder="FBCRED:FB...:..."
            .value=${this._fbcred}
            @input=${(e: Event) => this._onFbcredInput((e.target as HTMLInputElement).value)} />
        </label>
        <form class="flex flex-col gap-2" data-testid="credentials-form" autocomplete="on"
          @submit=${(e: Event) => { e.preventDefault(); this._saveCredentials(); }}>
        <label class="flex flex-col gap-1">
          <span class="text-xs text-gray-100">Access key id</span>
          <input type="text" name="username" data-testid="akid-input" class=${INPUT_CLASS} autocomplete="username" spellcheck="false"
            autocapitalize="characters"
            placeholder="FB + 18 characters"
            .value=${this._akid}
            @input=${(e: Event) => this._onAkidInput((e.target as HTMLInputElement).value)} />
        </label>
        <label class="flex flex-col gap-1">
          <span class="text-xs text-gray-100">Secret</span>
          <div class="flex gap-2">
            <input type=${this._showSecret ? 'text' : 'password'} name="password" data-testid="secret-input" class=${INPUT_CLASS}
              autocomplete="current-password" spellcheck="false" autocapitalize="off" placeholder="40 characters"
              .value=${this._secret}
              @input=${(e: Event) => { this._secret = (e.target as HTMLInputElement).value; }} />
            <button type="button" data-testid="secret-toggle"
              class="${BUTTON_CLASS} shrink-0 text-gray-100 bg-white/5 hover:bg-white/10"
              @click=${() => { this._showSecret = !this._showSecret; }}>${this._showSecret ? 'Hide' : 'Show'}</button>
          </div>
        </label>

        <div class="flex items-center gap-2">
          <button type="submit" data-testid="credentials-save"
            class="${BUTTON_CLASS} bg-purple-90 text-white hover:bg-purple-80"
            ?disabled=${!this._akid.trim() || !this._secret.trim()}>Save</button>
          <button type="button" data-testid="credentials-copy"
            class="${BUTTON_CLASS} text-gray-100 bg-white/5 hover:bg-white/10"
            ?disabled=${!this._akid.trim() || !this._secret.trim()}
            @click=${() => this._copyCredentials()}>Copy</button>
          <button type="button" data-testid="credentials-clear"
            class="${BUTTON_CLASS} text-gray-100 hover:bg-white/10"
            ?disabled=${!stored && !this._akid && !this._secret}
            @click=${() => this._clearCredentials()}>Clear</button>
        </div>
        </form>
        `}

        ${this._authStatus ? html`
          <div class="text-xs ${this._authStatus.error ? 'text-status-red' : 'text-gray-100'}"
            data-testid="device-access-status">${this._authStatus.text}</div>
        ` : ''}

        <div class="text-[11px] leading-snug text-gray-200">
          Hold the device's factory button for 6 s: it shows its access key id and secret, and a QR code
          that opens this IDE with them on a phone. They are stored in this browser only. On Save your
          browser offers to keep them in its password manager (username = key id, password = secret),
          which can fill them in here later. The connection to the device is not encrypted:
          signing proves that the requests come from you, but others on the network can read the traffic.
        </div>
      </section>
      <div class="h-px bg-border-default"></div>
    `;
  }

  private _onBackdropClick(e: MouseEvent) {
    if (e.target === e.currentTarget) this._close();
  }

  private _toggleRipple() {
    this._rippleEnabled = !this._rippleEnabled;
    document.body.style.animation = this._rippleEnabled ? '' : 'none';
  }

  render() {
    if (!this._open) return html``;

    return html`
      <div
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm"
        @click=${this._onBackdropClick}
      >
        <div class="w-[420px] max-w-[calc(100vw-32px)] max-h-[calc(100dvh-32px)] overflow-y-auto rounded-xl border border-border-default bg-bg-primary shadow-2xl">
          <!-- Header -->
          <div class="flex items-center justify-between px-5 py-4 border-b border-border-default">
            <h2 class="text-sm font-semibold text-text-default">Settings</h2>
            <button
              class="p-1 rounded-md text-gray-200 hover:text-text-default hover:bg-white/5 transition-colors"
              title="Close" aria-label="Close settings"
              @click=${this._close}
            >${XIcon()}</button>
          </div>

          <!-- Body -->
          <div class="px-5 py-4 flex flex-col gap-4 max-h-[80vh] overflow-y-auto">
            ${this._renderDeviceAccess()}

            <!-- Ripple Effect -->
            <div class="flex items-center justify-between gap-3">
              <div id="ripple-label">
                <div class="text-sm text-text-default">Background ripple</div>
                <div class="text-xs text-gray-200 mt-0.5">Animated gradient on the background</div>
              </div>
              <button type="button" role="switch" aria-checked=${this._rippleEnabled ? 'true' : 'false'} aria-labelledby="ripple-label"
                data-testid="ripple-toggle"
                class="relative shrink-0 w-9 h-5 rounded-full transition-colors ${this._rippleEnabled ? 'bg-purple-90' : 'bg-gray-80'}"
                @click=${this._toggleRipple}
              >
                <span class="absolute top-0.5 left-0.5 w-4 h-4 rounded-full bg-white transition-transform ${this._rippleEnabled ? 'translate-x-4' : ''}"></span>
              </button>
            </div>
          </div>
        </div>
      </div>
    `;
  }
}
