import { describe, it, expect, beforeEach, afterEach, afterAll, vi } from 'vitest';
import { Readable } from 'node:stream';
import { mockClient } from 'aws-sdk-client-mock';
import {
  S3Client, ListObjectsV2Command, GetObjectCommand, PutObjectCommand, DeleteObjectCommand, HeadObjectCommand,
} from '@aws-sdk/client-s3';
import {
  _setClientFactory, createS3Client, listObjects, getObject, putObject, deleteObject, headObject,
  rebootDevice, mapError, endpointFor, rebootHeaderMiddleware, contentTypeFor,
  S3Error, StorageUnavailableError, APP_BUCKET, REBOOT_FIELD,
} from '../device-storage';

/** An SDK-style service error as the client throws it. */
function sdkError(name: string, status: number | undefined, message = name) {
  const err = new Error(message) as Error & { $metadata?: { httpStatusCode?: number } };
  err.name = name;
  if (status !== undefined) err.$metadata = { httpStatusCode: status };
  return err;
}

describe('device-storage with the S3 client mocked', () => {
  const s3 = mockClient(S3Client);
  let created: string[] = [];

  beforeEach(() => {
    s3.reset();
    created = [];
    // Real S3Client instances (their send() is mocked), one per base URL.
    _setClientFactory((base) => { created.push(base); return createS3Client(base); });
  });

  afterEach(() => _setClientFactory(null));
  // mockClient(S3Client) patches every instance: undo it for the wire tests.
  afterAll(() => s3.restore());

  it('lists all pages, following NextContinuationToken', async () => {
    s3.on(ListObjectsV2Command, { Bucket: APP_BUCKET, ContinuationToken: undefined }).resolves({
      KeyCount: 2, IsTruncated: true, NextContinuationToken: 'p2',
      Contents: [
        { Key: 'config.json', Size: 10082, LastModified: new Date('2026-11-08T12:00:00Z'), ETag: '"a"' },
        { Key: 'scripts/main.js', Size: 120 },
      ],
    });
    s3.on(ListObjectsV2Command, { Bucket: APP_BUCKET, ContinuationToken: 'p2' }).resolves({
      KeyCount: 1, IsTruncated: false,
      Contents: [{ Key: 'scripts/util.js', Size: 250 }],
    });

    const result = await listObjects('');
    expect(result.objects.map(o => o.key)).toEqual(['config.json', 'scripts/main.js', 'scripts/util.js']);
    expect(result.objects[0]).toEqual({ key: 'config.json', size: 10082, lastModified: new Date('2026-11-08T12:00:00Z'), etag: '"a"' });
    expect(s3.commandCalls(ListObjectsV2Command)).toHaveLength(2);
    expect(s3.commandCalls(ListObjectsV2Command)[1].args[0].input).toMatchObject({ Bucket: 'app', ContinuationToken: 'p2' });
  });

  it('passes prefix/delimiter and returns CommonPrefixes', async () => {
    s3.on(ListObjectsV2Command).resolves({
      KeyCount: 1, IsTruncated: false,
      Contents: [{ Key: 'config.json', Size: 5 }],
      CommonPrefixes: [{ Prefix: 'notes/' }, { Prefix: 'scripts/' }],
    });
    const result = await listObjects('', { prefix: '', delimiter: '/' });
    expect(result.prefixes).toEqual(['notes/', 'scripts/']);
    expect(s3.commandCalls(ListObjectsV2Command)[0].args[0].input).toEqual({ Bucket: 'app', Delimiter: '/' });
  });

  it('lists and reads a read-only bucket when asked', async () => {
    s3.on(ListObjectsV2Command, { Bucket: 'web' }).resolves({
      KeyCount: 1, IsTruncated: false, Contents: [{ Key: 'index.html.gz', Size: 9000 }],
    });
    s3.on(GetObjectCommand, { Bucket: 'examples', Key: 'button.js' }).resolves({
      Body: { transformToString: async () => '// button' } as never,
    });
    expect((await listObjects('', { bucket: 'web' })).objects).toEqual([{ key: 'index.html.gz', size: 9000 }]);
    expect(await getObject('', 'button.js', 'examples')).toEqual({ text: '// button' });
    expect(s3.commandCalls(ListObjectsV2Command)[0].args[0].input).toEqual({ Bucket: 'web' });
  });

  it('drops 1970 mtimes (device without a clock)', async () => {
    s3.on(ListObjectsV2Command).resolves({
      KeyCount: 1, IsTruncated: false,
      Contents: [{ Key: 'a.txt', Size: 1, LastModified: new Date('1970-01-01T00:00:00Z') }],
    });
    const [obj] = (await listObjects('')).objects;
    expect(obj.lastModified).toBeUndefined();
  });

  it('stops when the device repeats the continuation token', async () => {
    s3.on(ListObjectsV2Command).resolves({ KeyCount: 0, IsTruncated: true, NextContinuationToken: 'same', Contents: [] });
    await listObjects('');
    expect(s3.commandCalls(ListObjectsV2Command)).toHaveLength(2);
  });

  it('treats a list answer without IsTruncated/KeyCount as "no storage API"', async () => {
    s3.on(ListObjectsV2Command).resolves({});
    await expect(listObjects('')).rejects.toBeInstanceOf(StorageUnavailableError);
  });

  it('maps an HTML 404 / network failure on list to StorageUnavailableError', async () => {
    s3.on(ListObjectsV2Command).rejects(sdkError('NotFound', 404));
    await expect(listObjects('')).rejects.toBeInstanceOf(StorageUnavailableError);
    s3.on(ListObjectsV2Command).rejects(new TypeError('Failed to fetch'));
    await expect(listObjects('')).rejects.toBeInstanceOf(StorageUnavailableError);
  });

  it('keeps S3 errors from the device as S3Error with Code + Message', async () => {
    s3.on(ListObjectsV2Command).rejects(sdkError('InternalError', 500, 'out of memory'));
    const err = await listObjects('').catch(e => e);
    expect(err).toBeInstanceOf(S3Error);
    expect(err.message).toBe('InternalError: out of memory');
    expect(err.status).toBe(500);
  });

  it('reads an object as text', async () => {
    s3.on(GetObjectCommand, { Bucket: 'app', Key: 'scripts/main.js' }).resolves({
      Body: { transformToString: async () => 'print(1);' } as never,
      ETag: '"e1"',
    });
    expect(await getObject('', 'scripts/main.js')).toEqual({ text: 'print(1);', etag: '"e1"' });
  });

  it('returns null for a missing object (NoSuchKey)', async () => {
    s3.on(GetObjectCommand).rejects(sdkError('NoSuchKey', 404));
    expect(await getObject('', 'scripts/main.js')).toBeNull();
  });

  it('headObject: true / false on 404', async () => {
    s3.on(HeadObjectCommand, { Key: 'a.js' }).resolves({});
    s3.on(HeadObjectCommand, { Key: 'b.js' }).rejects(sdkError('NotFound', 404));
    expect(await headObject('', 'a.js')).toBe(true);
    expect(await headObject('', 'b.js')).toBe(false);
  });

  it('puts text with a content type and reports reboot-required', async () => {
    s3.on(PutObjectCommand).resolves({ ETag: '"x"', [REBOOT_FIELD]: true } as never);
    const res = await putObject('', 'config.json', '{"a":1}');
    expect(res).toEqual({ etag: '"x"', rebootRequired: true });
    expect(s3.commandCalls(PutObjectCommand)[0].args[0].input).toEqual({
      Bucket: 'app', Key: 'config.json', Body: '{"a":1}', ContentType: 'application/json',
    });
  });

  it('surfaces device rule violations (403 / 413 / 400)', async () => {
    s3.on(PutObjectCommand).rejects(sdkError('AccessDenied', 403, 'this path is read-only'));
    await expect(putObject('', 'js_bytecode_meta.json', '')).rejects.toThrow('AccessDenied: this path is read-only');
    s3.on(PutObjectCommand).rejects(sdkError('EntityTooLarge', 413, 'too big'));
    await expect(putObject('', 'a.txt', '')).rejects.toThrow('EntityTooLarge');
    s3.on(PutObjectCommand).rejects(sdkError('InvalidArgument', 400, 'invalid JSON at offset 3'));
    await expect(putObject('', 'config.json', '{')).rejects.toThrow('InvalidArgument: invalid JSON at offset 3');
  });

  it('deletes an object', async () => {
    s3.on(DeleteObjectCommand).resolves({});
    await deleteObject('', 'scripts/old.js');
    expect(s3.commandCalls(DeleteObjectCommand)[0].args[0].input).toEqual({ Bucket: 'app', Key: 'scripts/old.js' });
  });

  it('reuses one client per endpoint', async () => {
    s3.on(DeleteObjectCommand).resolves({});
    await deleteObject('http://10.0.0.5', 'a.js');
    await deleteObject('http://10.0.0.5/', 'b.js');
    await deleteObject('', 'c.js');
    expect(created).toEqual(['http://10.0.0.5', '']);
  });
});

