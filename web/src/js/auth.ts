/**
 * Device access - AWS Signature Version 4 signing for the device API.
 *
 * The single place where the IDE signs its requests to the device. The device
 * generates an access key id + secret and shows them on its display (text and
 * a QR code of `FBCRED:<akid>:<secret>`); the user enters them once in
 * Settings → Device access and they are kept in localStorage, keyed by the
 * device origin.
 *
 * Contract with the firmware verifier:
 *   - AWS4-HMAC-SHA256, region "us-east-1", service "s3" for every request
 *   - header form (Authorization + x-amz-date + x-amz-content-sha256) for
 *     HTTP requests; SignedHeaders is always "host;x-amz-content-sha256;x-amz-date"
 *     so the device only has to keep those three headers
 *   - x-amz-content-sha256: UNSIGNED-PAYLOAD for FormData / Blob / stream
 *     bodies (multipart uploads), the SHA-256 of "" without a body, the real
 *     SHA-256 for string / byte bodies
 *   - S3 URI rules: canonical URI = path as sent (encoded once, not normalized)
 *   - presigned query form (SignedHeaders=host, UNSIGNED-PAYLOAD) for the
 *     REPL WebSocket upgrade, because browsers cannot set WebSocket headers
 *   - a refused request answers 403 with `x-freebutton-auth: required`
 *
 * The IDE is served over plain HTTP, where `crypto.subtle` does not exist on
 * a non-localhost origin, so hashing uses the pure-JS SHA-256 (no WebCrypto).
 * SigV4 gives authenticity and replay protection, NOT confidentiality.
 */
import { SignatureV4 } from '@smithy/signature-v4';
import { Sha256Js } from '@smithy/core/checksum';
import { escapeUri } from '@smithy/core/protocols';
import { toHex } from '@smithy/core/serde';
import type {
  AwsCredentialIdentity, AwsCredentialIdentityProvider, HttpRequest, QueryParameterBag,
  RequestSigner, RequestSigningArguments,
} from '@smithy/types';
import { emit } from './app-state';

export const REGION = 'us-east-1';
export const SERVICE = 's3';
export const UNSIGNED_PAYLOAD = 'UNSIGNED-PAYLOAD';
export const EMPTY_SHA256 = 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855';
/** Response header the device sets on a refused (unauthenticated) request. */
export const AUTH_RESPONSE_HEADER = 'x-freebutton-auth';
/** Default lifetime of a presigned WebSocket URL. */
export const PRESIGN_EXPIRES_SECONDS = 300;

const SHA256_HEADER = 'x-amz-content-sha256';
const DATE_HEADER = 'x-amz-date';
const AUTH_HEADER = 'authorization';
const STORAGE_PREFIX = 'freebutton.deviceAuth:';

export interface DeviceCredentials {
  accessKeyId: string;
  secretAccessKey: string;
}

// ---------------------------------------------------------------------------
// Credential format
// ---------------------------------------------------------------------------

const AKID_RE = /^FB[A-Z2-7]{18}$/;
const SECRET_RE = /^[A-Za-z0-9]{40}$/;

/** Strip the blanks a user may type when copying grouped characters. */
function compact(value: string): string {
  return value.replace(/\s+/g, '');
}

/** Normalize user input: no whitespace, access key id upper case (base32). */
export function normalizeCredentials(accessKeyId: string, secretAccessKey: string): DeviceCredentials {
  return { accessKeyId: compact(accessKeyId).toUpperCase(), secretAccessKey: compact(secretAccessKey) };
}

/** Error message for invalid credentials, or null when they are well-formed. */
export function validateCredentials(creds: DeviceCredentials): string | null {
  if (!AKID_RE.test(creds.accessKeyId)) {
    return 'Access key id must be "FB" followed by 18 characters A-Z / 2-7.';
  }
  if (!SECRET_RE.test(creds.secretAccessKey)) {
    return 'Secret must be 40 characters A-Z, a-z, 0-9.';
  }
  return null;
}

/**
 * Parse the device's QR payload `FBCRED:<akid>:<secret>`. Returns null when
 * the text is not a well-formed FBCRED string.
 */
export function parseFbcred(text: string): DeviceCredentials | null {
  const m = /^FBCRED:([^:]+):([^:]+)$/i.exec(text.trim());
  if (!m) return null;
  const creds = normalizeCredentials(m[1], m[2]);
  return validateCredentials(creds) === null ? creds : null;
}

