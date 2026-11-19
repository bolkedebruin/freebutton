/**
 * Device storage - thin wrapper around the AWS SDK v3 S3 client.
 *
 * The device exposes its storage as a minimal S3-compatible API, path-style,
 * with one bucket per kind of content (a LittleFS directory, or the media
 * store for fonts):
 *   - `app`      the user's files: LittleFS root minus the other buckets'
 *                directories; key = LittleFS path without the leading slash
 *                ("config.json", "scripts/main.js"). Read/write.
 *   - `examples` /scripts/examples ("button.js"). Read-only.
 *   - `web`      /www, the IDE bundle ("index.html.gz"). Read-only.
 *   - `fonts`    the TrueType fonts the device renders with (media store,
 *                flat keys "medium.ttf"). Readable by anyone, writable by
 *                signed S3 clients (.ttf/.otf, <= 2 MiB); a PUT / DELETE
 *                makes the device reload its fonts. The IDE uploads and
 *                deletes fonts (Assets) and reads them for the previews
 *                (font-loader.ts, getObjectBytes: a plain fetch of
 *                /fonts/x.ttf would hit the IDE's own static files).
 *   - `images`   the raster images the device draws (media store, flat keys
 *                "ico-rain-48x48.bin", LVGL 9 .bin). The IDE writes the
 *                `ico-*` rasters of the config's icons (icon-sync.ts).
 * Every function takes the bucket as an option and defaults to `app`. The
 * rest of the IDE talks to this module only, never to the SDK directly.
 *
 * SDK configuration:
 *   - endpoint = device base URL (same origin by default), forcePathStyle,
 *     region us-east-1 (required by the SDK, meaningless to the device)
 *   - SigV4 with the credentials from Settings > Device access (auth.ts):
 *     the device requires signatures for writes and for reads of bucket
 *     "app"; without stored credentials requests go out unsigned, so the
 *     anonymous buckets (web, examples, fonts) still work. The signer signs
 *     only host / x-amz-date / x-amz-content-sha256 to keep the headers the
 *     device has to buffer small.
 *   - maxAttempts 1: no automatic retries against a RAM-starved device
 *   - request checksums stay at the SDK default (string bodies are not
 *     streamed, so PutObject sends a plain body + x-amz-checksum-crc32,
 *     never aws-chunked)
 */
import type { StorageUsage } from './app-state';
import { oneAtATime } from './device-queue';
import {
  S3Client,
  ListObjectsV2Command,
  GetObjectCommand,
  PutObjectCommand,
  DeleteObjectCommand,
  HeadObjectCommand,
  type ListObjectsV2CommandOutput,
  type S3ClientConfig,
} from '@aws-sdk/client-s3';
import { cacheModeFor, versionFor } from './http-cache';
import { addAuthRequiredMiddleware, credentialsProvider, deviceS3Signer, signedFetch } from './auth';

/** The user's files (read/write). */
export const APP_BUCKET = 'app';
/**
 * Buckets the Assets panel lists on request (never downloaded there): device
 * examples, the IDE bundle, the device fonts. (`fonts` is also writable:
 * FONTS_BUCKET.)
 */
export const READ_ONLY_BUCKETS = ['examples', 'web', 'fonts'] as const;
export type ReadOnlyBucket = typeof READ_ONLY_BUCKETS[number];
/** Media-store bucket of raster images (icons); the IDE owns its `ico-*` keys. */
export const IMAGES_BUCKET = 'images';
/** Media-store bucket of the TrueType fonts (uploaded / deleted from Assets). */
export const FONTS_BUCKET = 'fonts';
export type Bucket = typeof APP_BUCKET | typeof IMAGES_BUCKET | ReadOnlyBucket;
/** Buckets the IDE writes. */
export type WritableBucket = typeof APP_BUCKET | typeof IMAGES_BUCKET | typeof FONTS_BUCKET;

export interface FsObject {
  key: string;
  size: number;
  /** Absent when the device has no real mtimes (reports 1970). */
  lastModified?: Date;
  etag?: string;
}

