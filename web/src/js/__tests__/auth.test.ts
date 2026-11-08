/**
 * Tests for device access signing (SigV4, src/js/auth.ts).
 *
 * Signatures are checked against published AWS vectors and against an
 * independent reference implementation (node:crypto) of the verifier the
 * firmware implements. No request leaves the test: fetch / WebSocket / the S3
 * request handler are fakes.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { createHash, createHmac } from 'node:crypto';
import { Readable } from 'node:stream';
import { S3Client, GetObjectCommand, PutObjectCommand } from '@aws-sdk/client-s3';
import {
  ANONYMOUS_IDENTITY, EMPTY_SHA256, UNSIGNED_PAYLOAD,
  addAuthRequiredMiddleware, clearCredentials, credentialsProvider, deviceOrigin, deviceS3Signer,
  getClockOffset, getCredentials, parseAuthErrorBody, parseFbcred, parseServerTime, presignUrl,
  setClockOffset, setCredentials, signRequest, signatureHeaders, signedFetch, validateCredentials,
  formatFbcred, importCredentialsFromLocation, takeImportedCredentials,
  type DeviceCredentials,
} from '../auth';
import { ReplWebSocket } from '../websocket';

const DEVICE = 'http://10.0.0.5';
const CREDS: DeviceCredentials = {
  accessKeyId: 'FBABCDEFGHIJKLMNOPQR',
  secretAccessKey: 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJ0123',
};

// ---------------------------------------------------------------------------
// Reference verifier (what the firmware computes), independent of smithy
// ---------------------------------------------------------------------------

function rfc3986(s: string): string {
  return encodeURIComponent(s).replace(/[!'()*]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase());
}

function hmac(key: Buffer | string, data: string): Buffer {
  return createHmac('sha256', key).update(data, 'utf8').digest();
}

/**
 * S3-style SigV4: canonical URI = path as sent; query decoded and re-encoded,
 * sorted; X-Amz-Signature excluded.
 */
function referenceSignature(opts: {
  method: string; path: string; search: string; headers: Record<string, string>;
  signedHeaders: string; payloadHash: string; amzDate: string; secret: string;
  region?: string; service?: string;
}): string {
  const pairs = [...new URLSearchParams(opts.search)]
    .filter(([k]) => k !== 'X-Amz-Signature')
    .map(([k, v]) => [rfc3986(k), rfc3986(v)] as const)
    .sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : a[1] < b[1] ? -1 : 1));
  const names = opts.signedHeaders.split(';');
  const lower = Object.fromEntries(Object.entries(opts.headers).map(([k, v]) => [k.toLowerCase(), v.trim()]));
  const canonical = [
    opts.method,
    opts.path,
    pairs.map(([k, v]) => `${k}=${v}`).join('&'),
    names.map(n => `${n}:${lower[n]}`).join('\n') + '\n',
    opts.signedHeaders,
    opts.payloadHash,
  ].join('\n');
  const day = opts.amzDate.slice(0, 8);
  const region = opts.region ?? 'us-east-1';
  const service = opts.service ?? 's3';
  const scope = `${day}/${region}/${service}/aws4_request`;
  const sts = ['AWS4-HMAC-SHA256', opts.amzDate, scope, createHash('sha256').update(canonical).digest('hex')].join('\n');
  const kSigning = hmac(hmac(hmac(hmac('AWS4' + opts.secret, day), region), service), 'aws4_request');
  return createHmac('sha256', kSigning).update(sts).digest('hex');
}

function authParts(authorization: string) {
  const m = /^AWS4-HMAC-SHA256 Credential=([^,]+), SignedHeaders=([^,]+), Signature=([0-9a-f]{64})$/.exec(authorization);
  expect(m, authorization).not.toBeNull();
  return { credential: m![1], signedHeaders: m![2], signature: m![3] };
}

/** In-memory Storage: recent Node versions shadow happy-dom's localStorage with their own (unusable) one. */
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

