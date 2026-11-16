# Downloads: how the device sends files

Every GET with a body, the web IDE's own files and the objects of the S3 API,
is sent by `src/core/StreamResponse.cpp`. The rules are in
`src/core/StreamLogic.h` (tested natively: `test/test_stream_logic`).

## Why

Loading the IDE from a device took about 32 KB of its heap at the peak.
btn_a00001 has about 38 KB free when idle and was seen with 1 to 4 KB left
(web/TODO.md, B38). What was alive at the low point, measured on the bench
device with the heap probe (below), for one download of a 320 KB file:

| Held by | Bytes | What |
| --- | --- | --- |
| stdio | 4,096 | read buffer of the Arduino `File` the server library opens |
| server library | 2,880 | its send buffer, filled from the file, then copied into the TCP stack |
| TCP stack | 4,596 | its copy of the unacknowledged bytes (3 segments' blocks) |
| Wi-Fi driver | 6,850 | its copy of each frame on its way out |
| LittleFS, request | 1,700 | open file, request and response headers |

A browser opens six connections, and each cost that again. Six parallel
downloads of a large file also showed a fault in the library's send logic:
five of the six stalled until the client gave up after 60 s.

## How

- **No buffers of ours.** A LittleFS file is read through its file
  descriptor (no stdio) one segment at a time into a block that lives for the
  call only, and handed to the TCP stack at once. A media file (fonts,
  images) is mapped flash: the TCP stack copies it straight from the mapping.
- **Few senders.** One download sends at a time; a second one only when the
  free heap is 40 KB or more. The others wait, with their file not yet open
  and their request headers freed (under 1 KB each). They start in the order
  they arrived, the moment a slot is free.
- **A silent client cannot block the queue.** A sender without progress for
  20 s loses its slot.
- **Static files** (`src/core/StaticFiles.cpp`) replace the library's
  `serveStatic()`: same URLs, `.gz` files, ETag/304 and cache rules.

## Not zero copy, and why

The TCP stack can take data by reference (`tcp_write` without
`TCP_WRITE_FLAG_COPY`), which would suit mapped flash. It was built and
measured, and is not used. The stack then builds a two-part segment (header
plus reference); the ESP network interface copies every such segment into a
fresh block before the Wi-Fi driver copies it once more
(`esp_netif/lwip/netif/wlanif.c`, `low_level_output`). Result on the bench
device: the same lowest free heap for a 1.3 MB font (46 KB either way), and
less after six interrupted downloads (38 KB by reference, 43 KB copied). By
reference the mapping would also have to outlive the connection, for
retransmissions. The copy the Wi-Fi driver makes of every frame cannot be
avoided from here.

## Measurements

Bench device btn_a00002 (about 58 KB free when idle), lowest free heap:

| Case | Before | Now |
| --- | --- | --- |
| One 320 KB download | 40.7 KB | 46.8 KB |
| Six parallel 320 KB downloads | 5 of 6 stalled | all done in 4.1 s |
| Cold load of the IDE | 24 to 26 KB | 38.7 KB |

What is left at the low point is the TCP stack's copy (3 KB per sender), the
Wi-Fi driver's frames (up to 7 KB) and the connections themselves.

## The heap probe

`src/debug/heap_probe.cpp` (diagnostic builds only; the file's header says
how) records which allocations are alive when the free heap reaches a new
low. `tools/scripts/heap_probe_decode.py <serial log>` prints them by call
site.

## Measuring on a device

`GET /api/device` reports, beside `heapMinFree` (lowest free heap since
boot):

- `heapLow`: the lowest free heap since the last `POST /api/heap/mark`
  (signed), so a measurement needs no reboot;
- `dlSending`, `dlWaiting`: downloads that send or wait now;
- `dlWaitingMax`, `dlServed`, `dlStalled`: since boot, the most that waited
  at once, downloads completed, and senders that lost their slot for silence.
