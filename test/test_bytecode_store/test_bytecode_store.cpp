//
// Native unit tests for the script store's pure logic
// (src/scripting/BytecodeStore.h): the entry header, the scan that builds
// the index from the js_user partition's bytes, which of two entries of a
// name is the script, the allocator, and the reader of the former JSON
// index (migration). The partition is a 256 KB byte array here.
//
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/scripting/BytecodeStore.h"

using bcstore::Entry;
using bcstore::HeaderCheck;
using bcstore::kHeaderLen;
using bcstore::kSectorSize;

namespace {

constexpr uint32_t kPartition = 256 * 1024;  // js_user: 64 sectors

std::vector<uint8_t> flash;  // erased before every test

void setUp_() { flash.assign(kPartition, 0xFF); }

// An image of `size` bytes that does not look like a header anywhere.
std::vector<uint8_t> image(uint32_t size, uint8_t seed) {
    std::vector<uint8_t> out(size);
    for (uint32_t i = 0; i < size; ++i) out[i] = static_cast<uint8_t>(seed + i * 7);
    return out;
}

Entry entry(const char* name, uint32_t region, uint32_t size, uint32_t sequence) {
    Entry e;
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.region = region;
    e.size = size;
    e.sequence = sequence;
    e.app_id = 0xA11CE000 + sequence;
    e.rom_base = 0x3C0F0000;
    e.stdlib_id = 0x57D11B00;
    return e;
}

// What a deploy does, in its order: the image first, the header last.
void writeImage(const Entry& e, uint8_t seed) {
    const auto bytes = image(e.size, seed);
    memcpy(flash.data() + e.imageOffset(), bytes.data(), bytes.size());
}

Entry writeHeader(Entry e, uint8_t seed) {
    const auto bytes = image(e.size, seed);
    e.checksum = bcstore::crc32(0, bytes.data(), bytes.size());
    uint8_t header[kHeaderLen];
    bcstore::encodeHeader(e, header);
    memcpy(flash.data() + e.region, header, kHeaderLen);
    return e;
}

Entry store(const char* name, uint32_t region, uint32_t size, uint32_t sequence, uint8_t seed = 1) {
    const Entry e = entry(name, region, size, sequence);
    writeImage(e, seed);
    return writeHeader(e, seed);
}

struct Scanned {
    std::vector<Entry> entries;
    bcstore::ScanResult result;
    const Entry* find(const char* name) const {
        for (const auto& e : entries) {
            if (strcmp(e.name, name) == 0) return &e;
        }
        return nullptr;
    }
};

Scanned scan() {
    Scanned s;
    s.result = bcstore::scanPartition(flash.data(), kPartition, s.entries);
    return s;
}

struct Range {
    uint32_t start;
    uint32_t end;
};

bool allocate(uint32_t imageSize, const std::vector<Range>& used, uint32_t* region, uint32_t floor = 0,
              uint32_t limit = kPartition) {
    auto each = [&used](auto&& visit) {
        for (const auto& r : used) visit(r.start, r.end);
    };
    return bcstore::allocateRegion(imageSize, limit, floor, each, region);
}

std::vector<bcstore::LegacyEntry> readLegacy(const std::string& json) {
    std::vector<bcstore::LegacyEntry> out;
    bcstore::LegacyIndexReader reader;
    for (const char c : json) {
        if (reader.feed(c)) out.push_back(reader.entry());
    }
    return out;
}

} // namespace

void setUp() { setUp_(); }
void tearDown() {}

// ---- header ----

void test_crc32_is_the_zlib_crc() {
    // esp_crc32_le(0, ...) gives the same value: the image CRC in a header
    // is the one the firmware computes with the ROM function.
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, bcstore::crc32(0, reinterpret_cast<const uint8_t*>("123456789"), 9));
    // In pieces, as the writer does.
    uint32_t crc = bcstore::crc32(0, reinterpret_cast<const uint8_t*>("1234"), 4);
    crc = bcstore::crc32(crc, reinterpret_cast<const uint8_t*>("56789"), 5);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc);
    TEST_ASSERT_EQUAL_HEX32(0, bcstore::crc32(0, nullptr, 0));
}

void test_header_layout() {
    TEST_ASSERT_EQUAL_UINT32(80, kHeaderLen);
    TEST_ASSERT_EQUAL_UINT32(0, kHeaderLen % 16);  // the image keeps its alignment

    Entry e = entry("main.jsc", 0x3000, 0x1234, 7);
    e.checksum = 0xDEADBEEF;
    e.app_id = 0x11223344;
    e.rom_base = 0x3C012340;
    e.stdlib_id = 0x55667788;
    uint8_t h[kHeaderLen];
    bcstore::encodeHeader(e, h);

    const uint8_t fixed[32] = {
        'F', 'B', 'J', 'S',      // magic
        0x01, 0x00,              // header version
        0x50, 0x00,              // header length
        0x07, 0x00, 0x00, 0x00,  // sequence
        0x34, 0x12, 0x00, 0x00,  // image size
        0xEF, 0xBE, 0xAD, 0xDE,  // image CRC
        0x44, 0x33, 0x22, 0x11,  // app_id
        0x40, 0x23, 0x01, 0x3C,  // rom_base
        0x88, 0x77, 0x66, 0x55,  // stdlib_id
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fixed, h, 32);
    TEST_ASSERT_EQUAL_STRING("main.jsc", reinterpret_cast<const char*>(h + 32));
    for (int i = 32 + 8; i < 76; ++i) TEST_ASSERT_EQUAL_HEX8(0, h[i]);  // name padding, reserved
    TEST_ASSERT_EQUAL_HEX32(bcstore::crc32(0, h, 76), bcstore::get32(h + 76));
}

void test_header_round_trip() {
    Entry e = entry("a-name-of-thirty-one-characters", 0x5000, 20000, 0xFFFFFFFE);
    TEST_ASSERT_EQUAL_size_t(31, strlen(e.name));
    e.checksum = 0x01020304;
    uint8_t h[kHeaderLen];
    bcstore::encodeHeader(e, h);

    Entry out;
    TEST_ASSERT_TRUE(bcstore::decodeHeader(h, 0x5000, kPartition, &out) == HeaderCheck::Ok);
    TEST_ASSERT_EQUAL_STRING(e.name, out.name);
    TEST_ASSERT_EQUAL_UINT32(0x5000, out.region);
    TEST_ASSERT_EQUAL_UINT32(20000, out.size);
    TEST_ASSERT_EQUAL_HEX32(0x01020304, out.checksum);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFE, out.sequence);
    TEST_ASSERT_EQUAL_HEX32(e.app_id, out.app_id);
    TEST_ASSERT_EQUAL_HEX32(e.rom_base, out.rom_base);
    TEST_ASSERT_EQUAL_HEX32(e.stdlib_id, out.stdlib_id);
    TEST_ASSERT_EQUAL_UINT32(0x5000 + 80, out.imageOffset());
    TEST_ASSERT_EQUAL_UINT32(0x5000, out.regionSize());  // 80 + 20000 -> 5 sectors
    TEST_ASSERT_EQUAL_UINT32(0xA000, out.regionEnd());
}

void test_region_size() {
    TEST_ASSERT_EQUAL_UINT32(4096, bcstore::regionSizeFor(16));
    TEST_ASSERT_EQUAL_UINT32(4096, bcstore::regionSizeFor(4096 - 80));
    TEST_ASSERT_EQUAL_UINT32(8192, bcstore::regionSizeFor(4096 - 79));  // the header costs a sector here
    TEST_ASSERT_EQUAL_UINT32(kPartition, bcstore::regionSizeFor(kPartition - 80));
    TEST_ASSERT_EQUAL_UINT32(0, bcstore::regionSizeFor(0xFFFFFFFF));
}

// Rebuilds the header of `e` with one change and reports how it is read.
template <class Change>
HeaderCheck checkChanged(const Entry& e, uint32_t region, Change&& change, bool fixCrc = true) {
    uint8_t h[kHeaderLen];
    bcstore::encodeHeader(e, h);
    change(h);
    if (fixCrc) bcstore::put32(h + 76, bcstore::crc32(0, h, 76));
    return bcstore::decodeHeader(h, region, kPartition, nullptr);
}