beforeEach(() => {
  vi.stubGlobal('localStorage', memoryStorage());
  setClockOffset(0);
});
afterEach(() => { vi.unstubAllGlobals(); });

// ---------------------------------------------------------------------------
// Published AWS vectors
// ---------------------------------------------------------------------------

describe('SigV4 against AWS test vectors', () => {
  // AWS SigV4 test suite credentials.
  const SUITE = { accessKeyId: 'AKIDEXAMPLE', secretAccessKey: 'wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY' };
  const SUITE_DATE = new Date('2015-08-30T12:36:00Z');
  const suiteRequest = (path: string, query: Record<string, string> = {}) => ({
    method: 'GET', protocol: 'https:', hostname: 'example.amazonaws.com', path, query,
    headers: { host: 'example.amazonaws.com' },
  });

  it('get-vanilla', async () => {
    const signed = await signRequest(suiteRequest('/'), SUITE, {
      date: SUITE_DATE, service: 'service', region: 'us-east-1', uriEscapePath: true,
    });
    expect(signed.headers['x-amz-date']).toBe('20150830T123600Z');
    expect(signed.headers.authorization).toBe(
      'AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20150830/us-east-1/service/aws4_request, ' +
      'SignedHeaders=host;x-amz-date, ' +
      'Signature=5fa00fa31553b73ebf1942676e86291e8372ff2a2260956d9b8aae1d763fbf31');
  });

  it('get-vanilla-query-order-key-case', async () => {
    const signed = await signRequest(suiteRequest('/', { Param2: 'value2', Param1: 'value1' }), SUITE, {
      date: SUITE_DATE, service: 'service', uriEscapePath: true,
    });
    expect(authParts(signed.headers.authorization).signature)
      .toBe('b97d918cfa904a5beff61c982a1b6f458b799221646efd99d3219ec94cdf2500');
  });

  // Amazon S3 docs, "Signature Calculations for the Authorization Header":
  // example GET Object with a Range header.
  const S3_DOCS = { accessKeyId: 'AKIAIOSFODNN7EXAMPLE', secretAccessKey: 'wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY' };
  const S3_DATE = new Date('2013-05-24T00:00:00Z');

  it('S3 GET Object (header form)', async () => {
    const signed = await signRequest({
      method: 'GET', protocol: 'https:', hostname: 'examplebucket.s3.amazonaws.com', path: '/test.txt', query: {},
      headers: {
        host: 'examplebucket.s3.amazonaws.com',
        range: 'bytes=0-9',
        'x-amz-content-sha256': EMPTY_SHA256,
      },
    }, S3_DOCS, { date: S3_DATE });
    expect(signed.headers.authorization).toBe(
      'AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request, ' +
      'SignedHeaders=host;range;x-amz-content-sha256;x-amz-date, ' +
      'Signature=f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41');
  });

  // Amazon S3 docs, "Authenticating Requests: Using Query Parameters".
  it('S3 presigned URL (query form)', async () => {
    const url = await presignUrl('https://examplebucket.s3.amazonaws.com/test.txt', 86400, S3_DOCS, S3_DATE);
    const q = new URL(url).searchParams;
    expect(q.get('X-Amz-Algorithm')).toBe('AWS4-HMAC-SHA256');
    expect(q.get('X-Amz-Credential')).toBe('AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request');
    expect(q.get('X-Amz-Date')).toBe('20130524T000000Z');
    expect(q.get('X-Amz-Expires')).toBe('86400');
    expect(q.get('X-Amz-SignedHeaders')).toBe('host');
    expect(q.get('X-Amz-Signature')).toBe('aeeed9bbccd4d02ee5c0109b86d86835f995330da4c265957d157751f604d404');
    expect(q.has('x-amz-content-sha256')).toBe(false);
    expect(url).toContain('X-Amz-Credential=AKIAIOSFODNN7EXAMPLE%2F20130524%2Fus-east-1%2Fs3%2Faws4_request');
  });

  it('the reference verifier used below agrees with the S3 docs vector', () => {
    expect(referenceSignature({
      method: 'GET', path: '/test.txt', search: '',
      headers: { host: 'examplebucket.s3.amazonaws.com', range: 'bytes=0-9', 'x-amz-content-sha256': EMPTY_SHA256, 'x-amz-date': '20130524T000000Z' },
      signedHeaders: 'host;range;x-amz-content-sha256;x-amz-date',
      payloadHash: EMPTY_SHA256, amzDate: '20130524T000000Z', secret: S3_DOCS.secretAccessKey,
    })).toBe('f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41');
  });
});

