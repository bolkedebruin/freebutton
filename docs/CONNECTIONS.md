# Connections and request limits

The web server answers every request on its own connection, and a connection
whose request is being received or answered costs 5 to 7 KB of heap for a
moment, whatever the size of the answer. A browser opens six at once. Two
things keep that within what the device has (`src/core/ConnectionGuard.cpp`,
rules in `GuardLogic.h`, tests in `test/test_guard_logic`).

## How many connections are in progress

The number allowed follows the free heap: what is free beyond a reserve of
12 KB, divided by 6 KB, at least 1 and at most 8. btn_a00001 (about 38 KB free
when idle) gets 4, btn_a00002 (58 KB) gets 7.

The limit is the listen backlog of the TCP stack. Every accepted connection
is counted against the listening socket (`tcp_backlog_delayed`); the stack
leaves the opening packet of a connection beyond the limit unanswered, and
the client sends it again after about a second. A connection that waits this
way costs nothing. The count ends when the connection closes, when the
server has closed it and the client has acknowledged everything
(`guard::tick`, twice a second from `loop()`), or when it is released:

- a download or listing, once it is handed to `StreamResponse` (its slot
  rule decides from there);
- the console WebSocket;
- a firmware or filesystem upload.

Measured on btn_a00002 with the limit forced to 4: six parallel conditional
requests, five rounds, dipped the free heap by 16 to 27 KB (28 to 34 KB
without a limit); twelve parallel by 30 KB (41 KB), at the price of 10 s
instead of 4 s. A full IDE load from btn_a00002 takes 11.7 to 12.5 s with the
guard and 9.3 to 10.1 s without: while the downloads run the free heap, and
with it the limit, is lower, and a connection that is put off comes back a
second later. Clients that open
connections and send nothing hold the limit for the 3 s the server waits for
a request; a request made meanwhile was answered after 5 s.

## What a request may bring

- A body is taken only by an object upload (PUT), `/upload`,
  `/api/bytecode/upload` and the WiFi setup form. Any other request with a
  body gets 413 before the body is read (the server library would collect a
  form body in memory first).
- `tools/scripts/patch_webserver.py` (a PlatformIO pre-script) edits the
  installed ESPAsyncWebServer 3.12.1: a request or header line over 2 KB
  aborts the connection; at most 40 headers and 32 query parameters are
  kept. Review it when the library version changes: it stops the build when
  the source no longer matches.
- The same script makes the library's `abort()` safe. It used to reset the
  connection from inside the data handler, which deletes the connection
  object while AsyncTCP still delivers queued packets to it; with several
  packets queued the firmware crashed (lwIP assert `tcp_update_rcv_ann_wnd`).
  The reset now happens at the connection's next poll.
