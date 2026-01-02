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

class BytecodeRegistry {
public:
    BytecodeRegistry();
    ~BytecodeRegistry();

    // Load registry from LittleFS (/js_bytecode_meta.json)
    bool load();

    // Save registry to LittleFS
    bool save();

    // Add new bytecode entry
    bool add(const char* name, uint32_t offset, uint32_t size,
             uint32_t checksum, uint32_t flash_addr);

    // Find bytecode by name
    const BytecodeEntry* find(const char* name) const;

    // List all active bytecode entries
    std::vector<BytecodeEntry> list() const;

    // Remove bytecode entry (marks as inactive)
    bool remove(const char* name);

    // Get next available offset in partition
    uint32_t getNextOffset() const;

    // Get free space remaining in partition
    size_t getFreeSpace() const;

    // Get partition base address (physical flash address)
    uint32_t getPartitionAddress() const;

    // Get/set virtual memory base address (from mmap)
    uint32_t getVirtualBaseAddress() const;
    void setVirtualBaseAddress(uint32_t addr);

private:
    std::vector<BytecodeEntry> entries_;
    const esp_partition_t* user_partition_;
    uint32_t next_offset_;
    uint32_t virtual_base_addr_;  // Virtual address from mmap (stable across reboots)
    esp_partition_mmap_handle_t mmap_handle_;  // Keep partition mapped permanently

    static constexpr const char* METADATA_PATH = "/js_bytecode_meta.json";
    static constexpr size_t MAX_PARTITION_SIZE = 256 * 1024;  // 256KB

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

// Find bytecode entry by name (C wrapper)
bool bytecode_registry_find(const char* name, BytecodeEntry* entry);

#ifdef __cplusplus
}
#endif

#endif // BYTECODE_REGISTRY_H