/** `FBCRED:<akid>:<secret>`, the text form of the credentials (copy / paste). */
export function formatFbcred(creds: DeviceCredentials): string {
  return `FBCRED:${creds.accessKeyId}:${creds.secretAccessKey}`;
}

// ---------------------------------------------------------------------------
// QR link: http://<device>/#fbcred=<akid>:<secret>
// ---------------------------------------------------------------------------
//
// The device's QR code is a link to its own IDE with the credentials in the
// URL fragment, which browsers never send over the network. A phone camera
// opens it; importCredentialsFromLocation() (run before anything else, see
// boot-credentials.ts) stores them for this origin and removes the fragment
// from the address bar and the history entry. Settings then offers copy and
// save-to-password-manager (takeImportedCredentials()).

const FRAGMENT_KEY = 'fbcred=';
let imported: DeviceCredentials | null = null;

export function importCredentialsFromLocation(
  loc: Pick<Location, 'hash' | 'pathname' | 'search'> = location,
  hist: Pick<History, 'replaceState' | 'state'> = history,
): DeviceCredentials | null {
  const hash = loc.hash ?? '';
  const at = hash.indexOf(FRAGMENT_KEY);
  if (at < 0) return null;
  // Whatever follows, the secret leaves the address bar and the history.
  try { hist.replaceState(hist.state, '', loc.pathname + loc.search); } catch { /* not fatal */ }
  let value = hash.slice(at + FRAGMENT_KEY.length).split('&')[0];
  try { value = decodeURIComponent(value); } catch { return null; }
  const creds = parseFbcred('FBCRED:' + value);
  if (!creds) return null;
  try { setCredentials(creds, ''); } catch { return null; }
  imported = creds;
  return creds;
}

/** Credentials imported from the QR link at startup, once (Settings shows them). */
export function takeImportedCredentials(): DeviceCredentials | null {
  const creds = imported;
  imported = null;
  return creds;
}

/**
 * Copy text to the clipboard. The device IDE runs on plain HTTP, where
 * navigator.clipboard does not exist (secure contexts only), so this falls
 * back to a selected textarea + execCommand('copy'), which mobile Safari and
 * Chrome still honour inside a user gesture.
 */
export async function copyText(text: string): Promise<boolean> {
  try {
    if (typeof navigator !== 'undefined' && navigator.clipboard && globalThis.isSecureContext) {
      await navigator.clipboard.writeText(text);
      return true;
    }
  } catch { /* fall back */ }
  if (typeof document === 'undefined') return false;
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.setAttribute('readonly', '');
  ta.style.position = 'fixed';
  ta.style.top = '0';
  ta.style.opacity = '0';
  document.body.appendChild(ta);
  ta.select();
  ta.setSelectionRange(0, text.length);
  let ok = false;
  try { ok = document.execCommand('copy'); } catch { ok = false; }
  ta.remove();
  return ok;
}

// ---------------------------------------------------------------------------
// Storage (localStorage, keyed per device origin)
// ---------------------------------------------------------------------------

function pageOrigin(): string {
  return typeof location !== 'undefined' && location.origin && location.origin !== 'null'
    ? location.origin
    : 'http://localhost';
}

/**
 * The origin that identifies a device: "" = the origin that served the IDE.
 * ws:// and wss:// URLs map to their http(s) origin, so the REPL socket and
 * the HTTP API of one device share one set of credentials.
 */
export function deviceOrigin(baseUrl = ''): string {
  const trimmed = baseUrl.trim();
  if (!trimmed) return pageOrigin();
  try {
    const url = new URL(trimmed.replace(/^ws(s?):/i, 'http$1:'), pageOrigin());
    return url.origin;
  } catch {
    return pageOrigin();
  }
}

function storageKey(origin: string): string {
  return STORAGE_PREFIX + origin;
}

/** Stored credentials for a device ("" = this origin), or null. */
export function getCredentials(baseUrl = ''): DeviceCredentials | null {
  try {
    const raw = localStorage.getItem(storageKey(deviceOrigin(baseUrl)));
    if (!raw) return null;
    const parsed = JSON.parse(raw) as Partial<DeviceCredentials>;
    if (typeof parsed.accessKeyId !== 'string' || typeof parsed.secretAccessKey !== 'string') return null;
    const creds = { accessKeyId: parsed.accessKeyId, secretAccessKey: parsed.secretAccessKey };
    return validateCredentials(creds) === null ? creds : null;
  } catch {
    return null;
  }
}

