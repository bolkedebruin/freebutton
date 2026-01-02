//
// Bytecode Writer - Flash write operations for user bytecode
// Handles validation, erase, write, and verification with streaming relocation
//

#ifndef BYTECODE_WRITER_H
#define BYTECODE_WRITER_H

#include "BytecodeRegistry.h"
#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>
#include <FS.h>

// Include mquickjs for types
extern "C" {
#include "lib/mquickjs/mquickjs.h"
}

class BytecodeWriter {
public:
    BytecodeWriter();
    ~BytecodeWriter();

    /**
     * Write bytecode to flash partition with streaming relocation
     * Accepts position-independent bytecode (base_addr == 0)
     * Performs relocation during write to target flash address
     * @param name Script identifier (filename)
     * @param bytecode Bytecode buffer
     * @param size Bytecode size in bytes
     * @param final Finalize the write and perform relocation
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t write(const char* name, const uint8_t* bytecode, size_t size, bool final);

    /**
     * Erase bytecode from registry (marks as inactive, doesn't erase flash)
     * @param name Script identifier
     * @return ESP_OK on success
     */
    esp_err_t erase(const char* name);

    /**
     * Validate bytecode format
     * @param bytecode Bytecode buffer
     * @param size Bytecode size
     * @return true if valid QuickJS bytecode
     */
    bool validate(const uint8_t* bytecode, size_t size);

private:
    BytecodeRegistry registry_;
    const esp_partition_t* partition_;

    static constexpr size_t MAX_BYTECODE_SIZE = 256 * 1024;  // 256KB max
    static constexpr size_t SECTOR_SIZE = 4096;  // 4KB flash sectors
    static constexpr size_t CHUNK_SIZE = 4096;   // 4KB streaming chunks

    // State for chunked writing
    char current_name_[32]{};
    uint32_t partition_offset_;  // Offset in js_user partition
    uint32_t write_offset_;      // Current write position
    uint32_t total_size_;        // Total bytes written
    uint32_t crc32_;             // Running CRC32
    bool writing_;

    // Streaming relocation state
    enum class RelocationState {
        HEADER_PENDING,
        BLOCKS_PENDING,
        COMPLETE
    };

    RelocationState reloc_state_;
    uint8_t chunk_buffer_[CHUNK_SIZE];       // 4KB work buffer
    uint8_t overflow_buffer_[CHUNK_SIZE];    // For partial blocks
    size_t overflow_size_;
    uintptr_t relocation_offset_;
    uintptr_t relocation_target_addr_;
    String temp_path_;
    File temp_file_;

    /**
     * Erase flash region aligned to sector boundaries
     * @param offset Offset in partition
     * @param size Size to erase
     * @return ESP_OK on success
     */
    esp_err_t eraseRegion(uint32_t offset, size_t size);

    /**
     * Stream-process temp file in 4KB chunks and write relocated bytecode to flash
     * Uses JS_RelocateHdr and JS_RelocateMtag for streaming relocation with ROM atom translation
     */
    esp_err_t streamRelocateToFlash();

    /**
     * Clean up temporary file
     */
    void cleanupTempFile();
};

#endif // BYTECODE_WRITER_H
