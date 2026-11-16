//
// Bytecode Registry - Implementation
// The index of the scripts in the js_user partition (docs/SCRIPT-STORE.md)
//

#include "BytecodeRegistry.h"
#include <esp_log.h>
#include <esp_crc.h>
#include <esp_app_desc.h>
#include "drivers/BusAwareFileSystem.h"  // migration only (migrateLegacyIndex())

#include <cstdlib>
#include <cstring>
#include <memory>

extern "C" {
#include "lib/mquickjs/mquickjs.h"
extern const JSSTDLibraryDef js_stdlib;
}

using namespace FreeButton;

static auto TAG = "BytecodeRegistry";

namespace {
StaticSemaphore_t s_lock_buf;
SemaphoreHandle_t s_lock = nullptr;
}

BytecodeRegistry::Lock::Lock() {
    if (!s_lock) BytecodeRegistry::instance();  // creates the mutex
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

BytecodeRegistry::Lock::~Lock() {
    xSemaphoreGiveRecursive(s_lock);
}

BytecodeRegistry& BytecodeRegistry::instance() {
    // Constructed (partition mapped and scanned) on first use; never
    // destroyed, so the mapping and its virtual address stay put.
    static BytecodeRegistry* registry = [] {
        s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lock_buf);
        auto* r = new BytecodeRegistry();
        r->load();
        // Here and not later: the web server starts before the script
        // engine, and an upload must not find the old images unprotected.
        // Needs the filesystem (mounted before the first instance(), see
        // main.cpp) and the stdlib table, no JS context.
        r->migrateLegacyIndex();
        return r;
    }();
    return *registry;
}

BytecodeRegistry::BytecodeRegistry()
    : user_partition_(nullptr)
    , virtual_base_addr_(0)
    , mmap_handle_(0) {
    initPartition();
    initVirtualMapping();
}

bool BytecodeRegistry::initPartition() {
    user_partition_ = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        "js_user"
    );

    if (!user_partition_) {
        ESP_LOGE(TAG, "js_user partition not found");
        return false;
    }

    ESP_LOGI(TAG, "Found js_user partition at 0x%lx, size %lu bytes",
             user_partition_->address, user_partition_->size);

    return true;
}

bool BytecodeRegistry::load() {
    Lock lock;
    entries_.clear();
    next_sequence_ = 1;
    if (virtual_base_addr_ == 0) {
        ESP_LOGE(TAG, "js_user is not mapped: no scripts");
        return false;
    }

    // 64 headers of 80 bytes, read in place: no flash driver calls, no heap
    // but the index itself.
    const bcstore::ScanResult scan = bcstore::scanPartition(
        reinterpret_cast<const uint8_t*>(virtual_base_addr_), usableSize(), entries_);
    next_sequence_ = bcstore::nextSequence(scan.newest_sequence);

    ESP_LOGI(TAG, "%zu script(s) in js_user (%lu header(s), %lu superseded), next sequence %lu",
             entries_.size(), static_cast<unsigned long>(scan.headers),
             static_cast<unsigned long>(scan.superseded), static_cast<unsigned long>(next_sequence_));
    return true;
}