/**
 * Store credentials for a device. Throws on malformed credentials (callers
 * validate first and show the message). Emits `device-credentials-change`.
 */
export function setCredentials(creds: DeviceCredentials, baseUrl = ''): void {
  const normalized = normalizeCredentials(creds.accessKeyId, creds.secretAccessKey);
  const error = validateCredentials(normalized);
  if (error) throw new Error(error);
  const origin = deviceOrigin(baseUrl);
  try {
    localStorage.setItem(storageKey(origin), JSON.stringify(normalized));
  } catch {
    throw new Error('Could not store the credentials (browser storage blocked?)');
  }
  emit('device-credentials-change', { origin, configured: true });
}

/** Forget the credentials of a device. Emits `device-credentials-change`. */
export function clearCredentials(baseUrl = ''): void {
  const origin = deviceOrigin(baseUrl);
  try {
    localStorage.removeItem(storageKey(origin));
  } catch {
    // storage unavailable: nothing stored either
  }
  emit('device-credentials-change', { origin, configured: false });
}

// ---------------------------------------------------------------------------
// Clock offset (device time - browser time)
// ---------------------------------------------------------------------------

let clockOffsetMs = 0;

/** Current correction applied to the signing date (ms). */
export function getClockOffset(): number {
  return clockOffsetMs;
}

export function setClockOffset(ms: number): void {
  clockOffsetMs = Number.isFinite(ms) ? ms : 0;
}

function signingDate(now: number = Date.now()): Date {
  return new Date(now + clockOffsetMs);
}

/** Parse a `Date` (RFC 1123) or `x-amz-date` (yyyymmddThhmmssZ) value. */
export function parseServerTime(value: string | null | undefined): number | null {
  if (!value) return null;
  const amz = /^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})Z$/.exec(value.trim());
  if (amz) {
    const [, y, mo, d, h, mi, s] = amz.map(Number);
    return Date.UTC(y, mo - 1, d, h, mi, s);
  }
  const t = Date.parse(value);
  return Number.isNaN(t) ? null : t;
}

// ---------------------------------------------------------------------------
// Signing core
// ---------------------------------------------------------------------------

export interface SignOptions {
  /** Signing time; defaults to now + clock offset. */
  date?: Date;
  /** Defaults to "us-east-1" / "s3" (the device contract). */
  region?: string;
  service?: string;
  /** Let the signer add x-amz-content-sha256 itself (default false: callers set it). */
  applyChecksum?: boolean;
  /** S3 rules (default): path signed as sent. false = generic SigV4 normalization. */
  uriEscapePath?: boolean;
}

function signer(creds: DeviceCredentials, opts: SignOptions): SignatureV4 {
  return new SignatureV4({
    credentials: creds,
    region: opts.region ?? REGION,
    service: opts.service ?? SERVICE,
    sha256: Sha256Js,
    uriEscapePath: opts.uriEscapePath ?? false,
    applyChecksum: opts.applyChecksum ?? false,
  });
}

/**
 * Header-sign a request (low level). Every header present on `request` is
 * signed; x-amz-date and Authorization are added. Returns a new request.
 */
export async function signRequest(request: HttpRequest, creds: DeviceCredentials, opts: SignOptions = {}): Promise<HttpRequest> {
  return signer(creds, opts).sign(request, {
    signingDate: opts.date ?? signingDate(),
    signingRegion: opts.region ?? REGION,
    signingService: opts.service ?? SERVICE,
  });
}

async function sha256Hex(data: Uint8Array): Promise<string> {
  const hash = new Sha256Js();
  hash.update(data);
  return toHex(await hash.digest());
}

/**
 * x-amz-content-sha256 for a body: hash of "" without a body, the real hash
 * of string / byte bodies, UNSIGNED-PAYLOAD for anything the device cannot
 * hash (FormData multipart, Blob, URLSearchParams, streams).
 */
export async function payloadHash(body: unknown): Promise<string> {
  if (body === undefined || body === null) return EMPTY_SHA256;
  if (typeof body === 'string') return sha256Hex(new TextEncoder().encode(body));
  if (body instanceof ArrayBuffer) return sha256Hex(new Uint8Array(body));
  if (ArrayBuffer.isView(body)) return sha256Hex(new Uint8Array(body.buffer, body.byteOffset, body.byteLength));
  return UNSIGNED_PAYLOAD;
}

function queryFromSearch(params: URLSearchParams): QueryParameterBag {
  const query: QueryParameterBag = {};
  for (const [k, v] of params) {
    const prev = query[k];
    if (prev === undefined || prev === null) query[k] = v;
    else query[k] = Array.isArray(prev) ? [...prev, v] : [prev, v];
  }
  return query;
}

