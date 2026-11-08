/**
 * Device storage - thin wrapper around the AWS SDK v3 S3 client.
 *
 * The device exposes LittleFS as a minimal S3-compatible API, path-style, with
 * one bucket per kind of content (each a directory on the device):
 *   - `app`      the user's files: LittleFS root minus the other buckets'
 *                directories; key = LittleFS path without the leading slash
 *                ("config.json", "scripts/main.js"). Read/write.
 *   - `examples` /scripts/examples ("button.js"). Read-only.
 *   - `web`      /www, the IDE bundle ("index.html.gz"). Read-only.
 *   - `fonts`    /fonts, the device fonts. Read-only.
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
import { addAuthRequiredMiddleware, credentialsProvider, deviceS3Signer, signedFetch } from './auth';

/** The user's files (read/write). */
export const APP_BUCKET = 'app';
/** Read-only buckets: device examples, the IDE bundle, the device fonts. */
export const READ_ONLY_BUCKETS = ['examples', 'web', 'fonts'] as const;
export type ReadOnlyBucket = typeof READ_ONLY_BUCKETS[number];
export type Bucket = typeof APP_BUCKET | ReadOnlyBucket;

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

type DeserializeHandler = (args: unknown) => Promise<{ response: unknown; output: unknown }>;

/**
 * The SDK drops response headers it does not model, so copy the device's
 * `x-freebutton-reboot-required` header onto the command output.
 */
export function rebootHeaderMiddleware(next: DeserializeHandler): DeserializeHandler {
  return async (args) => {
    const result = await next(args);
    const headers = (result.response as { headers?: Record<string, string> } | undefined)?.headers ?? {};
    const value = Object.entries(headers).find(([k]) => k.toLowerCase() === 'x-freebutton-reboot-required')?.[1];
    if (value !== undefined && String(value).toLowerCase() === 'true' && result.output && typeof result.output === 'object') {
      (result.output as Record<string, unknown>)[REBOOT_FIELD] = true;
    }
    return result;
  };
}

/** @param overrides test hook (e.g. a fake requestHandler). */
export function createS3Client(baseUrl: string, overrides: Partial<S3ClientConfig> = {}): S3Client {
  const client = new S3Client({
    endpoint: endpointFor(baseUrl),
    region: 'us-east-1',
    forcePathStyle: true,
    credentials: credentialsProvider(baseUrl),
    signer: deviceS3Signer(baseUrl),
    maxAttempts: 1,
    ...overrides,
  });
  client.middlewareStack.add(
    rebootHeaderMiddleware as unknown as Parameters<typeof client.middlewareStack.add>[0],
    { step: 'deserialize', name: 'freebuttonRebootHeader', priority: 'high' },
  );
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
  let token: string | undefined;
  for (let page = 0; page < MAX_LIST_PAGES; page++) {
    let out: ListObjectsV2CommandOutput;
    try {
      out = await client.send(new ListObjectsV2Command({
        Bucket: opts.bucket ?? APP_BUCKET,
        ...(opts.prefix ? { Prefix: opts.prefix } : {}),
        ...(opts.delimiter ? { Delimiter: opts.delimiter } : {}),
        ...(token ? { ContinuationToken: token } : {}),
      })) as ListObjectsV2CommandOutput;
    } catch (err) {
      throw mapError(err);
    }
    // A dev server answering index.html with 200 parses as an "empty" list;
    // every real ListBucketResult carries IsTruncated / KeyCount.
    if (!out || typeof out !== 'object' || (out.IsTruncated === undefined && out.KeyCount === undefined)) {
      throw new StorageUnavailableError('Response is not an S3 ListBucketResult');
    }
    for (const c of out.Contents ?? []) {
      if (!c.Key) continue;
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
  return { objects, prefixes };
}

/** Fetch an object's text. Returns null when it does not exist. */
export async function getObject(
  baseUrl: string,
  key: string,
  bucket: Bucket = APP_BUCKET,
): Promise<{ text: string; etag?: string } | null> {
  try {
    const out = await clientFor(baseUrl).send(new GetObjectCommand({ Bucket: bucket, Key: key })) as {
      Body?: { transformToString(): Promise<string> };
      ETag?: string;
    };
    const text = out.Body ? await out.Body.transformToString() : '';
    return { text, ...(out.ETag ? { etag: out.ETag } : {}) };
  } catch (err) {
    if (statusOf(err as SdkErrorLike) === 404) return null; // NoSuchKey
    throw mapError(err);
  }
}

/** Whether an object exists (HEAD). */
export async function headObject(baseUrl: string, key: string, bucket: Bucket = APP_BUCKET): Promise<boolean> {
  try {
    await clientFor(baseUrl).send(new HeadObjectCommand({ Bucket: bucket, Key: key }));
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
  return 'text/plain; charset=utf-8';
}

/**
 * Store `body` at `key` in bucket `app` (the only writable one). config.json responses carry
 * `x-freebutton-reboot-required: true` (config is applied at boot).
 */
export async function putObject(baseUrl: string, key: string, body: string): Promise<PutResult> {
  try {
    const out = await clientFor(baseUrl).send(new PutObjectCommand({
      Bucket: APP_BUCKET,
      Key: key,
      Body: body,
      ContentType: contentTypeFor(key),
    })) as { ETag?: string; [REBOOT_FIELD]?: boolean };
    return {
      ...(out?.ETag ? { etag: out.ETag } : {}),
      rebootRequired: out?.[REBOOT_FIELD] === true,
    };
  } catch (err) {
    throw mapError(err);
  }
}

export async function deleteObject(baseUrl: string, key: string): Promise<void> {
  try {
    await clientFor(baseUrl).send(new DeleteObjectCommand({ Bucket: APP_BUCKET, Key: key }));
  } catch (err) {
    throw mapError(err);
  }
}

/** POST /api/reboot (not part of S3; lives next to it on the device). */
export async function rebootDevice(baseUrl: string, fetchImpl: typeof fetch = signedFetch): Promise<void> {
  const res = await fetchImpl(`${baseUrl.replace(/\/+$/, '')}/api/reboot`, { method: 'POST' });
  if (!res.ok) throw new S3Error(res.status, '', `Reboot failed: HTTP ${res.status} ${res.statusText}`.trim());
}