// ---------------------------------------------------------------------------
// Credentials
// ---------------------------------------------------------------------------

describe('FBCRED parsing and validation', () => {
  it('parses the QR payload', () => {
    expect(parseFbcred(`FBCRED:${CREDS.accessKeyId}:${CREDS.secretAccessKey}`)).toEqual(CREDS);
  });

  it('tolerates surrounding whitespace, a lower-case access key id and a lower-case prefix', () => {
    expect(parseFbcred(`  fbcred:${CREDS.accessKeyId.toLowerCase()}:${CREDS.secretAccessKey}\n`)).toEqual(CREDS);
  });

  it('keeps the secret case-sensitive', () => {
    const parsed = parseFbcred(`FBCRED:${CREDS.accessKeyId}:${CREDS.secretAccessKey}`);
    expect(parsed?.secretAccessKey).toBe(CREDS.secretAccessKey);
  });

  it.each([
    ['', 'empty'],
    [`${CREDS.accessKeyId}:${CREDS.secretAccessKey}`, 'no prefix'],
    [`FBCRED:${CREDS.accessKeyId}`, 'no secret'],
    [`FBCRED:XX${CREDS.accessKeyId.slice(2)}:${CREDS.secretAccessKey}`, 'not FB'],
    [`FBCRED:FBABCDEFGHIJKLMNOPQ1:${CREDS.secretAccessKey}`, 'akid outside base32 (1)'],
    [`FBCRED:FBABC:${CREDS.secretAccessKey}`, 'akid too short'],
    [`FBCRED:${CREDS.accessKeyId}:${CREDS.secretAccessKey.slice(1)}`, 'secret too short'],
    [`FBCRED:${CREDS.accessKeyId}:${CREDS.secretAccessKey.slice(1)}+`, 'secret outside [A-Za-z0-9]'],
    [`FBCRED:${CREDS.accessKeyId}:${CREDS.secretAccessKey}:extra`, 'extra field'],
  ])('rejects %s (%s)', (text) => {
    expect(parseFbcred(text)).toBeNull();
  });

  it('validateCredentials names the bad field', () => {
    expect(validateCredentials(CREDS)).toBeNull();
    expect(validateCredentials({ ...CREDS, accessKeyId: 'FB1' })).toMatch(/Access key id/);
    expect(validateCredentials({ ...CREDS, secretAccessKey: 'short' })).toMatch(/Secret/);
  });
});