// The commit point. Everything before it wrote only to a region without a
// header (free space to a scan); the header makes it the script of `name`.
// A header torn by a power cut fails its CRC: still free space.
BytecodeRegistry::CommitResult BytecodeRegistry::commit(const char* name, uint32_t region, uint32_t size,
                                                        uint32_t checksum) {
    Lock lock;
    if (!user_partition_ || virtual_base_addr_ == 0 || !name || name[0] == '\0' ||
        strlen(name) >= sizeof(BytecodeEntry::name)) {
        ESP_LOGE(TAG, "Invalid name");
        return CommitResult::Refused;
    }

    BytecodeEntry* existing = nullptr;
    for (auto& entry : entries_) {
        if (strcmp(entry.name, name) == 0) existing = &entry;
    }

    BytecodeEntry entry;
    strncpy(entry.name, name, sizeof(entry.name) - 1);  // the rest stays 0
    entry.region = region;
    entry.size = size;
    entry.checksum = checksum;
    // Newer than every header the scan saw, and in any case than the entry
    // it replaces (a header with a number far off does not count as newest).
    entry.sequence = next_sequence_;
    if (existing && !bcstore::sequenceNewer(entry.sequence, existing->sequence)) {
        entry.sequence = bcstore::nextSequence(existing->sequence);
    }
    entry.app_id = bytecode_current_app_id();
    entry.rom_base = reinterpret_cast<uint32_t>(js_stdlib.stdlib_table);
    entry.stdlib_id = bytecode_stdlib_id();

    uint8_t header[bcstore::kHeaderLen];
    uint8_t readback[bcstore::kHeaderLen];
    bcstore::encodeHeader(entry, header);
    // What a scan will accept: a region or size that it would refuse is
    // not programmed.
    if (bcstore::decodeHeader(header, region, usableSize(), nullptr) != bcstore::HeaderCheck::Ok) {
        ESP_LOGE(TAG, "'%s': no valid entry at 0x%lx (%lu bytes)", name,
                 static_cast<unsigned long>(region), static_cast<unsigned long>(size));
        return CommitResult::Refused;
    }

    const esp_err_t err = esp_partition_write(user_partition_, region, header, sizeof(header));
    // The number is spent, also when the header did not land: it may be in
    // flash without us knowing (see below), and no later header may repeat it.
    next_sequence_ = bcstore::nextSequence(entry.sequence);
    // Decided by what is in flash: that is what the next boot finds.
    if (esp_partition_read(user_partition_, region, readback, sizeof(readback)) != ESP_OK ||
        memcmp(header, readback, sizeof(header)) != 0) {
        ESP_LOGE(TAG, "'%s': header not written (0x%x)", name, err);
        // The header may be whole in flash although the read failed. The
        // region is still only reserved (nothing runs from it, no index
        // names it): erase the header's sector so that the next boot agrees
        // with this index. If that fails too and the header is valid, the
        // next boot takes it as the script: it is complete and verified.
        // (The image stays behind without a header: BoundaryGuard saw it.)
        esp_partition_erase_range(user_partition_, region, SECTOR_SIZE);
        return CommitResult::Failed;
    }

    // The old entry of this name is free space from here on (to a scan: the
    // older sequence number); its region is reused once nothing runs from it.
    if (existing) {
        *existing = entry;
    } else {
        entries_.push_back(entry);
    }
    ESP_LOGI(TAG, "%s '%s': region 0x%lx, %lu bytes, sequence %lu", existing ? "Replaced" : "Added", name,
             static_cast<unsigned long>(region), static_cast<unsigned long>(size),
             static_cast<unsigned long>(entry.sequence));
    return CommitResult::Ok;
}

// Erases the region taken for an image of `size` bytes from the last sector
// down, and with it the whole region of every superseded entry whose header
// lies there (BytecodeStore.h, first rule): after a power cut at any sector
// such an entry still has its header or is gone entirely.
bool BytecodeRegistry::eraseRegion(uint32_t region, size_t size) {
    Lock lock;
    const uint32_t need = bcstore::regionSizeFor(static_cast<uint32_t>(size));
    if (!user_partition_ || virtual_base_addr_ == 0 || need == 0 || region % SECTOR_SIZE != 0 ||
        region > usableSize() || need > usableSize() - region) {
        return false;
    }
    auto used = [this](auto&& visit) { forEachUsed(visit); };
    const uint32_t end = bcstore::eraseEndFor(reinterpret_cast<const uint8_t*>(virtual_base_addr_), usableSize(),
                                              region, region + need, used);
    for (uint32_t sector = end; sector > region; sector -= SECTOR_SIZE) {
        if (esp_partition_erase_range(user_partition_, sector - SECTOR_SIZE, SECTOR_SIZE) != ESP_OK) {
            ESP_LOGE(TAG, "Erase failed at 0x%lx", static_cast<unsigned long>(sector - SECTOR_SIZE));
            return false;
        }
    }
    return true;
}

const BytecodeEntry* BytecodeRegistry::find(const char* name) const {
    Lock lock;
    if (!name) return nullptr;

    for (const auto& entry : entries_) {
        if (strcmp(entry.name, name) == 0) {
            return &entry;
        }
    }

    return nullptr;
}