void test_header_rejected() {
    const Entry e = entry("main.jsc", 0, 5000, 3);
    auto nothing = [](uint8_t*) {};
    TEST_ASSERT_TRUE(checkChanged(e, 0, nothing) == HeaderCheck::Ok);

    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { h[0] = 'f'; }) == HeaderCheck::NoMagic);
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { h[4] = 2; }) == HeaderCheck::BadVersion);
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { h[6] = 96; }) == HeaderCheck::BadLength);
    // One bit anywhere, CRC not updated.
    for (int byte : {8, 12, 16, 20, 24, 28, 32, 63, 64, 75, 76, 79}) {
        TEST_ASSERT_TRUE(checkChanged(e, 0, [byte](uint8_t* h) { h[byte] ^= 0x10; }, false) == HeaderCheck::BadCrc);
    }
    // Fields out of range, with a correct CRC (a writer bug, not a torn write).
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { bcstore::put32(h + 8, 0); }) == HeaderCheck::BadField);
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { bcstore::put32(h + 8, 0xFFFFFFFF); }) == HeaderCheck::BadField);
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { bcstore::put32(h + 12, 15); }) == HeaderCheck::BadField);
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { h[32] = 0; }) == HeaderCheck::BadField);              // no name
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { memset(h + 32, 'x', 32); }) == HeaderCheck::BadField);  // no NUL
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { h[50] = 'x'; }) == HeaderCheck::BadField);            // after the NUL
    // Bounds.
    TEST_ASSERT_TRUE(checkChanged(e, 0x800, nothing) == HeaderCheck::OutOfBounds);  // not a sector boundary
    TEST_ASSERT_TRUE(checkChanged(e, kPartition - kSectorSize, nothing) == HeaderCheck::OutOfBounds);  // 5000 > one sector
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { bcstore::put32(h + 12, kPartition - 79); }) ==
                     HeaderCheck::OutOfBounds);
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { bcstore::put32(h + 12, 0xFFFFFFF0); }) ==
                     HeaderCheck::OutOfBounds);
    TEST_ASSERT_TRUE(checkChanged(e, 0, [](uint8_t* h) { bcstore::put32(h + 12, kPartition - 80); }) == HeaderCheck::Ok);
}

// ---- scan ----