describe('credential storage', () => {
  it('stores per device origin and round-trips', () => {
    setCredentials(CREDS, DEVICE);
    expect(getCredentials(DEVICE)).toEqual(CREDS);
    expect(getCredentials(DEVICE + '/')).toEqual(CREDS);
    expect(getCredentials('ws://10.0.0.5/ws/repl')).toEqual(CREDS);
    expect(getCredentials('http://10.0.0.6')).toBeNull();
    expect(getCredentials('')).toBeNull();
  });

  it('"" means the origin that served the IDE', () => {
    setCredentials(CREDS);
    expect(getCredentials(location.origin)).toEqual(CREDS);
    expect(deviceOrigin('')).toBe(location.origin);
  });

  it('normalizes typed input (blanks, akid case)', () => {
    setCredentials({ accessKeyId: ' fbabcd efghijklmnopqr ', secretAccessKey: `${CREDS.secretAccessKey.slice(0, 20)} ${CREDS.secretAccessKey.slice(20)}` }, DEVICE);
    expect(getCredentials(DEVICE)).toEqual(CREDS);
  });

  it('refuses malformed credentials', () => {
    expect(() => setCredentials({ accessKeyId: 'nope', secretAccessKey: 'nope' }, DEVICE)).toThrow();
    expect(getCredentials(DEVICE)).toBeNull();
  });

  it('ignores corrupt storage', () => {
    localStorage.setItem('freebutton.deviceAuth:' + DEVICE, '{not json');
    expect(getCredentials(DEVICE)).toBeNull();
  });

  it('clears and announces changes', () => {
    const events: Array<{ origin: string; configured: boolean }> = [];
    const listener = (e: Event) => events.push((e as CustomEvent).detail);
    window.addEventListener('device-credentials-change', listener);
    setCredentials(CREDS, DEVICE);
    clearCredentials(DEVICE);
    window.removeEventListener('device-credentials-change', listener);
    expect(getCredentials(DEVICE)).toBeNull();
    expect(events).toEqual([{ origin: DEVICE, configured: true }, { origin: DEVICE, configured: false }]);
  });
});

// ---------------------------------------------------------------------------
// signedFetch
// ---------------------------------------------------------------------------

type FetchCall = { url: string; init: RequestInit };

function fakeFetch(responses: Array<() => Response>) {
  const calls: FetchCall[] = [];
  const impl = vi.fn(async (url: RequestInfo | URL, init: RequestInit = {}) => {
    calls.push({ url: String(url), init });
    const next = responses.shift();
    return next ? next() : new Response('{"status":"success"}', { status: 200 });
  });
  return { calls, fetchImpl: impl as unknown as typeof fetch };
}

function headersOf(call: FetchCall): Headers {
  return new Headers(call.init.headers);
}