void BytecodeRegistry::rebaseStale() {
    Lock lock;
    const uint32_t app = bytecode_current_app_id();
    const uint32_t lib = bytecode_stdlib_id();
    // By index with a copy: commit() replaces the entry in place.
    for (size_t i = 0; i < entries_.size(); ++i) {
        const BytecodeEntry entry = entries_[i];
        if (entry.app_id == app && !mappingMoved(entry)) {
            continue;
        }
        if (entry.stdlib_id != lib) {
            ESP_LOGW(TAG, "'%s' was uploaded for a firmware build with another stdlib "
                          "(%08lx, running %08lx): deploy it again",
                     entry.name, static_cast<unsigned long>(entry.stdlib_id),
                     static_cast<unsigned long>(lib));
            continue;
        }
        // Not Done: the entry stays as it was (stale, or tried again at the
        // next boot).
        moveImage(entry.name, entry.imageOffset(), entry.size, entry.checksum, entry.rom_base);
    }
}

// Lock held. The bytecode's own pointers were relocated for the virtual
// address js_user had at upload time; another mapping order at boot (e.g. a
// different set of memory-mapped media files before it) moves that address.
bool BytecodeRegistry::mappingMoved(const BytecodeEntry& entry) const {
    constexpr uint32_t kJsHeader = sizeof(JSBytecodeHeader);
    if (virtual_base_addr_ == 0 || entry.size <= kJsHeader || entry.imageOffset() + entry.size > usableSize()) {
        return false;
    }
    const auto* hdr = reinterpret_cast<const JSBytecodeHeader*>(virtual_base_addr_ + entry.imageOffset());
    return hdr->base_addr != virtual_base_addr_ + entry.imageOffset() + kJsHeader;
}

// Lock held. Streams the source image (mapped flash, read-only) through a
// 4 KB window into a new region: no JS context, no copy of the whole image.
// Write sequence (see the class comment): region erased, image written
// behind the place of the header, read back, then commit(). A power cut or
// an error before commit() leaves the source as the script of that name.
BytecodeRegistry::MoveResult BytecodeRegistry::moveImage(const char* name, uint32_t image, uint32_t size,
                                                         uint32_t checksum, uint32_t romBase) {
    constexpr uint32_t kWindow = 4096;
    constexpr uint32_t kJsHeader = sizeof(JSBytecodeHeader);
    if (user_partition_ == nullptr || virtual_base_addr_ == 0 || size <= kJsHeader || image > usableSize() ||
        size > usableSize() - image) {
        return MoveResult::Dropped;
    }
    const auto* src = reinterpret_cast<const uint8_t*>(virtual_base_addr_ + image);
    if (esp_crc32_le(0, src, size) != checksum) {
        ESP_LOGE(TAG, "'%s': checksum mismatch, not rebased: deploy it again", name);
        return MoveResult::Dropped;
    }
    const auto* hdr = reinterpret_cast<const JSBytecodeHeader*>(src);

    uint32_t to = 0;
    if (!allocate(size, &to)) {
        if (has_reservation_) {
            // The upload in progress may be what takes the room.
            ESP_LOGW(TAG, "'%s': no free region while an upload is in progress, not rebased now", name);
            return MoveResult::Retry;
        }
        ESP_LOGE(TAG, "'%s': no free region of %lu bytes to rebase it into",
                 name, static_cast<unsigned long>(size));
        return MoveResult::Dropped;
    }
    if (!reserve(to, size)) {
        ESP_LOGW(TAG, "'%s': an upload is in progress, not rebased now", name);
        return MoveResult::Retry;
    }
    struct Release {
        BytecodeRegistry* r;
        ~Release() { r->releaseReservation(); }
    } release{this};

    // The image lies behind the entry's header.
    const uint32_t toImage = to + bcstore::kHeaderLen;
    const uintptr_t newBase = virtual_base_addr_ + toImage + kJsHeader;
    const uintptr_t romNow = reinterpret_cast<uintptr_t>(js_stdlib.stdlib_table);
    const JSRebaseRange ranges[2] = {
        // the bytecode's own pointers: to the new region
        {hdr->base_addr, hdr->base_addr + (size - kJsHeader), newBase - hdr->base_addr},
        // the ROM atoms: by the stdlib table's displacement
        {romBase, romBase + js_stdlib.stdlib_table_len * sizeof(JSWord), romNow - romBase},
    };

    // Header sector included: the place of the header reads 0xFF until commit().
    if (!eraseRegion(to, size)) {
        ESP_LOGE(TAG, "'%s': erase failed, not rebased", name);
        return MoveResult::Retry;
    }
    std::unique_ptr<uint8_t, decltype(&free)> buf(static_cast<uint8_t*>(malloc(kWindow)), &free);
    if (!buf) {
        ESP_LOGE(TAG, "'%s': no memory to rebase it", name);
        return MoveResult::Retry;
    }
    uint32_t crc = 0;
    bcstore::BoundaryGuard guard;
    for (uint32_t off = 0; off < size; off += kWindow) {
        const uint32_t n = size - off < kWindow ? size - off : kWindow;
        memcpy(buf.get(), src + off, n);
        if (JS_RebaseBytecodeWindow(src, size, buf.get(), off, n, ranges, 2, newBase) != 0) {
            ESP_LOGE(TAG, "'%s': bytecode points outside itself and the stdlib, not rebased: "
                          "deploy it again", name);
            return MoveResult::Dropped;
        }
        if (!guard.feed(toImage + off, buf.get(), n)) {
            ESP_LOGE(TAG, "'%s': the image would put the store's header mark at a sector boundary, "
                          "not rebased: deploy it again", name);
            return MoveResult::Dropped;
        }
        if (esp_partition_write(user_partition_, toImage + off, buf.get(), n) != ESP_OK) {
            ESP_LOGE(TAG, "'%s': write failed, not rebased", name);
            return MoveResult::Retry;
        }
        crc = esp_crc32_le(crc, buf.get(), n);
    }
    // Read back what landed in flash.
    uint32_t check = 0;
    for (uint32_t off = 0; off < size; off += kWindow) {
        const uint32_t n = size - off < kWindow ? size - off : kWindow;
        if (esp_partition_read(user_partition_, toImage + off, buf.get(), n) != ESP_OK) {
            return MoveResult::Retry;
        }
        check = esp_crc32_le(check, buf.get(), n);
    }
    if (check != crc) {
        ESP_LOGE(TAG, "'%s': read back differs, not rebased", name);
        return MoveResult::Retry;
    }
    // Commit point: the new entry supersedes the source (a store entry: by
    // its sequence number; its region is free again, nothing runs from it yet).
    switch (commit(name, to, size, crc)) {
        case CommitResult::Ok:
            break;
        case CommitResult::Refused:
            return MoveResult::Dropped;
        case CommitResult::Failed:
            return MoveResult::Retry;
    }
    ESP_LOGI(TAG, "'%s' rebased for this firmware build (stdlib table moved by %ld bytes, "
                  "now at offset 0x%lx)",
             name, static_cast<long>(romNow - romBase), static_cast<unsigned long>(to));
    return MoveResult::Done;
}