void test_scan_empty_partition() {
    const Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(0, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(0, s.result.headers);
    TEST_ASSERT_EQUAL_UINT32(1, bcstore::nextSequence(s.result.newest_sequence));
}

void test_scan_finds_entries() {
    const Entry a = store("main.jsc", 0, 9000, 1, 3);         // sectors 0..2
    const Entry b = store("other.jsc", 0x3000, 300, 2, 9);    // sector 3
    const Entry c = store("third.jsc", 0x10000, 70000, 5, 5); // not adjacent
    const Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(3, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(3, s.result.headers);
    TEST_ASSERT_EQUAL_UINT32(0, s.result.superseded);
    TEST_ASSERT_EQUAL_UINT32(5, s.result.newest_sequence);
    TEST_ASSERT_EQUAL_UINT32(6, bcstore::nextSequence(s.result.newest_sequence));

    const Entry* m = s.find("main.jsc");
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_UINT32(0, m->region);
    TEST_ASSERT_EQUAL_UINT32(80, m->imageOffset());
    TEST_ASSERT_EQUAL_UINT32(9000, m->size);
    TEST_ASSERT_EQUAL_HEX32(a.checksum, m->checksum);
    TEST_ASSERT_EQUAL_HEX32(a.app_id, m->app_id);
    // The checksum in the header is the image's, where the header says it is.
    TEST_ASSERT_EQUAL_HEX32(m->checksum, bcstore::crc32(0, flash.data() + m->imageOffset(), m->size));

    const Entry* o = s.find("other.jsc");
    TEST_ASSERT_NOT_NULL(o);
    TEST_ASSERT_EQUAL_UINT32(0x3000, o->region);
    TEST_ASSERT_EQUAL_HEX32(b.checksum, o->checksum);
    const Entry* t = s.find("third.jsc");
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT32(0x10000, t->region);
    TEST_ASSERT_EQUAL_HEX32(c.checksum, bcstore::crc32(0, flash.data() + t->imageOffset(), t->size));
}

// A deploy stopped by a power cut, at each step: the old script stays.
void test_scan_torn_deploy_keeps_the_old_script() {
    store("main.jsc", 0, 6000, 4, 1);
    const Entry next = entry("main.jsc", 0x2000, 7000, 5);

    // 1. Region erased, nothing written.
    Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(4, s.find("main.jsc")->sequence);

    // 2. Half of the image written.
    const auto bytes = image(next.size, 2);
    memcpy(flash.data() + next.imageOffset(), bytes.data(), 3500);
    s = scan();
    TEST_ASSERT_EQUAL_UINT32(4, s.find("main.jsc")->sequence);

    // 3. Image complete, header place still erased (all 0xFF in front of it).
    writeImage(next, 2);
    s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(0, s.find("main.jsc")->region);

    // 4. Header half written: every prefix of it, the rest erased.
    Entry committed = next;
    committed.checksum = bcstore::crc32(0, bytes.data(), bytes.size());
    uint8_t header[kHeaderLen];
    bcstore::encodeHeader(committed, header);
    for (uint32_t n = 1; n < kHeaderLen; ++n) {
        memset(flash.data() + next.region, 0xFF, kHeaderLen);
        memcpy(flash.data() + next.region, header, n);
        s = scan();
        TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, s.find("main.jsc")->sequence, "a torn header was accepted");
        TEST_ASSERT_EQUAL_UINT32(5, bcstore::nextSequence(s.result.newest_sequence));
    }

    // 5. Header complete but with bits that did not program (still 1).
    memcpy(flash.data() + next.region, header, kHeaderLen);
    flash[next.region + 13] |= 0x80;
    s = scan();
    TEST_ASSERT_EQUAL_UINT32(4, s.find("main.jsc")->sequence);

    // 6. Header complete: the new script.
    memcpy(flash.data() + next.region, header, kHeaderLen);
    s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(5, s.find("main.jsc")->sequence);
    TEST_ASSERT_EQUAL_UINT32(0x2000, s.find("main.jsc")->region);
    TEST_ASSERT_EQUAL_UINT32(1, s.result.superseded);
    TEST_ASSERT_EQUAL_UINT32(6, bcstore::nextSequence(s.result.newest_sequence));
}

void test_scan_header_with_wrong_crc_is_free_space() {
    const Entry e = store("main.jsc", 0x4000, 5000, 9, 1);
    flash[e.region + 76] ^= 0x01;
    Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(0, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(0, s.result.newest_sequence);
}

// What older firmware left: images that start on a sector boundary without
// a header (they begin with the engine's own bytecode header).
void test_scan_image_without_header() {
    auto legacy = image(12000, 4);
    legacy[0] = 0xFB;  // not "FBJS": the engine's magic and version
    legacy[1] = 0xAC;
    legacy[2] = 0x01;
    legacy[3] = 0x00;
    memcpy(flash.data() + 0x1000, legacy.data(), legacy.size());
    Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(0, s.entries.size());

    // With a store entry behind it.
    store("main.jsc", 0x4000, 12000, 1, 4);
    s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(0x4000, s.find("main.jsc")->region);
}

void test_scan_higher_sequence_wins() {
    store("main.jsc", 0, 5000, 7, 1);
    store("main.jsc", 0x2000, 6000, 8, 2);
    Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(2, s.result.headers);
    TEST_ASSERT_EQUAL_UINT32(1, s.result.superseded);
    TEST_ASSERT_EQUAL_UINT32(0x2000, s.find("main.jsc")->region);
    TEST_ASSERT_EQUAL_UINT32(6000, s.find("main.jsc")->size);
    TEST_ASSERT_EQUAL_UINT32(8, s.find("main.jsc")->sequence);
}

// Uploads of one name alternate between two regions: the newer entry is
// as often below the older one as above it.
void test_scan_higher_sequence_wins_at_the_lower_offset() {
    store("main.jsc", 0, 6000, 12, 2);
    store("main.jsc", 0x2000, 5000, 11, 1);
    store("other.jsc", 0x4000, 100, 3, 3);
    Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(2, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(0, s.find("main.jsc")->region);
    TEST_ASSERT_EQUAL_UINT32(12, s.find("main.jsc")->sequence);
    TEST_ASSERT_EQUAL_UINT32(3, s.find("other.jsc")->sequence);
    TEST_ASSERT_EQUAL_UINT32(12, s.result.newest_sequence);  // of every header
}

void test_scan_three_versions_and_the_next_sequence() {
    // The middle one is the script; the highest number in the partition
    // belongs to another name.
    store("main.jsc", 0, 100, 2, 1);
    store("main.jsc", 0x1000, 100, 6, 2);
    store("main.jsc", 0x2000, 100, 4, 3);
    store("x.jsc", 0x3000, 100, 9, 4);
    Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(2, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(0x1000, s.find("main.jsc")->region);
    TEST_ASSERT_EQUAL_UINT32(2, s.result.superseded);
    TEST_ASSERT_EQUAL_UINT32(10, bcstore::nextSequence(s.result.newest_sequence));
}

void test_supersedes() {
    const Entry low = entry("a", 0x1000, 100, 5);
    const Entry high = entry("a", 0x3000, 100, 6);
    TEST_ASSERT_TRUE(bcstore::supersedes(high, low));
    TEST_ASSERT_FALSE(bcstore::supersedes(low, high));
    // Equal numbers (no writer makes them): the lower offset, either order.
    const Entry twin = entry("a", 0x3000, 100, 5);
    TEST_ASSERT_TRUE(bcstore::supersedes(low, twin));
    TEST_ASSERT_FALSE(bcstore::supersedes(twin, low));
}

// Sequence numbers are serial numbers: they wrap, and are never used up.
void test_sequence_wraps() {
    TEST_ASSERT_EQUAL_UINT32(1, bcstore::nextSequence(0));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFE, bcstore::nextSequence(0xFFFFFFFD));
    // 0 and 0xFFFFFFFF (what erased flash reads) are never numbers.
    TEST_ASSERT_EQUAL_UINT32(1, bcstore::nextSequence(0xFFFFFFFE));
    TEST_ASSERT_EQUAL_UINT32(1, bcstore::nextSequence(0xFFFFFFFF));

    TEST_ASSERT_TRUE(bcstore::sequenceNewer(6, 5));
    TEST_ASSERT_FALSE(bcstore::sequenceNewer(5, 6));
    TEST_ASSERT_FALSE(bcstore::sequenceNewer(5, 5));
    TEST_ASSERT_TRUE(bcstore::sequenceNewer(1, 0xFFFFFFFE));  // across the wrap
    TEST_ASSERT_FALSE(bcstore::sequenceNewer(0xFFFFFFFE, 1));
    TEST_ASSERT_TRUE(bcstore::sequenceNewer(0x80000000, 1));
    TEST_ASSERT_FALSE(bcstore::sequenceNewer(0x80000002, 1));  // more than 2^31 ahead: older

    // A replace across the wrap.
    store("main.jsc", 0, 100, 0xFFFFFFFE, 1);
    Scanned s = scan();
    TEST_ASSERT_EQUAL_UINT32(1, bcstore::nextSequence(s.result.newest_sequence));
    store("main.jsc", 0x1000, 100, 1, 2);
    s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(0x1000, s.find("main.jsc")->region);
    TEST_ASSERT_EQUAL_UINT32(1, s.result.newest_sequence);
    TEST_ASSERT_EQUAL_UINT32(2, bcstore::nextSequence(s.result.newest_sequence));
}

// A header with a number far from the others (damage, or made on purpose)
// is the older one: it does not replace a script and does not block deploys.
void test_far_off_sequence_does_not_win() {
    store("main.jsc", 0, 100, 7, 1);
    store("main.jsc", 0x1000, 100, 0xFFFFFFFE, 2);
    store("x.jsc", 0x2000, 100, 8, 3);
    for (int order = 0; order < 2; ++order) {
        const Scanned s = scan();
        TEST_ASSERT_EQUAL_size_t(2, s.entries.size());
        TEST_ASSERT_EQUAL_UINT32(7, s.find("main.jsc")->sequence);
        TEST_ASSERT_EQUAL_UINT32(8, s.result.newest_sequence);
        TEST_ASSERT_EQUAL_UINT32(9, bcstore::nextSequence(s.result.newest_sequence));
        // The other order in the partition.
        setUp_();
        store("main.jsc", 0, 100, 0xFFFFFFFE, 2);
        store("x.jsc", 0x1000, 100, 8, 3);
        store("main.jsc", 0x2000, 100, 7, 1);
    }
}

// Two headers of one name with the same number: commit() spends a number
// even when the header's read-back fails, so only a header that landed
// although its read-back and the erase after it both failed gives this. The
// lower offset is the script, whichever the scan meets first.
void test_scan_equal_sequences() {
    store("main.jsc", 0x3000, 100, 5, 1);
    store("main.jsc", 0x1000, 200, 5, 2);
    store("main.jsc", 0x5000, 300, 5, 3);
    const Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(0x1000, s.find("main.jsc")->region);
    TEST_ASSERT_EQUAL_UINT32(2, s.result.superseded);
    TEST_ASSERT_EQUAL_UINT32(6, bcstore::nextSequence(s.result.newest_sequence));
}

void test_scan_entry_to_the_end_of_the_partition() {
    // The last sector alone, and an entry whose image ends on the last byte.
    store("last.jsc", kPartition - kSectorSize, kSectorSize - kHeaderLen, 1, 1);
    Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(kPartition, s.find("last.jsc")->regionEnd());
    TEST_ASSERT_EQUAL_UINT32(kPartition, s.find("last.jsc")->imageOffset() + s.find("last.jsc")->size);

    setUp_();
    store("whole.jsc", 0, kPartition - kHeaderLen, 1, 1);
    s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(kPartition, s.find("whole.jsc")->regionEnd());

    // One byte more does not fit: not an entry.
    setUp_();
    Entry over = entry("over.jsc", kPartition - kSectorSize, kSectorSize - kHeaderLen + 1, 1);
    uint8_t header[kHeaderLen];
    bcstore::encodeHeader(over, header);
    memcpy(flash.data() + over.region, header, kHeaderLen);
    s = scan();
    TEST_ASSERT_EQUAL_size_t(0, s.entries.size());
}

void test_scan_garbage_between_entries() {
    store("a.jsc", 0, 5000, 1, 1);                 // sectors 0..1
    for (uint32_t i = 0x2000; i < 0x6000; ++i) {   // sectors 2..5: noise
        flash[i] = static_cast<uint8_t>(i * 31 + (i >> 8));
    }
    memcpy(flash.data() + 0x3000, "FBJS", 4);      // magic, nothing else right
    memset(flash.data() + 0x5000, 0, kSectorSize); // a zeroed sector
    store("b.jsc", 0x6000, 5000, 2, 2);
    const Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(2, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(2, s.result.headers);
    TEST_ASSERT_EQUAL_UINT32(0, s.find("a.jsc")->region);
    TEST_ASSERT_EQUAL_UINT32(0x6000, s.find("b.jsc")->region);
}

// A header is only looked for outside the entries already found: bytes of
// an image that look like one are not an entry.
void test_scan_skips_the_inside_of_an_entry() {
    const Entry outer = store("main.jsc", 0, 3 * kSectorSize, 1, 1);  // sectors 0..3
    Entry fake = entry("fake.jsc", 0x2000, 100, 99);
    uint8_t header[kHeaderLen];
    bcstore::encodeHeader(fake, header);
    memcpy(flash.data() + 0x2000, header, kHeaderLen);  // inside main.jsc's image
    TEST_ASSERT_TRUE(bcstore::decodeHeader(flash.data() + 0x2000, 0x2000, kPartition, nullptr) == HeaderCheck::Ok);

    const Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_NULL(s.find("fake.jsc"));
    TEST_ASSERT_EQUAL_UINT32(1, s.result.newest_sequence);
    TEST_ASSERT_EQUAL_UINT32(outer.regionEnd(), s.find("main.jsc")->regionEnd());
}

// Replace: the new entry goes into a free region, the old one stays in
// flash as it is. When its region is taken for a later upload, the erase
// removes its header.
void test_replace_then_reuse_of_the_old_region() {
    store("main.jsc", 0, 5000, 1, 1);
    store("main.jsc", 0x2000, 5000, 2, 2);
    Scanned s = scan();
    TEST_ASSERT_EQUAL_UINT32(0x2000, s.find("main.jsc")->region);

    // Third upload: the allocator sees only the script (sequence 2).
    uint32_t region = 99;
    TEST_ASSERT_TRUE(allocate(5000, {{s.find("main.jsc")->region, s.find("main.jsc")->regionEnd()}}, &region));
    TEST_ASSERT_EQUAL_UINT32(0, region);
    memset(flash.data() + region, 0xFF, bcstore::regionSizeFor(5000));  // erase
    s = scan();                                                         // power cut here
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(2, s.find("main.jsc")->sequence);
    TEST_ASSERT_EQUAL_UINT32(3, bcstore::nextSequence(s.result.newest_sequence));

    store("main.jsc", region, 5000, 3, 3);
    s = scan();
    TEST_ASSERT_EQUAL_UINT32(0, s.find("main.jsc")->region);
    TEST_ASSERT_EQUAL_UINT32(3, s.find("main.jsc")->sequence);
}

// ---- allocation ----

void test_allocate_empty_partition() {
    uint32_t region = 99;
    TEST_ASSERT_TRUE(allocate(100, {}, &region));
    TEST_ASSERT_EQUAL_UINT32(0, region);
    TEST_ASSERT_TRUE(allocate(kPartition - kHeaderLen, {}, &region));  // the whole partition
    TEST_ASSERT_EQUAL_UINT32(0, region);
    TEST_ASSERT_FALSE(allocate(kPartition - kHeaderLen + 1, {}, &region));
    TEST_ASSERT_FALSE(allocate(0, {}, &region));
    TEST_ASSERT_FALSE(allocate(0xFFFFFFFF, {}, &region));
}

void test_allocate_counts_the_header() {
    // An image of exactly one sector needs two with its header.
    uint32_t region = 99;
    TEST_ASSERT_TRUE(allocate(kSectorSize, {{0x2000, 0x3000}}, &region));
    TEST_ASSERT_EQUAL_UINT32(0, region);  // sectors 0..1
    TEST_ASSERT_TRUE(allocate(kSectorSize, {{0x1000, 0x2000}}, &region));
    TEST_ASSERT_EQUAL_UINT32(0x2000, region);  // sector 0 alone is too small
    TEST_ASSERT_TRUE(allocate(kSectorSize - kHeaderLen, {{0x1000, 0x2000}}, &region));
    TEST_ASSERT_EQUAL_UINT32(0, region);
}

void test_allocate_lowest_gap_around_entries() {
    const std::vector<Range> used = {{0, 0x2000}, {0x3000, 0x5000}, {0x8000, 0x9000}};
    uint32_t region = 99;
    TEST_ASSERT_TRUE(allocate(1000, used, &region));
    TEST_ASSERT_EQUAL_UINT32(0x2000, region);  // the one-sector gap
    TEST_ASSERT_TRUE(allocate(5000, used, &region));
    TEST_ASSERT_EQUAL_UINT32(0x5000, region);  // two sectors: after the second entry
    TEST_ASSERT_TRUE(allocate(3 * kSectorSize - kHeaderLen, used, &region));
    TEST_ASSERT_EQUAL_UINT32(0x5000, region);  // exactly the gap 0x5000..0x8000
    TEST_ASSERT_TRUE(allocate(3 * kSectorSize, used, &region));
    TEST_ASSERT_EQUAL_UINT32(0x9000, region);  // after everything
    // Ranges count as whole sectors.
    TEST_ASSERT_TRUE(allocate(1000, {{0x0010, 0x0020}, {0x1800, 0x2100}}, &region));
    TEST_ASSERT_EQUAL_UINT32(0x3000, region);
}

void test_allocate_around_pins_and_a_reservation() {
    // main.jsc runs from 0..0x2000 (pinned) and was replaced by the entry
    // at 0x2000..0x4000; an upload is being written at 0x4000..0x6000.
    const Range pin = {0, 0x2000};
    const Range entryNow = {0x2000, 0x4000};
    const Range reservation = {0x4000, 0x6000};
    uint32_t region = 99;
    TEST_ASSERT_TRUE(allocate(5000, {entryNow, pin, reservation}, &region));
    TEST_ASSERT_EQUAL_UINT32(0x6000, region);
    // The upload is done or gone: its region is free again.
    TEST_ASSERT_TRUE(allocate(5000, {entryNow, pin}, &region));
    TEST_ASSERT_EQUAL_UINT32(0x4000, region);
    // The engine restarted (unpinAll): the old region is free.
    TEST_ASSERT_TRUE(allocate(5000, {entryNow}, &region));
    TEST_ASSERT_EQUAL_UINT32(0, region);
    // The pin table overflowed: nothing below the floor.
    TEST_ASSERT_TRUE(allocate(5000, {entryNow}, &region, 0x7000));
    TEST_ASSERT_EQUAL_UINT32(0x7000, region);
    TEST_ASSERT_TRUE(allocate(5000, {{0x7000, 0x9000}}, &region, 0x7000));
    TEST_ASSERT_EQUAL_UINT32(0x9000, region);
}

void test_allocate_full_partition() {
    uint32_t region = 99;
    // Free: only the last sector.
    TEST_ASSERT_FALSE(allocate(5000, {{0, kPartition - kSectorSize}}, &region));
    TEST_ASSERT_EQUAL_UINT32(99, region);
    TEST_ASSERT_TRUE(allocate(4000, {{0, kPartition - kSectorSize}}, &region));
    TEST_ASSERT_EQUAL_UINT32(kPartition - kSectorSize, region);
    TEST_ASSERT_FALSE(allocate(100, {{0, kPartition}}, &region));
    // A floor at the end, and a used range that ends there.
    TEST_ASSERT_FALSE(allocate(100, {}, &region, kPartition));
    TEST_ASSERT_FALSE(allocate(100, {{kPartition - kSectorSize, kPartition}}, &region, kPartition - kSectorSize));
}

// ---- migration: the JSON index of older firmware ----

const char kLegacyJson[] =
    "{\"entries\":[{\"name\":\"main.jsc\",\"offset\":8192,\"size\":21340,\"checksum\":3735928559,"
    "\"flash_addr\":6430720,\"version\":1,\"active\":true,\"app\":2864434397,\"rom\":1007681536,"
    "\"lib\":305419896},"
    "{\"name\":\"script.jsc\",\"offset\":0,\"size\":512,\"checksum\":1,\"flash_addr\":6422528,"
    "\"version\":1,\"active\":false}]}";

void test_legacy_index_is_read() {
    const auto entries = readLegacy(kLegacyJson);
    TEST_ASSERT_EQUAL_size_t(2, entries.size());
    TEST_ASSERT_EQUAL_STRING("main.jsc", entries[0].name);
    TEST_ASSERT_EQUAL_UINT32(8192, entries[0].offset);
    TEST_ASSERT_EQUAL_UINT32(21340, entries[0].size);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, entries[0].checksum);
    TEST_ASSERT_EQUAL_HEX32(0xAABBCCDD, entries[0].app_id);
    TEST_ASSERT_EQUAL_HEX32(0x3C100000, entries[0].rom_base);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, entries[0].stdlib_id);
    TEST_ASSERT_TRUE(entries[0].active);
    // An entry from before the app / rom / lib fields existed.
    TEST_ASSERT_EQUAL_STRING("script.jsc", entries[1].name);
    TEST_ASSERT_EQUAL_UINT32(512, entries[1].size);
    TEST_ASSERT_EQUAL_UINT32(0, entries[1].app_id);
    TEST_ASSERT_EQUAL_UINT32(0, entries[1].rom_base);
    TEST_ASSERT_EQUAL_UINT32(0, entries[1].stdlib_id);
    TEST_ASSERT_FALSE(entries[1].active);
}

void test_legacy_index_other_forms() {
    TEST_ASSERT_EQUAL_size_t(0, readLegacy("").size());
    TEST_ASSERT_EQUAL_size_t(0, readLegacy("{\"entries\":[]}").size());
    TEST_ASSERT_EQUAL_size_t(0, readLegacy("{\"entries\":[{\"offset\":0,\"size\":5}]}").size());  // no name
    // Pretty-printed, other field order, an unknown field with a nested value.
    const auto entries = readLegacy(
        "{\n  \"entries\": [\n    {\n      \"active\": true,\n      \"extra\": {\"name\": \"no\", \"n\": [1, 2]},\n"
        "      \"lib\": 7,\n      \"size\": 100,\n      \"name\": \"a\\\"b.jsc\"\n    }\n  ]\n}\n");
    TEST_ASSERT_EQUAL_size_t(1, entries.size());
    TEST_ASSERT_EQUAL_STRING("a\"b.jsc", entries[0].name);
    TEST_ASSERT_EQUAL_UINT32(100, entries[0].size);
    TEST_ASSERT_EQUAL_UINT32(7, entries[0].stdlib_id);
    TEST_ASSERT_TRUE(entries[0].active);
    // A file cut off in the middle of an entry (power cut while the old
    // firmware rewrote it): the complete entries only.
    std::string cut(kLegacyJson);
    cut.resize(cut.find("script.jsc") + 20);
    TEST_ASSERT_EQUAL_size_t(1, readLegacy(cut).size());
    // A name that is too long is no name.
    const std::string longName =
        "{\"entries\":[{\"name\":\"" + std::string(40, 'n') + "\",\"size\":100,\"active\":true}]}";
    TEST_ASSERT_EQUAL_size_t(0, readLegacy(longName).size());
}

void test_legacy_decisions() {
    using bcstore::LegacyAction;
    const uint32_t lib = 0x12345678;
    const auto base = readLegacy(kLegacyJson)[0];
    TEST_ASSERT_TRUE(bcstore::legacyAction(base, kPartition, lib) == LegacyAction::Migrate);
    // Another stdlib table: rebasing cannot fix its ROM atoms.
    TEST_ASSERT_TRUE(bcstore::legacyAction(base, kPartition, lib + 1) == LegacyAction::DropOtherStdlib);

    auto e = base;
    e.active = false;
    TEST_ASSERT_TRUE(bcstore::legacyAction(e, kPartition, lib) == LegacyAction::SkipInactive);
    e = base;
    e.rom_base = 0;
    TEST_ASSERT_TRUE(bcstore::legacyAction(e, kPartition, lib) == LegacyAction::DropNoRebaseInfo);
    e = base;
    e.stdlib_id = 0;
    TEST_ASSERT_TRUE(bcstore::legacyAction(e, kPartition, 0) == LegacyAction::DropNoRebaseInfo);
    e = base;
    e.offset = kPartition - 100;
    TEST_ASSERT_TRUE(bcstore::legacyAction(e, kPartition, lib) == LegacyAction::DropInvalid);
    e = base;
    e.size = 0xFFFFFFF0;
    TEST_ASSERT_TRUE(bcstore::legacyAction(e, kPartition, lib) == LegacyAction::DropInvalid);
    e = base;
    e.size = 8;
    TEST_ASSERT_TRUE(bcstore::legacyAction(e, kPartition, lib) == LegacyAction::DropInvalid);
    e = base;
    e.offset = 8193;
    TEST_ASSERT_TRUE(bcstore::legacyAction(e, kPartition, lib) == LegacyAction::DropInvalid);
    e = base;
    e.malformed = true;
    TEST_ASSERT_TRUE(bcstore::legacyAction(e, kPartition, lib) == LegacyAction::DropInvalid);
    e = base;
    e.offset = kPartition - e.size;  // ends on the last byte
    e.offset -= e.offset % 4;
    TEST_ASSERT_TRUE(bcstore::legacyAction(e, kPartition, lib) == LegacyAction::Migrate);
}

// The migration in the partition: a header-less image at a sector boundary
// is copied into a new entry that keeps off every old image; the old bytes
// are free space afterwards.
void test_legacy_image_moves_into_an_entry() {
    const auto legacy = readLegacy(kLegacyJson)[0];  // image at 0x2000, 21340 bytes
    const auto bytes = image(legacy.size, 6);
    memcpy(flash.data() + legacy.offset, bytes.data(), bytes.size());
    Scanned s = scan();
    TEST_ASSERT_EQUAL_size_t(0, s.entries.size());

    uint32_t region = 99;
    TEST_ASSERT_TRUE(allocate(legacy.size, {{legacy.offset, legacy.offset + legacy.size}}, &region));
    TEST_ASSERT_EQUAL_UINT32(0x8000, region);  // 21340 + 80 bytes do not fit below the image
    const Entry moved = store(legacy.name, region, legacy.size, bcstore::nextSequence(s.result.newest_sequence), 6);
    s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(region + kHeaderLen, s.find("main.jsc")->imageOffset());
    TEST_ASSERT_EQUAL_HEX32(moved.checksum, s.find("main.jsc")->checksum);
    // Started again after a power cut before the file was deleted: moved
    // once more, as a newer entry; the first copy is superseded.
    TEST_ASSERT_TRUE(bcstore::legacyAction(legacy, kPartition, legacy.stdlib_id) == bcstore::LegacyAction::Migrate);
    TEST_ASSERT_TRUE(allocate(legacy.size, {{legacy.offset, legacy.offset + legacy.size}, {moved.region, moved.regionEnd()}},
                              &region));
    TEST_ASSERT_EQUAL_UINT32(0xE000, region);
    store(legacy.name, region, legacy.size, bcstore::nextSequence(s.result.newest_sequence), 6);
    s = scan();
    TEST_ASSERT_EQUAL_size_t(1, s.entries.size());
    TEST_ASSERT_EQUAL_UINT32(0xE000, s.find("main.jsc")->region);
    region = 99;
    // The next upload may take the old image's place.
    TEST_ASSERT_TRUE(allocate(1000, {{moved.region, moved.regionEnd()}}, &region));
    TEST_ASSERT_EQUAL_UINT32(0, region);
}

void test_legacy_reader_nits() {
    // A nested value does not become the value of the field it belongs to,
    // nor of the next one.
    auto entries = readLegacy(
        "{\"entries\":[{\"name\":\"a.jsc\",\"size\":{\"x\":77},\"offset\":[4096],\"lib\":[9,{\"q\":\"zz\"}],"
        "\"checksum\":5,\"active\":true}]}");
    TEST_ASSERT_EQUAL_size_t(1, entries.size());
    TEST_ASSERT_EQUAL_UINT32(0, entries[0].size);
    TEST_ASSERT_EQUAL_UINT32(0, entries[0].offset);
    TEST_ASSERT_EQUAL_UINT32(0, entries[0].stdlib_id);
    TEST_ASSERT_EQUAL_UINT32(5, entries[0].checksum);
    TEST_ASSERT_TRUE(entries[0].active);
    entries = readLegacy("{\"entries\":[{\"name\":{\"name\":\"inner.jsc\"},\"size\":100}]}");
    TEST_ASSERT_EQUAL_size_t(0, entries.size());

    // Numbers: decimal digits in 32 bits, nothing else.
    for (const char* bad : {"-4", "99999999999999999999", "4294967296", "1e9", "0x10", "1.5", "true", ""}) {
        const std::string json =
            std::string("{\"entries\":[{\"name\":\"a.jsc\",\"active\":true,\"size\":100,\"rom\":1,\"lib\":1,\"offset\":") +
            bad + "}]}";
        entries = readLegacy(json);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, entries.size(), bad);
        TEST_ASSERT_TRUE_MESSAGE(entries[0].malformed, bad);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, entries[0].offset, bad);
        TEST_ASSERT_TRUE(bcstore::legacyAction(entries[0], kPartition, 1) == bcstore::LegacyAction::DropInvalid);
    }
    entries = readLegacy("{\"entries\":[{\"name\":\"a.jsc\",\"active\":true,\"size\":4294967295,\"offset\":0}]}");
    TEST_ASSERT_EQUAL_size_t(1, entries.size());
    TEST_ASSERT_FALSE(entries[0].malformed);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, entries[0].size);

    // Nesting deeper than a byte counts: no entry at depth 259 (256 + 3).
    const std::string entry = "{\"name\":\"deep.jsc\",\"active\":true,\"size\":100}";
    TEST_ASSERT_EQUAL_size_t(0, readLegacy(std::string(258, '[') + entry).size());
    TEST_ASSERT_EQUAL_size_t(0, readLegacy(std::string(300, '[') + entry).size());
    TEST_ASSERT_EQUAL_size_t(1, readLegacy(std::string(2, '[') + entry).size());
    // More closings than openings, then a document.
    TEST_ASSERT_EQUAL_size_t(2, readLegacy(std::string("}]}]") + kLegacyJson).size());
}

