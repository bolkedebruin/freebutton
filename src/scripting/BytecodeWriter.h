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
    // atoms_resolved: the image already refers to this build's ROM atoms
    // (the IDE's compiler did that): no lookup context is allocated.
    // exact_size: total_size is the image's own size (the upload's "size"
    // parameter), not the request's length: the region is taken for it and
    // an upload that brings another number of image bytes is not stored.
    esp_err_t beginWrite(const char* name, size_t total_size, bool atoms_resolved = false,
                         bool exact_size = false);

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

private:
    BytecodeRegistry& registry_;  // the shared instance
    const esp_partition_t* partition_;
    bool reserved_ = false;       // holds the registry's upload reservation

    static constexpr size_t MAX_BYTECODE_SIZE = 256 * 1024 - bcstore::kHeaderLen;  // js_user less the entry header
    static constexpr size_t CHUNK_SIZE = 4096;   // 4KB streaming chunks

    // State for chunked writing
    char current_name_[32]{};
    uint32_t partition_offset_;  // Offset of the entry's region in js_user (its header)
    uint32_t image_offset_;      // Offset of the image: behind the header
    uint32_t write_offset_;      // Current write position
    uint32_t total_size_;        // Total bytes written
    uint32_t crc32_;             // CRC32 of the image bytes handed to the flash driver
    bcstore::BoundaryGuard guard_;  // see writeImage()
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
    uintptr_t relocation_target_addr_;

    // Persistent relocation context (allocated in beginWrite, freed on final/error)
    JSContext* reloc_ctx_;
    uint8_t* reloc_ctx_mem_;
    uint8_t* reloc_read_buffer_;
    static constexpr size_t RELOC_READ_BUFFER_SIZE = 256;
    static constexpr size_t RELOC_CTX_HEAP_SIZE = 8 * 1024;  // 8KB

    // Streaming state for relocation read callback
    size_t expected_size_;           // Image size, or at least that (the request's Content-Length)
    bool exact_size_ = false;        // expected_size_ is the image size
    uint32_t flash_data_start_;      // Flash address where data section starts
    uint32_t data_section_offset_;   // Current position in data section
    uint32_t chunk_data_offset_;     // Offset of current chunk in data section
    size_t chunk_size_;              // Size of data in current chunk

    // Writes image bytes at write_offset_ and advances it (and total_size_,
    // crc32_); refuses bytes that must not be in the store.
    esp_err_t writeImage(const uint8_t* data, size_t size);

    /**
     * Clean up relocation context and buffers
     */
    void cleanup();

    /**
     * Finalize the write operation
     * Calculates CRC32, commits the entry (programs its header), cleans up context
     * @return ESP_OK on success
     */
    esp_err_t finalizeWrite();

    // Static callback for streaming relocation reads
    static uint8_t* relocReadCallback(void* opaque, uintptr_t offset, size_t size);
};

#endif // BYTECODE_WRITER_H