function requestFromUrl(url: URL, method: string, headers: Record<string, string>): HttpRequest {
  return {
    method: method.toUpperCase(),
    protocol: url.protocol,
    hostname: url.hostname,
    port: url.port ? Number(url.port) : undefined,
    path: url.pathname || '/',
    query: queryFromSearch(url.searchParams),
    headers: { host: url.host, ...headers },
  };
}

function absoluteUrl(input: string | URL): URL {
  return new URL(String(input), typeof location !== 'undefined' && location.href ? location.href : pageOrigin());
}

/**
 * The three SigV4 headers for one request, or null when no credentials are
 * stored for the URL's device.
 */
export async function signatureHeaders(
  input: string | URL,
  method = 'GET',
  body?: unknown,
  creds: DeviceCredentials | null = getCredentials(absoluteUrl(input).origin),
  date?: Date,
): Promise<Record<string, string> | null> {
  if (!creds) return null;
  const url = absoluteUrl(input);
  const hash = await payloadHash(body);
  const signed = await signRequest(requestFromUrl(url, method, { [SHA256_HEADER]: hash }), creds, { date });
  return {
    [AUTH_HEADER]: signed.headers[AUTH_HEADER],
    [DATE_HEADER]: signed.headers[DATE_HEADER],
    [SHA256_HEADER]: hash,
  };
}

// ---------------------------------------------------------------------------
// Refused requests
// ---------------------------------------------------------------------------

export interface AuthFailure {
  origin: string;
  /** SignatureDoesNotMatch | AccessDenied | RequestTimeTooSkewed | MissingAuthentication | ... */
  code: string;
  message: string;
}

/** Codes of the device contract (anything else: show the device's message). */
export const KNOWN_AUTH_CODES = new Set(['SignatureDoesNotMatch', 'AccessDenied', 'RequestTimeTooSkewed', 'MissingAuthentication']);

/** Human-readable explanation for an auth error code. */
export function describeAuthFailure(code: string, configured: boolean): string {
  switch (code) {
    case 'SignatureDoesNotMatch':
      return 'The device rejected the access key or secret. Check both against the device display.';
    case 'RequestTimeTooSkewed':
      return 'The clock of this computer and the device differ too much.';
    case 'AccessDenied':
      return configured
        ? 'The device refused access with these credentials (unknown access key id?).'
        : 'The device requires access credentials.';
    case 'MissingAuthentication':
    default:
      return configured
        ? 'The device refused the request. Check the access key and secret.'
        : 'The device requires access credentials. Enter the access key id and secret shown on the device display.';
  }
}

/** Tell the app (Settings → Device access opens) that the device refused a request. */
export function notifyAuthRequired(failure: AuthFailure): void {
  emit('auth-required', failure);
}

/** Parse the code + message of a JSON or S3 XML error body. */
export function parseAuthErrorBody(text: string): { code: string; message: string } {
  try {
    const json = JSON.parse(text) as { code?: unknown; message?: unknown };
    if (json && typeof json === 'object') {
      return {
        code: typeof json.code === 'string' ? json.code : '',
        message: typeof json.message === 'string' ? json.message : '',
      };
    }
  } catch {
    // not JSON: try S3 XML below
  }
  const code = /<Code>([^<]*)<\/Code>/.exec(text)?.[1] ?? '';
  const message = /<Message>([^<]*)<\/Message>/.exec(text)?.[1] ?? '';
  return { code, message };
}

function isAuthRefusal(status: number, headerValue: string | null | undefined): boolean {
  return status === 403 && String(headerValue ?? '').toLowerCase() === 'required';
}

/** Update the clock offset from a response's Date / x-amz-date header. */
function adoptServerTime(headerValue: string | null | undefined, sentAt: number): boolean {
  const serverTime = parseServerTime(headerValue);
  if (serverTime === null) return false;
  setClockOffset(serverTime - sentAt);
  return true;
}

// ---------------------------------------------------------------------------
// signedFetch
// ---------------------------------------------------------------------------

export interface SignedFetchDeps {
  fetchImpl?: typeof fetch;
}

function canResend(body: unknown): boolean {
  return !(typeof ReadableStream !== 'undefined' && body instanceof ReadableStream);
}

