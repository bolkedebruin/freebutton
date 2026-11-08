#!/usr/bin/env python3
"""AWS SigV4 signing for the FreeButton device API (stdlib only).

The device authenticates its HTTP API with AWS Signature Version 4
(docs/AUTH.md): region "us-east-1", service "s3" for every endpoint. The
credentials are shown on the device (factory button: hold 6 s); the QR code
holds "FBCRED:<akid>:<secret>".

Credentials come from FB_CREDENTIALS ("FBCRED:<akid>:<secret>" or
"<akid>:<secret>") or FB_ACCESS_KEY_ID + FB_SECRET_ACCESS_KEY.

Library use:

    from fb_sigv4 import credentials, sign_headers, presign_url
    akid, secret = credentials()
    headers = sign_headers("POST", "http://10.0.0.5/api/reboot", {}, b"", akid, secret)
    url = presign_url("http://10.0.0.5/ws/repl", akid, secret, expires=300)

CLI (prints the headers / URL):

    fb_sigv4.py POST http://10.0.0.5/api/reboot
    fb_sigv4.py --presign GET http://10.0.0.5/ws/repl

The path and query must be percent-encoded as they go on the wire (a '+'
must be sent as %2B: the device decodes a raw '+' as a space).
"""
import datetime
import hashlib
import hmac
import os
import sys
import urllib.parse

REGION = "us-east-1"
SERVICE = "s3"
UNSIGNED = "UNSIGNED-PAYLOAD"
_UNRESERVED = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~")


def credentials():
    spec = os.environ.get("FB_CREDENTIALS", "")
    if spec:
        parts = spec.split(":")
        if parts[0] == "FBCRED":
            parts = parts[1:]
        if len(parts) == 2:
            return parts[0], parts[1]
        raise SystemExit("FB_CREDENTIALS must be FBCRED:<akid>:<secret> or <akid>:<secret>")
    akid, secret = os.environ.get("FB_ACCESS_KEY_ID"), os.environ.get("FB_SECRET_ACCESS_KEY")
    if not akid or not secret:
        raise SystemExit("set FB_CREDENTIALS or FB_ACCESS_KEY_ID / FB_SECRET_ACCESS_KEY (shown on the device)")
    return akid, secret


def _enc(raw: bytes, keep_slash: bool) -> str:
    return "".join(chr(b) if b in _UNRESERVED or (keep_slash and b == 0x2F) else "%%%02X" % b for b in raw)


def _canonical_query(query: str, drop_signature: bool) -> str:
    pairs = []
    for item in query.split("&") if query else []:
        name, _, value = item.partition("=")
        name = urllib.parse.unquote_to_bytes(name)
        value = urllib.parse.unquote_to_bytes(value)
        if not name or (drop_signature and name == b"X-Amz-Signature"):
            continue
        pairs.append((_enc(name, False), _enc(value, False)))
    return "&".join("%s=%s" % p for p in sorted(pairs))


def _key(secret: str, date8: str) -> bytes:
    k = ("AWS4" + secret).encode()
    for msg in (date8, REGION, SERVICE, "aws4_request"):
        k = hmac.new(k, msg.encode(), hashlib.sha256).digest()
    return k


def _signature(method, url, headers, signed, payload, amz_date, secret, presigned):
    u = urllib.parse.urlsplit(url)
    path = _enc(urllib.parse.unquote_to_bytes(u.path or "/"), True)
    lower = {k.lower(): " ".join(str(v).split()) for k, v in headers.items()}
    canonical = "\n".join([
        method.upper(), path, _canonical_query(u.query, presigned),
        "".join("%s:%s\n" % (h, lower[h]) for h in signed.split(";")),
        signed, payload])
    scope = "%s/%s/%s/aws4_request" % (amz_date[:8], REGION, SERVICE)
    sts = "\n".join(["AWS4-HMAC-SHA256", amz_date, scope, hashlib.sha256(canonical.encode()).hexdigest()])
    return hmac.new(_key(secret, amz_date[:8]), sts.encode(), hashlib.sha256).hexdigest(), scope


def _now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def sign_headers(method, url, headers, body, akid, secret, unsigned_payload=False, amz_date=None):
    """Returns the headers to send (Host, x-amz-date, x-amz-content-sha256,
    Authorization) merged with `headers`. Multipart uploads (/upload,
    /api/bytecode/upload) must use unsigned_payload=True."""
    amz_date = amz_date or _now()
    out = dict(headers)
    out["Host"] = urllib.parse.urlsplit(url).netloc
    out["x-amz-date"] = amz_date
    payload = UNSIGNED if unsigned_payload else hashlib.sha256(body or b"").hexdigest()
    out["x-amz-content-sha256"] = payload
    signed = ";".join(sorted({"host", "x-amz-date", "x-amz-content-sha256"} |
                             {k.lower() for k in headers if k.lower() in ("content-type", "content-md5")}))
    sig, scope = _signature(method, url, out, signed, payload, amz_date, secret, False)
    out["Authorization"] = "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s" % (
        akid, scope, signed, sig)
    return out


def presign_url(url, akid, secret, expires=300, method="GET", amz_date=None):
    """Presigned URL (X-Amz-* query), e.g. for the /ws/repl WebSocket upgrade."""
    amz_date = amz_date or _now()
    u = urllib.parse.urlsplit(url)
    params = [
        ("X-Amz-Algorithm", "AWS4-HMAC-SHA256"),
        ("X-Amz-Credential", "%s/%s/%s/%s/aws4_request" % (akid, amz_date[:8], REGION, SERVICE)),
        ("X-Amz-Date", amz_date),
        ("X-Amz-Expires", str(expires)),
        ("X-Amz-SignedHeaders", "host"),
    ]
    query = (u.query + "&" if u.query else "") + "&".join("%s=%s" % (k, _enc(v.encode(), False)) for k, v in params)
    unsigned = urllib.parse.urlunsplit((u.scheme, u.netloc, u.path, query, ""))
    sig, _ = _signature(method, unsigned, {"host": u.netloc}, "host", UNSIGNED, amz_date, secret, True)
    return unsigned + "&X-Amz-Signature=" + sig


def main(argv):
    presign = "--presign" in argv
    args = [a for a in argv if a != "--presign"]
    if len(args) != 2:
        raise SystemExit(__doc__)
    method, url = args
    akid, secret = credentials()
    if presign:
        print(presign_url(url, akid, secret, method=method))
    else:
        for k, v in sign_headers(method, url, {}, b"", akid, secret).items():
            print("%s: %s" % (k, v))


if __name__ == "__main__":
    main(sys.argv[1:])
