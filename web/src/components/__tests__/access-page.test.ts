/**
 * Phone page for the device's QR link (fb-access-page).
 */
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { getCredentials, importCredentialsFromLocation } from '../../js/auth.js';

const AKID = 'FBABCDEFGHIJKLMNOP23';  // test values
const SECRET = 'abcdefghijABCDEFGHIJ0123456789klmnopqrst';

function memoryStorage(): Storage {
  const data = new Map<string, string>();
  return {
    get length() { return data.size; },
    clear: () => data.clear(),
    getItem: (k: string) => data.get(k) ?? null,
    key: (i: number) => [...data.keys()][i] ?? null,
    removeItem: (k: string) => { data.delete(k); },
    setItem: (k: string, v: string) => { data.set(k, String(v)); },
  };
}

const q = <T extends Element>(el: Element, id: string) => el.querySelector(`[data-testid="${id}"]`) as T | null;

describe('fb-access-page', () => {
  let el: HTMLElement & { updateComplete: Promise<unknown> };

  beforeEach(async () => {
    vi.stubGlobal('localStorage', memoryStorage());
    importCredentialsFromLocation({ hash: `#fbcred=${AKID}:${SECRET}`, pathname: '/', search: '' },
      { state: null, replaceState: () => {} });
    await import('../fb-access-page.js');
    el = document.createElement('fb-access-page') as typeof el;
    document.body.appendChild(el);
    await el.updateComplete;
  });
  afterEach(() => { el.remove(); vi.unstubAllGlobals(); });

  it('shows the imported key as a login form for the password manager', () => {
    expect(getCredentials('')?.accessKeyId).toBe(AKID);
    const akid = q<HTMLInputElement>(el, 'access-akid')!;
    const secret = q<HTMLInputElement>(el, 'access-secret')!;
    expect(akid.value).toBe(AKID);
    expect(akid.autocomplete).toBe('username');
    expect(secret.value).toBe(SECRET);
    expect(secret.type).toBe('password');
    expect(secret.autocomplete).toBe('current-password');
  });

  it('Save submits the form without navigating and replaces it', async () => {
    const form = q<HTMLFormElement>(el, 'access-form')!;
    const ev = new Event('submit', { cancelable: true });
    form.dispatchEvent(ev);
    expect(ev.defaultPrevented).toBe(true);
    await el.updateComplete;
    expect(q(el, 'access-form')).toBeNull();
    expect(q(el, 'access-saved')).not.toBeNull();
  });

  it('Copy puts the FBCRED text on the clipboard (execCommand on plain HTTP)', async () => {
    const exec = vi.fn(() => true);
    (document as unknown as { execCommand: unknown }).execCommand = exec;
    q<HTMLButtonElement>(el, 'access-copy')!.click();
    await new Promise((r) => setTimeout(r, 0));
    await el.updateComplete;
    expect(exec).toHaveBeenCalledWith('copy');
    expect(q(el, 'access-status')?.textContent).toContain('Copied');
  });
});