// ---- no bytes of an entry without its header ----

namespace {

// The registry on top of BytecodeStore.h, with flash in RAM: the same
// steps in the same order as BytecodeRegistry / BytecodeWriter (allocate,
// eraseRegion, guarded image write, commit).
struct Model {
    std::vector<Entry> entries;
    std::vector<Range> pins;
    uint32_t next = 1;
    long sectorsErased = 0;

    template <class Visit>
    void used(Visit&& visit) const {
        for (const auto& e : entries) visit(e.region, e.regionEnd());
        for (const auto& p : pins) visit(p.start, p.end);
    }
    const Entry* find(const char* name) const {
        for (const auto& e : entries) {
            if (strcmp(e.name, name) == 0) return &e;
        }
        return nullptr;
    }
    void boot() {
        entries.clear();
        pins.clear();
        next = bcstore::nextSequence(bcstore::scanPartition(flash.data(), kPartition, entries).newest_sequence);
    }
    void restart() {  // ScriptEngine::restart(): unpinAll(), then the boot script is found and pinned
        pins.clear();
        const Entry* boot = find("main.jsc");
        if (!boot) boot = find("script.jsc");
        if (boot) pins.push_back({boot->region, boot->regionEnd()});
    }
    bool alloc(uint32_t size, uint32_t* region) const {
        return bcstore::allocateRegion(size, kPartition, 0, [this](auto&& v) { used(v); }, region);
    }
    // eraseRegion(): last sector first; stops after `sectors` (a power cut)
    // when that is not negative. True when it ran to the end.
    bool erase(uint32_t region, uint32_t size, int sectors = -1) {
        const uint32_t end = bcstore::eraseEndFor(flash.data(), kPartition, region, region + bcstore::regionSizeFor(size),
                                                  [this](auto&& v) { used(v); });
        for (uint32_t sector = end; sector > region; sector -= kSectorSize) {
            if (sectors-- == 0) return false;
            memset(flash.data() + sector - kSectorSize, 0xFF, kSectorSize);
            ++sectorsErased;
        }
        return true;
    }
    // The image through the guard, in pieces as an upload brings them, up
    // to `count` bytes. False: refused, the offending piece is not written.
    static bool writeImage(uint32_t region, const std::vector<uint8_t>& bytes, uint32_t count, uint32_t piece = 1500) {
        bcstore::BoundaryGuard guard;
        for (uint32_t off = 0; off < count; off += piece) {
            const uint32_t n = count - off < piece ? count - off : piece;
            if (!guard.feed(region + kHeaderLen + off, bytes.data() + off, n)) return false;
            memcpy(flash.data() + region + kHeaderLen + off, bytes.data() + off, n);
        }
        return true;
    }
    // commit(): `prefix` < kHeaderLen bytes of the header for a torn write.
    void commit(const char* name, uint32_t region, const std::vector<uint8_t>& bytes, uint32_t prefix = kHeaderLen) {
        Entry e = entry(name, region, static_cast<uint32_t>(bytes.size()), next);
        Entry* existing = nullptr;
        for (auto& x : entries) {
            if (strcmp(x.name, name) == 0) existing = &x;
        }
        if (existing && !bcstore::sequenceNewer(e.sequence, existing->sequence)) {
            e.sequence = bcstore::nextSequence(existing->sequence);
        }
        e.checksum = bcstore::crc32(0, bytes.data(), bytes.size());
        uint8_t header[kHeaderLen];
        bcstore::encodeHeader(e, header);
        memcpy(flash.data() + region, header, prefix);
        next = bcstore::nextSequence(e.sequence);
        if (prefix < kHeaderLen) return;
        if (existing) {
            *existing = e;
        } else {
            entries.push_back(e);
        }
    }
    // A whole deploy. `reserve`: the size the region is taken for (an
    // upload knows the request's length, not the image's).
    bool deploy(const char* name, const std::vector<uint8_t>& bytes, uint32_t reserve = 0) {
        uint32_t region = 0;
        const uint32_t size = static_cast<uint32_t>(bytes.size());
        if (reserve < size) reserve = size;
        if (!alloc(reserve, &region)) return false;
        erase(region, reserve);
        if (!writeImage(region, bytes, size)) return false;
        commit(name, region, bytes);
        return true;
    }
    // What must hold at any time: the index is what a scan gives, every
    // indexed image has its checksum (`images`: the slow part), no two
    // entries overlap.
    const char* check(bool images = true) const {
        std::vector<Entry> scanned;
        bcstore::scanPartition(flash.data(), kPartition, scanned);
        if (scanned.size() != entries.size()) return "the scan finds another number of scripts than the index has";
        for (const auto& e : entries) {
            const Entry* twin = nullptr;
            for (const auto& x : scanned) {
                if (strcmp(x.name, e.name) == 0) twin = &x;
            }
            if (!twin || twin->region != e.region || twin->sequence != e.sequence) return "the scan differs from the index";
            if (images && bcstore::crc32(0, flash.data() + e.imageOffset(), e.size) != e.checksum) {
                return "an indexed image is damaged";
            }
        }
        for (size_t i = 0; i < entries.size(); ++i) {
            for (size_t j = i + 1; j < entries.size(); ++j) {
                if (entries[i].region < entries[j].regionEnd() && entries[j].region < entries[i].regionEnd()) {
                    return "two entries overlap";
                }
            }
        }
        return nullptr;
    }
};

// An image whose bytes at image offset `at` are a complete, valid header.
std::vector<uint8_t> imageWithHeaderInside(uint32_t size, uint32_t at, const char* name, uint32_t sequence) {
    std::vector<uint8_t> bytes(size, 0x11);
    const Entry fake = entry(name, 0, 100, sequence);
    uint8_t header[kHeaderLen];
    bcstore::encodeHeader(fake, header);
    memcpy(bytes.data() + at, header, kHeaderLen);
    return bytes;
}

} // namespace

