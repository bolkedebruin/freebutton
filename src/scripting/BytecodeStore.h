//
// Script store: the layout of the js_user partition (compiled scripts).
// Pure logic, no ESP or Arduino includes: the entry header, the partition
// scan, the allocator and the reader of the former JSON index. Flash access
// is in BytecodeRegistry / BytecodeWriter. Native test:
// test/test_bytecode_store. Description: docs/SCRIPT-STORE.md.
//
// An entry starts on a 4 KB sector boundary and takes whole sectors:
//
//   +0   header, kHeaderLen (80) bytes
//   +80  bytecode image (relocated for the address it lies at), image_size bytes
//   ...  unused up to the next sector boundary (erased, 0xFF)
//
// Header, little-endian:
//
//   offset size field
//   0      4    magic "FBJS" (0x534A4246)
//   4      2    header version (1)
//   6      2    header length (80): where the image starts
//   8      4    sequence number, 1..0xFFFFFFFE: of two entries with one name
//               the newer one (sequenceNewer()) is the script, the other is
//               free space
//   12     4    image size in bytes (16..partition size - 80)
//   16     4    CRC-32 of the image (the CRC of esp_crc32_le / zlib, start 0)
//   20     4    app_id: the firmware build the image was relocated for
//   24     4    rom_base: where that build's stdlib ROM table was
//   28     4    stdlib_id: which stdlib table that was
//   32     32   name, NUL-padded (at most 31 characters)
//   64     12   reserved, zero
//   76     4    CRC-32 of header bytes 0..75
//
// The header is programmed after the image is written and verified: a
// region without a valid header is free space, whatever it holds. The header
// length is a multiple of 16, so the image has the same alignment as when it
// started on the sector boundary (the engine needs 4 bytes: its values are
// 32-bit words read in place).
//
// The scan reads a header at every sector boundary outside the entries it
// found, so the bytes of an entry must never be left behind without its
// header. Two rules keep that:
//   - a region is erased from its last sector down, together with the whole
//     region of every superseded entry whose header lies in it
//     (eraseEndFor()): an entry loses its header last;
//   - nothing but a header is ever written with the magic at a sector
//     boundary: an image that has it there is refused before those bytes
//     reach flash (BoundaryGuard), also when the upload is never finished.
//

#ifndef BYTECODE_STORE_H
#define BYTECODE_STORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace bcstore {

inline constexpr uint32_t kSectorSize = 4096;
inline constexpr uint32_t kMagic = 0x534A4246;  // "FBJS"
inline constexpr uint16_t kHeaderVersion = 1;
inline constexpr uint32_t kHeaderLen = 80;
inline constexpr uint32_t kNameLen = 32;        // with the terminating NUL
inline constexpr uint32_t kMinImageSize = 16;   // the engine's own bytecode header (32-bit)
inline constexpr uint32_t kHeaderCrcOffset = kHeaderLen - 4;

inline constexpr uint32_t alignDown(uint32_t v, uint32_t a) { return v & ~(a - 1); }
inline constexpr uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

// One script in the partition (what the header holds, and where it lies).
struct Entry {
    char name[kNameLen] = {};  // script identifier (filename), NUL-padded
    uint32_t region = 0;       // offset of the entry (its header) in js_user: a sector boundary
    uint32_t size = 0;         // image size in bytes
    uint32_t checksum = 0;     // CRC-32 of the image
    uint32_t sequence = 0;     // see the header
    // App build the bytecode was relocated for: the first 4 bytes of the
    // running app's ELF SHA-256 at upload time. Relocation writes absolute
    // addresses of the stdlib's ROM atoms, which live in the APP's rodata,
    // so the bytecode cannot run on another build as it is.
    uint32_t app_id = 0;
    // Where that build's stdlib ROM table was, and which table it was
    // (bytecode_stdlib_id()). After a firmware update with the same table
    // the ROM atoms only moved: BytecodeRegistry::rebaseStale() moves the
    // bytecode's pointers to them by the table's displacement.
    uint32_t rom_base = 0;
    uint32_t stdlib_id = 0;