/**
 * fetch() with SigV4 headers when credentials are stored for the target
 * device; plain fetch otherwise (anonymous endpoints keep working). A 403
 * with `x-freebutton-auth: required` emits `auth-required`; a
 * RequestTimeTooSkewed refusal that carries the device time is retried once
 * with a corrected clock. Same signature as fetch, so it can be passed
 * wherever a `fetchImpl` is accepted.
 */
export async function signedFetch(
  input: RequestInfo | URL,
  init: RequestInit = {},
  deps: SignedFetchDeps = {},
): Promise<Response> {
  const fetchImpl = deps.fetchImpl ?? ((...args: Parameters<typeof fetch>) => fetch(...args));
  const request = typeof Request !== 'undefined' && input instanceof Request ? input : null;
  const url = absoluteUrl(request ? request.url : (input as string | URL));
  const origin = url.origin;
  const creds = getCredentials(origin);
  const method = (init.method ?? request?.method ?? 'GET').toUpperCase();
  // A Request's own body is a stream: signed as UNSIGNED-PAYLOAD.
  const body: unknown = init.body ?? request?.body ?? undefined;

  const send = async (): Promise<{ res: Response; sentAt: number }> => {
    const headers = new Headers(init.headers ?? request?.headers);
    const sentAt = Date.now();
    const sig = await signatureHeaders(url, method, body, creds, creds ? signingDate(sentAt) : undefined);
    if (sig) for (const [k, v] of Object.entries(sig)) headers.set(k, v);
    const res = await fetchImpl(request ?? String(input), { ...init, method, headers });
    return { res, sentAt };
  };

  let { res, sentAt } = await send();
  if (!isAuthRefusal(res.status, res.headers.get(AUTH_RESPONSE_HEADER))) return res;

  let { code, message } = parseAuthErrorBody(await res.clone().text().catch(() => ''));
  if (code === 'RequestTimeTooSkewed' && creds && !request && canResend(body)
      && adoptServerTime(res.headers.get('x-amz-date') ?? res.headers.get('date'), sentAt)) {
    ({ res, sentAt } = await send());
    if (!isAuthRefusal(res.status, res.headers.get(AUTH_RESPONSE_HEADER))) return res;
    ({ code, message } = parseAuthErrorBody(await res.clone().text().catch(() => '')));
  }
  notifyAuthRequired({
    origin,
    code: code || 'MissingAuthentication',
    message: KNOWN_AUTH_CODES.has(code) || !message ? describeAuthFailure(code, creds !== null) : message,
  });
  return res;
}

// ---------------------------------------------------------------------------
// Presigned URLs (REPL WebSocket)
// ---------------------------------------------------------------------------

/**
 * Presign a URL in the query form (SignedHeaders=host, UNSIGNED-PAYLOAD).
 * ws:// / wss:// URLs keep their scheme (the scheme is not signed). Returns the
 * URL unchanged when no credentials are stored for its device. Sign right
 * before connecting: the URL expires after `expiresSeconds`.
 */
export async function presignUrl(
  input: string | URL,
  expiresSeconds: number = PRESIGN_EXPIRES_SECONDS,
  creds: DeviceCredentials | null = getCredentials(String(input)),
  date?: Date,
): Promise<string> {
  const original = String(input);
  if (!creds) return original;
  const url = new URL(original, pageOrigin());
  const request = requestFromUrl(url, 'GET', { [SHA256_HEADER]: UNSIGNED_PAYLOAD });
  const keep = new Set([SHA256_HEADER]);
  const signed = await signer(creds, {}).presign(request, {
    signingDate: date ?? signingDate(),
    expiresIn: expiresSeconds,
    signingRegion: REGION,
    signingService: SERVICE,
    // The payload hash is an input of the signature, but neither a signed
    // header nor a query parameter.
    unhoistableHeaders: keep,
    unsignableHeaders: keep,
  });
  const parts: string[] = [];
  for (const [k, v] of Object.entries(signed.query ?? {})) {
    if (v === null || v === undefined) continue;
    for (const one of Array.isArray(v) ? v : [v]) parts.push(`${escapeUri(k)}=${escapeUri(one)}`);
  }
  return `${url.protocol}//${url.host}${url.pathname}?${parts.join('&')}${url.hash}`;
}

// ---------------------------------------------------------------------------
// AWS SDK S3 client integration (device-storage.ts)
// ---------------------------------------------------------------------------

/** Identity used while no credentials are stored; deviceS3Signer() sends such requests unsigned. */
export const ANONYMOUS_IDENTITY: AwsCredentialIdentity = Object.freeze({ accessKeyId: '', secretAccessKey: '' });

