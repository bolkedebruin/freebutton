//
// Bytecode Registry - Implementation
// Manages user-uploaded JavaScript bytecode metadata
//

#include "BytecodeRegistry.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_crc.h>
#include <esp_app_desc.h>
#include "drivers/BusAwareFileSystem.h"

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
    // Constructed (partition mapped, metadata loaded) on first use; never
    // destroyed, so the mapping and its virtual address stay put.
    static BytecodeRegistry* registry = [] {
        s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lock_buf);
        auto* r = new BytecodeRegistry();
        r->load();
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

    if (!loadFromFile(METADATA_PATH)) {
        ESP_LOGW(TAG, "No metadata file found or failed to load, starting fresh");
        return true;  // Not an error, just empty
    }

    ESP_LOGI(TAG, "Loaded %zu entries, next offset: 0x%lx",
             entries_.size(), getNextOffset());

    return true;
}

bool BytecodeRegistry::save() {
    Lock lock;
    return saveToFile(METADATA_PATH);
}

bool BytecodeRegistry::add(const char* name, uint32_t offset, uint32_t size,
                           uint32_t checksum, uint32_t flash_addr) {
    Lock lock;
    if (!name || strlen(name) >= 32) {
        ESP_LOGE(TAG, "Invalid name");
        return false;
    }

    // Check if name already exists
    for (auto& entry : entries_) {
        if (strcmp(entry.name, name) == 0) {
            // Update existing entry
            entry.offset = offset;
            entry.size = size;
            entry.checksum = checksum;
            entry.flash_addr = flash_addr;
            entry.active = true;
            entry.version = 1;
            entry.app_id = bytecode_current_app_id();
            entry.rom_base = reinterpret_cast<uint32_t>(js_stdlib.stdlib_table);
            entry.stdlib_id = bytecode_stdlib_id();
            ESP_LOGI(TAG, "Updated entry '%s'", name);
            return true;
        }
    }

    // Add new entry
    BytecodeEntry entry;
    strncpy(entry.name, name, sizeof(entry.name) - 1);
    entry.name[sizeof(entry.name) - 1] = '\0';
    entry.offset = offset;
    entry.size = size;
    entry.checksum = checksum;
    entry.flash_addr = flash_addr;
    entry.active = true;
    entry.version = 1;
    entry.app_id = bytecode_current_app_id();
    entry.rom_base = reinterpret_cast<uint32_t>(js_stdlib.stdlib_table);
    entry.stdlib_id = bytecode_stdlib_id();

    entries_.push_back(entry);

    ESP_LOGI(TAG, "Added entry '%s' at offset 0x%lx, size %lu bytes",
             name, offset, size);

    return true;
}

const BytecodeEntry* BytecodeRegistry::find(const char* name) const {
    Lock lock;
    if (!name) return nullptr;

    for (const auto& entry : entries_) {
        if (entry.active && strcmp(entry.name, name) == 0) {
            return &entry;
        }
    }

    return nullptr;
}

void BytecodeRegistry::rebaseStale() {
    Lock lock;
    const uint32_t app = bytecode_current_app_id();
    const uint32_t lib = bytecode_stdlib_id();
    bool changed = false;
    // By index with a copy: rebaseEntry() updates the entry in place (add()).
    for (size_t i = 0; i < entries_.size(); ++i) {
        const BytecodeEntry entry = entries_[i];
        if (!entry.active || (entry.app_id == app && !mappingMoved(entry))) {
            continue;
        }
        if (entry.rom_base == 0 || entry.stdlib_id == 0) {
            ESP_LOGW(TAG, "'%s' was uploaded for another firmware build, before rebasing "
                          "existed: deploy it again", entry.name);
            continue;
        }
        if (entry.stdlib_id != lib) {
            ESP_LOGW(TAG, "'%s' was uploaded for a firmware build with another stdlib "
                          "(%08lx, running %08lx): deploy it again",
                     entry.name, static_cast<unsigned long>(entry.stdlib_id),
                     static_cast<unsigned long>(lib));
            continue;
        }
        changed |= rebaseEntry(entry);
    }
    if (changed && !save()) {
        ESP_LOGE(TAG, "Failed to save the registry after rebasing");
    }
}