describe('device-storage helpers', () => {
  it('endpoint defaults to the serving origin', () => {
    expect(endpointFor('')).toBe(location.origin);
    expect(endpointFor('http://10.0.0.5/')).toBe('http://10.0.0.5');
  });

  it('mapError classifies errors', () => {
    expect(mapError(new TypeError('Failed to fetch'))).toBeInstanceOf(StorageUnavailableError);
    expect(mapError(sdkError('UnknownError', 404))).toBeInstanceOf(StorageUnavailableError);
    expect(mapError(sdkError('AccessDenied', 403))).toBeInstanceOf(S3Error);
  });

  it('content types', () => {
    expect(contentTypeFor('a.json')).toBe('application/json');
    expect(contentTypeFor('a.js')).toBe('application/javascript');
    expect(contentTypeFor('a.txt')).toMatch(/^text\/plain/);
  });

  it('rebootHeaderMiddleware copies x-freebutton-reboot-required to the output', async () => {
    const mw = rebootHeaderMiddleware(async () => ({
      response: { headers: { 'X-FreeButton-Reboot-Required': 'true' } },
      output: { ETag: '"x"' },
    }));
    const { output } = await mw({});
    expect((output as Record<string, unknown>)[REBOOT_FIELD]).toBe(true);
    const none = await rebootHeaderMiddleware(async () => ({ response: { headers: {} }, output: {} }))({});
    expect((none.output as Record<string, unknown>)[REBOOT_FIELD]).toBeUndefined();
  });

  it('rebootDevice POSTs /api/reboot', async () => {
    const fetchImpl = vi.fn(async () => new Response('{"status":"rebooting"}', { status: 200 }));
    await rebootDevice('http://10.0.0.5', fetchImpl as unknown as typeof fetch);
    expect(fetchImpl).toHaveBeenCalledWith('http://10.0.0.5/api/reboot', { method: 'POST' });
  });
});

