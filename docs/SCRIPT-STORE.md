# Script store

Compiled scripts (`main.jsc`, ...) live in the raw `js_user` partition (256 KB,
64 sectors of 4 KB), where the engine executes them in place through the
flash mapping. The partition describes itself: every bytecode image has a
header in front of it, and the index in RAM (`BytecodeRegistry`) is rebuilt
from those headers at boot. Nothing about scripts is stored in LittleFS.
Code: `src/scripting/BytecodeStore.h` (layout, scan, allocator: pure logic,
`test/test_bytecode_store`), `BytecodeRegistry` (index), `BytecodeWriter`.

## Entry layout

An entry starts on a sector boundary and takes whole sectors: an 80-byte
header, then the image, then erased bytes up to the next boundary.
Little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `FBJS` (0x534A4246) |
| 4 | 2 | header version (1) |
| 6 | 2 | header length (80): where the image starts |
| 8 | 4 | sequence number, 1..0xFFFFFFFE |
| 12 | 4 | image size in bytes (16..partition size − 80) |
| 16 | 4 | CRC-32 of the image (`esp_crc32_le`, start 0) |
| 20 | 4 | `app_id`: the firmware build the image was relocated for |
| 24 | 4 | `rom_base`: where that build's stdlib ROM table was |
| 28 | 4 | `stdlib_id`: which stdlib table that was |
| 32 | 32 | name, NUL-padded (at most 31 characters) |
| 64 | 12 | reserved, zero |
| 76 | 4 | CRC-32 of header bytes 0..75 |

The image is relocated for the address it lies at: mapping base + entry
offset + 80. 80 is a multiple of 16, so the image is aligned as it was when
it started on the sector boundary (the engine needs 4 bytes). The largest
image is 262 064 bytes.

Upload: `POST /api/bytecode/upload?size=<bytes>&atoms=<id>` (multipart, one
file part whose filename is the entry's name). `size` is the image's own
length: the region is taken for exactly that, and an upload that brings
another number of image bytes is not stored. A `size` that is not a plain
decimal number, below 16, above 262 064 or above the request's length is
refused. Without `size` (an older IDE bundle, curl) the region is taken for
the request's length, which includes about 200 bytes of multipart framing,
so the largest image is that much smaller. `atoms`: the compiler resolved
the built-in names for this build (`GET /api/bytecode/next_address`).

## Writing an entry: the commit point

Upload (`BytecodeWriter`), rebase and migration (`BytecodeRegistry::moveImage`)
all do the same:

1. Pick the lowest free region (`allocate`) and reserve it.
2. Erase it (`eraseRegion`), last sector first.
3. Write the image at +80, read it back and compare its CRC with the CRC of
   the bytes that were written.
4. Program the header (`BytecodeRegistry::commit`) and read it back.

Step 4 is the commit point. A region without a valid header is free space,
whatever it holds: a power cut in steps 1-3 leaves an unfinished image that
the scan does not see, and a header torn in step 4 fails its CRC. The entry
that had this name before is not written to at any step, so after any power
cut the partition holds the old script or the new one, complete. If the
header's read-back fails, its sector is erased again and the sequence number
is not reused.

## Replace and delete

A new entry gets the next sequence number. Of two entries with one name the
newer number is the script and the other is free space. The old entry is
left as it is: no "dead" mark is programmed into it. That needs no second
flash write (nothing to order against the commit), and it does not rely on
clearing bits in programmed flash, which flash encryption does not allow
(it is off on this device). The old region is handed out again once it is
not pinned. There is no delete command; it would be: erase the entry's
region, last sector first, once unpinned.

Sequence numbers are serial numbers (modulo 2^32): the newer one lies less
than 2^31 after the other. They wrap and are never used up, and a header
with a number far from the others is the older one, so it can neither
replace a script nor block deploys. Equal numbers: the lower offset wins.

The scan looks for a header at every sector boundary outside the entries it
found, so no bytes of an entry may be left without its header. Two rules:

- Step 2 also erases the whole region of every superseded entry whose
  header lies in the region taken, and goes from the last sector down: at
  any power cut such an entry still has its header (and is still
  superseded) or is gone. A region starts at offset 0 or at the end of a
  used one, so it never begins inside an old entry.
- Image bytes that would put the magic at a sector boundary are refused
  before they are written (`BoundaryGuard`): the deploy fails; any edit of
  the script moves them. So an unfinished upload cannot leave a header either.

If the newest entry of a name is damaged later (flash fault), the device
reports "checksum mismatch" and does not go back to an older entry of that
name, which may be partly erased. A torn header is different: it never
became an entry, so the older one is still the script.

## Boot scan

`BytecodeRegistry::load()` reads the header at every sector boundary that is
not inside an entry found before it, through the mapping (at most 64 headers,
no heap but the index). It accepts a header when magic, version, length,
header CRC, name, sequence number and bounds are right, keeps one entry per
name, and sets the next sequence number to the newest seen plus one. Image
CRCs are not checked here: a header exists only after its image was
verified (step 3), and an image is checked before it is used
(`runBootScript`: "checksum mismatch"; before a rebase).

## Pins

The engine executes the boot script from flash, so `bytecode_registry_find`
pins the whole region of the entry it returns: it is not handed out, also
after its entry was replaced, until `ScriptEngine::restart()` (after a
deploy) calls `unpinAll()` or the device reboots. So uploads of one name
alternate between two regions.

## Firmware update, filesystem update, erase

- **Firmware update.** The images point at the old build's ROM atoms. At
  boot `rebaseStale()` copies each entry with the same `stdlib_id` into a new
  entry rebased for the new build (steps 1-4); the old one is superseded.
  Another `stdlib_id`: the entry stays, `/api/bytecode/list` reports it
  `stale`, the engine reports "other firmware build": deploy again.
- **Filesystem update or format** (`uploadfs`, `ota_fs.py`): no effect.
- **Erasing `js_user`**: "no script" until the next deploy.

## Migration from the JSON index

Firmware before the script store kept the index in LittleFS
(`/js_bytecode_meta.json`) and stored images without a header, starting on a
sector boundary. While that file exists, `migrateLegacyIndex()` runs at boot,
when the registry is made (before the web server can take an upload): each
active entry whose `stdlib_id` is this build's and whose checksum is right
is copied into a free region as a new entry, the way a rebase does it (steps
1-4; allocation and erase keep off the old images), then the file is
deleted. Dropped, each with a warning in the log: an entry for another
stdlib or from before rebasing existed, one whose image fails its checksum
or lies outside the partition, one with pointers outside itself and the
stdlib, one without room. Then there is no script until the next deploy
("no script"). After a flash error or without memory the file is kept for
the next boot. A power cut before a commit leaves the file and the old image
for the next boot; after it, the next boot moves the image once more and
deletes the file. The old images are not written to while scripts move.
When all have moved, their sectors are erased where nothing else uses them
(older firmware wrote them without the guard), and only then the file is
deleted; a power cut in between leaves the file, the old images fail their
checksums at the next boot, the new entries stay, and the erase runs again.

A rollback to older firmware finds no index file, so it runs no script until
a deploy, and that deploy writes a header-less image from offset 0 over the
store. On the way back the file is there again: its image supersedes a store
entry of the same name (it can only be newer), store entries it overwrote
are gone or fail their checksum. The
code is marked MIGRATION in `BytecodeStore.h` and `BytecodeRegistry.cpp`.
