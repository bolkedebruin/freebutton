/**
 * Settings → Device access: credential entry and the auth-required flow.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import '../fb-settings-modal.js';
import { emit } from '../../js/app-state.js';
import { getCredentials } from '../../js/auth.js';

const AKID = 'FBABCDEFGHIJKLMNOPQR';
const SECRET = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJ0123';

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

async function settled(el: Element): Promise<void> {
  await (el as unknown as { updateComplete: Promise<unknown> }).updateComplete;
}

function q<T extends Element>(el: Element, testid: string): T {
  const found = el.querySelector(`[data-testid="${testid}"]`);
  expect(found, testid).not.toBeNull();
  return found as T;
}

function type(input: HTMLInputElement, value: string) {
  input.value = value;
  input.dispatchEvent(new Event('input'));
}

describe('fb-settings-modal device access', () => {
  let el: HTMLElement;

  beforeEach(() => {
    vi.stubGlobal('localStorage', memoryStorage());
    el = document.createElement('fb-settings-modal');
    document.body.appendChild(el);
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.unstubAllGlobals();
  });

  it('opens on auth-required at Device access with the message', async () => {
    expect(el.querySelector('[data-testid="device-access"]')).toBeNull();
    emit('auth-required', { origin: location.origin, code: 'MissingAuthentication', message: 'The device requires access credentials.' });
    await settled(el);
    expect(q(el, 'device-access-alert').textContent).toContain('requires access credentials');
    expect(q(el, 'device-access-state').textContent).toContain('No credentials stored');
  });

  it('pasting FBCRED fills both fields and Save stores them for the device', async () => {
    emit('open-settings');
    await settled(el);
    type(q<HTMLInputElement>(el, 'fbcred-input'), `FBCRED:${AKID}:${SECRET}`);
    await settled(el);
    expect(q<HTMLInputElement>(el, 'akid-input').value).toBe(AKID);
    expect(q<HTMLInputElement>(el, 'secret-input').value).toBe(SECRET);
    expect(q<HTMLInputElement>(el, 'secret-input').type).toBe('password');
    q<HTMLButtonElement>(el, 'credentials-save').click();
    await settled(el);
    expect(getCredentials()).toEqual({ accessKeyId: AKID, secretAccessKey: SECRET });
    expect(q(el, 'device-access-state').textContent).toContain(AKID);
  });

  it('show/hide toggles the secret field', async () => {
    emit('open-settings');
    await settled(el);
    q<HTMLButtonElement>(el, 'secret-toggle').click();
    await settled(el);
    expect(q<HTMLInputElement>(el, 'secret-input').type).toBe('text');
  });

  it('rejects malformed input with a message and stores nothing', async () => {
    emit('open-settings');
    await settled(el);
    type(q<HTMLInputElement>(el, 'akid-input'), 'FB123');
    type(q<HTMLInputElement>(el, 'secret-input'), 'short');
    await settled(el);
    q<HTMLButtonElement>(el, 'credentials-save').click();
    await settled(el);
    expect(q(el, 'device-access-status').textContent).toMatch(/Access key id/);
    expect(getCredentials()).toBeNull();
  });

  it('Clear removes stored credentials', async () => {
    emit('open-settings');
    await settled(el);
    type(q<HTMLInputElement>(el, 'akid-input'), `FBCRED:${AKID}:${SECRET}`); // paste into the first field
    await settled(el);
    q<HTMLButtonElement>(el, 'credentials-save').click();
    await settled(el);
    expect(getCredentials()).not.toBeNull();
    q<HTMLButtonElement>(el, 'credentials-clear').click();
    await settled(el);
    expect(getCredentials()).toBeNull();
    expect(q<HTMLInputElement>(el, 'akid-input').value).toBe('');
  });
});