describe('signedFetch', () => {
  it('sends no signature without stored credentials', async () => {
    const { calls, fetchImpl } = fakeFetch([]);
    await signedFetch(`${DEVICE}/api/device`, {}, { fetchImpl });
    const h = headersOf(calls[0]);
    expect(h.has('authorization')).toBe(false);
    expect(h.has('x-amz-date')).toBe(false);
  });

  it('signs a body-less POST with the hash of "" and exactly host;x-amz-content-sha256;x-amz-date', async () => {
    setCredentials(CREDS, DEVICE);
    const { calls, fetchImpl } = fakeFetch([]);
    await signedFetch(`${DEVICE}/api/reboot`, { method: 'POST' }, { fetchImpl });
    const h = headersOf(calls[0]);
    expect(calls[0].url).toBe(`${DEVICE}/api/reboot`);
    expect(calls[0].init.method).toBe('POST');
    expect(h.get('x-amz-content-sha256')).toBe(EMPTY_SHA256);
    expect(h.get('x-amz-date')).toMatch(/^\d{8}T\d{6}Z$/);
    const auth = authParts(h.get('authorization')!);
    expect(auth.signedHeaders).toBe('host;x-amz-content-sha256;x-amz-date');
    expect(auth.credential).toBe(`${CREDS.accessKeyId}/${h.get('x-amz-date')!.slice(0, 8)}/us-east-1/s3/aws4_request`);
    expect(auth.signature).toBe(referenceSignature({
      method: 'POST', path: '/api/reboot', search: '',
      headers: { host: '10.0.0.5', 'x-amz-content-sha256': EMPTY_SHA256, 'x-amz-date': h.get('x-amz-date')! },
      signedHeaders: auth.signedHeaders, payloadHash: EMPTY_SHA256,
      amzDate: h.get('x-amz-date')!, secret: CREDS.secretAccessKey,
    }));
  });

  it('signs a multipart upload as UNSIGNED-PAYLOAD and keeps the body and caller headers', async () => {
    setCredentials(CREDS, DEVICE);
    const { calls, fetchImpl } = fakeFetch([]);
    const form = new FormData();
    form.append('file', new Blob([new Uint8Array([1, 2, 3])]), 'main.jsc');
    await signedFetch(`${DEVICE}/api/bytecode/upload`, { method: 'POST', body: form, headers: { 'X-Test': '1' } }, { fetchImpl });
    const h = headersOf(calls[0]);
    expect(calls[0].init.body).toBe(form);
    expect(h.get('x-test')).toBe('1');
    expect(h.get('x-amz-content-sha256')).toBe(UNSIGNED_PAYLOAD);
    const auth = authParts(h.get('authorization')!);
    expect(auth.signature).toBe(referenceSignature({
      method: 'POST', path: '/api/bytecode/upload', search: '',
      headers: { host: '10.0.0.5', 'x-amz-content-sha256': UNSIGNED_PAYLOAD, 'x-amz-date': h.get('x-amz-date')! },
      signedHeaders: auth.signedHeaders, payloadHash: UNSIGNED_PAYLOAD,
      amzDate: h.get('x-amz-date')!, secret: CREDS.secretAccessKey,
    }));
  });

  it('hashes string bodies and signs query, port and encoded path like the device verifies them', async () => {
    const base = 'http://freebutton.local:8080';
    setCredentials(CREDS, base);
    const date = new Date('2026-11-08T10:00:00Z');
    const url = `${base}/fs/scripts/my%20file.js?prefix=a b&list-type=2&x=%2F`;
    const sig = (await signatureHeaders(url, 'PUT', 'hello', getCredentials(base), date))!;
    const hash = createHash('sha256').update('hello').digest('hex');
    expect(sig['x-amz-content-sha256']).toBe(hash);
    const auth = authParts(sig.authorization);
    const parsed = new URL(url);
    expect(auth.signature).toBe(referenceSignature({
      method: 'PUT', path: parsed.pathname, search: parsed.search,
      headers: { host: 'freebutton.local:8080', 'x-amz-content-sha256': hash, 'x-amz-date': '20261108T100000Z' },
      signedHeaders: auth.signedHeaders, payloadHash: hash,
      amzDate: '20261108T100000Z', secret: CREDS.secretAccessKey,
    }));
  });

  it('a relative URL is signed for the page origin', async () => {
    setCredentials(CREDS);
    const { calls, fetchImpl } = fakeFetch([]);
    await signedFetch('/api/reboot', { method: 'POST' }, { fetchImpl });
    expect(calls[0].url).toBe('/api/reboot');
    expect(headersOf(calls[0]).has('authorization')).toBe(true);
  });

  it('a 403 with x-freebutton-auth: required emits auth-required with the code', async () => {
    const events: Array<{ origin: string; code: string; message: string }> = [];
    const listener = (e: Event) => events.push((e as CustomEvent).detail);
    window.addEventListener('auth-required', listener);
    const { fetchImpl } = fakeFetch([
      () => new Response('{"status":"error","code":"MissingAuthentication","message":"no auth"}', {
        status: 403, headers: { 'x-freebutton-auth': 'required', 'content-type': 'application/json' },
      }),
    ]);
    const res = await signedFetch(`${DEVICE}/api/reboot`, { method: 'POST' }, { fetchImpl });
    window.removeEventListener('auth-required', listener);
    expect(res.status).toBe(403);
    expect(await res.json()).toMatchObject({ code: 'MissingAuthentication' }); // body still readable
    expect(events).toHaveLength(1);
    expect(events[0].origin).toBe(DEVICE);
    expect(events[0].code).toBe('MissingAuthentication');
    expect(events[0].message).toMatch(/requires access credentials/);
  });

  it('a 403 without the auth header is passed through silently', async () => {
    const listener = vi.fn();
    window.addEventListener('auth-required', listener);
    const { fetchImpl } = fakeFetch([() => new Response('nope', { status: 403 })]);
    const res = await signedFetch(`${DEVICE}/api/reboot`, { method: 'POST' }, { fetchImpl });
    window.removeEventListener('auth-required', listener);
    expect(res.status).toBe(403);
    expect(listener).not.toHaveBeenCalled();
  });

  it('RequestTimeTooSkewed with the device time: corrects the clock and retries once', async () => {
    setCredentials(CREDS, DEVICE);
    const deviceTime = Date.now() - 3 * 3600_000; // device is 3 h behind
    const listener = vi.fn();
    window.addEventListener('auth-required', listener);
    const { calls, fetchImpl } = fakeFetch([
      () => new Response('{"status":"error","code":"RequestTimeTooSkewed","message":"skew"}', {
        status: 403, headers: { 'x-freebutton-auth': 'required', date: new Date(deviceTime).toUTCString() },
      }),
    ]);
    const res = await signedFetch(`${DEVICE}/api/reboot`, { method: 'POST' }, { fetchImpl });
    window.removeEventListener('auth-required', listener);
    expect(res.status).toBe(200);
    expect(calls).toHaveLength(2);
    expect(listener).not.toHaveBeenCalled();
    expect(Math.abs(getClockOffset() + 3 * 3600_000)).toBeLessThan(5000);
    const retried = parseServerTime(headersOf(calls[1]).get('x-amz-date'))!;
    expect(Math.abs(retried - deviceTime)).toBeLessThan(5000);
  });

  it('a wrong signature reports SignatureDoesNotMatch (S3 XML body)', async () => {
    setCredentials(CREDS, DEVICE);
    const events: Array<{ code: string; message: string }> = [];
    const listener = (e: Event) => events.push((e as CustomEvent).detail);
    window.addEventListener('auth-required', listener);
    const { fetchImpl } = fakeFetch([
      () => new Response('<Error><Code>SignatureDoesNotMatch</Code><Message>bad</Message></Error>', {
        status: 403, headers: { 'x-freebutton-auth': 'required' },
      }),
    ]);
    await signedFetch(`${DEVICE}/config`, {}, { fetchImpl });
    window.removeEventListener('auth-required', listener);
    expect(events[0].code).toBe('SignatureDoesNotMatch');
    expect(events[0].message).toMatch(/access key or secret/);
  });
});