void test_boundary_guard() {
    const uint8_t magic[4] = {'F', 'B', 'J', 'S'};
    std::vector<uint8_t> bytes(3 * kSectorSize, 0x22);
    // Anywhere but a sector boundary: fine.
    memcpy(bytes.data() + 100, magic, 4);
    memcpy(bytes.data() + kSectorSize - 1, magic, 4);
    memcpy(bytes.data() + kSectorSize + 1, magic, 4);
    bcstore::BoundaryGuard ok;
    TEST_ASSERT_TRUE(ok.feed(0x5000 + kHeaderLen, bytes.data() + kHeaderLen, bytes.size() - kHeaderLen));
    // On a boundary, in one piece and split at every place.
    memcpy(bytes.data() + 2 * kSectorSize, magic, 4);
    bcstore::BoundaryGuard whole;
    TEST_ASSERT_FALSE(whole.feed(0x5000 + kHeaderLen, bytes.data() + kHeaderLen, bytes.size() - kHeaderLen));
    TEST_ASSERT_FALSE(whole.ok());
    for (uint32_t split = 2 * kSectorSize - 2; split <= 2 * kSectorSize + 5; ++split) {
        bcstore::BoundaryGuard guard;
        const bool first = guard.feed(0x5000 + kHeaderLen, bytes.data() + kHeaderLen, split - kHeaderLen);
        // Only the piece that completes the magic is refused.
        TEST_ASSERT_EQUAL(split < 2 * kSectorSize + 4, first);
        TEST_ASSERT_EQUAL(false, guard.feed(0x5000 + split, bytes.data() + split, bytes.size() - split));
    }
    // Three of the four bytes: fine.
    bytes[2 * kSectorSize + 3] = 's';
    bcstore::BoundaryGuard near;
    TEST_ASSERT_TRUE(near.feed(0x5000 + kHeaderLen, bytes.data() + kHeaderLen, bytes.size() - kHeaderLen));
}