export interface ListResult {
  objects: FsObject[];
  /** CommonPrefixes when a delimiter was given ("scripts/"). */
  prefixes: string[];
  /** Space of the bucket's store, when the device reported it with the listing. */
  usage?: StorageUsage;
}

export interface PutResult {
  etag?: string;
  rebootRequired: boolean;
}

/** An S3 error answered by the device (Code + Message). */
export class S3Error extends Error {
  constructor(
    readonly status: number,
    readonly code: string,
    message: string,
  ) {
    super(code ? `${code}: ${message}` : message);
    this.name = 'S3Error';
  }
}

/**
 * The endpoint does not speak the storage API at all: network failure,
 * firmware without the S3 API, or a dev server answering with HTML. Callers fall
 * back to the in-browser file store.
 */
export class StorageUnavailableError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'StorageUnavailableError';
  }
}

/** Minimal surface of S3Client we use; lets tests inject a fake. */
export interface S3Like {
  send(command: unknown): Promise<unknown>;
}

/** Guard against a device that keeps returning IsTruncated with the same token. */
const MAX_LIST_PAGES = 64;
/** mtimes before this are treated as "device has no clock-based mtime". */
const MIN_VALID_MTIME = Date.UTC(1971, 0, 1);
/** Error codes that prove an S3 implementation answered. */
const S3_ERROR_CODES = new Set([
  'AccessDenied', 'EntityTooLarge', 'InvalidArgument', 'InvalidRequest', 'NoSuchKey',
  'NoSuchBucket', 'NotImplemented', 'InternalError', 'ServiceUnavailable', 'SlowDown',
  'MethodNotAllowed', 'InvalidObjectState', 'PreconditionFailed',
  // Authentication refusals (403): the storage API is there, access is not.
  'SignatureDoesNotMatch', 'RequestTimeTooSkewed', 'MissingAuthentication',
]);

/** Resolve the endpoint: "" means the origin that served the IDE. */
export function endpointFor(baseUrl: string): string {
  const trimmed = baseUrl.trim().replace(/\/+$/, '');
  if (trimmed) return trimmed;
  return typeof location !== 'undefined' ? location.origin : 'http://localhost';
}

/** Output field the middleware below sets from x-freebutton-reboot-required. */
export const REBOOT_FIELD = 'FreebuttonRebootRequired';

/** Output field the middleware below sets from the x-fb-fs-* / x-fb-media-* headers. */
export const USAGE_FIELD = 'FreebuttonUsage';

/**
 * The space of a bucket's store, from the headers the device puts on its
 * listings and HeadBucket: x-fb-fs-total/-used (the filesystem behind app,
 * web and examples) or x-fb-media-total/-used/-free (fonts, images).
 * Undefined when there are none (older firmware, another server).
 */
export function usageFromHeaders(headers: Record<string, string>): StorageUsage | undefined {
  const num = (name: string): number | undefined => {
    const v = Object.entries(headers).find(([k]) => k.toLowerCase() === name)?.[1];
    return v !== undefined && /^\d+$/.test(String(v).trim()) ? Number(v) : undefined;
  };
  const out: StorageUsage = {};
  const fsTotal = num('x-fb-fs-total'), fsUsed = num('x-fb-fs-used');
  if (fsTotal !== undefined && fsUsed !== undefined) out.fs = { total: fsTotal, used: fsUsed };
  const mTotal = num('x-fb-media-total'), mUsed = num('x-fb-media-used'), mFree = num('x-fb-media-free');
  if (mTotal !== undefined && mUsed !== undefined && mFree !== undefined) out.media = { total: mTotal, used: mUsed, free: mFree };
  return out.fs || out.media ? out : undefined;
}

type DeserializeHandler = (args: unknown) => Promise<{ response: unknown; output: unknown }>;

/**
 * The SDK drops response headers it does not model, so copy the device's
 * `x-freebutton-reboot-required` header onto the command output.
 */
