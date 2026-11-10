# Media store

Fonts (TrueType) and, later, rendered images live in the raw `media`
partition, where the UI reads them in place through the flash cache
(memory-mapped): no LittleFS reads, no heap copy. The user manages the fonts
through the S3 bucket `fonts`, which is backed by this store (not by LittleFS).

Image *sources* (`.svg`, `.png`, `.jpg`) are not stored here: they are read
once, so they stay on LittleFS. Only what LVGL draws directly is in `media`:

| Content | Where | Owner | Evictable |
|---|---|---|---|
| Fonts (`.ttf`) | `media`, bucket `fonts` | user (S3 PUT/DELETE) | no |
| Ready-made LVGL bitmaps (later) | `media`, bucket `images` | user | no |
| Rendered SVG / decoded JPEG, PNG (later) | `media`, internal bucket `render` | device | yes |
| Image sources | LittleFS | user | no |

Measured on the device (ESP32-S3, 2026-11-10): TrueType from mapped flash
renders a new glyph in 0.55-1.2 ms, the old `.bin` fonts streamed from
LittleFS in 1.6-5.3 ms, TrueType read through LittleFS in 8-14 ms. The cost
was file I/O, not rasterizing.

## Partition layout (`tools/partition_tables/button_v2.csv`)

```
# Name,    Type, SubType,  Offset,    Size
nvs,       data, nvs,      0x9000,    0x5000,     # 20 KB
otadata,   data, ota,      0xE000,    0x2000,
app0,      app,  ota_0,    0x10000,   0x300000,   # 3 MB
app1,      app,  ota_1,    0x310000,  0x300000,   # 3 MB
coredump,  data, coredump, 0x610000,  0x10000,    # 64 KB
js_user,   data, 0x41,     0x620000,  0x40000,    # 256 KB, compiled scripts
media,     data, 0x42,     0x660000,  0x500000,   # 5 MB, this store
data,      data, spiffs,   0xB60000,  0x4A0000,   # 4.6 MB LittleFS
```

Moving to this layout erases the device once (serial flash with erase):
WiFi/credentials (NVS), scripts and LittleFS files have to be restored.

## Allocation: 64 KB pages, mapped scattered

`media` is cut into 64 KB pages (the ESP32-S3 MMU page size; the partition
starts on a 64 KB boundary). `spi_flash_mmap_pages()` maps any list of
physical pages to one contiguous virtual range, so a file does not need
contiguous flash and **large files never fragment and never move**.

- Page 0 and 1: the directory (two halves, see below). The rest: data.
- Large file (> 32 KB): whole pages, wherever free. Waste: the tail of the
  last page.
- Small file (<= 32 KB): packed in shared pages in 4 KB sectors (the erase
  unit), contiguous inside one page. Only shared pages can fragment; a nearly
  empty shared page is emptied by copying its few files elsewhere (not while
  they are open), then freed.

RAM: a free-page bitmap (bits) and a small table of open files. Lookups scan
the directory in mapped flash; no RAM index.

## Directory: append-only log, two halves

Every put/delete appends one record (bucket, key, size, mtime, MD5, page
list / sector range, sequence number, CRC). A record counts only when it is
complete and its CRC matches, so a power cut during an upload leaves the old
version valid. When the active half is full, the live records are written as
a compact snapshot into the other half (higher generation), then the old half
is erased.

At boot the free map is rebuilt from the directory: every page or sector that
no live record references is free. This also reclaims uploads cut short by a
power loss.

## Garbage collection: reference counts

A mapped file has a use count (font slot loaded, image on screen). Delete or
replace only appends a record; the old pages are freed when the count drops
to 0 (the UI rebuild after a config change releases them). There is no
tracing GC.

## When the store is full

An upload states its size first (S3 PUT: `Content-Length`, or
`x-amz-decoded-content-length` for aws-chunked bodies), so space is reserved
before any data is written, under a lock (parallel uploads cannot overbook):

1. use free pages;
2. count pages that become free (deleted/replaced, use count 0);
3. evict derived entries (`render`) that are not open, oldest first;
4. otherwise refuse with `507 Insufficient Storage` and write nothing. User
   files are never evicted.

Replacing a file needs room for both versions until the new one is committed
(atomic PUT). Listings report used / free / reclaimable space so a client can
check before a batch upload.

Flash must be erased before it is written: free pages are erased in the
background when idle (`media_maintain()`), so uploads do not wait for it.
Each erase briefly stalls flash reads (the screen can stutter).

## On-flash format

Version 1. All numbers are little-endian. Offsets are relative to the start
of the partition; a "page" is 64 KB, a "sector" 4 KB. Page numbers are one
byte (a partition has at most 256 pages = 16 MB). The partition size is read
from the partition table, never assumed: pages `0` and `1` are the directory
halves, pages `2 .. n-1` hold data (78 pages = 76 data pages in 5 MB).

