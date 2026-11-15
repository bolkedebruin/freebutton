//
// Bytecode Registry - Manages user-uploaded JavaScript bytecode
// Bytecode and its index are both in the js_user flash partition: every
// image has a header in front of it (BytecodeStore.h, docs/SCRIPT-STORE.md)
// and the index in RAM is rebuilt from those headers at boot.
//

#ifndef BYTECODE_REGISTRY_H
#define BYTECODE_REGISTRY_H

#include <initializer_list>
#include <vector>
#include <stdint.h>
#include <esp_partition.h>
#include "BytecodeStore.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// One script in js_user: name, region (sector offset of its header), size
// and checksum of the image, sequence number, and the build it was relocated
// for (app_id, rom_base, stdlib_id). The image starts at imageOffset().
using BytecodeEntry = bcstore::Entry;

// The script that runs at start: the IDE's boot script, else the legacy name
// (BytecodeRegistry::bootScriptName()).
inline constexpr char kBootScriptName[] = "main.jsc";
inline constexpr char kLegacyBootScriptName[] = "script.jsc";

// One registry per device: it maps the js_user partition once (the mapping's
// virtual address is baked into relocated bytecode) and is shared by the
// uploader (async_tcp task) and the boot loader (JS task).
// Every public method is serialised by a recursive mutex; callers that make
// several calls in a row hold a BytecodeRegistry::Lock.
//
// Space management: an upload goes into the lowest free gap that does not
// overlap an entry, a pinned region (bytecode loaded into the current
// JS context - the engine executes it in place from flash, so it must not
// be overwritten while that context lives) or an upload in progress.
// Replacing a name frees its old region once it is no longer pinned: after
// a reboot, or after a script restart (ScriptEngine::restart() frees the
// context and calls unpinAll()). So repeated uploads of the same name
// alternate between two regions instead of marching through the partition.
//
// Storing a script (upload, rebase, migration) is one sequence:
//   1. allocate() + reserve() a free region,
//   2. erase it (eraseRegion(): last sector first),
//   3. write the image behind the place of the header and read it back,
//   4. commit(): program the header. This is the commit point.
// A power cut before 4 leaves a region without a header: free space, and
// the previous entry of that name is untouched (its region is in use, so 1
// never picks it). After 4 the new entry has the newer sequence number and
// the previous one is free space; nothing is written to the previous entry
// (its header stays until its region is erased for a later upload).
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

    // Rebuilds the index from the headers in the partition (read through
    // the mapping, no flash writes). One entry per name: the one with the
    // highest sequence number. Image checksums are checked where an image is
    // used (ScriptEngine::runBootScript(), rebaseStale()), not here.
    bool load();

    // Boot, before any bytecode is found or run: every entry relocated for
    // another firmware build whose stdlib ROM table is the same as this
    // build's (same bytecode_stdlib_id()) is rebased into a new region for
    // this build (JS_RebaseBytecodeWindow: no JS context, a 4 KB buffer).
    // The new entry has a newer sequence number; the old one is free space.
    // Entries with another table stay stale ("deploy again").
    // Also rebased: entries of this build whose js_user mapping moved (the
    // partition maps at another virtual address than at upload time).
    void rebaseStale();

    // The commit point of storing a script: programs the header of the
    // entry at `region`, whose image (`size` bytes at region +
    // bcstore::kHeaderLen, CRC-32 `checksum`) is written and verified. The
    // image must be relocated for the running build: the header is stamped
    // with its app id, ROM table address and stdlib id, and the next
    // sequence number. Replaces the entry of the same name in the index.
    enum class CommitResult {
        Ok,
        Refused,  // not a valid entry (name, region, size): nothing written
        Failed,   // the header did not land in flash
    };
    CommitResult commit(const char* name, uint32_t region, uint32_t size, uint32_t checksum);

    // Step 2 of storing a script: erases the region reserved for an image
    // of `size` bytes at `region`, last sector first, and with it the whole
    // region of every superseded entry whose header lies there.
    bool eraseRegion(uint32_t region, size_t size);

    // Find bytecode by name. The pointer is valid while a Lock is held.
    const BytecodeEntry* find(const char* name) const;

    // Calls f(const BytecodeEntry&) for every entry (holds the lock).
    template <class F>
    void forEachActive(F&& f) const {
        Lock lock;
        for (const auto& entry : entries_) {
            f(entry);
        }
    }

    // First free region (sector offset) for an image of `size` bytes and
    // its header (see class comment).
    bool allocate(size_t size, uint32_t* region) const;

    // Mark the region of an image of `size` bytes at `region` as being
    // written by an upload. One at a time.
    bool reserve(uint32_t region, size_t size);
    void releaseReservation();

    // Mark [offset, offset+size) as executing from flash (the whole region
    // of an entry, header included): kept until unpinAll() or a reboot.
    void pin(uint32_t offset, uint32_t size);

    // Forget every pin (and the floor set when the pin table overflowed).
    // Only when no JS context exists that could execute the pinned bytecode:
    // ScriptEngine::restart() calls it between freeing the old context and
    // creating the new one. Entries and the reservation of an upload in
    // progress are not touched.
    void unpinAll();

    // The boot script rule, in one place: the script that runs at start is
    // kBootScriptName, else kLegacyBootScriptName. Asks present(name) for the
    // names in that order and returns the first for which it is true,
    // nullptr when it is true for none.
    template <class Present>
    static const char* bootScriptName(Present&& present) {
        for (const char* name : {kBootScriptName, kLegacyBootScriptName}) {
            if (present(name)) return name;
        }
        return nullptr;
    }

    // True when `name` is the script that runs at start: kBootScriptName, or
    // kLegacyBootScriptName while there is no kBootScriptName. Does
    // not pin.
    bool isBootScript(const char* name) const;

    // Bytes of js_user the store uses (whole sectors).
    uint32_t capacity() const { return usableSize(); }

    // Virtual memory base address of the partition (from mmap)
    uint32_t getVirtualBaseAddress() const;

