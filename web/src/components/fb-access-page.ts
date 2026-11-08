import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { copyText, formatFbcred, peekImportedCredentials, type DeviceCredentials } from '../js/auth.js';

const INPUT_CLASS = 'w-full bg-white/5 border border-white/10 rounded-lg px-3 py-2.5 text-sm text-text-default font-mono outline-none focus:border-purple-80';
const BUTTON_CLASS = 'flex-1 px-4 py-3 rounded-lg text-sm font-medium transition-colors';

/**
 * Phone page for the device's QR link (main.ts loads it instead of the IDE
 * when a phone opens http://<device>/#fbcred=...). The credentials are stored
 * in this browser already; the page offers to keep them elsewhere:
 *   - Save: a real login form (username = key id, password = secret), so the
 *     password manager (iCloud Keychain, Google) offers to store them and can
 *     fill them in the IDE later. The form never goes over the network.
 *   - Copy: FBCRED:<key id>:<secret> (execCommand fallback: plain HTTP has no
 *     navigator.clipboard).
 */
@customElement('fb-access-page')
export class FbAccessPage extends LitElement {
  createRenderRoot() { return this; }

  private _creds: DeviceCredentials | null = peekImportedCredentials();
  @state() private _showSecret = false;
  @state() private _saved = false;
  @state() private _status: { text: string; error: boolean } | null = null;

  private _onSubmit(e: Event) {
    e.preventDefault();
    // Removing the submitted form is what password managers take as a
    // successful sign-in (no navigation: the secret stays on the phone).
    this._saved = true;
    this._status = { text: 'If your password manager offered to save it, the key is stored there for this device.', error: false };
  }

  private async _copy() {
    if (!this._creds) return;
    const ok = await copyText(formatFbcred(this._creds));
    this._status = ok
      ? { text: 'Copied as FBCRED:<key id>:<secret>. Paste it in Settings → Device access of the IDE.', error: false }
      : { text: 'This browser did not allow copying. Tap Show and copy the fields by hand.', error: true };
  }

  render() {
    const host = location.host;
    const creds = this._creds;
    return html`
      <main class="min-h-dvh bg-bg-primary text-text-default px-4 py-6 flex flex-col gap-5 max-w-md mx-auto">
        <header>
          <h1 class="text-lg font-semibold">Device access</h1>
          <p class="text-sm text-gray-200 mt-1">FreeButton at <span class="font-mono">${host}</span></p>
        </header>

        ${creds ? html`
          <p class="text-sm leading-relaxed">
            The access key is stored in this browser. Keep a copy in your password manager, or copy it
            to paste into the IDE on another device.
          </p>

          ${this._saved ? html`
            <div class="rounded-lg bg-white/5 px-3 py-3 text-sm" data-testid="access-saved">Saved.</div>
          ` : html`
            <form class="flex flex-col gap-3" autocomplete="on" data-testid="access-form" @submit=${this._onSubmit}>
              <label class="flex flex-col gap-1">
                <span class="text-xs text-gray-100">Access key id</span>
                <input type="text" name="username" autocomplete="username" readonly class=${INPUT_CLASS}
                  .value=${creds.accessKeyId} data-testid="access-akid" />
              </label>
              <label class="flex flex-col gap-1">
                <span class="text-xs text-gray-100">Secret</span>
                <div class="flex gap-2">
                  <input type=${this._showSecret ? 'text' : 'password'} name="password" autocomplete="current-password"
                    readonly class=${INPUT_CLASS} .value=${creds.secretAccessKey} data-testid="access-secret" />
                  <button type="button" class="px-3 rounded-lg text-sm text-gray-100 bg-white/5"
                    @click=${() => { this._showSecret = !this._showSecret; }}>${this._showSecret ? 'Hide' : 'Show'}</button>
                </div>
              </label>
              <div class="flex gap-3 mt-1">
                <button type="submit" class="${BUTTON_CLASS} bg-purple-90 text-white" data-testid="access-save">Save to password manager</button>
              </div>
            </form>
          `}

          <div class="flex gap-3">
            <button type="button" class="${BUTTON_CLASS} bg-white/10 text-text-default" data-testid="access-copy"
              @click=${() => this._copy()}>Copy</button>
          </div>

          ${this._status ? html`
            <p class="text-sm ${this._status.error ? 'text-status-red' : 'text-gray-100'}" data-testid="access-status">${this._status.text}</p>
          ` : ''}
        ` : html`<p class="text-sm text-status-red">No access key in the link.</p>`}

        <p class="text-xs leading-relaxed text-gray-200 mt-auto">
          The IDE needs a larger screen: open <span class="font-mono">http://${host}/</span> on a computer; your
          password manager can fill in the key in Settings → Device access.
          The connection to the device is not encrypted: the key signs your requests, but others on the
          network can read the traffic.
          <a class="underline" href="/">Open the IDE anyway</a>
        </p>
      </main>
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'fb-access-page': FbAccessPage;
  }
}
