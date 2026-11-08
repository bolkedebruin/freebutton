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

extern "C" {
#include "lib/mquickjs/mquickjs.h"
}

class BytecodeWriter {
public:
    BytecodeWriter();
    ~BytecodeWriter();

    /**
     * Begin a bytecode write operation
     * Must be called before write() with the total expected size
     * Erases flash region upfront and initializes relocation context
     * @param name Script identifier (filename)
     * @param total_size Total bytecode size from Content-Length
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t beginWrite(const char* name, size_t total_size);

    /**
     * Write bytecode chunk to flash with inline relocation
     * Accepts position-independent bytecode (base_addr == 0)
     * Performs relocation during write to target flash address
     * @param data Bytecode chunk buffer
     * @param size Chunk size in bytes
     * @param final True if this is the last chunk
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t write(const uint8_t* data, size_t size, bool final);

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
    static bool validate(const uint8_t* bytecode, size_t size);

private:
    BytecodeRegistry& registry_;  // the shared instance
    const esp_partition_t* partition_;
    bool reserved_ = false;       // holds the registry's upload reservation

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
    uint8_t chunk_buffer_[CHUNK_SIZE]{};       // 4KB work buffer
    uint8_t overflow_buffer_[CHUNK_SIZE]{};    // For partial blocks
    size_t overflow_size_;
    uintptr_t relocation_offset_;
    uintptr_t relocation_target_addr_;

    // Persistent relocation context (allocated in beginWrite, freed on final/error)
    JSContext* reloc_ctx_;
    uint8_t* reloc_ctx_mem_;
    uint8_t* reloc_read_buffer_;
    static constexpr size_t RELOC_READ_BUFFER_SIZE = 256;
    static constexpr size_t RELOC_CTX_HEAP_SIZE = 8 * 1024;  // 8KB

    // Streaming state for relocation read callback
    size_t expected_size_;           // Total expected size from Content-Length
    uint32_t flash_data_start_;      // Flash address where data section starts
    uint32_t data_section_offset_;   // Current position in data section
    uint32_t chunk_data_offset_;     // Offset of current chunk in data section
    size_t chunk_size_;              // Size of data in current chunk

    /**
     * Erase flash region aligned to sector boundaries
     * @param offset Offset in partition
     * @param size Size to erase
     * @return ESP_OK on success
     */
    esp_err_t eraseRegion(uint32_t offset, size_t size) const;

    /**
     * Clean up relocation context and buffers
     */
    void cleanup();

    /**
     * Finalize the write operation
     * Calculates CRC32, updates registry, cleans up context
     * @return ESP_OK on success
     */
    esp_err_t finalizeWrite();

    // Static callback for streaming relocation reads
    static uint8_t* relocReadCallback(void* opaque, uintptr_t offset, size_t size);
};

#endif // BYTECODE_WRITER_H
