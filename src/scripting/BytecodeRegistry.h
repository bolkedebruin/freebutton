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
    // App build the bytecode was relocated for: the first 4 bytes of the
    // running app's ELF SHA-256 at upload time. Relocation writes absolute
    // addresses of the stdlib's ROM atoms, which live in the APP's rodata,
    // so the bytecode cannot run on another build as it is. 0 = unknown
    // (uploaded before this field existed): treated as stale.
    uint32_t app_id;
    // Where that build's stdlib ROM table was, and which table it was
    // (bytecode_stdlib_id()). After a firmware update with the same table
    // the ROM atoms only moved: rebaseStale() moves the bytecode's pointers
    // to them by the table's displacement, so no deploy is needed. 0 =
    // unknown (uploaded before these fields existed): not rebased.
    uint32_t rom_base;
    uint32_t stdlib_id;

    BytecodeEntry()
        : offset(0), size(0), checksum(0), flash_addr(0), version(1), active(false), app_id(0),
          rom_base(0), stdlib_id(0) {
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
// overlap an active entry, a pinned region (bytecode loaded into the current
// JS context - the engine executes it in place from flash, so it must not
// be overwritten while that context lives) or an upload in progress.
// Replacing a name frees its old region once it is no longer pinned: after
// a reboot, or after a script restart (ScriptEngine::restart() frees the
// context and calls unpinAll()). So repeated uploads of the same name
// alternate between two regions instead of marching through the partition.
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

    // Boot, before any bytecode is found or run: every entry relocated for
    // another firmware build whose stdlib ROM table is the same as this
    // build's (same bytecode_stdlib_id()) is rebased into a new region for
    // this build (JS_RebaseBytecodeWindow: no JS context, a 4 KB buffer).
    // Entries with another or an unknown table stay stale ("deploy again").
    // Also rebased: entries of this build whose js_user mapping moved (the
    // partition maps at another virtual address than at upload time).
    void rebaseStale();

    // Add new bytecode entry (replaces an existing entry of the same name).
    // The bytecode must be relocated for the running build: the entry is
    // stamped with its app id, ROM table address and stdlib id.
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

    // Mark [offset, offset+size) as executing from flash: kept until
    // unpinAll() or a reboot.
    void pin(uint32_t offset, uint32_t size);

    // Forget every pin (and the floor set when the pin table overflowed).
    // Only when no JS context exists that could execute the pinned bytecode:
    // ScriptEngine::restart() calls it between freeing the old context and
    // creating the new one. Active entries and the reservation of an upload
    // in progress are not touched.
    void unpinAll();

    // True when `name` is the script that runs at start: "main.jsc", or
    // "script.jsc" while there is no active "main.jsc". Does not pin.
    bool isBootScript(const char* name) const;

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

    // Rebases one stale entry (lock held); false leaves it as it was.
    bool rebaseEntry(const BytecodeEntry& entry);
    bool mappingMoved(const BytecodeEntry& entry) const;
};

// C interface for mquickjs binding
#ifdef __cplusplus
extern "C" {
#endif

// Find bytecode entry by name (C wrapper). A found entry is pinned: callers
// map and execute it from flash.
bool bytecode_registry_find(const char* name, BytecodeEntry* entry);

// ID of the running app build (see BytecodeEntry::app_id).
uint32_t bytecode_current_app_id(void);

// ID of the running build's stdlib ROM table (see BytecodeEntry::stdlib_id):
// a CRC32 of the table with its references to itself taken as offsets, so
// it is the same in every build with the same table, wherever it lies.
uint32_t bytecode_stdlib_id(void);
// Address of this build's stdlib ROM table (what BytecodeEntry::rom_base holds).
uint32_t bytecode_stdlib_rom_base(void);
// Identity of this build's ROM atoms (the engine's JS_StdlibAtomsId): the
// IDE's compiler has the same value when it was built from the same stdlib.
uint32_t bytecode_stdlib_atoms_id(void);

#ifdef __cplusplus
}
#endif

#endif // BYTECODE_REGISTRY_H