export function rebootHeaderMiddleware(next: DeserializeHandler): DeserializeHandler {
  return async (args) => {
    const result = await next(args);
    const headers = (result.response as { headers?: Record<string, string> } | undefined)?.headers ?? {};
    const usage = usageFromHeaders(headers);
    if (usage && result.output && typeof result.output === 'object') {
      (result.output as Record<string, unknown>)[USAGE_FIELD] = usage;
    }
    const value = Object.entries(headers).find(([k]) => k.toLowerCase() === 'x-freebutton-reboot-required')?.[1];
    if (value !== undefined && String(value).toLowerCase() === 'true' && result.output && typeof result.output === 'object') {
      (result.output as Record<string, unknown>)[REBOOT_FIELD] = true;
    }
    return result;
  };
}

const VERSION_PARAM = 'v';
/** ETags of the objects seen in listings, by endpoint + bucket + key (http-cache.ts). */
const knownEtags = new Map<string, string>();
const etagKey = (baseUrl: string, bucket: string, key: string) => `${endpointFor(baseUrl)}|${bucket}|${key}`;
/** The `v` of the request being sent; requests go one at a time (oneAtATime). */
let sendingVersion: string | undefined;

type BuildHandler = (args: { request?: { query?: Record<string, unknown> } }) => Promise<unknown>;
function versionMiddleware(next: BuildHandler): BuildHandler {
  return async (args) => {
    if (sendingVersion && args.request) {
      args.request.query = { ...(args.request.query ?? {}), [VERSION_PARAM]: sendingVersion };
    }
    return next(args);
  };
}

/** Send a GetObject, as `?v=<etag>` from the browser's cache when the object's ETag is known. */
async function sendGet<T>(baseUrl: string, bucket: string, key: string): Promise<T> {
  sendingVersion = versionFor(bucket, key, knownEtags.get(etagKey(baseUrl, bucket, key)));
  try {
    return await clientFor(baseUrl).send(new GetObjectCommand({ Bucket: bucket, Key: key })) as T;
  } finally {
    sendingVersion = undefined;
  }
}

/** Test hook. */
export function _forgetEtags(): void { knownEtags.clear(); }

/** @param overrides test hook (e.g. a fake requestHandler). */
export function createS3Client(baseUrl: string, overrides: Partial<S3ClientConfig> = {}): S3Client {
  const client = new S3Client({
    endpoint: endpointFor(baseUrl),
    region: 'us-east-1',
    forcePathStyle: true,
    credentials: credentialsProvider(baseUrl),
    signer: deviceS3Signer(baseUrl),
    maxAttempts: 1,
    requestHandler: {
      requestInit: (req: { method: string; query?: Record<string, unknown> }) =>
        ({ cache: cacheModeFor(req.method, typeof req.query?.[VERSION_PARAM] === 'string') }),
    },
    ...overrides,
  });
  client.middlewareStack.add(
    rebootHeaderMiddleware as unknown as Parameters<typeof client.middlewareStack.add>[0],
    { step: 'deserialize', name: 'freebuttonRebootHeader', priority: 'high' },
  );
  // Before signing: the version is part of the signed URL.
  (client.middlewareStack as unknown as { add(mw: unknown, opts: { step: 'build'; name: string }): void })
    .add(versionMiddleware, { step: 'build', name: 'freebuttonObjectVersion' });
  addAuthRequiredMiddleware(client, baseUrl);
  return client;
}

let factory: (baseUrl: string) => S3Like = (baseUrl) => createS3Client(baseUrl);
const clients = new Map<string, S3Like>();

function clientFor(baseUrl: string): S3Like {
  const endpoint = endpointFor(baseUrl);
  let client = clients.get(endpoint);
  if (!client) {
    client = factory(baseUrl);
    clients.set(endpoint, client);
  }
  return client;
}

/** Test hook: replace how clients are built (and drop cached ones). */
export function _setClientFactory(fn: ((baseUrl: string) => S3Like) | null): void {
  factory = fn ?? ((baseUrl) => createS3Client(baseUrl));
  clients.clear();
}

interface SdkErrorLike {
  name?: string;
  message?: string;
  Code?: string;
  $metadata?: { httpStatusCode?: number };
  $response?: { statusCode?: number };
}

