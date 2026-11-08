# HTTP API authentication (AWS SigV4)

The device has no resources for HTTPS, so its HTTP API authenticates requests
with **AWS Signature Version 4** (`AWS4-HMAC-SHA256`) instead of TLS.

What this gives:

- **Authenticity**: only a holder of the device secret can produce a valid
  signature. The secret never goes over the network.
- **Integrity of what is signed**: method, path, query string, every header
  in `SignedHeaders` (always including `host`), and the body when
  `x-amz-content-sha256` carries its SHA-256 (S3 PUT).
- **Replay protection**: `x-amz-date` must be within +-15 minutes of the
  device clock; a presigned URL is valid from 15 minutes before `X-Amz-Date`
  until `X-Amz-Date + X-Amz-Expires` (max 604800 s). Inside that window a
  captured request can be replayed.

What it does **not** give: **confidentiality**. Bodies and responses cross the
network in clear text, including `GET /config` (WiFi and MQTT passwords) and
uploaded scripts. Anyone on the network can read them; they cannot forge or
alter signed requests.

If the device clock is not set (year < 2025, no NTP), the time window is not
checked; the signature still is (and replays are then not limited).

## Credentials

- Access key id: `FB` + 18 characters base32 (`A-Z2-7`), e.g. `FBX2Q7...`
- Secret: 40 characters `A-Za-z0-9`
- Generated on first boot from the hardware RNG (after WiFi starts, so the
  RF entropy source is on), stored in NVS namespace `fbauth`, keys `akid`
  and `secret`. They survive firmware and filesystem updates; an NVS erase
  (full flash erase) creates new ones.
- Scope for **all** signed requests (S3 and non-S3 endpoints): region
  `us-east-1`, service `s3`, terminator `aws4_request`.

### Reading them from the device (factory button)

The credentials are only shown on the device's main display:

| Gesture | Result |
| --- | --- |
| Hold 2 s | Device info (as before, while held) |
| Keep holding until 6 s | The credentials screen: key id, secret (in groups of 10) and a QR code `FBCRED:<akid>:<secret>`. It stays after you release. |
| Short press on the credentials screen | Close (it also closes by itself after 2 minutes) |
| New hold >= 10 s on the credentials screen | **Rotate**: new credentials are generated, stored and shown; the old ones stop working at once. Released earlier: nothing changes (a countdown is shown). |

The rotation hold has to start on the credentials screen, so a single long
hold from idle can never rotate. The QR code (~2.5 KB of LVGL memory) exists
only while the screen is open.

## Signing requests

Header form (the IDE, the S3 SDK, aws-cli, boto3, rclone):

```
Authorization: AWS4-HMAC-SHA256 Credential=<akid>/<yyyymmdd>/us-east-1/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=<hex>
x-amz-date: <yyyymmddThhmmssZ>
x-amz-content-sha256: UNSIGNED-PAYLOAD | <hex sha256 of the body> | STREAMING-AWS4-HMAC-SHA256-PAYLOAD
```

- `host` must be signed. `x-amz-date` is required (the `Date` header is not
  used for signing).
- Without `x-amz-content-sha256` a request must have no body (the empty-body
  hash is then used, as in the generic SigV4 suite).

Presigned query form (the REPL WebSocket, links):
`X-Amz-Algorithm=AWS4-HMAC-SHA256`, `X-Amz-Credential`, `X-Amz-Date`,
`X-Amz-Expires` (1..604800), `X-Amz-SignedHeaders` (must contain `host`),
`X-Amz-Signature`; the payload is always `UNSIGNED-PAYLOAD`. Using both an
`Authorization` header and `X-Amz-Algorithm` is refused.

Canonical request: S3 rules. The canonical URI is the path percent-encoded
once (RFC 3986 unreserved characters and `/` kept, no double encoding, no
`.`/`..` or `//` normalisation); the canonical query is every URL query
parameter (minus `X-Amz-Signature` when presigned), name and value
percent-encoded, sorted by encoded name, then value.