// Lock held. The bytecode's own pointers were relocated for the virtual
// address js_user had at upload time; another mapping order at boot (e.g. a
// different set of memory-mapped media files before it) moves that address.
bool BytecodeRegistry::mappingMoved(const BytecodeEntry& entry) const {
    constexpr uint32_t kHeader = sizeof(JSBytecodeHeader);
    if (virtual_base_addr_ == 0 || entry.size <= kHeader || entry.offset + entry.size > usableSize()) {
        return false;
    }
    const auto* hdr = reinterpret_cast<const JSBytecodeHeader*>(virtual_base_addr_ + entry.offset);
    return hdr->base_addr != virtual_base_addr_ + entry.offset + kHeader;
}

// Lock held. Streams the old image (mapped flash, read-only) through a 4 KB
// window into a new region: no JS context, no copy of the whole image.
bool BytecodeRegistry::rebaseEntry(const BytecodeEntry& old) {
    constexpr uint32_t kWindow = 4096;
    constexpr uint32_t kHeader = sizeof(JSBytecodeHeader);
    if (user_partition_ == nullptr || virtual_base_addr_ == 0 || old.size <= kHeader ||
        old.offset + old.size > usableSize()) {
        return false;
    }
    const auto* src = reinterpret_cast<const uint8_t*>(virtual_base_addr_ + old.offset);
    if (esp_crc32_le(0, src, old.size) != old.checksum) {
        ESP_LOGE(TAG, "'%s': checksum mismatch, not rebased: deploy it again", old.name);
        return false;
    }
    const auto* hdr = reinterpret_cast<const JSBytecodeHeader*>(src);

    uint32_t to = 0;
    if (!allocate(old.size, &to) || !reserve(to, old.size)) {
        ESP_LOGE(TAG, "'%s': no free region of %lu bytes to rebase it into",
                 old.name, static_cast<unsigned long>(old.size));
        return false;
    }
    struct Release {
        BytecodeRegistry* r;
        ~Release() { r->releaseReservation(); }
    } release{this};

    const uintptr_t newBase = virtual_base_addr_ + to + kHeader;
    const uintptr_t romNow = reinterpret_cast<uintptr_t>(js_stdlib.stdlib_table);
    const JSRebaseRange ranges[2] = {
        // the bytecode's own pointers: to the new region
        {hdr->base_addr, hdr->base_addr + (old.size - kHeader), newBase - hdr->base_addr},
        // the ROM atoms: by the stdlib table's displacement
        {old.rom_base, old.rom_base + js_stdlib.stdlib_table_len * sizeof(JSWord), romNow - old.rom_base},
    };

    const uint32_t erase = (old.size + SECTOR_SIZE - 1) / SECTOR_SIZE * SECTOR_SIZE;
    if (esp_partition_erase_range(user_partition_, to, erase) != ESP_OK) {
        ESP_LOGE(TAG, "'%s': erase failed, not rebased", old.name);
        return false;
    }
    std::unique_ptr<uint8_t, decltype(&free)> buf(static_cast<uint8_t*>(malloc(kWindow)), &free);
    if (!buf) {
        ESP_LOGE(TAG, "'%s': no memory to rebase it", old.name);
        return false;
    }
    uint32_t crc = 0;
    for (uint32_t off = 0; off < old.size; off += kWindow) {
        const uint32_t n = old.size - off < kWindow ? old.size - off : kWindow;
        memcpy(buf.get(), src + off, n);
        if (JS_RebaseBytecodeWindow(src, old.size, buf.get(), off, n, ranges, 2, newBase) != 0) {
            ESP_LOGE(TAG, "'%s': bytecode points outside itself and the stdlib, not rebased: "
                          "deploy it again", old.name);
            return false;
        }
        if (esp_partition_write(user_partition_, to + off, buf.get(), n) != ESP_OK) {
            ESP_LOGE(TAG, "'%s': write failed, not rebased", old.name);
            return false;
        }
        crc = esp_crc32_le(crc, buf.get(), n);
    }
    // Read back what landed in flash.
    uint32_t check = 0;
    for (uint32_t off = 0; off < old.size; off += kWindow) {
        const uint32_t n = old.size - off < kWindow ? old.size - off : kWindow;
        if (esp_partition_read(user_partition_, to + off, buf.get(), n) != ESP_OK) {
            return false;
        }
        check = esp_crc32_le(check, buf.get(), n);
    }
    if (check != crc) {
        ESP_LOGE(TAG, "'%s': read back differs, not rebased", old.name);
        return false;
    }
    // Replaces the entry (its old region is free again: nothing runs from it yet).
    if (!add(old.name, to, old.size, crc, getPartitionAddress() + to)) {
        return false;
    }
    ESP_LOGI(TAG, "'%s' rebased for this firmware build (stdlib table moved by %ld bytes, "
                  "now at offset 0x%lx)",
             old.name, static_cast<long>(romNow - old.rom_base), static_cast<unsigned long>(to));
    return true;
}