// ---------------------------------------------------------------------------
// MIGRATION from firmware that kept the index in LittleFS and stored images
// without a header (docs/SCRIPT-STORE.md, "Migration"). Runs at boot while
// /js_bytecode_meta.json exists; remove it together with LegacyIndexReader /
// legacyAction in BytecodeStore.h and legacy_used_ once no device has that
// file any more.
//
// A header-less image starts on a sector boundary, so there is no room for
// a header in front of it: each script is copied into a free region as a
// new entry with moveImage(), the way rebaseStale() moves one (which also
// rebases it for this firmware build). Then the file is deleted.
//
// A store entry of the same name is superseded: the file only exists when
// older firmware stored a script after the store was made (downgrade,
// deploy, upgrade), so an image of the file whose checksum is right is the
// newer one. An old index put back by a filesystem restore names images
// that are gone or overwritten: their checksums fail and they are dropped.
//
// Power cut: before a script's commit() the file and the old image are
// intact and the next boot starts again; after it the next boot moves the
// image once more (a newer entry of the same content), then deletes the
// file. The old images are not touched while scripts move (allocate() and
// eraseRegion() keep off them through legacy_used_). When all have moved,
// the old images are erased where nothing else uses the sectors, and only
// then the file is deleted (see the end of the function).
//
// Called from instance(), before the web server can take an upload.
// ---------------------------------------------------------------------------
void BytecodeRegistry::migrateLegacyIndex() {
    static constexpr char kLegacyIndexPath[] = "/js_bytecode_meta.json";
    static constexpr int kMaxLegacy = 8;  // the old index rarely had more than two

    Lock lock;
    auto& fs = getBusAwareFileSystem();
    if (!fs.exists(kLegacyIndexPath)) {
        return;
    }
    ESP_LOGI(TAG, "Index file of older firmware found: moving its scripts into the store");

    bcstore::LegacyEntry legacy[kMaxLegacy];
    int count = 0;
    int dropped = 0;
    if (auto file = fs.open(kLegacyIndexPath, FILE_READ)) {
        bcstore::LegacyIndexReader reader;
        uint8_t chunk[64];
        size_t got;
        while ((got = file.read(chunk, sizeof(chunk))) > 0) {
            for (size_t i = 0; i < got; ++i) {
                if (!reader.feed(static_cast<char>(chunk[i]))) continue;
                if (count < kMaxLegacy) {
                    legacy[count++] = reader.entry();
                } else {
                    ++dropped;
                    ESP_LOGW(TAG, "'%s' dropped: more than %d entries in the old index", reader.entry().name,
                             kMaxLegacy);
                }
            }
        }
        file.close();
    }

    const uint32_t lib = bytecode_stdlib_id();
    // The images still to move are in nobody's index: keep new entries off them.
    Range used[kMaxLegacy];
    uint8_t usedCount = 0;
    for (int i = 0; i < count; ++i) {
        if (bcstore::legacyAction(legacy[i], usableSize(), lib) == bcstore::LegacyAction::Migrate) {
            used[usedCount++] = {legacy[i].offset, legacy[i].offset + legacy[i].size};
        }
    }
    legacy_used_ = used;
    legacy_used_count_ = usedCount;

    // The scan read these images' bytes too: a "header" at a sector boundary
    // inside an image that still has its checksum is part of that image,
    // not an entry (older firmware wrote images unguarded).
    const auto* mapped = reinterpret_cast<const uint8_t*>(virtual_base_addr_);
    for (int i = 0; i < count && mapped != nullptr; ++i) {
        const bcstore::LegacyEntry& e = legacy[i];
        if (bcstore::legacyAction(e, usableSize(), lib) != bcstore::LegacyAction::Migrate ||
            esp_crc32_le(0, mapped + e.offset, e.size) != e.checksum) {
            continue;
        }
        for (size_t k = entries_.size(); k-- > 0;) {
            if (entries_[k].region >= e.offset && entries_[k].region < e.offset + e.size) {
                ESP_LOGW(TAG, "'%s' at 0x%lx lies inside the old image '%s': not an entry", entries_[k].name,
                         static_cast<unsigned long>(entries_[k].region), e.name);
                entries_.erase(entries_.begin() + k);
            }
        }
    }

    bool retry = false;
    for (int i = 0; i < count; ++i) {
        const bcstore::LegacyEntry& e = legacy[i];
        switch (bcstore::legacyAction(e, usableSize(), lib)) {
            case bcstore::LegacyAction::Migrate:
                switch (moveImage(e.name, e.offset, e.size, e.checksum, e.rom_base)) {
                    case MoveResult::Done:
                        break;
                    case MoveResult::Dropped:  // moveImage() logged why
                        ++dropped;
                        ESP_LOGW(TAG, "'%s' dropped: not moved into the store", e.name);
                        break;
                    case MoveResult::Retry:
                        retry = true;
                        break;
                }
                break;
            case bcstore::LegacyAction::SkipInactive:
                break;
            case bcstore::LegacyAction::DropInvalid:
                ++dropped;
                ESP_LOGW(TAG, "'%.31s' dropped: its entry in the old index is not usable", e.name);
                break;
            case bcstore::LegacyAction::DropNoRebaseInfo:
                ++dropped;
                ESP_LOGW(TAG, "'%s' dropped: uploaded before rebasing existed, it cannot be moved", e.name);
                break;
            case bcstore::LegacyAction::DropOtherStdlib:
                ++dropped;
                ESP_LOGW(TAG, "'%s' dropped: uploaded for a firmware build with another stdlib "
                              "(%08lx, running %08lx)",
                         e.name, static_cast<unsigned long>(e.stdlib_id), static_cast<unsigned long>(lib));
                break;
        }
    }
    legacy_used_ = nullptr;
    legacy_used_count_ = 0;

    if (retry) {
        ESP_LOGW(TAG, "Old index kept: tried again at the next boot");
        return;
    }
    if (dropped) {
        ESP_LOGW(TAG, "%d script(s) of the old index dropped (reasons above): deploy them again. "
                      "The old index is deleted.", dropped);
    }

    // Every script that could move is a complete entry now. The old images
    // are free space, but written without the BoundaryGuard: erase what is
    // not in use (last sector first), then delete the file. A power cut in
    // between: the file is still there, the old images fail their checksums
    // at the next boot (the entries made above stay), and this runs again.
    auto usedNow = [this](auto&& visit) { forEachUsed(visit); };
    auto eraseSector = [this, mapped](uint32_t sector) {
        bool blank = true;
        for (uint32_t i = 0; i < SECTOR_SIZE && blank; i += 4) {
            blank = bcstore::get32(mapped + sector + i) == UINT32_MAX;
        }
        return blank || esp_partition_erase_range(user_partition_, sector, SECTOR_SIZE) == ESP_OK;
    };
    for (int i = 0; i < count && mapped != nullptr; ++i) {
        uint32_t start = 0;
        uint32_t end = 0;
        if (!bcstore::legacyRange(legacy[i], usableSize(), &start, &end)) continue;
        if (!bcstore::eraseFreeSectors(start, end, usedNow, eraseSector)) {
            ESP_LOGW(TAG, "Old images not erased, old index kept: tried again at the next boot");
            return;
        }
    }
    if (!fs.remove(kLegacyIndexPath)) {
        ESP_LOGW(TAG, "Old index not deleted: tried again at the next boot");
    }
}
// ------------------------------ end of MIGRATION ---------------------------