Limitations of the server's URL parsing (ESPAsyncWebServer decodes the URL
before the verifier sees it):

- A raw `+` in the path or query is decoded as a space. Send `+` as `%2B`
  (all SigV4 clients do); a raw `+` fails with `SignatureDoesNotMatch`.
- An encoded slash `%2F` inside a path segment arrives as `/` and is checked
  as `/`; object keys must not rely on it.

A Python signer (stdlib only) for tools and scripts is in
`tools/scripts/fb_sigv4.py`; the IDE has its own (web/).

## Endpoints

Signature required (anything else gets 403):

| Endpoint | Payload rule |
| --- | --- |
| `POST /api/reboot` | no body: `UNSIGNED-PAYLOAD` or the empty-body hash |
| `POST /upload` (firmware / filesystem image, multipart) | `UNSIGNED-PAYLOAD` |
| `POST /ota_update` | no body |
| `POST /updatefirmware` | no body |
| `POST /resetWifi` (now POST only) | no body |
| `POST /api/bytecode/upload` (multipart) | `UNSIGNED-PAYLOAD` |
| `GET /config` (contains secrets) | no body |
| `GET /ws/repl` (WebSocket upgrade) | presigned URL; an unsigned upgrade is refused with 403 |

A sub-path (`/config/x`) is protected like the path itself; every method but
`OPTIONS` is checked.

Why `UNSIGNED-PAYLOAD` for the multipart uploads: the web server hands the
handlers only the decoded file parts of a multipart body, never the raw bytes
a SHA-256 in `x-amz-content-sha256` would cover, so a body hash cannot be
checked there. A hex hash is refused on these endpoints (and on the bodyless
ones unless the body is empty and the hash is the empty-body hash) rather than
accepted unchecked. The signature still covers method, path, query and
headers, so an upload cannot be triggered without the secret; the image
itself is not integrity-protected by the signature (the firmware image has
its own checksum, a filesystem image its LittleFS magic).

Anonymous: `GET /api/version`, `GET /api/device`, `GET /api/bytecode/list`,
`GET /api/bytecode/next_address`, the `/upgrade` page (its own unsigned form
post to `/upload` now gets 403: use the IDE or `tools/scripts/ota_fs.py` with
signing), the `/bytecode` dev page (same: its upload is refused), static
files (`/www`), the captive portal (`/`, `/scan`, `/connect` in AP mode),
and CORS preflights (`OPTIONS`). The former `/ws-test` page was removed: it
could not sign the WebSocket upgrade.

The S3 API (`S3Server`) verifies with the same API (see below). Anonymous
there: reads of the buckets `web`, `examples`, `fonts` and ListBuckets;
everything else signed, including reads of bucket `app` (it holds
`config.json`); the rule is `s3::anonymousAllowed()` with the bucket table's
`publicRead` flag (S3Logic.h). PutObject verifies before it opens the temp
file, then checks the body against the signed hash (`XAmzContentSHA256Mismatch`,
400) or each aws-chunked chunk signature as it arrives; nothing is committed
unless every check passed. Refusals are S3 XML errors with the same codes and
the `x-freebutton-auth: required` header.

### Refusals

`403`, header `x-freebutton-auth: required`, body

```json
{"status":"error","code":"SignatureDoesNotMatch","message":"..."}
```

| code | meaning |
| --- | --- |
| `MissingAuthentication` | no `Authorization` header and no `X-Amz-Algorithm` |
| `AccessDenied` | malformed, unknown access key id, wrong region/service/date in the scope, `host` not signed, presigned URL expired, unsupported payload mode |
| `SignatureDoesNotMatch` | well-formed but the signature (or the empty-body hash) is wrong |
| `RequestTimeTooSkewed` | correctly signed, but `x-amz-date` is more than 15 minutes off the device clock (presigned: not valid yet) |