    // Where the image starts in the partition, and the sectors the entry takes.
    constexpr uint32_t imageOffset() const { return region + kHeaderLen; }
    constexpr uint32_t regionSize() const { return alignUp(kHeaderLen + size, kSectorSize); }
    constexpr uint32_t regionEnd() const { return region + regionSize(); }
};

// The sectors an image of `imageSize` bytes takes with its header (0 when
// it cannot fit in 32 bits).
inline constexpr uint32_t regionSizeFor(uint32_t imageSize) {
    return imageSize > UINT32_MAX - kHeaderLen - kSectorSize ? 0 : alignUp(kHeaderLen + imageSize, kSectorSize);
}

// CRC-32 (reflected, polynomial 0xEDB88320), the same function as
// esp_crc32_le(): crc32(0, "123456789", 9) == 0xCBF43926.
inline uint32_t crc32(uint32_t crc, const uint8_t* data, size_t len) {
    static constexpr uint32_t kTable[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        crc = (crc >> 4) ^ kTable[crc & 15];
        crc = (crc >> 4) ^ kTable[crc & 15];
    }
    return ~crc;
}

inline void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
inline void put32(uint8_t* p, uint32_t v) {
    put16(p, static_cast<uint16_t>(v));
    put16(p + 2, static_cast<uint16_t>(v >> 16));
}
inline uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

// The name as the header stores it: 1..31 characters, then NULs to the end.
inline bool nameOk(const char* name) {
    size_t len = 0;
    while (len < kNameLen && name[len] != '\0') ++len;
    if (len == 0 || len == kNameLen) return false;
    for (size_t i = len; i < kNameLen; ++i) {
        if (name[i] != '\0') return false;
    }
    return true;
}

// The header of `e` (e.region is not stored: it is where the header lies).
inline void encodeHeader(const Entry& e, uint8_t out[kHeaderLen]) {
    memset(out, 0, kHeaderLen);
    put32(out + 0, kMagic);
    put16(out + 4, kHeaderVersion);
    put16(out + 6, static_cast<uint16_t>(kHeaderLen));
    put32(out + 8, e.sequence);
    put32(out + 12, e.size);
    put32(out + 16, e.checksum);
    put32(out + 20, e.app_id);
    put32(out + 24, e.rom_base);
    put32(out + 28, e.stdlib_id);
    memcpy(out + 32, e.name, kNameLen);
    put32(out + kHeaderCrcOffset, crc32(0, out, kHeaderCrcOffset));
}

enum class HeaderCheck {
    Ok,
    NoMagic,      // erased flash, an image without header, anything else
    BadVersion,   // a header of another format version
    BadLength,
    BadCrc,       // torn or damaged header
    BadField,     // name, sequence number or image size out of range
    OutOfBounds,  // not on a sector boundary, or the image runs past the partition
};

// Reads the header at `p` (kHeaderLen bytes), which lies at offset `region`
// of a partition of `partitionSize` bytes. Fills `out` only for Ok.
inline HeaderCheck decodeHeader(const uint8_t* p, uint32_t region, uint32_t partitionSize, Entry* out) {
    if (get32(p + 0) != kMagic) return HeaderCheck::NoMagic;
    if (get16(p + 4) != kHeaderVersion) return HeaderCheck::BadVersion;
    if (get16(p + 6) != kHeaderLen) return HeaderCheck::BadLength;
    if (get32(p + kHeaderCrcOffset) != crc32(0, p, kHeaderCrcOffset)) return HeaderCheck::BadCrc;

    Entry e;
    e.region = region;
    e.sequence = get32(p + 8);
    e.size = get32(p + 12);
    e.checksum = get32(p + 16);
    e.app_id = get32(p + 20);
    e.rom_base = get32(p + 24);
    e.stdlib_id = get32(p + 28);
    memcpy(e.name, p + 32, kNameLen);
    if (!nameOk(e.name) || e.sequence == 0 || e.sequence == UINT32_MAX || e.size < kMinImageSize) {
        return HeaderCheck::BadField;
    }
    if (region % kSectorSize != 0 || region > partitionSize || partitionSize - region < kHeaderLen ||
        e.size > partitionSize - region - kHeaderLen) {
        return HeaderCheck::OutOfBounds;
    }
    if (out) *out = e;
    return HeaderCheck::Ok;
}