// Whole sectors of the partition, at most what is mapped.
uint32_t BytecodeRegistry::usableSize() const {
    if (!user_partition_) return 0;
    const uint32_t size = user_partition_->size < MAX_PARTITION_SIZE ? user_partition_->size : MAX_PARTITION_SIZE;
    return bcstore::alignDown(size, SECTOR_SIZE);
}

// Used: the region of every entry in the index (the old version of a name
// stays in the index until the new one is committed), the pins, the upload
// in progress and, during the migration, the images of the old index. An
// entry that was superseded is in none of these once it is unpinned, so
// its region is handed out again; erasing that region removes its header.
template <class Visit>
void BytecodeRegistry::forEachUsed(Visit&& visit) const {
    for (const auto& entry : entries_) visit(entry.region, entry.regionEnd());
    for (int i = 0; i < pin_count_; ++i) visit(pins_[i].start, pins_[i].end);
    if (has_reservation_) visit(reservation_.start, reservation_.end);
    for (int i = 0; i < legacy_used_count_; ++i) visit(legacy_used_[i].start, legacy_used_[i].end);
}

bool BytecodeRegistry::allocate(size_t size, uint32_t* region) const {
    Lock lock;
    if (size > usableSize()) return false;
    auto used = [this](auto&& visit) { forEachUsed(visit); };
    return bcstore::allocateRegion(static_cast<uint32_t>(size), usableSize(), pin_floor_, used, region);
}