describe('helpers', () => {
  it('parseAuthErrorBody reads JSON and S3 XML', () => {
    expect(parseAuthErrorBody('{"code":"AccessDenied","message":"m"}')).toEqual({ code: 'AccessDenied', message: 'm' });
    expect(parseAuthErrorBody('<Error><Code>AccessDenied</Code><Message>m</Message></Error>')).toEqual({ code: 'AccessDenied', message: 'm' });
    expect(parseAuthErrorBody('')).toEqual({ code: '', message: '' });
  });

  it('parseServerTime reads RFC 1123 and x-amz-date', () => {
    expect(parseServerTime('20130524T000000Z')).toBe(Date.UTC(2013, 4, 24));
    expect(parseServerTime('Fri, 24 May 2013 00:00:00 GMT')).toBe(Date.UTC(2013, 4, 24));
    expect(parseServerTime('garbage')).toBeNull();
    expect(parseServerTime(null)).toBeNull();
  });
});

// ---------------------------------------------------------------------------
// Presigned WebSocket URL
// ---------------------------------------------------------------------------

describe('presignUrl', () => {
  it('returns the URL unchanged without credentials', async () => {
    expect(await presignUrl('ws://10.0.0.5/ws/repl')).toBe('ws://10.0.0.5/ws/repl');
  });

  it('presigns ws:// with SignedHeaders=host and UNSIGNED-PAYLOAD (device-verifiable)', async () => {
    setCredentials(CREDS, DEVICE);
    const date = new Date('2026-11-08T10:00:00Z');
    const url = await presignUrl('ws://10.0.0.5/ws/repl', 300, getCredentials(DEVICE), date);
    const parsed = new URL(url);
    expect(parsed.protocol).toBe('ws:');
    expect(parsed.pathname).toBe('/ws/repl');
    expect(parsed.searchParams.get('X-Amz-Expires')).toBe('300');
    expect(parsed.searchParams.get('X-Amz-SignedHeaders')).toBe('host');
    expect(parsed.searchParams.get('X-Amz-Signature')).toBe(referenceSignature({
      method: 'GET', path: '/ws/repl', search: parsed.search,
      headers: { host: '10.0.0.5' }, signedHeaders: 'host', payloadHash: UNSIGNED_PAYLOAD,
      amzDate: '20261108T100000Z', secret: CREDS.secretAccessKey,
    }));
  });
});