### Directory half (page 0 or 1)

Header, 32 bytes at the start of the half:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `"MDIR"` (`0x5249444D`) |
| 4 | 2 | version `1` |
| 6 | 2 | header length `32` |
| 8 | 4 | generation (the half with the higher one is active; compared as `(int32_t)(a - b)`) |
| 12 | 4 | next sequence number (every seq used before is lower) |
| 16 | 2 | partition pages at format time (informational) |
| 18 | 2 | directory pages (`2`) |
| 20 | 8 | zero |
| 28 | 4 | CRC-32 of bytes 0..27 |

Records follow from offset 32, back to back, each a multiple of 4 bytes
(4-byte aligned). The log ends at the first word that reads `0xFFFFFFFF`.
Every record ends with a CRC-32 (IEEE 802.3, as zlib: reflected, init and
final XOR `0xFFFFFFFF`) of all its bytes before it. Common start:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | type: `1` PUT, `2` DEL |
| 1 | 1 | bucket (`0` fonts, `1` images, `2` render) |
| 2 | 2 | record length in bytes (16..256, multiple of 4) |
| 4 | 4 | seq: sequence number of the operation |
| 8 | 2 | kill: ordinal (0-based record index in this half) of the PUT this record ends, `0xFFFF` = none |

DEL (16 bytes): bytes 10-11 zero, CRC at 12. Its kill is the deleted entry.

PUT:

| Offset | Size | Field |
|---|---|---|
| 10 | 1 | key length (1..96) |
| 11 | 1 | location kind: `0` empty (size 0), `1` sectors (size <= 32 KB), `2` pages (larger); must match the size |
| 12 | 4 | size in bytes (at most 4 MB) |
| 16 | 4 | mtime |
| 20 | 16 | MD5 (S3 ETag) |
| 36 | ... | location: kind 1 = page, first sector, sector count, `0` (4 bytes); kind 2 = one page number per 64 KB of the file, in file order; kind 0 = nothing |
| ... | ... | key bytes (no NUL), then zero padding to a multiple of 4 |
| len-4 | 4 | CRC-32 |

A replace is a PUT whose kill is the previous version (the new version and
the end of the old one commit in one record). A compaction move is a PUT
with the same seq, metadata and key as the record it kills, at a new
location. An entry is live when it is a PUT that no later record kills. A
kill always points backwards inside the same half (a snapshot drops dead
records, so nothing needs to point across halves).

Keys: 1..96 bytes, any byte except NUL, control characters (< 0x20) and
DEL (0x7F); `/` and UTF-8 are allowed. Per bucket.

### Mount

1. Read both headers. A header counts when magic, version, length and CRC
   match. Neither counts: the store is blank or unreadable and is formatted
   (erase page 0, write a header with generation 1). Otherwise the valid
   half with the higher generation is active; the other one is stale and
   is verified blank or erased before it is reused (`media_maintain`).
2. Scan the active half once, keeping one RAM bit per record (dead or not;
   at most 1024 records per half = 128 bytes): every kill marks its target
   dead, every DEL marks itself dead. A record must have a sane length and
   type, a matching CRC, valid fields (page numbers inside the data area,
   location matching the size, key length) and a kill that points to an
   earlier live PUT.
3. Second pass: every live PUT marks its sectors in use in the RAM map
   (one uint16 per page: sectors in use; another: sectors known erased,
   unknown = 0 after boot). Everything else is free: uploads cut short,
   files replaced or deleted while they were open before the reboot.

**Torn record.** A write cut by a power loss leaves a record whose CRC (or
length, or type) does not match. The scan stops there: that record and
anything after it do not count, so the operation it would have committed did
not happen. Because the bytes after a torn record are not known to be
erased, the half is marked dirty and the next append first writes a
snapshot into the other half. In addition, every append reads its target
bytes back and requires `0xFF` before programming; if not, it snapshots too.
Flash is therefore never programmed over non-erased bytes.

### Commit order of the operations

| Operation | Writes, in order | Commit point |
|---|---|---|
| put | reserve (RAM only, may append DELs for evicted RENDER entries), erase sectors/pages not known erased, data, PUT record | the PUT record |
| replace | as put, the PUT kills the old version | the PUT record; the old space is freed in RAM after it (on the last close if open) |
| delete | DEL record | the DEL record |
| compaction | erase the target sectors (one per `media_maintain` call), copy the file, PUT (same seq) killing the old location | the PUT record |
| snapshot | erase the other half (unless known blank), copy the live PUT records (kill = none) from offset 32, then the header with generation + 1 | the header |