bool BytecodeRegistry::reserve(uint32_t region, size_t size) {
    Lock lock;
    if (has_reservation_) return false;
    reservation_ = {region, region + bcstore::regionSizeFor(static_cast<uint32_t>(size))};
    has_reservation_ = true;
    return true;
}

void BytecodeRegistry::releaseReservation() {
    Lock lock;
    has_reservation_ = false;
}

void BytecodeRegistry::pin(uint32_t offset, uint32_t size) {
    Lock lock;
    const uint32_t end = offset + size;
    for (int i = 0; i < pin_count_; ++i) {
        if (pins_[i].start == offset && pins_[i].end == end) return;
    }
    if (pin_count_ < kMaxPins) {
        pins_[pin_count_++] = {offset, end};
        return;
    }
    // Table full: never hand out anything below the highest pinned byte.
    uint32_t top = bcstore::alignUp(end, SECTOR_SIZE);
    for (int i = 0; i < pin_count_; ++i) {
        const uint32_t e = bcstore::alignUp(pins_[i].end, SECTOR_SIZE);
        if (e > top) top = e;
    }
    if (top > pin_floor_) pin_floor_ = top;
    ESP_LOGW(TAG, "Pin table full; uploads go above 0x%lx until the script restarts", (unsigned long)pin_floor_);
}

void BytecodeRegistry::unpinAll() {
    Lock lock;
    if (pin_count_ || pin_floor_) {
        ESP_LOGI(TAG, "Released %d pinned region(s)", static_cast<int>(pin_count_));
    }
    pin_count_ = 0;
    pin_floor_ = 0;
}

