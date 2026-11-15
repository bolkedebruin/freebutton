//
// Bytecode Writer - Implementation with direct-to-flash streaming relocation
// Flash write operations for user bytecode
//

#include "BytecodeWriter.h"
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_crc.h>
#include <cstring>

// Include QuickJS for bytecode validation and structures
extern "C" {
#include "lib/mquickjs/mquickjs.h"
#include "lib/mquickjs/mquickjs_ext.h"

// BCRelocState, JS_RelocateHdr and JS_RelocateMtag come from mquickjs.h.
extern const JSSTDLibraryDef js_stdlib;  // For creating temp context with ROM atoms
} // C

static auto TAG = "BytecodeWriter";

// Static callback for streaming relocation reads
// Uses BytecodeWriter* as opaque data
uint8_t* BytecodeWriter::relocReadCallback(void* opaque, uintptr_t offset, size_t size) {
    auto* self = static_cast<BytecodeWriter*>(opaque);

    // Check if data is in current chunk buffer
    if (offset >= self->chunk_data_offset_ &&
        offset + size <= self->chunk_data_offset_ + self->chunk_size_) {
        return self->chunk_buffer_ + (offset - self->chunk_data_offset_);
    }

    // Check if data is in flash (already written)
    if (offset + size <= self->data_section_offset_) {
        // Read from flash into temp buffer
        if (size > RELOC_READ_BUFFER_SIZE) {
            ESP_LOGE(TAG, "Read size %zu exceeds buffer %zu", size, RELOC_READ_BUFFER_SIZE);
            return nullptr;
        }
        uint32_t flash_offset = self->flash_data_start_ + offset;
        esp_err_t err = esp_partition_read(self->partition_, flash_offset, self->reloc_read_buffer_, size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read from flash at 0x%lx: 0x%x", flash_offset, err);
            return nullptr;
        }
        return self->reloc_read_buffer_;
    }

    // Data not yet accessible (in a later chunk)
    ESP_LOGW(TAG, "Data at offset 0x%lx not accessible yet", (uint32_t)offset);
    return nullptr;
}

BytecodeWriter::BytecodeWriter()
    : registry_(BytecodeRegistry::instance())
    , partition_(nullptr)
    , partition_offset_(0)
    , write_offset_(0)
    , total_size_(0)
    , crc32_(0)
    , writing_(false)
    , reloc_state_(RelocationState::HEADER_PENDING)
    , overflow_size_(0)
    , relocation_offset_(0)
    , relocation_target_addr_(0)
    , reloc_ctx_(nullptr)
    , reloc_ctx_mem_(nullptr)
    , reloc_read_buffer_(nullptr)
    , expected_size_(0)
    , flash_data_start_(0)
    , data_section_offset_(0)
    , chunk_data_offset_(0)
    , chunk_size_(0) {

    current_name_[0] = '\0';

    // Find partition
    partition_ = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        "js_user"
    );

    if (!partition_) {
        ESP_LOGE(TAG, "js_user partition not found");
    }
}

BytecodeWriter::~BytecodeWriter() {
    cleanup();
}