private:
    BytecodeRegistry();
    ~BytecodeRegistry() = default;

    struct Range {
        uint32_t start;
        uint32_t end;
    };

    enum class MoveResult {
        Done,     // stored as a new entry
        Dropped,  // cannot be moved as it is (damaged, no room): the
                  // migration gives it up; rebaseStale() leaves the entry
                  // stale and tries again at the next boot
        Retry,    // no memory, a flash error or an upload in the way: the
                  // migration keeps the old index for the next boot
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
    uint32_t next_sequence_ = 1;  // for the next header

    // Migration only: the images of the JSON index while they are moved
    // (allocate() keeps off them). Points into migrateLegacyIndex()'s stack.
    const Range* legacy_used_ = nullptr;
    uint8_t legacy_used_count_ = 0;

    static constexpr size_t MAX_PARTITION_SIZE = 256 * 1024;  // 256KB
    static constexpr uint32_t SECTOR_SIZE = bcstore::kSectorSize;

    uint32_t usableSize() const;

    // visit(start, end) for every range that must not be written: entries,
    // pins, the upload in progress, the old images during the migration.
    template <class Visit>
    void forEachUsed(Visit&& visit) const;

    // Migration only: moves the scripts listed in /js_bytecode_meta.json
    // (older firmware) into the store and deletes that file. From instance().
    void migrateLegacyIndex();

    // Find partition on initialization
    bool initPartition();

    // Map partition to virtual memory (called once at startup)
    bool initVirtualMapping();

    // Copies the image at partition offset `image` (`size` bytes, CRC-32
    // `checksum`, relocated for a build whose stdlib table was at
    // `romBase`) into a new entry `name`, rebased for this build and its
    // new address (lock held). The source is not changed.
    MoveResult moveImage(const char* name, uint32_t image, uint32_t size, uint32_t checksum, uint32_t romBase);
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