// Sequence numbers are compared as serial numbers (modulo 2^32): `a` is
// newer than `b` when it lies less than 2^31 after it. So the numbers wrap
// and are never used up, and one header with a number far from the others
// (0xFFFFFFFE next to 5) is the OLDER one: it cannot take the place of a
// script or block later entries. The order is right while the entries of a
// name lie within 2^31 numbers of each other; an entry this firmware
// supersedes is erased long before that.
inline constexpr bool sequenceNewer(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) > 0;
}

// Of two entries with one name: is `a` the script (and `b` free space)?
// The newer sequence number. Equal numbers (a header whose read-back failed
// after it was programmed, BytecodeRegistry::commit(), is the one way to
// get them): the entry at the lower offset, in either scan order.
inline bool supersedes(const Entry& a, const Entry& b) {
    return a.sequence != b.sequence ? sequenceNewer(a.sequence, b.sequence) : a.region < b.region;
}

// The sequence number after `sequence` (0: none yet). Wraps past
// 0xFFFFFFFE to 1: 0 and 0xFFFFFFFF are never numbers.
inline constexpr uint32_t nextSequence(uint32_t sequence) {
    return sequence >= UINT32_MAX - 1 ? 1 : sequence + 1;
}

struct ScanResult {
    uint32_t headers = 0;          // valid headers found
    uint32_t superseded = 0;       // of those: older entries of a name (free space)
    uint32_t newest_sequence = 0;  // newest sequence number of any valid header (0: none)
};

// Calls visit(const Entry&) for the valid header at every sector boundary
// that is not inside an entry found before it, lowest offset first.
template <class Visit>
void forEachHeader(const uint8_t* partition, uint32_t partitionSize, Visit&& visit) {
    uint32_t offset = 0;
    while (offset < partitionSize && partitionSize - offset >= kSectorSize) {
        Entry found;
        if (decodeHeader(partition + offset, offset, partitionSize, &found) != HeaderCheck::Ok) {
            offset += kSectorSize;
            continue;
        }
        visit(static_cast<const Entry&>(found));
        offset = found.regionEnd();
    }
}

// Builds the index from the partition's bytes (forEachHeader()). `out` (a
// vector of Entry, cleared by the caller) gets one entry per name. Image
// CRCs are not checked here: a header is only written after its image was
// verified, and whoever uses an image checks its CRC first.
template <class Entries>
ScanResult scanPartition(const uint8_t* partition, uint32_t partitionSize, Entries& out) {
    ScanResult result;
    forEachHeader(partition, partitionSize, [&](const Entry& found) {
        if (result.headers++ == 0 || sequenceNewer(found.sequence, result.newest_sequence)) {
            result.newest_sequence = found.sequence;
        }
        for (size_t i = 0; i < out.size(); ++i) {
            if (strcmp(out[i].name, found.name) == 0) {
                if (supersedes(found, out[i])) out[i] = found;
                ++result.superseded;
                return;
            }
        }
        out.push_back(found);
    });
    return result;
}

// Where the erase of the region [region, end) that is about to be written
// has to stop: `end`, or further when a header lies in the region whose
// entry runs past it (an entry that was superseded: it is in no index, so
// its region counted as free). Its whole region goes, so that none of its
// bytes are left without their header. Never into a used range
// (used(visit), as for allocateRegion()): that only happens when something
// else wrote into the partition (older firmware after a downgrade).
// Erase [region, result) from the last sector down.
template <class Used>
uint32_t eraseEndFor(const uint8_t* partition, uint32_t partitionSize, uint32_t region, uint32_t end, Used&& used) {
    uint32_t result = end;
    forEachHeader(partition, partitionSize, [&](const Entry& found) {
        if (found.region >= region && found.region < end && found.regionEnd() > result) result = found.regionEnd();
    });
    used([&](uint32_t s, uint32_t e) {
        s = alignDown(s, kSectorSize);
        if (s < e && alignUp(e, kSectorSize) > end && s < result) result = s > end ? s : end;
    });
    return result;
}