/**
 * Wire level: the real SDK pipeline with a fake request handler, to pin down
 * what the device actually receives (path style, key encoding, no SigV4).
 */
describe('device-storage on the wire', () => {
  interface Seen { method: string; path: string; query: Record<string, unknown>; headers: Record<string, string>; body: unknown; hostname: string }
  let seen: Seen[] = [];

  function respond(status: number, body: string, headers: Record<string, string> = {}) {
    _setClientFactory((base) => createS3Client(base, {
      requestHandler: {
        handle: async (req: Seen) => {
          seen.push(req);
          return {
            response: {
              statusCode: status,
              headers: { 'content-type': 'application/xml', ...headers },
              body: Readable.from([Buffer.from(body)]),
            },
          };
        },
      } as never,
    }));
  }

  beforeEach(() => { seen = []; });
  afterEach(() => _setClientFactory(null));

  it('lists path-style on the device endpoint, unsigned', async () => {
    respond(200, '<?xml version="1.0"?><ListBucketResult><KeyCount>1</KeyCount><Contents><Key>config.json</Key><Size>5</Size></Contents><IsTruncated>false</IsTruncated></ListBucketResult>');
    const result = await listObjects('http://10.0.0.5');
    expect(result.objects).toEqual([{ key: 'config.json', size: 5 }]);
    expect(seen[0].hostname).toBe('10.0.0.5');
    expect(seen[0].path).toBe('/app/');
    expect(seen[0].query).toMatchObject({ 'list-type': '2' });
    expect(Object.keys(seen[0].headers).map(h => h.toLowerCase())).not.toContain('authorization');
  });

  it('addresses a read-only bucket path-style', async () => {
    respond(200, '<ListBucketResult><KeyCount>1</KeyCount><Contents><Key>index.html.gz</Key><Size>9</Size></Contents><IsTruncated>false</IsTruncated></ListBucketResult>');
    expect((await listObjects('', { bucket: 'web' })).objects).toEqual([{ key: 'index.html.gz', size: 9 }]);
    expect(seen[0].path).toBe('/web/');
  });

  it('parses XML pagination and CommonPrefixes', async () => {
    respond(200, '<ListBucketResult><KeyCount>0</KeyCount><CommonPrefixes><Prefix>scripts/</Prefix></CommonPrefixes><IsTruncated>false</IsTruncated></ListBucketResult>');
    expect((await listObjects('', { delimiter: '/' })).prefixes).toEqual(['scripts/']);
  });

  it('a dev server answering index.html is "no storage API", not an empty device', async () => {
    respond(200, '<!doctype html><html><body>vite</body></html>', { 'content-type': 'text/html' });
    await expect(listObjects('')).rejects.toBeInstanceOf(StorageUnavailableError);
    respond(404, '<html>Not found</html>', { 'content-type': 'text/html' });
    await expect(listObjects('')).rejects.toBeInstanceOf(StorageUnavailableError);
  });

  it('encodes each key segment', async () => {
    respond(404, '<Error><Code>NoSuchKey</Code><Message>not found</Message></Error>');
    expect(await getObject('', 'scripts/my file#1.js')).toBeNull();
    expect(seen[0].path).toBe('/app/scripts/my%20file%231.js');
  });

  it('PUT sends a plain string body (no aws-chunked) and reads the reboot header', async () => {
    respond(200, '', { etag: '"abc"', 'x-freebutton-reboot-required': 'true' });
    const res = await putObject('', 'config.json', '{"a":1}');
    expect(res).toEqual({ etag: '"abc"', rebootRequired: true });
    const req = seen[0];
    expect(req.method).toBe('PUT');
    expect(req.path).toBe('/app/config.json');
    expect(req.body).toBe('{"a":1}');
    expect(req.headers['content-type']).toBe('application/json');
    expect(req.headers['content-encoding'] ?? '').not.toContain('aws-chunked');
    expect(req.headers['x-amz-checksum-crc32']).toBeTruthy();
    expect(Object.keys(req.headers).map(h => h.toLowerCase())).not.toContain('authorization');
  });

  it('turns an S3 <Error> body into Code: Message', async () => {
    respond(400, '<Error><Code>InvalidArgument</Code><Message>invalid JSON at offset 3</Message></Error>');
    const err = await putObject('', 'config.json', '{"a":').catch(e => e);
    expect(err).toBeInstanceOf(S3Error);
    expect(err.message).toBe('InvalidArgument: invalid JSON at offset 3');
    expect(err.status).toBe(400);
  });
});
