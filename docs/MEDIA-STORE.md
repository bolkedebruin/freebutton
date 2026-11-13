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
# FreeButton partition table v2 (16 MB flash). Layout and rationale:
# docs/MEDIA-STORE.md; moving a device from v1: docs/MIGRATION-PARTITIONS-V2.md.
# gen_esp32part.py rejects comments after a row (read as flags): keep them on
# their own lines.
# No phy_init: CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION is off (PHY data is in the app).
#   nvs 20 KB | app0/app1 3 MB each | coredump 64 KB | js_user 256 KB compiled
#   scripts | media 5 MB media store (fonts, bucket "fonts") | data 4.6 MB LittleFS
# Name,    Type, SubType,  Offset,    Size,      Flags
nvs,       data, nvs,      0x9000,    0x5000,
otadata,   data, ota,      0xE000,    0x2000,
app0,      app,  ota_0,    0x10000,   0x300000,
app1,      app,  ota_1,    0x310000,  0x300000,
coredump,  data, coredump, 0x610000,  0x10000,
js_user,   data, 0x41,     0x620000,  0x40000,
media,     data, 0x42,     0x660000,  0x500000,
data,      data, spiffs,   0xB60000,  0x4A0000,
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
  unit), contiguous inside one page. Only shared pages can fragment; when
  whole pages run low, a nearly empty shared page is emptied by copying its
  few files elsewhere (not while they are open), then freed.

**Wear leveling** (as littlefs does it): allocation is next-fit. A cursor
sweeps the data pages and wraps, so a file that is replaced over and over
moves through all free pages instead of wearing out the first free one.
The cursor lives in RAM and starts at a pseudo-random page on every mount.
Data at rest never moves (no static leveling): the fonts' pages do not
wear, the rest of the store shares the erases. Measured on a 5 MB store
with 1.5 MB of fonts (`test_wear_*`): the busiest sector gets 1.06-1.08
times the even share; before next-fit it was 3.7-30 times.

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

The allocation policy is RAM state only (cursor, fill page); the on-flash
format does not record it, so changing it needs no migration.

- Large file (> 32 KB): `ceil(size / 64 KB)` whole pages: the next free
  pages from the cursor on, in sweep order (scattered and across the wrap
  are fine: a mapping takes any list of pages). The cursor then moves past
  the last one. At most 64 pages (4 MB). The cursor starts at a
  pseudo-random page on mount, from `next_seq` and the directory generation.
- Small file (1..32 KB): `ceil(size / 4 KB)` contiguous sectors inside one
  page, appended to the fill page; when it has no room, the next fresh page
  from the cursor becomes the fill page. Sectors freed in older shared pages
  are not reused one by one: once every file of a page is gone, the page is
  free and the cursor reuses it in turn. Only while fewer than 4 whole
  pages are free are holes in shared pages filled first.
- Size 0: no space, location kind 0; `media_open` returns `data == NULL`.
- Erase: `media_put_write` erases a sector (small) or the page (large, at
  its first byte, only when no sector of it is known erased: a sector is
  never erased twice for nothing) just before writing into it, only when
  RAM does not know it erased. `media_maintain` does one step per call:
  erase the stale directory half, or one compaction step, or verify (read)
  and if needed erase the next free page from the cursor on (sector by
  sector if part of it is already erased), or a free sector of the fill
  page ahead of its next file. Holes in other shared pages are erased when
  they are used.
- Compaction: runs only while fewer than 4 whole pages are free, or after a
  large upload failed for lack of whole pages (it costs writes and erases,
  and next-fit leaves sparse pages behind on purpose). The shared page with
  the fewest used sectors (at most 8, not mapped, not the fill page) gives
  one file per call to a fuller shared page (never the other way round, so
  it converges). Open files and pages with an open file are never touched.
- Wear counters (RAM, since mount): sectors erased in total and on the
  busiest page, and the directory generation, in `media_usage()` and on the
  S3 HeadBucket (`x-fb-media-erase-sectors`, `x-fb-media-erase-page-max`,
  `x-fb-media-dir-generation`). The directory pages are not leveled: a
  64 KB half takes about 500 updates (worst case, 48 KB of live records,
  about 125) before it is erased, so even an update every 5 minutes erases
  each half about once a day.
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
`media/fonts/` (the upstream files, unmodified) and uploaded to the bucket
`fonts` (keys below). config.json `"style"."fonts"` can add fonts from other
keys in the bucket ([STYLE.md](STYLE.md)). A missing or invalid font falls
back to LVGL's built-in Montserrat 12, the only compiled font.

| Key | Use |
|---|---|
| `medium.ttf` | system font `montserrat`: UI text, medium weight (Montserrat Medium, 198 KB). Protected |
| `semibold.ttf` | system font `montserrat_semibold`: titles, semibold (Montserrat SemiBold, 198 KB). Protected |
| `icons.ttf` | an ordinary font file: Material Design Icons. The factory config names it `mdi`, the fallback of the system fonts |

- The two system fonts exist without a config entry, and the firmware
  screens (modals, progress bar, LVGL theme) use them directly. S3 DELETE of
  `medium.ttf` or `semibold.ttf` answers 403; PUT is allowed (checked by
  `ttf_check`), so they can be updated. A `"style"."fonts"` entry with a
  system name may set `id`, `fallback` and `lineheight`, not `src`.
- A font has a fallback only when config.json gives it one; no font name is
  special in the firmware.

- `icons.ttf` is the full Material Design Icons 7.4.47 webfont (7,447 icons,
  1.31 MB), addressed by MDI's own codepoints (U+F0001-F1D17, cmap (3,10)
  format 12).
- tiny_ttf glyph and kerning caches are off (`lv_conf.h`): LVGL draws in
  bands and fetches each glyph again per band, so a cache smaller than the
  page's working set only misses, and a large one does not fit the LVGL pool.
  An uncached glyph costs 0.6-1 ms.
- The font `montserrat` keeps the old 1.03 em line box at 48 px and up (page
  layouts unchanged); a descender or an accent on a capital can be clipped
  by up to ~0.1 em.
- `ttf_check.c` validates a font's tables before stb_truetype reads them (it
  trusts every offset); a damaged upload falls back to the built-in font.
- Fonts reload about 1.5 s after the last PUT or DELETE in bucket `fonts`
  (no reboot): every font instance (a font at one size) is a stable
  wrapper; each instance of a changed file gets a new tiny_ttf font, one at
  a time, every display is restyled, and the old file is closed once no
  instance reads it (the store frees a replaced file's pages then). An
  instance waits while the LVGL pool would keep less than 4 KB free.