void test_erase_end() {
    auto none = [](auto&&) {};
    // Nothing there: the region itself.
    TEST_ASSERT_EQUAL_UINT32(0x2000, bcstore::eraseEndFor(flash.data(), kPartition, 0, 0x2000, none));
    // A superseded entry that starts in the region and runs past it.
    store("old.jsc", 0x1000, 3 * kSectorSize, 1, 1);  // 0x1000..0x5000
    TEST_ASSERT_EQUAL_UINT32(0x5000, bcstore::eraseEndFor(flash.data(), kPartition, 0, 0x2000, none));
    TEST_ASSERT_EQUAL_UINT32(0x5000, bcstore::eraseEndFor(flash.data(), kPartition, 0x1000, 0x2000, none));
    TEST_ASSERT_EQUAL_UINT32(0x6000, bcstore::eraseEndFor(flash.data(), kPartition, 0, 0x6000, none));
    // One that starts before or behind the region does not count.
    TEST_ASSERT_EQUAL_UINT32(0x1000, bcstore::eraseEndFor(flash.data(), kPartition, 0, 0x1000, none));
    TEST_ASSERT_EQUAL_UINT32(0x9000, bcstore::eraseEndFor(flash.data(), kPartition, 0x8000, 0x9000, none));
    // Never into a used range (only after something else wrote here).
    auto usedAt3000 = [](auto&& visit) { visit(0x3000u, 0x3800u); };
    TEST_ASSERT_EQUAL_UINT32(0x3000, bcstore::eraseEndFor(flash.data(), kPartition, 0, 0x2000, usedAt3000));
    auto usedAt1800 = [](auto&& visit) { visit(0x1800u, 0x2800u); visit(0x0u, 0x1000u); };
    TEST_ASSERT_EQUAL_UINT32(0x2000, bcstore::eraseEndFor(flash.data(), kPartition, 0x1000, 0x2000, usedAt1800));
}

// The reviewer's case. lib.jsc (3 sectors) carries a complete header for
// "main.jsc" with sequence 0xFFFFFFFE where its second sector starts. It is
// replaced; a later one-sector deploy takes its first sector. The bytes
// behind must not be left: a scan would take them for the newest main.jsc.
void test_header_inside_an_image_never_becomes_an_entry() {
    const std::vector<uint8_t> plain(9000, 0x44);
    const std::vector<uint8_t> small(1000, 0x33);
    const std::vector<uint8_t> mainImage(3000, 0x22);

    // 1. Through the writer: refused before the bytes reach flash, also
    //    when the upload stops right there.
    Model m;
    const auto evil = imageWithHeaderInside(9000, kSectorSize - kHeaderLen, "main.jsc", 0xFFFFFFFE);
    TEST_ASSERT_FALSE(m.deploy("lib.jsc", evil));
    TEST_ASSERT_NULL(m.check());
    m.boot();
    TEST_ASSERT_EQUAL_size_t(0, m.entries.size());
    TEST_ASSERT_EQUAL_UINT32(1, m.next);
    // The same bytes one off the boundary are an ordinary image.
    TEST_ASSERT_TRUE(m.deploy("lib.jsc", imageWithHeaderInside(9000, kSectorSize - kHeaderLen + 1, "main.jsc", 0xFFFFFFFE)));
    TEST_ASSERT_NULL(m.check());

    // 2. Such an entry is in flash all the same (written by something that
    //    had no guard): the erase rule removes all of it with its header.
    setUp_();
    m = Model();
    Entry lib = entry("lib.jsc", 0, 9000, 1);
    memcpy(flash.data() + lib.imageOffset(), evil.data(), evil.size());
    lib.checksum = bcstore::crc32(0, evil.data(), evil.size());
    uint8_t header[kHeaderLen];
    bcstore::encodeHeader(lib, header);
    memcpy(flash.data(), header, kHeaderLen);
    m.boot();
    TEST_ASSERT_EQUAL_size_t(1, m.entries.size());
    TEST_ASSERT_TRUE(m.deploy("main.jsc", mainImage));  // 0x3000
    TEST_ASSERT_TRUE(m.deploy("lib.jsc", small));       // 0x4000: replaces lib.jsc
    TEST_ASSERT_EQUAL_UINT32(0x4000, m.find("lib.jsc")->region);
    TEST_ASSERT_TRUE(m.deploy("x.jsc", small));         // 0: the old lib.jsc's first sector
    TEST_ASSERT_EQUAL_UINT32(0, m.find("x.jsc")->region);
    TEST_ASSERT_EQUAL_HEX8(0xFF, flash[0x1000]);        // its other sectors went with it
    TEST_ASSERT_EQUAL_HEX8(0xFF, flash[0x2FFF]);
    TEST_ASSERT_NULL(m.check());
    m.boot();
    TEST_ASSERT_EQUAL_size_t(3, m.entries.size());
    TEST_ASSERT_EQUAL_UINT32(2, m.find("main.jsc")->sequence);
    TEST_ASSERT_EQUAL_UINT32(5, m.next);
    TEST_ASSERT_NULL(m.check());

    // 3. A power cut at every sector of that erase: the old entry keeps its
    //    header until the rest of it is gone.
    for (int cut = 0; cut <= 3; ++cut) {
        setUp_();
        m = Model();
        memcpy(flash.data() + lib.imageOffset(), evil.data(), evil.size());
        memcpy(flash.data(), header, kHeaderLen);
        m.boot();
        TEST_ASSERT_TRUE(m.deploy("main.jsc", mainImage));
        TEST_ASSERT_TRUE(m.deploy("lib.jsc", small));
        uint32_t region = 99;
        TEST_ASSERT_TRUE(m.alloc(1000, &region));
        TEST_ASSERT_EQUAL_UINT32(0, region);
        TEST_ASSERT_EQUAL(cut == 3, m.erase(region, 1000, cut));
        TEST_ASSERT_NULL_MESSAGE(m.check(), "after a power cut in the erase");
        m.boot();
        TEST_ASSERT_EQUAL_size_t(2, m.entries.size());
        TEST_ASSERT_EQUAL_UINT32(2, m.find("main.jsc")->sequence);
        TEST_ASSERT_EQUAL_UINT32(4, m.next);
    }

    // 4. Without the rule (only the new region erased, as before): the
    //    header inside shows. Even then it is the older main.jsc, and
    //    deploys go on.
    setUp_();
    m = Model();
    memcpy(flash.data() + lib.imageOffset(), evil.data(), evil.size());
    memcpy(flash.data(), header, kHeaderLen);
    m.boot();
    TEST_ASSERT_TRUE(m.deploy("main.jsc", mainImage));
    TEST_ASSERT_TRUE(m.deploy("lib.jsc", small));
    memset(flash.data(), 0xFF, kSectorSize);
    m.commit("x.jsc", 0, small);
    memcpy(flash.data() + kHeaderLen, small.data(), small.size());
    m.boot();
    TEST_ASSERT_EQUAL_UINT32(2, m.find("main.jsc")->sequence);
    TEST_ASSERT_EQUAL_UINT32(0x3000, m.find("main.jsc")->region);
    TEST_ASSERT_EQUAL_UINT32(5, m.next);
    TEST_ASSERT_TRUE(m.deploy("main.jsc", plain));
    TEST_ASSERT_EQUAL_UINT32(5, m.find("main.jsc")->sequence);
}