uint32_t BytecodeRegistry::usableSize() const {
    if (!user_partition_) return 0;
    return user_partition_->size < MAX_PARTITION_SIZE ? user_partition_->size : MAX_PARTITION_SIZE;
}

static uint32_t alignDown(uint32_t v, uint32_t a) { return v & ~(a - 1); }
static uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

// Is any byte of [start, end) used by an active entry, a pin or the upload
// in progress? All ranges are widened to whole flash sectors.
bool BytecodeRegistry::overlapsUsed(uint32_t start, uint32_t end) const {
    auto hit = [&](uint32_t s, uint32_t e) {
        s = alignDown(s, SECTOR_SIZE);
        e = alignUp(e, SECTOR_SIZE);
        return s < end && start < e;
    };
    for (const auto& entry : entries_) {
        if (entry.active && entry.size && hit(entry.offset, entry.offset + entry.size)) return true;
    }
    for (int i = 0; i < pin_count_; ++i) {
        if (hit(pins_[i].start, pins_[i].end)) return true;
    }
    return has_reservation_ && hit(reservation_.start, reservation_.end);
}

// Start of the first used range at or after `from` (usableSize() if none).
uint32_t BytecodeRegistry::nextUsedStart(uint32_t from) const {
    uint32_t next = usableSize();
    auto consider = [&](uint32_t s, uint32_t e) {
        s = alignDown(s, SECTOR_SIZE);
        if (alignUp(e, SECTOR_SIZE) > from && s < next) next = s < from ? from : s;
    };
    for (const auto& entry : entries_) {
        if (entry.active && entry.size) consider(entry.offset, entry.offset + entry.size);
    }
    for (int i = 0; i < pin_count_; ++i) consider(pins_[i].start, pins_[i].end);
    if (has_reservation_) consider(reservation_.start, reservation_.end);
    return next;
}

bool BytecodeRegistry::allocate(size_t size, uint32_t* offset) const {
    Lock lock;
    const uint32_t limit = usableSize();
    const uint32_t need = alignUp(static_cast<uint32_t>(size ? size : 1), SECTOR_SIZE);
    if (need > limit) return false;

    // Candidates: the floor and the end of every used range; take the lowest that fits.
    bool found = false;
    uint32_t best = 0;
    auto tryAt = [&](uint32_t c) {
        c = alignUp(c, SECTOR_SIZE);
        if (c < pin_floor_ || c + need > limit || (found && c >= best)) return;
        if (overlapsUsed(c, c + need)) return;
        best = c;
        found = true;
    };
    tryAt(pin_floor_);
    for (const auto& entry : entries_) {
        if (entry.active) tryAt(entry.offset + entry.size);
    }
    for (int i = 0; i < pin_count_; ++i) tryAt(pins_[i].end);
    if (has_reservation_) tryAt(reservation_.end);

    if (found && offset) *offset = best;
    return found;
}