bool BytecodeRegistry::isBootScript(const char* name) const {
    Lock lock;
    if (!name) return false;
    // `name` counts as present: the first candidate is then `name` itself,
    // unless a name that goes before it is stored.
    const char* boot = bootScriptName([&](const char* candidate) {
        return strcmp(candidate, name) == 0 || find(candidate) != nullptr;
    });
    return boot != nullptr && strcmp(boot, name) == 0;
}

bool BytecodeRegistry::initVirtualMapping() {
    if (!user_partition_) {
        ESP_LOGE(TAG, "Cannot init virtual mapping: partition not initialized");
        return false;
    }

    // Map entire partition (256KB) permanently
    const void* mapped_ptr = nullptr;
    esp_err_t err = esp_partition_mmap(
        user_partition_,
        0,                      // offset 0
        MAX_PARTITION_SIZE,     // 256KB
        ESP_PARTITION_MMAP_DATA,
        &mapped_ptr,
        &mmap_handle_
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap partition: 0x%x", err);
        return false;
    }

    virtual_base_addr_ = reinterpret_cast<uint32_t>(mapped_ptr);

    ESP_LOGI(TAG, "Partition permanently mapped to virtual address 0x%lx", virtual_base_addr_);

    return true;
}

uint32_t BytecodeRegistry::getVirtualBaseAddress() const {
    return virtual_base_addr_;
}

// C interface for mquickjs binding
extern "C" {

uint32_t bytecode_current_app_id(void) {
    const uint8_t* sha = esp_app_get_description()->app_elf_sha256;
    const uint32_t id = (uint32_t(sha[0]) << 24) | (uint32_t(sha[1]) << 16) | (uint32_t(sha[2]) << 8) | sha[3];
    return id ? id : 1;  // never 0
}

uint32_t bytecode_stdlib_rom_base(void) {
    return reinterpret_cast<uint32_t>(js_stdlib.stdlib_table);
}

uint32_t bytecode_stdlib_atoms_id(void) {
    static uint32_t cached = 0;
    if (cached == 0) {
        cached = JS_StdlibAtomsId(&js_stdlib);
    }
    return cached;
}

uint32_t bytecode_stdlib_id(void) {
    static uint32_t cached = 0;
    if (cached != 0) {
        return cached;
    }
    // The table holds tagged pointers to itself (JS_ROM_VALUE): count them
    // as offsets from its start, so its position does not change the id.
    const JSWord* table = js_stdlib.stdlib_table;
    const uintptr_t start = reinterpret_cast<uintptr_t>(table);
    const uintptr_t end = start + js_stdlib.stdlib_table_len * sizeof(JSWord);
    const uint32_t layout[] = {js_stdlib.stdlib_table_len, js_stdlib.sorted_atoms_offset,
                               js_stdlib.global_object_offset, js_stdlib.class_count};
    uint32_t crc = esp_crc32_le(0, reinterpret_cast<const uint8_t*>(layout), sizeof(layout));
    for (uint32_t i = 0; i < js_stdlib.stdlib_table_len; ++i) {
        uint32_t w = table[i];
        if (w >= start && w < end + sizeof(JSWord)) {
            w -= start;
        }
        crc = esp_crc32_le(crc, reinterpret_cast<const uint8_t*>(&w), sizeof(w));
    }
    cached = crc ? crc : 1;  // 0 means "unknown" in the old JSON index
    return cached;
}

bool bytecode_registry_find(const char* name, BytecodeEntry* entry) {
    auto& registry = BytecodeRegistry::instance();
    BytecodeRegistry::Lock lock;
    const BytecodeEntry* found = registry.find(name);
    if (!found) return false;
    // Bytecode relocated for another app build points at that build's ROM
    // atoms: running it reads garbage (seen on btn_a00001 after an OTA).
    if (found->app_id != bytecode_current_app_id()) {
        ESP_LOGE(TAG, "'%s' was uploaded for another firmware build (app %08lx, running %08lx): "
                      "not running it; deploy the script again",
                 name, static_cast<unsigned long>(found->app_id),
                 static_cast<unsigned long>(bytecode_current_app_id()));
        return false;
    }
    // The caller maps and runs it in place: keep the entry's region (header
    // and image) while the context lives (unpinAll()).
    registry.pin(found->region, found->regionSize());
    if (entry) *entry = *found;
    return true;
}

} // extern "C"
