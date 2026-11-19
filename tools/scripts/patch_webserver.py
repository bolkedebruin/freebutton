#!/usr/bin/env python3
"""
PlatformIO pre-script: bound what ESPAsyncWebServer keeps of a request.

The pinned library (3.12.1) stores every header and query parameter of a
request and grows its line buffer without limit, before any handler of ours
runs: one unauthenticated request could take the whole heap. This script
edits the installed copy (.pio/libdeps/<env>/ESPAsyncWebServer) in place:

  - a request or header line longer than 2 KB aborts the connection (the
    library's own path for a failed allocation);
  - at most 40 headers and 32 query parameters are kept, the rest dropped
    (a signed request that loses one fails its signature check).

It also makes AsyncWebServerRequest::abort() safe. The library calls it
from inside its data handler (line too long, NUL byte in a header, bad
multipart boundary, failed allocation, TLS hello), where it resets the
connection at once; that deletes the connection object while AsyncTCP is
still delivering data to it (AsyncClient::_recv goes on with the next queued
packet). With several packets queued the firmware crashed (lwIP assert in
tcp_update_rcv_ann_wnd). abort() now only marks the request as failed, and
the next poll of the connection (within about half a second) resets it.

It is idempotent (marker FB-LIMITS) and fails the build when the library
source no longer has the lines it changes: review the patch when the
library version in platformio.ini changes.
"""
import os
import sys

Import("env")  # noqa: F821  (PlatformIO)

MARK = "/* FB-LIMITS */"
EDITS = [
    (
        "        if (!_temp.reserve(_temp.length() + len)) {\n",
        "        if (_temp.length() + len > 2048 " + MARK + " || !_temp.reserve(_temp.length() + len)) {\n",
    ),
    (
        "    _headers.emplace_back(std::move(header));\n",
        "    if (_headers.size() < 40) " + MARK + " {\n      _headers.emplace_back(std::move(header));\n    }\n",
    ),
    (
        "    if (name.length()) {\n      _params.emplace_back(name, value);\n",
        "    if (name.length() && _params.size() < 32 " + MARK + ") {\n      _params.emplace_back(name, value);\n",
    ),
    (
        '    async_ws_log_v("Abort request: %s", _url.c_str());\n    _client->abort();\n',
        '    async_ws_log_v("Abort request: %s", _url.c_str());\n'
        "    _parseState = PARSE_REQ_FAIL; " + MARK + "  // the connection is reset from _onPoll\n"
        "    _temp = String();\n",
    ),
    (
        "void AsyncWebServerRequest::_onPoll() {\n"
        "  std::shared_ptr<AsyncWebServerRequest> self = _this;  // Ensure we stay in scope over the function\n",
        "void AsyncWebServerRequest::_onPoll() {\n"
        "  std::shared_ptr<AsyncWebServerRequest> self = _this;  // Ensure we stay in scope over the function\n"
        "  if (_parseState == PARSE_REQ_FAIL && _client) { " + MARK + "\n"
        "    _client->abort();  // a request that was aborted while its data was being handled\n"
        "    return;\n"
        "  }\n",
    ),
]


def patch():
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),  # noqa: F821
                        "ESPAsyncWebServer", "src", "WebRequest.cpp")
    if not os.path.isfile(path):
        return  # not installed yet: PlatformIO installs it and runs this script again
    with open(path, encoding="utf-8") as f:
        source = f.read()
    if source.count(MARK) == len(EDITS):
        return
    if MARK in source:
        sys.exit("patch_webserver.py: %s is partly patched; delete the library folder and build again" % path)
    for old, new in EDITS:
        if source.count(old) != 1:
            sys.exit("patch_webserver.py: ESPAsyncWebServer changed, cannot apply the request limits:\n  %r" % old)
        source = source.replace(old, new)
    with open(path, "w", encoding="utf-8") as f:
        f.write(source)
    print("patch_webserver.py: request limits applied to", path)


patch()