/**
 * `credentials:` option for the S3 client of one device. Always resolves
 * (the stored credentials, else ANONYMOUS_IDENTITY) so anonymous reads keep
 * working; read on every request, so saving new credentials needs no new client.
 */
export function credentialsProvider(baseUrl = ''): AwsCredentialIdentityProvider {
  return async () => getCredentials(baseUrl) ?? ANONYMOUS_IDENTITY;
}

/**
 * `signer:` option for the S3 client. Signs only host + x-amz-content-sha256
 * + x-amz-date (the device does not have to keep the SDK's other headers),
 * uses the SDK's skew-corrected signing date, and leaves the request
 * unsigned when no credentials are stored for the device.
 */
export function deviceS3Signer(baseUrl = ''): RequestSigner {
  return {
    async sign(request: HttpRequest, opts: RequestSigningArguments = {}): Promise<HttpRequest> {
      const creds = getCredentials(baseUrl);
      if (!creds) return request;
      const headers = request.headers ?? {};
      const existing = Object.entries(headers).find(([k]) => k.toLowerCase() === SHA256_HEADER)?.[1];
      const hash = existing ?? await payloadHash(request.body);
      const hostHeader = Object.entries(headers).find(([k]) => k.toLowerCase() === 'host')?.[1]
        ?? `${request.hostname}${request.port ? `:${request.port}` : ''}`;
      const minimal: HttpRequest = { ...request, headers: { host: hostHeader, [SHA256_HEADER]: hash } };
      // One skew estimate, not two: ours (learned from refusals, also of S3
      // requests) wins; else the SDK's skew-corrected date.
      const date = clockOffsetMs !== 0 || !opts.signingDate ? signingDate() : new Date(opts.signingDate);
      const signed = await signRequest(minimal, creds, { date });
      const out: Record<string, string> = {};
      for (const [k, v] of Object.entries(headers)) {
        const lk = k.toLowerCase();
        if (lk !== AUTH_HEADER && lk !== DATE_HEADER && lk !== SHA256_HEADER) out[k] = v;
      }
      out[SHA256_HEADER] = hash;
      out[DATE_HEADER] = signed.headers[DATE_HEADER];
      out[AUTH_HEADER] = signed.headers[AUTH_HEADER];
      return { ...request, headers: out };
    },
  };
}

type DeserializeNext = (args: unknown) => Promise<{ response: unknown; output: unknown }>;

interface SdkErrorLike {
  $response?: { statusCode?: number; headers?: Record<string, string>; body?: unknown };
  Code?: string;
  name?: string;
  message?: string;
}

function headerOf(headers: Record<string, string> | undefined, name: string): string | undefined {
  if (!headers) return undefined;
  return Object.entries(headers).find(([k]) => k.toLowerCase() === name)?.[1];
}

/**
 * S3 client middleware (deserialize step): a device refusal (403 +
 * `x-freebutton-auth: required`) emits `auth-required` before the SDK error
 * propagates. Add with addAuthRequiredMiddleware(client, baseUrl).
 */
export function authRequiredMiddleware(baseUrl = ''): (next: DeserializeNext) => DeserializeNext {
  return (next) => async (args) => {
    try {
      return await next(args);
    } catch (err) {
      const e = err as SdkErrorLike;
      const response = e?.$response;
      if (response && isAuthRefusal(response.statusCode ?? 0, headerOf(response.headers, AUTH_RESPONSE_HEADER))) {
        const code = e.Code ?? e.name ?? '';
        if (code === 'RequestTimeTooSkewed') {
          adoptServerTime(headerOf(response.headers, 'x-amz-date') ?? headerOf(response.headers, 'date'), Date.now());
        }
        const configured = getCredentials(baseUrl) !== null;
        notifyAuthRequired({
          origin: deviceOrigin(baseUrl),
          code: code || 'MissingAuthentication',
          message: describeAuthFailure(code, configured),
        });
      }
      throw err;
    }
  };
}

interface MiddlewareStackLike {
  middlewareStack: { add(middleware: never, options: { step: 'deserialize'; name: string; priority: 'high' }): void };
}

/** Register authRequiredMiddleware on an S3 client. */
export function addAuthRequiredMiddleware(client: MiddlewareStackLike, baseUrl = ''): void {
  client.middlewareStack.add(
    authRequiredMiddleware(baseUrl) as never,
    { step: 'deserialize', name: 'freebuttonAuthRequired', priority: 'high' },
  );
}
