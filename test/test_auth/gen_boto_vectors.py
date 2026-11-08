import datetime
import unittest.mock as um
from urllib.parse import urlsplit

import botocore.auth as ba
from botocore.auth import S3SigV4Auth, S3SigV4QueryAuth
from botocore.awsrequest import AWSRequest
from botocore.credentials import Credentials

AK = "FBABCDEFGHIJKLMNOP23"
SK = "abcdefghijABCDEFGHIJ0123456789klmnopqrst"
creds = Credentials(AK, SK)
T = datetime.datetime(2026, 11, 8, 12, 0, 0)

out = []
with um.patch.object(ba, "get_current_datetime", return_value=T):
    cases = [
        ("reboot", "POST", "http://192.168.1.50/api/reboot", {}, b"", False),
        ("config", "GET", "http://192.168.1.50/config", {}, b"", False),
        ("s3-put", "PUT", "http://192.168.1.50/fs/scripts/main%20file%24.js", {"Content-Type": "application/javascript"}, b"print('hi')", False),
        ("s3-list", "GET", "http://192.168.1.50/fs?list-type=2&prefix=scripts%2F&delimiter=%2F", {}, b"", False),
        ("ws-presign", "GET", "http://192.168.1.50/ws/repl", {}, b"", True),
        ("presign-query", "GET", "http://192.168.1.50:8080/fs/a.txt?versionId=x%2Fy", {}, b"", True),
    ]
    for name, method, url, headers, body, presign in cases:
        r = AWSRequest(method=method, url=url, headers=dict(headers), data=body)
        if presign:
            S3SigV4QueryAuth(creds, "s3", "us-east-1", expires=300).add_auth(r)
        else:
            S3SigV4Auth(creds, "s3", "us-east-1").add_auth(r)
        p = r.prepare()
        u = urlsplit(p.url)
        target = u.path + ("?" + u.query if u.query else "")
        hdrs = [("Host", u.netloc)] + [(k, v) for k, v in p.headers.items() if k.lower() != "host"]
        out.append((name, method, target, hdrs, body))

def c(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

print("// Generated with botocore %s (S3SigV4Auth / S3SigV4QueryAuth), time 2026-11-08T12:00:00Z," % __import__("botocore").__version__)
print("// akid %s secret %s. Do not edit." % (AK, SK))
print("#pragma once")
print("struct BotoVector { const char* name; const char* method; const char* target; const char* const* headers; const char* body; };")
for name, method, target, hdrs, body in out:
    ident = name.replace("-", "_")
    items = ", ".join("%s, %s" % (c(k), c(v)) for k, v in hdrs)
    print("static const char* const b_%s[] = {%s, nullptr};" % (ident, items))
print("static const BotoVector kBoto[] = {")
for name, method, target, hdrs, body in out:
    print("    {%s, %s, %s, b_%s, %s}," % (c(name), c(method), c(target), name.replace("-", "_"), c(body.decode())))
print("};")
