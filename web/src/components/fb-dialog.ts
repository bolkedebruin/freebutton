import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';

export interface DialogChoice {
  id: string;
  label: string;
  /** The suggested action (filled button, gets the focus). */
  primary?: boolean;
  /** A destructive action (red). */
  danger?: boolean;
}

export interface DialogRequest {
  title: string;
  message?: string;
  choices: DialogChoice[];
  /** Label of the button that changes nothing (also Escape / a click outside). */
  cancel?: string;
}

interface Pending extends DialogRequest {
  resolve: (id: string | null) => void;
}

/**
 * In-app question dialog (instead of window.confirm): stays in the IDE's
 * look, names its actions, and can offer more than two. One at a time; a
 * second request waits for the first. Use askChoice / askConfirm.
 */
@customElement('fb-dialog')
export class FbDialog extends LitElement {
  createRenderRoot() { return this; }

  @state() private _current: Pending | null = null;
  private _queue: Pending[] = [];

  request(req: DialogRequest): Promise<string | null> {
    return new Promise(resolve => {
      this._queue.push({ ...req, resolve });
      if (!this._current) this._next();
    });
  }

  private _next() {
    this._current = this._queue.shift() ?? null;
    if (this._current) {
      this.updateComplete.then(() => this.querySelector<HTMLElement>('[data-primary]')?.focus());
    }
  }

  private _answer(id: string | null) {
    this._current?.resolve(id);
    this._next();
  }

  private _onKeydown = (e: KeyboardEvent) => {
    if (e.key === 'Escape') { e.stopPropagation(); this._answer(null); }
  };

  render() {
    const d = this._current;
    if (!d) return html``;
    return html`
      <div class="fixed inset-0 z-[60] flex items-center justify-center bg-black/60 backdrop-blur-sm"
        @click=${(e: MouseEvent) => { if (e.target === e.currentTarget) this._answer(null); }}
        @keydown=${this._onKeydown}>
        <div class="w-[400px] max-w-[calc(100vw-32px)] rounded-xl border border-border-default bg-bg-primary shadow-2xl p-5 flex flex-col gap-3"
          role="alertdialog" aria-modal="true" aria-label=${d.title} data-testid="dialog">
          <h2 class="text-sm font-semibold text-text-default">${d.title}</h2>
          ${d.message ? html`<p class="text-xs leading-relaxed text-gray-100 whitespace-pre-line break-words">${d.message}</p>` : ''}
          <div class="flex flex-wrap justify-end gap-2 pt-1">
            <button class="px-3 py-1.5 rounded-md text-xs text-gray-100 hover:bg-white/10" data-choice="cancel"
              @click=${() => this._answer(null)}>${d.cancel ?? 'Cancel'}</button>
            ${d.choices.map(c => html`
              <button data-choice=${c.id} ?data-primary=${!!c.primary}
                class="px-3 py-1.5 rounded-md text-xs transition-colors ${c.danger
                  ? 'bg-status-red/20 text-status-red hover:bg-status-red/30'
                  : c.primary ? 'bg-purple-90 text-white hover:bg-purple-active' : 'bg-white/5 text-text-default hover:bg-white/10'}"
                @click=${() => this._answer(c.id)}>${c.label}</button>
            `)}
          </div>
        </div>
      </div>
    `;
  }
}

function host(): FbDialog {
  let el = document.querySelector<FbDialog>('fb-dialog');
  if (!el) {
    el = document.createElement('fb-dialog') as FbDialog;
    document.body.appendChild(el);
  }
  return el;
}

/** Ask with named actions; resolves with the chosen id, or null when cancelled. */
export function askChoice(req: DialogRequest): Promise<string | null> {
  return host().request(req);
}

/**
 * Yes/no question. A message that starts with "Delete" / "Discard" /
 * "Remove" gets that word as its (red) action.
 */
export async function askConfirm(message: string, action?: string): Promise<boolean> {
  const verb = action ?? /^(Delete|Discard|Remove|Replace|Clear|Reboot)\b/.exec(message)?.[1] ?? 'OK';
  const danger = /^(Delete|Discard|Remove|Clear)$/.test(verb);
  const id = await askChoice({ title: message, choices: [{ id: 'ok', label: verb, primary: !danger, danger }] });
  return id === 'ok';
}