// Watches the image bytes on their way to flash, in the order they are
// written: ok() turns false when they would put the magic at a sector
// boundary of the partition. The caller then does not write that buffer
// and gives up the image. (Such an image only comes from someone who
// builds it on purpose, or once in 2^32 sectors by chance: any edit of the
// script moves it.)
class BoundaryGuard {
public:
    // `offset`: where in the partition `data` is about to be written.
    bool feed(uint32_t offset, const uint8_t* data, size_t len) {
        static constexpr uint8_t kMagicBytes[4] = {'F', 'B', 'J', 'S'};
        size_t i = 0;
        while (i < len && ok_) {
            const uint32_t pos = (offset + static_cast<uint32_t>(i)) % kSectorSize;
            if (pos >= 4) {
                i += kSectorSize - pos;
                continue;
            }
            if (pos == 0) matched_ = 0;
            if (matched_ == pos && data[i] == kMagicBytes[pos]) ++matched_;
            if (matched_ == 4) ok_ = false;
            ++i;
        }
        return ok_;
    }
    bool ok() const { return ok_; }

private:
    uint8_t matched_ = 0;  // magic bytes seen from the last boundary on
    bool ok_ = true;
};

// The "size" parameter of an upload: the exact number of image bytes, so
// that the region is taken for the image and not for the request (whose
// length includes the multipart framing). True and *out set when `text` is
// a plain decimal number that can be an image in a partition of
// `partitionSize` bytes and is not more than the request carries.
inline bool parseImageSize(const char* text, uint32_t contentLength, uint32_t partitionSize, uint32_t* out) {
    if (text == nullptr || partitionSize < kHeaderLen) return false;
    size_t digits = 0;
    while (text[digits] >= '0' && text[digits] <= '9') ++digits;
    if (digits == 0 || digits > 10 || text[digits] != '\0') return false;  // no sign, space, hex, fraction
    char* end = nullptr;
    const unsigned long value = strtoul(text, &end, 10);
    if (end != text + digits) return false;
    if (value < kMinImageSize || value > partitionSize - kHeaderLen || value > contentLength) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

// The lowest region for an image of `imageSize` bytes (with its header) in
// a partition of `limit` bytes, at or above `floor`, that touches no used
// range. used(visit) calls visit(start, end) for every used range (entries,
// pins, the upload in progress); ranges count as whole sectors. used() is
// called several times and from inside its own visit.
template <class Used>
bool allocateRegion(uint32_t imageSize, uint32_t limit, uint32_t floor, Used&& used, uint32_t* region) {
    const uint32_t need = regionSizeFor(imageSize);
    limit = alignDown(limit, kSectorSize);
    if (imageSize == 0 || need == 0 || need > limit) return false;

    bool found = false;
    uint32_t best = 0;
    // Candidates: the floor and the end of every used range; the lowest that fits.
    auto tryAt = [&](uint32_t candidate) {
        if (candidate > limit - need) return;  // also a candidate that alignUp() would wrap
        candidate = alignUp(candidate, kSectorSize);
        if (candidate < floor || candidate > limit - need || (found && candidate >= best)) return;
        const uint32_t end = candidate + need;
        bool hit = false;
        used([&](uint32_t s, uint32_t e) {
            if (s < e && alignDown(s, kSectorSize) < end && candidate < alignUp(e, kSectorSize)) hit = true;
        });
        if (hit) return;
        best = candidate;
        found = true;
    };
    tryAt(floor);
    used([&](uint32_t s, uint32_t e) {
        if (s < e) tryAt(e);
    });
    if (found && region) *region = best;
    return found;
}

// ---------------------------------------------------------------------------
// MIGRATION from firmware that kept the index in LittleFS
// (/js_bytecode_meta.json) and stored images without a header. Used once,
// at the first boot of a firmware with the script store, and again after a
// downgrade to such firmware and back
// (BytecodeRegistry::migrateLegacyIndex()). Remove with that function.
// ---------------------------------------------------------------------------

// One entry of the JSON index: an image at `offset`, no header in front.
struct LegacyEntry {
    char name[kNameLen] = {};
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t checksum = 0;
    uint32_t app_id = 0;
    uint32_t rom_base = 0;   // 0: written before rebasing existed
    uint32_t stdlib_id = 0;
    bool active = false;
    bool malformed = false;  // a field that is not a plain number in 32 bits
};

// Reads {"entries":[{"name":"main.jsc","offset":0,"size":1234,...},...]}
// one character at a time (the file is read through a small buffer, no
// document in memory). feed() returns true when an entry is complete:
// entry() is valid until the next feed(). Takes every object two levels
// below the root; unknown fields and nested values are skipped.
class LegacyIndexReader {
public:
    bool feed(char c) {
        if (in_string_) {
            if (escaped_) {
                escaped_ = false;
                append(c);
            } else if (c == '\\') {
                escaped_ = true;
            } else if (c == '"') {
                in_string_ = false;
                if (depth_ == kEntryDepth && !want_value_) {
                    memcpy(key_, token_, sizeof(key_));
                    token_len_ = 0;
                    token_[0] = '\0';
                } else {
                    token_is_string_ = true;
                }
            } else {
                append(c);
            }
            return false;
        }
        switch (c) {
            case '"':
                in_string_ = true;
                token_len_ = 0;
                token_[0] = '\0';
                token_too_long_ = false;
                return false;
            case '{':
            case '[':
                // An object or array as the value of a field: not a value
                // of ours, and nothing inside it is.
                if (depth_ == kEntryDepth) nested_value_ = true;
                ++depth_;
                if (c == '{' && depth_ == kEntryDepth) {
                    entry_ = LegacyEntry();
                    startPair();
                }
                return false;
            case '}':
            case ']': {
                bool complete = false;
                if (c == '}' && depth_ == kEntryDepth) {
                    assign();
                    complete = entry_.name[0] != '\0';
                }
                if (depth_ > 0) --depth_;
                return complete;
            }
            case ':':
                if (depth_ == kEntryDepth) {
                    want_value_ = true;
                    token_len_ = 0;
                    token_[0] = '\0';
                    token_is_string_ = false;
                    token_too_long_ = false;
                }
                return false;
            case ',':
                if (depth_ == kEntryDepth) {
                    assign();
                    startPair();
                }
                return false;
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                return false;
            default:
                append(c);  // a number, true, false, null
                return false;
        }
    }

    const LegacyEntry& entry() const { return entry_; }

private:
    static constexpr uint32_t kEntryDepth = 3;  // root object, "entries" array, entry object

    LegacyEntry entry_;
    char key_[kNameLen] = {};
    char token_[kNameLen] = {};
    uint8_t token_len_ = 0;
    uint32_t depth_ = 0;  // one per character at most: cannot wrap
    bool nested_value_ = false;
    bool in_string_ = false;
    bool escaped_ = false;
    bool want_value_ = false;
    bool token_is_string_ = false;
    bool token_too_long_ = false;

    void append(char c) {
        if (token_len_ + 1u < sizeof(token_)) {
            token_[token_len_++] = c;
            token_[token_len_] = '\0';
        } else {
            token_too_long_ = true;
        }
    }

    void startPair() {
        want_value_ = false;
        key_[0] = '\0';
        token_len_ = 0;
        token_[0] = '\0';
        token_is_string_ = false;
        token_too_long_ = false;
        nested_value_ = false;
    }

    // Decimal digits only, in 32 bits: no sign, fraction, exponent or hex.
    static bool parseNumber(const char* text, uint32_t* out) {
        if (*text == '\0') return false;
        uint32_t value = 0;
        for (; *text != '\0'; ++text) {
            if (*text < '0' || *text > '9') return false;
            const uint32_t digit = static_cast<uint32_t>(*text - '0');
            if (value > (UINT32_MAX - digit) / 10) return false;
            value = value * 10 + digit;
        }
        *out = value;
        return true;
    }

    // The pair key_ : token_ of the entry being read.
    void assign() {
        if (!want_value_ || key_[0] == '\0' || nested_value_) return;
        if (token_too_long_) {
            if (!token_is_string_ || strcmp(key_, "name") == 0) entry_.malformed = true;
            return;
        }
        if (token_is_string_) {
            if (strcmp(key_, "name") == 0) {
                memset(entry_.name, 0, sizeof(entry_.name));
                memcpy(entry_.name, token_, token_len_);
            }
            return;
        }
        if (strcmp(key_, "active") == 0) {
            entry_.active = strcmp(token_, "true") == 0;
            return;
        }
        uint32_t* field = nullptr;
        if (strcmp(key_, "offset") == 0) field = &entry_.offset;
        else if (strcmp(key_, "size") == 0) field = &entry_.size;
        else if (strcmp(key_, "checksum") == 0) field = &entry_.checksum;
        else if (strcmp(key_, "app") == 0) field = &entry_.app_id;
        else if (strcmp(key_, "rom") == 0) field = &entry_.rom_base;
        else if (strcmp(key_, "lib") == 0) field = &entry_.stdlib_id;
        if (field && !parseNumber(token_, field)) entry_.malformed = true;
    }
};

// The sectors an old image took, when its entry names a place in the
// partition (active or not).
inline bool legacyRange(const LegacyEntry& e, uint32_t partitionSize, uint32_t* start, uint32_t* end) {
    if (e.malformed || e.size == 0 || e.offset > partitionSize || e.size > partitionSize - e.offset) return false;
    *start = alignDown(e.offset, kSectorSize);
    *end = alignUp(e.offset + e.size, kSectorSize);
    return true;
}

// After the migration: calls eraseSector(offset) for every sector of
// [start, end) that touches no used range, last sector first, so that no
// bytes written by older firmware (which had no BoundaryGuard) stay in
// free space. Stops and returns false when eraseSector() does.
template <class Used, class EraseSector>
bool eraseFreeSectors(uint32_t start, uint32_t end, Used&& used, EraseSector&& eraseSector) {
    start = alignDown(start, kSectorSize);
    for (uint32_t sector = alignUp(end, kSectorSize); sector > start; sector -= kSectorSize) {
        const uint32_t from = sector - kSectorSize;
        bool hit = false;
        used([&](uint32_t s, uint32_t e) {
            if (s < e && alignDown(s, kSectorSize) < sector && from < alignUp(e, kSectorSize)) hit = true;
        });
        if (!hit && !eraseSector(from)) return false;
    }
    return true;
}

enum class LegacyAction {
    Migrate,          // copy the image into a new entry, rebased for this build
    SkipInactive,     // was not a script any more
    DropInvalid,      // no name, a bad field, or the image does not lie in the partition
    DropNoRebaseInfo, // written before rebasing existed: it cannot be moved
    DropOtherStdlib,  // relocated for another stdlib table: deploy again
};

// What to do with one entry of the JSON index. An image can only move to
// make room for its header by rebasing it, which needs the same stdlib
// table as this build's. A store entry of the same name does not matter:
// the file exists only when older firmware stored a script after the store
// was made (a downgrade), so an image of the file whose checksum is right
// (checked by the move) is the newer script and supersedes that entry.
inline LegacyAction legacyAction(const LegacyEntry& e, uint32_t partitionSize, uint32_t stdlibId) {
    if (!e.active) return LegacyAction::SkipInactive;
    if (e.malformed || !nameOk(e.name) || e.size < kMinImageSize || e.offset % 4 != 0 || e.offset > partitionSize ||
        e.size > partitionSize - e.offset) {
        return LegacyAction::DropInvalid;
    }
    if (e.rom_base == 0 || e.stdlib_id == 0) return LegacyAction::DropNoRebaseInfo;
    if (e.stdlib_id != stdlibId) return LegacyAction::DropOtherStdlib;
    return LegacyAction::Migrate;
}

} // namespace bcstore

#endif // BYTECODE_STORE_H