esp_err_t BytecodeWriter::eraseRegion(uint32_t offset, size_t size) const {
    if (!partition_) {
        return ESP_ERR_INVALID_STATE;
    }

    // Align to sector boundaries
    uint32_t aligned_offset = (offset / SECTOR_SIZE) * SECTOR_SIZE;
    size_t aligned_size = ((size + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;

    ESP_LOGI(TAG, "Erasing flash region: offset=0x%lx, size=%zu (aligned from 0x%lx, %zu)",
             aligned_offset, aligned_size, offset, size);

    esp_err_t err = esp_partition_erase_range(partition_, aligned_offset, aligned_size);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase flash: 0x%x", err);
    }

    return err;
}

void BytecodeWriter::cleanup() {
    if (reserved_) {
        registry_.releaseReservation();
        reserved_ = false;
    }
    if (reloc_ctx_) {
        JS_FreeContext(reloc_ctx_);
        reloc_ctx_ = nullptr;
    }
    if (reloc_ctx_mem_) {
        free(reloc_ctx_mem_);
        reloc_ctx_mem_ = nullptr;
    }
    if (reloc_read_buffer_) {
        free(reloc_read_buffer_);
        reloc_read_buffer_ = nullptr;
    }
}

esp_err_t BytecodeWriter::beginWrite(const char* name, size_t total_size, const bool atoms_resolved) {
    if (!partition_) {
        ESP_LOGE(TAG, "Partition not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!name || total_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (total_size > MAX_BYTECODE_SIZE) {
        ESP_LOGE(TAG, "Bytecode too large: %zu bytes (max %zu)", total_size, MAX_BYTECODE_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (writing_) {
        ESP_LOGE(TAG, "Write already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Beginning bytecode upload: %s (%zu bytes)", name, total_size);

    // Store name and expected size
    strncpy(current_name_, name, sizeof(current_name_) - 1);
    current_name_[sizeof(current_name_) - 1] = '\0';
    expected_size_ = total_size;

    // Pick the target region: lowest free gap that overlaps neither an active
    // entry (including the old version of this name) nor bytecode executing
    // since boot, and reserve it against a concurrent upload.
    {
        BytecodeRegistry::Lock lock;
        uint32_t offset = 0;
        if (!registry_.allocate(total_size, &offset)) {
            ESP_LOGE(TAG, "No free region of %zu bytes in js_user", total_size);
            return ESP_ERR_NO_MEM;
        }
        if (!registry_.reserve(offset, total_size)) {
            ESP_LOGE(TAG, "Another bytecode upload is in progress");
            return ESP_ERR_INVALID_STATE;
        }
        reserved_ = true;
        partition_offset_ = offset;
    }
    relocation_target_addr_ = registry_.getVirtualBaseAddress() + partition_offset_;

    ESP_LOGI(TAG, "Will relocate to: 0x%lx (partition offset: 0x%lx)",
             relocation_target_addr_, partition_offset_);

    // Erase flash region upfront
    size_t aligned_size = ((total_size + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;
    if (esp_err_t err = eraseRegion(partition_offset_, aligned_size); err != ESP_OK) {
        cleanup();
        return err;
    }

    // QuickJS context for the ROM atom lookup; not needed for an image whose
    // ROM atoms the compiler resolved (reloc_ctx_ stays null).
    if (!atoms_resolved) {
        reloc_ctx_mem_ = static_cast<uint8_t*>(malloc(RELOC_CTX_HEAP_SIZE));
        if (!reloc_ctx_mem_) {
            ESP_LOGE(TAG, "Failed to allocate context heap");
            cleanup();
            return ESP_ERR_NO_MEM;
        }

        reloc_ctx_ = JS_NewContext(reloc_ctx_mem_, RELOC_CTX_HEAP_SIZE, &js_stdlib);
        if (!reloc_ctx_) {
            ESP_LOGE(TAG, "Failed to create QuickJS context");
            cleanup();
            return ESP_ERR_NO_MEM;
        }
    }

    // Allocate read buffer for relocation callbacks
    reloc_read_buffer_ = static_cast<uint8_t*>(malloc(RELOC_READ_BUFFER_SIZE));
    if (!reloc_read_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate reloc read buffer");
        cleanup();
        return ESP_ERR_NO_MEM;
    }

    // Initialize state
    write_offset_ = partition_offset_;
    total_size_ = 0;
    crc32_ = 0;
    reloc_state_ = RelocationState::HEADER_PENDING;
    overflow_size_ = 0;
    flash_data_start_ = partition_offset_ + sizeof(JSBytecodeHeader);
    data_section_offset_ = 0;
    chunk_data_offset_ = 0;
    chunk_size_ = 0;
    writing_ = true;

    ESP_LOGI(TAG, "Ready for direct-to-flash streaming relocation");
    return ESP_OK;
}

esp_err_t BytecodeWriter::write(const uint8_t* data, size_t size, bool final) {
    if (!writing_) {
        ESP_LOGE(TAG, "beginWrite() not called");
        return ESP_ERR_INVALID_STATE;
    }

    // ESPAsyncWebServer can end an upload with an empty final chunk (a small
    // file arrives whole in the first call, then len 0 with final set).
    if (size == 0) {
        if (!final) {
            return ESP_OK;
        }
        if (reloc_state_ == RelocationState::HEADER_PENDING) {
            ESP_LOGE(TAG, "Empty upload");
            cleanup();
            writing_ = false;
            return ESP_ERR_INVALID_SIZE;
        }
        return finalizeWrite();
    }
    if (!data) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // Check size limit
    if (total_size_ + size > expected_size_) {
        ESP_LOGE(TAG, "Data exceeds expected size: %lu + %zu > %zu",
                 total_size_, size, expected_size_);
        cleanup();
        writing_ = false;
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = ESP_OK;

    // Create BCRelocState for relocation operations
    BCRelocState reloc_state;
    reloc_state.ctx = reloc_ctx_;
    reloc_state.offset = relocation_target_addr_ + sizeof(JSBytecodeHeader);
    reloc_state.update_atoms = reloc_ctx_ != nullptr ? 1 : 0;   // no context: the compiler resolved them
    reloc_state.read_func = relocReadCallback;
    reloc_state.read_opaque = this;
    // The IDE resolves the ROM atoms itself when it knows this build's table
    // (/api/bytecode/next_address): a chunk can refer to a string that only
    // arrives in a later chunk, and such a reference cannot be looked up here.
    reloc_state.rom_lo = reinterpret_cast<uintptr_t>(js_stdlib.stdlib_table);
    reloc_state.rom_hi = reloc_state.rom_lo + js_stdlib.stdlib_table_len * sizeof(JSWord);

    // Process header on first chunk
    if (reloc_state_ == RelocationState::HEADER_PENDING) {
        // Need at least header size
        if (size < sizeof(JSBytecodeHeader)) {
            ESP_LOGE(TAG, "First chunk too small for header: %zu < %zu",
                     size, sizeof(JSBytecodeHeader));
            cleanup();
            writing_ = false;
            return ESP_ERR_INVALID_ARG;
        }

        // Validate bytecode format
        if (!JS_IsBytecode(data, size)) {
            ESP_LOGE(TAG, "Invalid bytecode format");
            cleanup();
            writing_ = false;
            return ESP_ERR_INVALID_ARG;
        }

        // Copy header to chunk buffer for modification
        memcpy(chunk_buffer_, data, sizeof(JSBytecodeHeader));
        auto* hdr = reinterpret_cast<JSBytecodeHeader*>(chunk_buffer_);

        // Verify position-independent
        if (hdr->base_addr != 0) {
            ESP_LOGE(TAG, "Only position-independent bytecode supported (base_addr must be 0)");
            ESP_LOGE(TAG, "Got base_addr: 0x%lx", (uint32_t)hdr->base_addr);
            cleanup();
            writing_ = false;
            return ESP_ERR_INVALID_ARG;
        }

        ESP_LOGI(TAG, "Header BEFORE relocation: version=0x%04x, base_addr=0x%lx",
                 hdr->version, (uint32_t)hdr->base_addr);

        // Relocate header
        uintptr_t target_base_addr = relocation_target_addr_ + sizeof(JSBytecodeHeader);
        if (JS_RelocateHdr(&reloc_state, hdr, target_base_addr) != 0) {
            ESP_LOGE(TAG, "JS_RelocateHdr failed");
            cleanup();
            writing_ = false;
            return ESP_ERR_INVALID_STATE;
        }

        ESP_LOGI(TAG, "Header AFTER relocation: version=0x%04x, base_addr=0x%lx",
                 hdr->version, (uint32_t)hdr->base_addr);

        // Write relocated header to flash
        err = esp_partition_write(partition_, write_offset_, chunk_buffer_, sizeof(JSBytecodeHeader));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write header to flash: 0x%x", err);
            cleanup();
            writing_ = false;
            return err;
        }

        write_offset_ += sizeof(JSBytecodeHeader);
        total_size_ += sizeof(JSBytecodeHeader);

        ESP_LOGI(TAG, "Header relocated and written to flash");

        // Advance data pointer past header
        data += sizeof(JSBytecodeHeader);
        size -= sizeof(JSBytecodeHeader);

        reloc_state_ = RelocationState::BLOCKS_PENDING;
    }

    // Process data blocks
    if (reloc_state_ == RelocationState::BLOCKS_PENDING && size > 0) {
        // Copy incoming data to chunk buffer, prepending any overflow
        size_t chunk_start = 0;

        if (overflow_size_ > 0) {
            // Check if combined size fits in buffer
            if (overflow_size_ + size > CHUNK_SIZE) {
                // Handle large block by allocating temp buffer
                ESP_LOGW(TAG, "Large block detected, overflow=%zu + chunk=%zu", overflow_size_, size);

                uint8_t* large_buffer = static_cast<uint8_t*>(malloc(overflow_size_ + size));
                if (!large_buffer) {
                    ESP_LOGE(TAG, "Failed to allocate large buffer of %lu byte", overflow_size_ + size);
                    cleanup();
                    writing_ = false;
                    return ESP_ERR_NO_MEM;
                }

                // Combine overflow and new data
                memcpy(large_buffer, overflow_buffer_, overflow_size_);
                memcpy(large_buffer + overflow_size_, data, size);
                size_t combined_size = overflow_size_ + size;

                // Update chunk context for relocation callback
                // Overflow data starts at data_section_offset_ - overflow_size_
                chunk_data_offset_ = data_section_offset_ - overflow_size_;
                chunk_size_ = combined_size;

                // Temporarily point chunk_buffer_ at large buffer for callback
                uint8_t* saved_chunk_buffer = chunk_buffer_;
                // Note: Can't reassign array, so we handle this differently
                // The callback uses chunk_buffer_, so we need to process differently

                // Process blocks from large buffer
                size_t processed = 0;
                while (processed < combined_size) {
                    uint8_t* block_start = large_buffer + processed;
                    size_t available = combined_size - processed;

                    if (available < 4) {
                        // Partial header - save to overflow
                        overflow_size_ = available;
                        memcpy(overflow_buffer_, block_start, available);
                        break;
                    }

                    size_t block_size = JS_GetMemBlockSize(block_start);
                    if (block_size > available) {
                        // Partial block - save to overflow
                        overflow_size_ = available;
                        memcpy(overflow_buffer_, block_start, available);
                        break;
                    }

                    // For the callback, we need to update state
                    // Copy block to chunk_buffer_ for the callback to find it
                    if (block_size <= CHUNK_SIZE) {
                        memcpy(chunk_buffer_, block_start, block_size);
                        chunk_data_offset_ = data_section_offset_;
                        chunk_size_ = block_size;

                        if (JS_RelocateMtag(&reloc_state, chunk_buffer_, block_size) != 0) {
                            ESP_LOGE(TAG, "JS_RelocateMtag failed for large block");
                            free(large_buffer);
                            cleanup();
                            writing_ = false;
                            return ESP_ERR_INVALID_STATE;
                        }

                        // Write to flash
                        err = esp_partition_write(partition_, write_offset_, chunk_buffer_, block_size);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to write block to flash: 0x%x", err);
                            free(large_buffer);
                            cleanup();
                            writing_ = false;
                            return err;
                        }

                        write_offset_ += block_size;
                        total_size_ += block_size;
                        data_section_offset_ += block_size;
                        processed += block_size;
                        overflow_size_ = 0;
                    } else {
                        ESP_LOGE(TAG, "Block size %zu exceeds chunk buffer", block_size);
                        free(large_buffer);
                        cleanup();
                        writing_ = false;
                        return ESP_ERR_INVALID_SIZE;
                    }
                }

                free(large_buffer);
                // Skip normal processing
                goto finalize_check;
            }

            // Normal case: prepend overflow to chunk buffer
            memcpy(chunk_buffer_, overflow_buffer_, overflow_size_);
            chunk_start = overflow_size_;
            chunk_data_offset_ = data_section_offset_ - overflow_size_;
            overflow_size_ = 0;
        } else {
            chunk_data_offset_ = data_section_offset_;
        }

        // Copy new data after overflow
        memcpy(chunk_buffer_ + chunk_start, data, size);
        size_t chunk_total = chunk_start + size;
        chunk_size_ = chunk_total;

        // Process blocks in chunk
        size_t processed = 0;
        while (processed < chunk_total) {
            uint8_t* block_start = chunk_buffer_ + processed;
            size_t available = chunk_total - processed;

            // Check for partial block header
            if (available < 4) {
                overflow_size_ = available;
                memcpy(overflow_buffer_, block_start, available);
                ESP_LOGD(TAG, "Partial block header saved to overflow: %zu bytes", overflow_size_);
                break;
            }

            // Get block size
            size_t block_size = JS_GetMemBlockSize(block_start);
            if (block_size > available) {
                // Partial block - save to overflow
                overflow_size_ = available;
                memcpy(overflow_buffer_, block_start, available);
                ESP_LOGD(TAG, "Partial block saved to overflow: %zu bytes (block_size=%zu)",
                         overflow_size_, block_size);
                break;
            }

            // Relocate block with ROM atom remapping
            if (JS_RelocateMtag(&reloc_state, block_start, block_size) != 0) {
                ESP_LOGE(TAG, "JS_RelocateMtag failed for block");
                cleanup();
                writing_ = false;
                return ESP_ERR_INVALID_STATE;
            }

            processed += block_size;
        }

        // Write relocated blocks to flash (excluding overflow)
        if (size_t write_size = processed; write_size > 0) {
            err = esp_partition_write(partition_, write_offset_, chunk_buffer_, write_size);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write data to flash: 0x%x", err);
                cleanup();
                writing_ = false;
                return err;
            }

            write_offset_ += write_size;
            total_size_ += write_size;
            data_section_offset_ += write_size;

            ESP_LOGD(TAG, "Wrote %zu bytes to flash (total: %lu, data_section: %lu)",
                     write_size, total_size_, data_section_offset_);
        }
    }

finalize_check:
    if (final) {
        return finalizeWrite();
    }

    return ESP_OK;
}

esp_err_t BytecodeWriter::finalizeWrite() {
    ESP_LOGI(TAG, "Finalizing write (%lu bytes)", total_size_);

    // Handle any remaining overflow
    if (overflow_size_ > 0) {
        ESP_LOGW(TAG, "Trailing data in overflow buffer: %zu bytes", overflow_size_);

        esp_err_t err = esp_partition_write(partition_, write_offset_,
                                            overflow_buffer_, overflow_size_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write overflow to flash: 0x%x", err);
            cleanup();
            writing_ = false;
            return err;
        }
        write_offset_ += overflow_size_;
        total_size_ += overflow_size_;
        overflow_size_ = 0;
    }

    reloc_state_ = RelocationState::COMPLETE;
    ESP_LOGI(TAG, "Streaming relocation complete, wrote %lu bytes", total_size_);

    // Calculate CRC32 of relocated bytecode in flash
    ESP_LOGI(TAG, "Calculating CRC32 of relocated bytecode...");
    uint32_t relocated_crc = 0;
    uint32_t bytes_remaining = total_size_;
    uint32_t read_offset = partition_offset_;

    while (bytes_remaining > 0) {
        size_t read_size = (bytes_remaining > CHUNK_SIZE) ? CHUNK_SIZE : bytes_remaining;

        esp_err_t err = esp_partition_read(partition_, read_offset, chunk_buffer_, read_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read back for CRC32: 0x%x", err);
            cleanup();
            writing_ = false;
            return err;
        }

        relocated_crc = esp_crc32_le(relocated_crc, chunk_buffer_, read_size);

        bytes_remaining -= read_size;
        read_offset += read_size;
    }

    ESP_LOGI(TAG, "Relocated bytecode CRC32: 0x%lx", relocated_crc);

    // Update registry (replaces the old entry of this name; its region is
    // reusable once nothing executes from it any more)
    BytecodeRegistry::Lock lock;
    uint32_t flash_addr = registry_.getPartitionAddress() + partition_offset_;
    if (!registry_.add(current_name_, partition_offset_, total_size_, relocated_crc, flash_addr)) {
        ESP_LOGE(TAG, "Failed to update registry");
        cleanup();
        writing_ = false;
        return ESP_ERR_INVALID_STATE;
    }

    if (!registry_.save()) {
        ESP_LOGE(TAG, "Failed to save registry");
        cleanup();
        writing_ = false;
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Successfully wrote relocated bytecode for '%s'", current_name_);

    // Clean up
    cleanup();
    writing_ = false;
    current_name_[0] = '\0';

    return ESP_OK;
}