describe('REPL WebSocket signing', () => {
  let opened: string[];
  let originalWebSocket: typeof WebSocket;

  class FakeWebSocket {
    static OPEN = 1;
    readyState = 0;
    onopen: (() => void) | null = null;
    onmessage: (() => void) | null = null;
    onclose: (() => void) | null = null;
    onerror: (() => void) | null = null;
    constructor(public url: string) { opened.push(url); }
    close(): void { this.readyState = 3; }
    send(): void {}
  }

  beforeEach(() => {
    opened = [];
    originalWebSocket = globalThis.WebSocket;
    globalThis.WebSocket = FakeWebSocket as unknown as typeof WebSocket;
  });
  afterEach(() => { globalThis.WebSocket = originalWebSocket; });

  it('connects to a presigned URL when credentials are stored', async () => {
    setCredentials(CREDS, DEVICE);
    const ws = new ReplWebSocket();
    ws.connect(DEVICE);
    await vi.waitFor(() => expect(opened).toHaveLength(1));
    const url = new URL(opened[0]);
    expect(`${url.protocol}//${url.host}${url.pathname}`).toBe('ws://10.0.0.5/ws/repl');
    expect(url.searchParams.get('X-Amz-Credential')).toMatch(/^FBABCDEFGHIJKLMNOPQR\/\d{8}\/us-east-1\/s3\/aws4_request$/);
    ws.disconnect();
  });

  it('a disconnect before the presign finishes opens nothing', async () => {
    setCredentials(CREDS, DEVICE);
    const ws = new ReplWebSocket();
    ws.connect(DEVICE);
    ws.disconnect();
    await new Promise(r => setTimeout(r, 20));
    expect(opened).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// S3 SDK integration (what device-storage.ts will use)
// ---------------------------------------------------------------------------

describe('S3 client with deviceS3Signer', () => {
  interface Seen { method: string; path: string; query: Record<string, string>; headers: Record<string, string>; body: unknown; hostname: string }
  let seen: Seen[];

  function client(status: number, body: string, headers: Record<string, string> = {}) {
    const c = new S3Client({
      endpoint: DEVICE, region: 'us-east-1', forcePathStyle: true, maxAttempts: 1,
      credentials: credentialsProvider(DEVICE),
      signer: deviceS3Signer(DEVICE),
      requestHandler: {
        handle: async (req: Seen) => {
          seen.push(req);
          return { response: { statusCode: status, headers: { 'content-type': 'application/xml', ...headers }, body: Readable.from([Buffer.from(body)]) } };
        },
      } as never,
    });
    addAuthRequiredMiddleware(c, DEVICE);
    return c;
  }

  beforeEach(() => { seen = []; });

  it('credentialsProvider resolves to the anonymous identity until credentials are stored', async () => {
    expect(await credentialsProvider(DEVICE)()).toEqual(ANONYMOUS_IDENTITY);
    setCredentials(CREDS, DEVICE);
    expect(await credentialsProvider(DEVICE)()).toEqual(CREDS);
  });

  it('anonymous reads stay unsigned', async () => {
    await client(200, 'hi').send(new GetObjectCommand({ Bucket: 'web', Key: 'index.html' }));
    expect(Object.keys(seen[0].headers).map(h => h.toLowerCase())).not.toContain('authorization');
  });

  it('PUT is signed over host;x-amz-content-sha256;x-amz-date with the body hash', async () => {
    setCredentials(CREDS, DEVICE);
    await client(200, '', { etag: '"x"' }).send(new PutObjectCommand({ Bucket: 'app', Key: 'scripts/my file.js', Body: 'abc', ContentType: 'text/javascript' }));
    const req = seen[0];
    const hash = createHash('sha256').update('abc').digest('hex');
    expect(req.headers['x-amz-content-sha256']).toBe(hash);
    expect(req.headers['content-type']).toBe('text/javascript'); // other headers kept, unsigned
    const auth = authParts(req.headers.authorization);
    expect(auth.signedHeaders).toBe('host;x-amz-content-sha256;x-amz-date');
    const search = new URLSearchParams(req.query as Record<string, string>).toString();
    expect(auth.signature).toBe(referenceSignature({
      method: 'PUT', path: req.path, search,
      headers: { host: '10.0.0.5', 'x-amz-content-sha256': hash, 'x-amz-date': req.headers['x-amz-date'] },
      signedHeaders: auth.signedHeaders, payloadHash: hash,
      amzDate: req.headers['x-amz-date'], secret: CREDS.secretAccessKey,
    }));
    expect(req.path).toBe('/app/scripts/my%20file.js');
  });

  it('a refused S3 request emits auth-required', async () => {
    const events: Array<{ code: string }> = [];
    const listener = (e: Event) => events.push((e as CustomEvent).detail);
    window.addEventListener('auth-required', listener);
    await expect(client(403, '<Error><Code>AccessDenied</Code><Message>no</Message></Error>', { 'x-freebutton-auth': 'required' })
      .send(new GetObjectCommand({ Bucket: 'app', Key: 'x' }))).rejects.toThrow();
    window.removeEventListener('auth-required', listener);
    expect(events).toHaveLength(1);
    expect(events[0].code).toBe('AccessDenied');
  });
});

// ---------------------------------------------------------------------------
// QR link http://<device>/#fbcred=<akid>:<secret>
// ---------------------------------------------------------------------------

describe('QR link import', () => {
  const AKID = 'FBABCDEFGHIJKLMNOP23';  // test values (same as the firmware tests)
  const SECRET = 'abcdefghijABCDEFGHIJ0123456789klmnopqrst';
  const loc = (hash: string) => ({ hash, pathname: '/', search: '?x=1' });
  const hist = () => ({ state: { a: 1 }, replaceState: vi.fn() });

  it('stores the credentials for this origin and clears the fragment', () => {
    const h = hist();
    const creds = importCredentialsFromLocation(loc(`#fbcred=${AKID}:${SECRET}`), h);
    expect(creds).toEqual({ accessKeyId: AKID, secretAccessKey: SECRET });
    expect(getCredentials('')).toEqual(creds);
    expect(h.replaceState).toHaveBeenCalledWith({ a: 1 }, '', '/?x=1');
    expect(takeImportedCredentials()).toEqual(creds);
    expect(takeImportedCredentials()).toBeNull();  // once
    expect(formatFbcred(creds!)).toBe(`FBCRED:${AKID}:${SECRET}`);
  });

  it('ignores other fragments and leaves the URL alone', () => {
    const h = hist();
    expect(importCredentialsFromLocation(loc('#editor'), h)).toBeNull();
    expect(importCredentialsFromLocation(loc(''), h)).toBeNull();
    expect(h.replaceState).not.toHaveBeenCalled();
    expect(getCredentials('')).toBeNull();
  });

  it('clears a malformed fragment but stores nothing', () => {
    const h = hist();
    expect(importCredentialsFromLocation(loc(`#fbcred=${AKID}:short`), h)).toBeNull();
    expect(importCredentialsFromLocation(loc('#fbcred=%E0%A4%A'), h)).toBeNull();
    expect(h.replaceState).toHaveBeenCalledTimes(2);
    expect(getCredentials('')).toBeNull();
    expect(takeImportedCredentials()).toBeNull();
  });
});