When the device clock is valid, every refusal also carries `Date` (RFC 1123)
and `x-amz-date` with the device time, so a client can correct its clock
offset and retry once. The signature is checked before the time, so only a
correctly signed request learns that its clock is off.

### CORS (dev only)

Same-origin use (the IDE served by the device) needs no CORS. For a
cross-origin client (`Origin` != `http://<Host>`, e.g. Vite on
`localhost:5173`) a server middleware answers the preflight (methods
`GET, POST, PUT, HEAD, DELETE`, the requested headers allowed, e.g.
`authorization, x-amz-date, x-amz-content-sha256`) and adds
`Access-Control-Allow-Origin: *` plus
`Access-Control-Expose-Headers: x-freebutton-auth, Date, x-amz-date, ETag, ...`
to responses that do not set their own.

## Firmware API (for handlers such as the S3 server)

`src/core/Auth.h`, pure logic in `src/core/AuthLogic.h` (tested in
`test/test_auth`):

```cpp
fbauth::Result auth;                       // ~90 B, on the stack or per request
if (!fbauth::verifyRequest(request, auth)) {
    // auth.status: MissingAuthentication / AccessDenied /
    //              SignatureDoesNotMatch / RequestTimeTooSkewed
    // non-S3: fbauth::sendDenied(request, auth);
    // S3:     XML error, Code = fbauth::statusCode(auth.status),
    //         Message = auth.message, status 403, plus
    //         fbauth::addDeniedHeaders(response);
}
```

- Call it as soon as the headers are complete (`canHandle()` or the first
  `handleBody()` call) and write nothing before it succeeded. Keep the
  verdict in the per-request struct; do not verify twice (the clock moves).
- `auth.payload`:
  - `Payload::Unsigned`: the body is not covered.
  - `Payload::Sha256`: hash the body while it streams with `fbauth::Sha256`
    (`init()`, `update()`, `finish()`; mbedTLS, hardware SHA, ~120 B) and
    compare with `auth.payloadSha256` (`fbauth::digestEqual`) before
    committing the object. S3 answers a mismatch with
    `XAmzContentSHA256Mismatch` (400).
  - `Payload::Streaming` (`STREAMING-AWS4-HMAC-SHA256-PAYLOAD`, aws-chunked):
    `fbauth::ChunkVerifier cv; cv.begin(auth);` then per chunk
    `cv.verify(sha256_of_chunk_data, chunk_signature_hex)`, including the
    final zero-length chunk (hash of ""). The string to sign is
    `AWS4-HMAC-SHA256-PAYLOAD\n<date>\n<scope>\n<previous signature>\n<sha256("")>\n<sha256(chunk)>`,
    seeded with the request signature (`auth.signature`). A failed chunk
    breaks the chain for good.
  - `STREAMING-UNSIGNED-PAYLOAD-TRAILER` (`Payload::UnsignedStreaming`):
    aws-chunked, body not signed, checksum trailer. aws-cli's CRT transfer
    client (`aws s3 cp`) sends it even over plain HTTP, so S3 PutObject
    accepts it like `UNSIGNED-PAYLOAD` (the headers, path and key are still
    signed; the body is not). The other endpoints refuse it.
    `STREAMING-AWS4-HMAC-SHA256-PAYLOAD-TRAILER` is refused (AccessDenied).
- Pure helpers for tests or other servers: `fbauth::verify<Sha>(req, cfg, keyFn, result)`,
  `writeCanonicalRequest`, `chunkSignature`, `deriveSigningKey`,
  `generateCredentials`, `formatQrPayload` (AuthLogic.h).

Costs: no heap per request (the canonical request is streamed into SHA-256,
the query is sorted without an index array); ~1 KB stack in the verifier;
the signing key (4 HMACs) is cached for one date (32 B); credentials and
cache ~110 B of RAM.