// Allocator, erase rule, guard and scan together (after the reviewer's
// sim.cpp): random deploys of four names, uploads that stop, restarts,
// reboots and power cuts in the erase, in the image and in the header.
// Some images carry complete headers at sector boundaries of their region
// (refused) or elsewhere (stored).
void test_random_deploys_power_cuts_and_reboots() {
    const char* names[] = {"main.jsc", "script.jsc", "lib.jsc", "x.jsc"};
    long deploys = 0, refused = 0, cuts = 0;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        setUp_();
        Model m;
        m.next = seed == 6 ? 0xFFFFFFF0 : 1;  // one run across the wrap
        uint32_t rng = seed * 2654435761u;
        auto rnd = [&rng]() {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return rng;
        };
        for (int step = 0; step < 800; ++step) {
            const uint32_t op = rnd() % 100;
            if (op < 70) {
                const char* name = names[rnd() % 4];
                const uint32_t size = 16 + rnd() % (rnd() % 4 == 0 ? 120000 : 20000);
                const uint32_t reserve = size + rnd() % 500;  // the multipart overhead
                std::vector<uint8_t> bytes(size, static_cast<uint8_t>(rnd()));
                for (uint32_t i = 0; i < size; i += 61) bytes[i] = static_cast<uint8_t>(rnd());
                bool onBoundary = false;
                if (rnd() % 8 == 0 && size > 2 * kHeaderLen) {
                    // A complete header in the image, for a name in use, with a number far ahead.
                    onBoundary = rnd() % 2 == 0 && size >= kSectorSize;
                    const uint32_t at = onBoundary ? kSectorSize - kHeaderLen + kSectorSize * (rnd() % (size / kSectorSize))
                                                   : rnd() % (size - kHeaderLen);
                    if (at + kHeaderLen <= size) {
                        const auto with = imageWithHeaderInside(kHeaderLen, 0, names[rnd() % 4], m.next + 1000);
                        memcpy(bytes.data() + at, with.data(), kHeaderLen);
                    } else {
                        onBoundary = false;
                    }
                }
                uint32_t region = 0;
                if (!m.alloc(reserve, &region)) continue;
                const uint32_t cut = rnd() % 30;  // 0: in the erase, 1: in the image, 2: in the header
                if (cut == 0) {
                    m.erase(region, reserve, static_cast<int>(rnd() % 4));
                    ++cuts;
                    TEST_ASSERT_NULL_MESSAGE(m.check(), "power cut in the erase");
                    m.boot();
                } else {
                    m.erase(region, reserve);
                    const bool written = Model::writeImage(region, bytes, cut == 1 ? rnd() % size : size, 1 + rnd() % 4096);
                    if (cut == 1) {
                        ++cuts;
                        TEST_ASSERT_NULL_MESSAGE(m.check(), "power cut in the image");
                        m.boot();
                    } else if (!written) {
                        ++refused;  // the upload fails; the region stays as it is
                        TEST_ASSERT_TRUE_MESSAGE(onBoundary, "an image without a header on a boundary was refused");
                    } else {
                        TEST_ASSERT_FALSE_MESSAGE(onBoundary && size > kSectorSize, "a header on a boundary was written");
                        if (cut == 2) {
                            m.commit(name, region, bytes, rnd() % kHeaderLen);
                            ++cuts;
                            TEST_ASSERT_NULL_MESSAGE(m.check(), "power cut in the header");
                            m.boot();
                        } else {
                            m.commit(name, region, bytes);
                            ++deploys;
                            if (rnd() % 3) m.restart();
                        }
                    }
                }
            } else if (op < 80) {
                m.restart();
            } else {
                TEST_ASSERT_NULL_MESSAGE(m.check(), "before a reboot");
                m.boot();
                m.restart();
            }
            // Every step: index against scan. The images: at every reboot
            // and power cut above, and every 50th step.
            const char* wrong = m.check(step % 50 == 0);
            if (wrong) {
                char where[96];
                snprintf(where, sizeof(where), "%s (seed %u, step %d)", wrong, static_cast<unsigned>(seed), step);
                TEST_FAIL_MESSAGE(where);
            }
        }
    }
    // The run did what it is for.
    TEST_ASSERT_TRUE(deploys > 2000);
    TEST_ASSERT_TRUE(refused > 20);
    TEST_ASSERT_TRUE(cuts > 200);
}

// ---- the upload's size parameter ----

void test_upload_size_parameter() {
    uint32_t size = 99;
    // The image's size, at most what the request carries.
    TEST_ASSERT_TRUE(bcstore::parseImageSize("21340", 21540, kPartition, &size));
    TEST_ASSERT_EQUAL_UINT32(21340, size);
    TEST_ASSERT_TRUE(bcstore::parseImageSize("16", 16, kPartition, &size));
    TEST_ASSERT_EQUAL_UINT32(16, size);
    TEST_ASSERT_TRUE(bcstore::parseImageSize("262064", 262300, kPartition, &size));  // the largest image
    TEST_ASSERT_EQUAL_UINT32(kPartition - kHeaderLen, size);
    TEST_ASSERT_TRUE(bcstore::parseImageSize("0000021340", 21540, kPartition, &size));
    TEST_ASSERT_EQUAL_UINT32(21340, size);
    // The region it gives: without the framing a sector less here.
    TEST_ASSERT_EQUAL_UINT32(4096, bcstore::regionSizeFor(4016));
    TEST_ASSERT_EQUAL_UINT32(8192, bcstore::regionSizeFor(4016 + 200));

    size = 99;
    for (const char* bad : {"", " 100", "100 ", "+100", "-100", "0x100", "1e3", "100.0", "abc", "15", "0",
                            "262065", "4294967296", "99999999999", "18446744073709551616", "100\n"}) {
        TEST_ASSERT_FALSE_MESSAGE(bcstore::parseImageSize(bad, 300000, kPartition, &size), bad);
    }
    TEST_ASSERT_FALSE(bcstore::parseImageSize(nullptr, 300000, kPartition, &size));
    TEST_ASSERT_FALSE(bcstore::parseImageSize("5000", 4999, kPartition, &size));  // more than the request has
    TEST_ASSERT_FALSE(bcstore::parseImageSize("100", 300000, 64, &size));         // no partition
    TEST_ASSERT_EQUAL_UINT32(99, size);
}

// ---- migration: the old images go when the scripts have moved ----