bool BytecodeRegistry::reserve(uint32_t offset, size_t size) {
    Lock lock;
    if (has_reservation_) return false;
    reservation_ = {offset, offset + static_cast<uint32_t>(size)};
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
    uint32_t top = alignUp(end, SECTOR_SIZE);
    for (int i = 0; i < pin_count_; ++i) {
        const uint32_t e = alignUp(pins_[i].end, SECTOR_SIZE);
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

uint32_t BytecodeRegistry::getNextOffset() const {
    Lock lock;
    uint32_t offset = 0;
    return allocate(SECTOR_SIZE, &offset) ? offset : usableSize();
}

uint32_t BytecodeRegistry::getPartitionAddress() const {
    if (!user_partition_) return 0;
    return user_partition_->address;
}

bool BytecodeRegistry::loadFromFile(const char* path) {
    auto& fs = getBusAwareFileSystem();

    if (!fs.exists(path)) {
        return false;
    }

    auto file = fs.open(path, FILE_READ);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open metadata file for reading");
        return false;
    }

    // Read file content
    String content = file.readString();
    file.close();

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", error.c_str());
        return false;
    }

    // Load entries
    auto array = doc["entries"].as<JsonArray>();
    for (JsonObject obj : array) {
        BytecodeEntry entry;

        const char* name = obj["name"];
        if (!name) continue;

        strncpy(entry.name, name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';

        entry.offset = obj["offset"].as<uint32_t>();
        entry.size = obj["size"].as<uint32_t>();
        entry.checksum = obj["checksum"].as<uint32_t>();
        entry.flash_addr = obj["flash_addr"].as<uint32_t>();
        entry.version = obj["version"].as<uint32_t>();
        entry.active = obj["active"].as<bool>();
        entry.app_id = obj["app"].as<uint32_t>();  // absent (older uploads) -> 0 = stale
        entry.rom_base = obj["rom"] | 0u;           // absent -> 0 = not rebased
        entry.stdlib_id = obj["lib"] | 0u;

        entries_.push_back(entry);
    }

    ESP_LOGI(TAG, "Loaded %zu entries from %s", entries_.size(), path);
    return true;
}

bool BytecodeRegistry::saveToFile(const char* path) {
    auto& fs = getBusAwareFileSystem();

    auto file = fs.open(path, FILE_WRITE);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open metadata file for writing");
        return false;
    }

    // Create JSON document
    JsonDocument doc;
    auto array = doc["entries"].to<JsonArray>();

    for (const auto& entry : entries_) {
        auto obj = array.add<JsonObject>();
        obj["name"] = entry.name;
        obj["offset"] = entry.offset;
        obj["size"] = entry.size;
        obj["checksum"] = entry.checksum;
        obj["flash_addr"] = entry.flash_addr;
        obj["version"] = entry.version;
        obj["active"] = entry.active;
        obj["app"] = entry.app_id;
        obj["rom"] = entry.rom_base;
        obj["lib"] = entry.stdlib_id;
    }

    // Serialize to file
    size_t written = serializeJson(doc, file);
    file.close();

    if (written == 0) {
        ESP_LOGE(TAG, "Failed to write JSON to file");
        return false;
    }

    ESP_LOGI(TAG, "Saved %zu entries to %s (%zu bytes)",
             entries_.size(), path, written);

    return true;
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
    return id ? id : 1;  // 0 means "unknown" in BytecodeEntry::app_id
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
    cached = crc ? crc : 1;  // 0 means "unknown" in BytecodeEntry::stdlib_id
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
    // The caller maps and runs it in place: keep the region while the
    // context lives (unpinAll()).
    registry.pin(found->offset, found->size);
    if (entry) *entry = *found;
    return true;
}

} // extern "C"