Space becomes reusable only after the record that ends its owner is on
flash, so after any power cut the rebuilt map frees exactly what no live
record references: the state is the old one or the new one and nothing
leaks. An eviction is a separate DEL per entry before the upload starts:
a power cut during an upload that needed evictions can leave RENDER
entries evicted (they are derived and are regenerated).

### Snapshot (rollover)

When the next record does not fit in the active half (64 KB, or 1024
records), or the half is dirty: erase the other half if it is not known
blank, write the live records into it, then its header (generation + 1,
the current next seq). The header is the commit point: a cut before or
during it leaves the new half without a valid header, so the old half
(still intact, never touched by the snapshot) stays active, and the
partially written half is erased before the next attempt. After the
header, the new half wins by generation even though the old one is still
valid; it is erased later by `media_maintain`. The record that caused the
rollover is appended to the new half afterwards (its kill translated to
the new ordinal).

Limits keep snapshots rare and always possible: the live records may use
at most 48 KB (so at least 16 KB of log follow each snapshot) and there
are at most 1000 live entries; `media_put_begin` returns
`MEDIA_ERR_NO_SPACE` beyond that. With the longest keys (96 bytes) a
record is at most 136 bytes plus its page list, and all page lists
together are at most 76 bytes (a page belongs to one live file), so at
least 350 entries always fit; with short keys about 1000.

### Allocation details

- Small file (1..32 KB): `ceil(size / 4 KB)` contiguous sectors inside one
  page. Preference: a shared page already in use (packing), then a run that
  is already erased, then the fullest page.
- Large file (> 32 KB): `ceil(size / 64 KB)` whole pages. Preference: the
  shortest contiguous run of free pages that fits (erased first), so the
  mapping is one block; otherwise any free pages, erased first, in
  ascending order. At most 64 pages (4 MB).
- Size 0: no space, location kind 0; `media_open` returns `data == NULL`.
- Erase: `media_put_write` erases a sector (small) or the page (large, at
  its first byte) just before writing into it, only when RAM does not know
  it erased. `media_maintain` does one step per call: erase the stale
  directory half, or one compaction step, or verify (read) a free page /
  free sector of a shared page and erase it if it is not blank.
- Compaction: the shared page with the fewest used sectors (at most 8, not
  mapped) gives one file per call to a fuller shared page (never the other
  way round, so it converges). Open files and pages with an open file are
  never touched.
- Mapping: one mapping per open large file; small files in the same shared
  page share one page mapping (IDF refuses to map a page twice). The ESP
  backend checks with `spi_flash_cache2phys` that a multi-block mapping
  came out contiguous (`spi_flash_mmap_pages` places each block first-fit
  and does not check) and fails the open otherwise (`MEDIA_ERR_IO`).

## Implementation and tools

| File | What |
|---|---|
| `src/media/media_store.h` | the API |
| `src/media/media_core.{h,c}` | platform-independent store over `media_flash_t` (read, write, erase, map pages, unmap, lock) |
| `src/media/media_esp.c` | ESP backend: partition `media` (data, subtype `0x42`), `esp_partition_*`, `spi_flash_mmap_pages` with absolute page numbers, one FreeRTOS mutex, `media_init()` (maps nothing) |
| `test/test_media_store/` | native tests on a NOR RAM flash with power-cut injection: `pio test -e native -f test_media_store` |
| `tools/media/mkmedia.cpp`, `build.sh` | host tool on the same core: `mkmedia out.bin --size 0x500000 fonts/medium.ttf=path ...`, `mkmedia --list out.bin` (lists and checks every MD5) |
| `tools/scripts/flash_media.py` | builds mkmedia, packs `media/<bucket>/<key>`, verifies, flashes the image at the `media` offset from the partition CSV (`--no-flash`, `--dry-run`, `--port`) |

Cost on the device (measured on the objects, 2026-11-10): about 13.4 KB of
flash, 1.8 KB static RAM (tables, mutex) and 4 bytes of heap per page at
mount (312 bytes for 78 pages); a directory scan uses about 1 KB of the
caller's stack, a commit that rolls the directory over about 2 KB. Nothing
is linked until something calls `media_init()`.

## Fonts

LVGL `tiny_ttf` (stb_truetype) renders from the mapped TTF
(`lv_tiny_ttf_create_data_ex`). Default fonts are committed in
`media/fonts/` and uploaded to the bucket `fonts` (keys below). A missing or
invalid font falls back to LVGL's built-in Montserrat 14, the only compiled
font.

| Key | Use |
|---|---|
| `medium.ttf` | text, medium weight (all `fnt_medium_*` sizes) |
| `semibold.ttf` | text, semibold (`fnt_semibold_*`) |
| `icons.ttf` | Material Design Icons; fallback of both text fonts |