function statusOf(err: SdkErrorLike): number | undefined {
  return err.$metadata?.httpStatusCode ?? err.$response?.statusCode;
}

function codeOf(err: SdkErrorLike): string {
  return err.Code ?? err.name ?? '';
}

/**
 * Map an SDK error: an S3 <Error> from the device -> S3Error; anything that
 * shows there is no storage API behind the endpoint -> StorageUnavailableError.
 */
export function mapError(err: unknown): S3Error | StorageUnavailableError {
  const e = (typeof err === 'object' && err !== null ? err : { message: String(err) }) as SdkErrorLike;
  const status = statusOf(e);
  const code = codeOf(e);
  if (status === undefined) {
    // No HTTP response at all (network error / CORS) or an unparseable body.
    return new StorageUnavailableError(`Device storage not reachable (${e.message ?? code})`);
  }
  if (S3_ERROR_CODES.has(code)) return new S3Error(status, code, e.message ?? '');
  return new StorageUnavailableError(`No storage API on this device (HTTP ${status}${code ? ' ' + code : ''})`);
}

function parseLastModified(d: Date | undefined): Date | undefined {
  if (!d || Number.isNaN(d.getTime()) || d.getTime() < MIN_VALID_MTIME) return undefined;
  return d;
}

/** List all objects, following NextContinuationToken while IsTruncated. */
export async function listObjects(
  baseUrl: string,
  opts: { prefix?: string; delimiter?: string; bucket?: Bucket } = {},
): Promise<ListResult> {
  const client = clientFor(baseUrl);
  const objects: FsObject[] = [];
  const prefixes: string[] = [];
  let usage: StorageUsage | undefined;
  let token: string | undefined;
  for (let page = 0; page < MAX_LIST_PAGES; page++) {
    let out: ListObjectsV2CommandOutput;
    try {
      out = await oneAtATime(() => client.send(new ListObjectsV2Command({
        Bucket: opts.bucket ?? APP_BUCKET,
        ...(opts.prefix ? { Prefix: opts.prefix } : {}),
        ...(opts.delimiter ? { Delimiter: opts.delimiter } : {}),
        ...(token ? { ContinuationToken: token } : {}),
      }))) as ListObjectsV2CommandOutput;
    } catch (err) {
      throw mapError(err);
    }
    // A dev server answering index.html with 200 parses as an "empty" list;
    // every real ListBucketResult carries IsTruncated / KeyCount.
    if (!out || typeof out !== 'object' || (out.IsTruncated === undefined && out.KeyCount === undefined)) {
      throw new StorageUnavailableError('Response is not an S3 ListBucketResult');
    }
    usage = (out as unknown as Record<string, StorageUsage | undefined>)[USAGE_FIELD] ?? usage;
    for (const c of out.Contents ?? []) {
      if (!c.Key) continue;
      const known = etagKey(baseUrl, opts.bucket ?? APP_BUCKET, c.Key);
      if (c.ETag) knownEtags.set(known, c.ETag); else knownEtags.delete(known);
      objects.push({
        key: c.Key,
        size: c.Size ?? 0,
        ...(parseLastModified(c.LastModified) ? { lastModified: c.LastModified } : {}),
        ...(c.ETag ? { etag: c.ETag } : {}),
      });
    }
    for (const p of out.CommonPrefixes ?? []) {
      if (p.Prefix) prefixes.push(p.Prefix);
    }
    const next = out.NextContinuationToken;
    if (!out.IsTruncated || !next || next === token) break;
    token = next;
  }
  return { objects, prefixes, ...(usage ? { usage } : {}) };
}

/** Fetch an object's text. Returns null when it does not exist. */
export async function getObject(
  baseUrl: string,
  key: string,
  bucket: Bucket = APP_BUCKET,
): Promise<{ text: string; etag?: string } | null> {
  try {
    // The body is read inside the queue's turn: the device is still sending it.
    return await oneAtATime(async () => {
      const out = await sendGet<{
        Body?: { transformToString(): Promise<string> };
        ETag?: string;
      }>(baseUrl, bucket, key);
      const text = out.Body ? await out.Body.transformToString() : '';
      return { text, ...(out.ETag ? { etag: out.ETag } : {}) };
    });
  } catch (err) {
    if (statusOf(err as SdkErrorLike) === 404) return null; // NoSuchKey
    throw mapError(err);
  }
}

