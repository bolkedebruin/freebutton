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
