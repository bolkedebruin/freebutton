//
// Bytecode Registry - Manages user-uploaded JavaScript bytecode
// Metadata stored in LittleFS, bytecode stored in flash partition
//

#ifndef BYTECODE_REGISTRY_H
#define BYTECODE_REGISTRY_H

#include <vector>
#include <string>
#include <stdint.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Metadata for a single bytecode entry
struct BytecodeEntry {
    char name[32];              // Script identifier (filename)
    uint32_t offset;            // Offset in js_user partition
    uint32_t size;              // Bytecode size in bytes
    uint32_t checksum;          // CRC32 for integrity verification
    uint32_t flash_addr;        // Actual flash address (for relocation)
    uint8_t version;            // Bytecode format version
    bool active;                // Is this entry valid?

    BytecodeEntry()
        : offset(0), size(0), checksum(0), flash_addr(0), version(1), active(false) {
        name[0] = '\0';
    }
};

// One registry per device: it maps the js_user partition once (the mapping's
// virtual address is baked into relocated bytecode) and is shared by the
// uploader (async_tcp task), the boot loader and loadUserBytecode() (JS task).
// Every public method is serialised by a recursive mutex; callers that make
// several calls in a row hold a BytecodeRegistry::Lock.
//
// Space management: an upload goes into the lowest free gap that does not
// overlap an active entry, a pinned region (bytecode loaded since boot -
// the JS engine executes it in place from flash, so it must not be
// overwritten until the next reboot) or an upload in progress. Replacing a
// name frees its old region once it is no longer pinned (i.e. after a
// reboot), so repeated uploads of the same name alternate between two
// regions instead of marching through the partition.
class BytecodeRegistry {
public:
    static BytecodeRegistry& instance();

    class Lock {
    public:
        Lock();
        ~Lock();
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    };

    // Load registry from LittleFS (/js_bytecode_meta.json)
    bool load();

    // Save registry to LittleFS
    bool save();

    // Add new bytecode entry (replaces an existing entry of the same name)
    bool add(const char* name, uint32_t offset, uint32_t size,
             uint32_t checksum, uint32_t flash_addr);

    // Find bytecode by name. The pointer is valid while a Lock is held.
    const BytecodeEntry* find(const char* name) const;

    // List all active bytecode entries
    std::vector<BytecodeEntry> list() const;

    // Calls f(const BytecodeEntry&) for every active entry (holds the lock).
    template <class F>
    void forEachActive(F&& f) const {
        Lock lock;
        for (const auto& entry : entries_) {
            if (entry.active) f(entry);
        }
    }

    // Remove bytecode entry (marks as inactive)
    bool remove(const char* name);

    // First free offset that fits `size` bytes (see class comment).
    bool allocate(size_t size, uint32_t* offset) const;

    // Mark [offset, offset+size) as being written by an upload. One at a time.
    bool reserve(uint32_t offset, size_t size);
    void releaseReservation();

    // Mark [offset, offset+size) as executing from flash until reboot.
    void pin(uint32_t offset, uint32_t size);

    // Offset the next (small) upload would get, and the free gap there.
    uint32_t getNextOffset() const;
    size_t getFreeSpace() const;

    // Get partition base address (physical flash address)
    uint32_t getPartitionAddress() const;

    // Get/set virtual memory base address (from mmap)
    uint32_t getVirtualBaseAddress() const;
    void setVirtualBaseAddress(uint32_t addr);

private:
    BytecodeRegistry();
    ~BytecodeRegistry() = default;

    struct Range {
        uint32_t start;
        uint32_t end;
    };

    std::vector<BytecodeEntry> entries_;
    const esp_partition_t* user_partition_;
    uint32_t virtual_base_addr_;  // Virtual address from mmap (stable across reboots)
    esp_partition_mmap_handle_t mmap_handle_;  // Keep partition mapped permanently

    static constexpr int kMaxPins = 4;
    Range pins_[kMaxPins] = {};
    uint8_t pin_count_ = 0;
    uint32_t pin_floor_ = 0;  // when the pin table overflowed: allocate above this
    Range reservation_ = {};
    bool has_reservation_ = false;

    static constexpr const char* METADATA_PATH = "/js_bytecode_meta.json";
    static constexpr size_t MAX_PARTITION_SIZE = 256 * 1024;  // 256KB
    static constexpr uint32_t SECTOR_SIZE = 4096;

    uint32_t usableSize() const;
    bool overlapsUsed(uint32_t start, uint32_t end) const;
    uint32_t nextUsedStart(uint32_t from) const;

    // Serialize/deserialize to JSON for LittleFS storage
    bool loadFromFile(const char* path);
    bool saveToFile(const char* path);

    // Find partition on initialization
    bool initPartition();

    // Map partition to virtual memory (called once at startup)
    bool initVirtualMapping();
};

// C interface for mquickjs binding
#ifdef __cplusplus
extern "C" {
#endif

// Find bytecode entry by name (C wrapper). A found entry is pinned: callers
// map and execute it from flash.
bool bytecode_registry_find(const char* name, BytecodeEntry* entry);

#ifdef __cplusplus
}
#endif

#endif // BYTECODE_REGISTRY_H