/**
 * Fetch an object's bytes (fonts, rasters: getObject would decode them as
 * text). Returns null when it does not exist.
 */
export async function getObjectBytes(
  baseUrl: string,
  key: string,
  bucket: Bucket = APP_BUCKET,
): Promise<{ bytes: Uint8Array; etag?: string } | null> {
  try {
    return await oneAtATime(async () => {
      const out = await sendGet<{
        Body?: { transformToByteArray(): Promise<Uint8Array> };
        ETag?: string;
      }>(baseUrl, bucket, key);
      const bytes = out.Body ? await out.Body.transformToByteArray() : new Uint8Array(0);
      return { bytes, ...(out.ETag ? { etag: out.ETag } : {}) };
    });
  } catch (err) {
    if (statusOf(err as SdkErrorLike) === 404) return null; // NoSuchKey
    throw mapError(err);
  }
}

/** Whether an object exists (HEAD). */
export async function headObject(baseUrl: string, key: string, bucket: Bucket = APP_BUCKET): Promise<boolean> {
  try {
    await oneAtATime(() => clientFor(baseUrl).send(new HeadObjectCommand({ Bucket: bucket, Key: key })));
    return true;
  } catch (err) {
    // HEAD has no body, so a 404 arrives without an S3 code.
    if (statusOf(err as SdkErrorLike) === 404) return false;
    throw mapError(err);
  }
}

export function contentTypeFor(key: string): string {
  const ext = key.slice(key.lastIndexOf('.') + 1).toLowerCase();
  if (ext === 'json') return 'application/json';
  if (ext === 'js') return 'application/javascript';
  if (ext === 'svg') return 'image/svg+xml';
  if (ext === 'bin') return 'application/octet-stream';
  if (ext === 'ttf') return 'font/ttf';
  if (ext === 'otf') return 'font/otf';
  return 'text/plain; charset=utf-8';
}

/**
 * Store `body` at `key` (bucket `app` by default; `images` for rasters,
 * `fonts` for TrueType fonts).
 * config.json responses carry `x-freebutton-reboot-required: true` (config
 * is applied at boot). A Uint8Array body is sent as is, with its length
 * known up front (media buckets require Content-Length).
 */
export async function putObject(
  baseUrl: string,
  key: string,
  body: string | Uint8Array,
  bucket: WritableBucket = APP_BUCKET,
): Promise<PutResult> {
  try {
    const out = await oneAtATime(() => clientFor(baseUrl).send(new PutObjectCommand({
      Bucket: bucket,
      Key: key,
      Body: body,
      ContentType: contentTypeFor(key),
    }))) as { ETag?: string; [REBOOT_FIELD]?: boolean };
    // Read fresh until the next listing gives the new ETag.
    knownEtags.delete(etagKey(baseUrl, bucket, key));
    return {
      ...(out?.ETag ? { etag: out.ETag } : {}),
      rebootRequired: out?.[REBOOT_FIELD] === true,
    };
  } catch (err) {
    throw mapError(err);
  }
}

export async function deleteObject(baseUrl: string, key: string, bucket: WritableBucket = APP_BUCKET): Promise<void> {
  try {
    await oneAtATime(() => clientFor(baseUrl).send(new DeleteObjectCommand({ Bucket: bucket, Key: key })));
    knownEtags.delete(etagKey(baseUrl, bucket, key));
  } catch (err) {
    throw mapError(err);
  }
}

/** POST /api/reboot (not part of S3; lives next to it on the device). */
export async function rebootDevice(baseUrl: string, fetchImpl: typeof fetch = signedFetch): Promise<void> {
  const res = await fetchImpl(`${baseUrl.replace(/\/+$/, '')}/api/reboot`, { method: 'POST' });
  if (!res.ok) throw new S3Error(res.status, '', `Reboot failed: HTTP ${res.status} ${res.statusText}`.trim());
}