void test_erase_free_sectors() {
    std::vector<uint32_t> erased;
    auto erase = [&erased](uint32_t sector) {
        erased.push_back(sector);
        return true;
    };
    auto none = [](auto&&) {};
    // Whole sectors of the range, last first.
    TEST_ASSERT_TRUE(bcstore::eraseFreeSectors(0x2000, 0x2000 + 9000, none, erase));
    TEST_ASSERT_EQUAL_size_t(3, erased.size());
    TEST_ASSERT_EQUAL_UINT32(0x4000, erased[0]);
    TEST_ASSERT_EQUAL_UINT32(0x3000, erased[1]);
    TEST_ASSERT_EQUAL_UINT32(0x2000, erased[2]);
    // Not a sector that something uses, also in part.
    erased.clear();
    auto used = [](auto&& visit) {
        visit(0x3800u, 0x3900u);
        visit(0x0u, 0x2000u);
    };
    TEST_ASSERT_TRUE(bcstore::eraseFreeSectors(0x1000, 0x6000, used, erase));
    TEST_ASSERT_EQUAL_size_t(3, erased.size());
    TEST_ASSERT_EQUAL_UINT32(0x5000, erased[0]);
    TEST_ASSERT_EQUAL_UINT32(0x4000, erased[1]);
    TEST_ASSERT_EQUAL_UINT32(0x2000, erased[2]);
    // A failed erase stops it.
    erased.clear();
    auto failing = [&erased](uint32_t sector) {
        erased.push_back(sector);
        return erased.size() < 2;
    };
    TEST_ASSERT_FALSE(bcstore::eraseFreeSectors(0, 0x4000, none, failing));
    TEST_ASSERT_EQUAL_size_t(2, erased.size());
    // Nothing to do.
    erased.clear();
    TEST_ASSERT_TRUE(bcstore::eraseFreeSectors(0x3000, 0x3000, none, erase));
    TEST_ASSERT_EQUAL_size_t(0, erased.size());

    uint32_t start = 9, end = 9;
    bcstore::LegacyEntry e = readLegacy(kLegacyJson)[1];  // inactive, offset 0, 512 bytes
    TEST_ASSERT_TRUE(bcstore::legacyRange(e, kPartition, &start, &end));
    TEST_ASSERT_EQUAL_UINT32(0, start);
    TEST_ASSERT_EQUAL_UINT32(0x1000, end);
    e.offset = kPartition;
    TEST_ASSERT_FALSE(bcstore::legacyRange(e, kPartition, &start, &end));
    e.offset = 0;
    e.size = 0;
    TEST_ASSERT_FALSE(bcstore::legacyRange(e, kPartition, &start, &end));
    e.size = 512;
    e.malformed = true;
    TEST_ASSERT_FALSE(bcstore::legacyRange(e, kPartition, &start, &end));
}

// The migration's order in the partition, with a power cut after every
// step. The old image (older firmware: no guard) carries a complete header
// for "main.jsc" at a sector boundary. After the migration the store has
// the moved script and nothing else, whatever the old image held.
void test_migration_erases_the_old_images() {
    const auto legacy = readLegacy(kLegacyJson)[0];  // main.jsc: image at 0x2000, 21340 bytes
    auto oldImage = image(legacy.size, 6);
    const auto fake = imageWithHeaderInside(kHeaderLen, 0, "main.jsc", 3);
    memcpy(oldImage.data() + 2 * kSectorSize, fake.data(), kHeaderLen);  // partition offset 0x4000
    const uint32_t oldCrc = bcstore::crc32(0, oldImage.data(), oldImage.size());
    auto oldIntact = [&]() { return bcstore::crc32(0, flash.data() + legacy.offset, legacy.size) == oldCrc; };

    // What migrateLegacyIndex() does, up to `steps` flash operations.
    // Returns true when it came to deleting the file.
    auto migrate = [&](Model& m, int steps) {
        m.boot();
        uint32_t start = 0, end = 0;
        TEST_ASSERT_TRUE(bcstore::legacyRange(legacy, kPartition, &start, &end));
        if (oldIntact()) {
            // A header inside an old image that has its checksum is no entry.
            for (size_t k = m.entries.size(); k-- > 0;) {
                if (m.entries[k].region >= legacy.offset && m.entries[k].region < legacy.offset + legacy.size) {
                    m.entries.erase(m.entries.begin() + static_cast<long>(k));
                }
            }
            // Move: the old image counts as used.
            m.pins.push_back({start, end});
            uint32_t region = 0;
            TEST_ASSERT_TRUE(m.alloc(legacy.size, &region));
            if (steps-- == 0) return false;
            m.erase(region, legacy.size);
            if (steps-- == 0) return false;
            Model::writeImage(region, oldImage, legacy.size);  // (rebased on the device)
            if (steps-- == 0) return false;
            m.commit(legacy.name, region, oldImage);
            m.pins.clear();
        }
        // Erase the old image where nothing uses the sectors, last first.
        bool cut = false;
        bcstore::eraseFreeSectors(start, end, [&m](auto&& v) { m.used(v); }, [&](uint32_t sector) {
            if (steps-- == 0) cut = true;
            if (cut) return false;
            memset(flash.data() + sector, 0xFF, kSectorSize);
            return true;
        });
        return !cut;
    };

    for (int steps = 0; steps < 12; ++steps) {
        setUp_();
        memcpy(flash.data() + legacy.offset, oldImage.data(), oldImage.size());
        Model m;
        m.boot();
        TEST_ASSERT_EQUAL_size_t(1, m.entries.size());  // the scan takes the header inside for main.jsc
        TEST_ASSERT_EQUAL_UINT32(0x4000, m.find("main.jsc")->region);

        const bool done = migrate(m, steps);
        // Power cut here; the file is still there unless `done`. Either the
        // moved entry is complete, or the old image is for a repeat.
        Model after;
        after.boot();
        const Entry* moved = after.find("main.jsc");
        const bool complete = moved && moved->size == legacy.size &&
                              bcstore::crc32(0, flash.data() + moved->imageOffset(), moved->size) == moved->checksum;
        TEST_ASSERT_TRUE_MESSAGE(complete || oldIntact(), "neither the new entry nor the old image");
        if (!done) {
            // The next boot starts again and gets there.
            TEST_ASSERT_TRUE(migrate(after, 100));
            after.boot();
        }
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, after.entries.size(), "after the migration");
        TEST_ASSERT_EQUAL_UINT32(legacy.size, after.find("main.jsc")->size);
        TEST_ASSERT_NULL(after.check());
        TEST_ASSERT_TRUE(after.find("main.jsc")->region >= 0x8000);
        for (uint32_t i = 0x2000; i < 0x8000; ++i) {
            if (flash[i] != 0xFF) TEST_FAIL_MESSAGE("bytes of the old image are left");
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_is_the_zlib_crc);
    RUN_TEST(test_header_layout);
    RUN_TEST(test_header_round_trip);
    RUN_TEST(test_region_size);
    RUN_TEST(test_header_rejected);
    RUN_TEST(test_scan_empty_partition);
    RUN_TEST(test_scan_finds_entries);
    RUN_TEST(test_scan_torn_deploy_keeps_the_old_script);
    RUN_TEST(test_scan_header_with_wrong_crc_is_free_space);
    RUN_TEST(test_scan_image_without_header);
    RUN_TEST(test_scan_higher_sequence_wins);
    RUN_TEST(test_scan_higher_sequence_wins_at_the_lower_offset);
    RUN_TEST(test_scan_three_versions_and_the_next_sequence);
    RUN_TEST(test_supersedes);
    RUN_TEST(test_sequence_wraps);
    RUN_TEST(test_far_off_sequence_does_not_win);
    RUN_TEST(test_scan_equal_sequences);
    RUN_TEST(test_scan_entry_to_the_end_of_the_partition);
    RUN_TEST(test_scan_garbage_between_entries);
    RUN_TEST(test_scan_skips_the_inside_of_an_entry);
    RUN_TEST(test_replace_then_reuse_of_the_old_region);
    RUN_TEST(test_allocate_empty_partition);
    RUN_TEST(test_allocate_counts_the_header);
    RUN_TEST(test_allocate_lowest_gap_around_entries);
    RUN_TEST(test_allocate_around_pins_and_a_reservation);
    RUN_TEST(test_allocate_full_partition);
    RUN_TEST(test_legacy_index_is_read);
    RUN_TEST(test_legacy_index_other_forms);
    RUN_TEST(test_legacy_decisions);
    RUN_TEST(test_legacy_image_moves_into_an_entry);
    RUN_TEST(test_legacy_reader_nits);
    RUN_TEST(test_boundary_guard);
    RUN_TEST(test_erase_end);
    RUN_TEST(test_header_inside_an_image_never_becomes_an_entry);
    RUN_TEST(test_random_deploys_power_cuts_and_reboots);
    RUN_TEST(test_upload_size_parameter);
    RUN_TEST(test_erase_free_sectors);
    RUN_TEST(test_migration_erases_the_old_images);
    return UNITY_END();
}
