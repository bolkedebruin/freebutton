//
// Bytecode Writer - Implementation with streaming relocation
// Flash write operations for user bytecode
//

#include "BytecodeWriter.h"
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_crc.h>
#include <cstring>
#include "drivers/BusAwareFileSystem.h"

// Include QuickJS for bytecode validation and structures
extern "C" {
#include "lib/mquickjs/mquickjs.h"
#include "lib/mquickjs/mquickjs_priv.h"  // For JS_MTAG_* constants

// QuickJS internal relocation structures and functions for streaming ROM atom remapping
typedef struct {
    JSContext *ctx;
    uintptr_t offset;        // Relocation offset (target - base)
    int update_atoms;        // BOOL
    uintptr_t current_ptr;   // Current position in bytecode buffer (for ROM translation)
    uint32_t data_offset;    // Offset from start of bytecode data (for ROM table lookup)
    void* rom_collect_buf;   // ROM collection buffer (WASM only, for building ROM table)
    int* rom_collect_count;  // ROM collection count (WASM only)
} BCRelocState;

int JS_RelocateHdr(BCRelocState *s, JSBytecodeHeader *hdr, uintptr_t new_base_addr);
int JS_RelocateMtag(BCRelocState *s, uint8_t *buf, uint32_t buf_len);
extern const JSSTDLibraryDef js_stdlib;  // For creating temp context with ROM atoms
}

static auto TAG = "BytecodeWriter";

BytecodeWriter::BytecodeWriter()
    : partition_(nullptr)
    , partition_offset_(0)
    , write_offset_(0)
    , total_size_(0)
    , crc32_(0)
    , writing_(false)
    , reloc_state_(RelocationState::HEADER_PENDING)
    , overflow_size_(0)
    , relocation_offset_(0)
    , relocation_target_addr_(0) {

    current_name_[0] = '\0';

    // Load registry
    registry_.load();

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
    //cleanupTempFile();
}

bool BytecodeWriter::validate(const uint8_t* bytecode, size_t size) {
    if (!bytecode || size == 0) {
        ESP_LOGE(TAG, "Invalid bytecode pointer or size");
        return false;
    }

    if (size > MAX_BYTECODE_SIZE) {
        ESP_LOGE(TAG, "Bytecode too large: %zu bytes (max %zu)",
                 size, MAX_BYTECODE_SIZE);
        return false;
    }

    // Use QuickJS validation
    if (!JS_IsBytecode(bytecode, size)) {
        ESP_LOGE(TAG, "Invalid QuickJS bytecode format");
        return false;
    }

    ESP_LOGI(TAG, "Bytecode validation passed (%zu bytes)", size);
    return true;
}

esp_err_t BytecodeWriter::eraseRegion(uint32_t offset, size_t size) {
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

void BytecodeWriter::cleanupTempFile() {
    if (temp_file_) {
        temp_file_.close();
    }

    if (temp_path_.length() > 0) {
        auto& fs = FreeButton::getBusAwareFileSystem();
        if (fs.exists(temp_path_)) {
            fs.remove(temp_path_);
            ESP_LOGI(TAG, "Cleaned up temp file: %s", temp_path_.c_str());
        }
        temp_path_ = "";
    }
}

esp_err_t BytecodeWriter::streamRelocateToFlash() {
    ESP_LOGI(TAG, "Starting streaming relocation with ROM atom translation (4KB chunks)");

    // Create temporary QuickJS context for ROM atom remapping
    const size_t ctx_heap_size = 8 * 1024;  // 8KB
    uint8_t* ctx_mem_buf = (uint8_t*)malloc(ctx_heap_size);
    if (!ctx_mem_buf) {
        ESP_LOGE(TAG, "Failed to allocate context heap");
        return ESP_ERR_NO_MEM;
    }

    JSContext* temp_ctx = JS_NewContext(ctx_mem_buf, ctx_heap_size, &js_stdlib);
    if (!temp_ctx) {
        ESP_LOGE(TAG, "Failed to create temporary QuickJS context");
        free(ctx_mem_buf);
        return ESP_ERR_NO_MEM;
    }

    // ROM translation table buffer (allocated if v0x0002 bytecode)
    uint8_t* rom_table_buf = nullptr;

    // Create BCRelocState for streaming relocation
    // ROM atoms: translated via ROM translation table (happens first in bc_reloc_value)
    // Non-ROM strings: embedded in bytecode via update_atoms=1
    BCRelocState reloc_state;
    reloc_state.ctx = temp_ctx;
    reloc_state.offset = relocation_target_addr_ + sizeof(JSBytecodeHeader);  // Data section offset
    reloc_state.update_atoms = 1;  // Embed non-ROM strings; ROM atoms handled by translation table
    reloc_state.current_ptr = 0;   // Will be set by JS_RelocateMtag() for each block
    reloc_state.data_offset = 0;   // Current position in bytecode data (updated per chunk)
    reloc_state.rom_collect_buf = nullptr;   // Not used on ESP32 (WASM only)
    reloc_state.rom_collect_count = nullptr; // Not used on ESP32 (WASM only)

    ESP_LOGI(TAG, "Streaming relocation with ROM translation table");

    auto& fs = FreeButton::getBusAwareFileSystem();

    // Open temp file for reading
    File temp_file = fs.open(temp_path_.c_str(), FILE_READ);
    if (!temp_file) {
        ESP_LOGE(TAG, "Failed to open temp file: %s", temp_path_.c_str());
        JS_FreeContext(temp_ctx);
        free(ctx_mem_buf);
        return ESP_ERR_INVALID_STATE;
    }

    // Calculate relocation offset (from base_addr 0 to target)
    // Pointers in bytecode are relative to DATA (after header), not start of file
    relocation_offset_ = relocation_target_addr_ + sizeof(JSBytecodeHeader);
    ESP_LOGI(TAG, "Relocation offset: 0x%lx (target: 0x%lx, data starts at: 0x%lx)",
             relocation_offset_, relocation_target_addr_,
             relocation_target_addr_ + sizeof(JSBytecodeHeader));

    uint32_t flash_write_offset = partition_offset_;
    reloc_state_ = RelocationState::HEADER_PENDING;
    overflow_size_ = 0;

    // Erase flash region upfront
    size_t aligned_size = ((total_size_ + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;
    esp_err_t err = eraseRegion(partition_offset_, aligned_size);
    if (err != ESP_OK) {
        temp_file.close();
        return err;
    }

    // Process file in chunks
    while (temp_file.available()) {

        if (reloc_state_ == RelocationState::HEADER_PENDING) {
            // Read and relocate header
            size_t header_size = sizeof(JSBytecodeHeader);
            size_t bytes_read = temp_file.read(chunk_buffer_, header_size);

            if (bytes_read != header_size) {
                ESP_LOGE(TAG, "Failed to read header (got %zu, expected %zu)",
                         bytes_read, header_size);
                temp_file.close();
                return ESP_ERR_INVALID_STATE;
            }

            auto* hdr = reinterpret_cast<JSBytecodeHeader*>(chunk_buffer_);

            // Verify it's position-independent
            if (hdr->base_addr != 0) {
                ESP_LOGE(TAG, "Bytecode not position-independent (base_addr: 0x%lx)",
                         (uint32_t)hdr->base_addr);
                temp_file.close();
                JS_FreeContext(temp_ctx);
                free(ctx_mem_buf);
                return ESP_ERR_INVALID_ARG;
            }

            // Check for ROM table size (needed for correct base_addr calculation)
            size_t rom_table_size = JS_GetRomTableSize(hdr);
            // IMPORTANT: Save rom_atom_count NOW before chunk_buffer_ gets overwritten!
            uint16_t rom_atom_count = hdr->rom_atom_count;

            ESP_LOGI(TAG, "Header BEFORE relocation: version=0x%04x, rom_atom_count=%u, base_addr=0x%lx",
                     hdr->version, rom_atom_count, (uint32_t)hdr->base_addr);
            ESP_LOGI(TAG, "ROM table size: %zu bytes, target_base_addr will be: 0x%lx",
                     rom_table_size, relocation_target_addr_ + sizeof(JSBytecodeHeader) + rom_table_size);

            // Use JS_RelocateHdr for header relocation
            // For v0x0002, base_addr must point to data section (after ROM table)
            uintptr_t target_base_addr = relocation_target_addr_ + sizeof(JSBytecodeHeader) + rom_table_size;
            if (JS_RelocateHdr(&reloc_state, hdr, target_base_addr) != 0) {
                ESP_LOGE(TAG, "JS_RelocateHdr failed");
                temp_file.close();
                JS_FreeContext(temp_ctx);
                free(ctx_mem_buf);
                return ESP_ERR_INVALID_STATE;
            }

            ESP_LOGI(TAG, "Header AFTER relocation: version=0x%04x, rom_atom_count=%u, base_addr=0x%lx",
                     hdr->version, hdr->rom_atom_count, (uint32_t)hdr->base_addr);

            // Write relocated header to flash
            err = esp_partition_write(partition_, flash_write_offset,
                                      chunk_buffer_, header_size);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write header to flash: 0x%x", err);
                temp_file.close();
                return err;
            }

            flash_write_offset += header_size;

            ESP_LOGI(TAG, "Header relocated and written to flash");

            // Copy ROM translation table if present (rom_table_size calculated earlier)
            if (rom_table_size > 0) {
                // Read and copy ROM table (don't process it!)
                ESP_LOGI(TAG, "v0x0002 bytecode detected: copying ROM table (%u entries, %zu bytes)",
                         hdr->rom_atom_count, rom_table_size);

                size_t rom_bytes_read = temp_file.read(chunk_buffer_, rom_table_size);
                if (rom_bytes_read != rom_table_size) {
                    ESP_LOGE(TAG, "Failed to read ROM table (got %zu, expected %zu)",
                             rom_bytes_read, rom_table_size);
                    temp_file.close();
                    JS_FreeContext(temp_ctx);
                    free(ctx_mem_buf);
                    return ESP_ERR_INVALID_STATE;
                }

                // Allocate persistent buffer for ROM translation table
                // (chunk_buffer_ will be reused for bytecode blocks)
                rom_table_buf = (uint8_t*)malloc(rom_table_size);
                if (!rom_table_buf) {
                    ESP_LOGE(TAG, "Failed to allocate ROM table buffer");
                    temp_file.close();
                    JS_FreeContext(temp_ctx);
                    free(ctx_mem_buf);
                    return ESP_ERR_NO_MEM;
                }
                memcpy(rom_table_buf, chunk_buffer_, rom_table_size);

                // Set up global ROM translation table for use during relocation
                // This allows bc_reloc_value() to translate WASM ROM pointers → ESP32 ROM pointers
                extern const ROMAtomEntry* g_rom_translation_table;
                extern uint16_t g_rom_translation_count;
                g_rom_translation_table = (const ROMAtomEntry*)rom_table_buf;
                g_rom_translation_count = rom_atom_count;  // Use saved count (hdr overwritten by ROM table read!)
                ESP_LOGI(TAG, "Set up ROM translation table (%u entries) for relocation", g_rom_translation_count);

                // Dump ROM table for debugging
                ESP_LOGI(TAG, "=== ROM Translation Table ===");
                for (uint16_t i = 0; i < g_rom_translation_count && i < 20; i++) {
                    ESP_LOGI(TAG, "  [%2u] offset=0x%04lx → rom_index=%u",
                             i,
                             (unsigned long)g_rom_translation_table[i].bytecode_offset,
                             g_rom_translation_table[i].rom_index);
                }
                if (g_rom_translation_count > 20) {
                    ESP_LOGI(TAG, "  ... (%u more entries)", g_rom_translation_count - 20);
                }
                ESP_LOGI(TAG, "=============================");

                // Write ROM table to flash as-is (no relocation needed)
                err = esp_partition_write(partition_, flash_write_offset,
                                          chunk_buffer_, rom_table_size);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to write ROM table to flash: 0x%x", err);
                    free(rom_table_buf);
                    temp_file.close();
                    JS_FreeContext(temp_ctx);
                    free(ctx_mem_buf);
                    return ESP_ERR_INVALID_STATE;
                }

                flash_write_offset += rom_table_size;
                ESP_LOGI(TAG, "ROM table copied to flash");

                // NOTE: rom_table_buf will be freed at the end of relocation
                //       (we need it for the entire relocation process)
            }

            reloc_state_ = RelocationState::BLOCKS_PENDING;
        }

        if (reloc_state_ == RelocationState::BLOCKS_PENDING) {
            // Read next chunk
            size_t chunk_read = temp_file.read(chunk_buffer_, CHUNK_SIZE);

            if (chunk_read == 0) {
                break;  // EOF
            }

            // Prepend overflow from previous chunk if any
            if (overflow_size_ > 0) {
                // Check if prepending would overflow
                if (overflow_size_ + chunk_read > CHUNK_SIZE) {
                    // Need to handle large block - allocate temp buffer
                    ESP_LOGW(TAG, "Large block detected, overflow=%zu + chunk=%zu",
                             overflow_size_, chunk_read);

                    uint8_t* large_buffer = (uint8_t*)malloc(overflow_size_ + chunk_read);
                    if (!large_buffer) {
                        ESP_LOGE(TAG, "Failed to allocate large buffer");
                        temp_file.close();
                        return ESP_ERR_NO_MEM;
                    }

                    // Combine overflow and chunk
                    memcpy(large_buffer, overflow_buffer_, overflow_size_);
                    memcpy(large_buffer + overflow_size_, chunk_buffer_, chunk_read);
                    size_t combined_size = overflow_size_ + chunk_read;

                    // Process the large block using JS_RelocateMtag
                    size_t processed = 0;
                    uint8_t* block_start = large_buffer;
                    size_t available = combined_size;

                    // Get block size and relocate with ROM atom remapping
                    size_t block_size = JS_GetMemBlockSize(block_start);
                    if (block_size > available) {
                        ESP_LOGE(TAG, "Block size %zu exceeds available %zu", block_size, available);
                        free(large_buffer);
                        temp_file.close();
                        JS_FreeContext(temp_ctx);
                        free(ctx_mem_buf);
                        return ESP_ERR_INVALID_STATE;
                    }

                    if (JS_RelocateMtag(&reloc_state, block_start, block_size) != 0) {
                        ESP_LOGE(TAG, "JS_RelocateMtag failed for large block");
                        free(large_buffer);
                        temp_file.close();
                        JS_FreeContext(temp_ctx);
                        free(ctx_mem_buf);
                        return ESP_ERR_INVALID_STATE;
                    }

                    processed = block_size;
                    reloc_state.data_offset += block_size;  // Update position in bytecode data
                    if (processed > 0 && processed <= combined_size) {

                        // Write relocated block to flash
                        err = esp_partition_write(partition_, flash_write_offset,
                                                  block_start, processed);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to write large block: 0x%x", err);
                            free(large_buffer);
                            temp_file.close();
                            return err;
                        }

                        flash_write_offset += processed;

                        // Save any remaining to overflow
                        overflow_size_ = combined_size - processed;
                        if (overflow_size_ > 0) {
                            memcpy(overflow_buffer_, large_buffer + processed, overflow_size_);
                        }
                    }

                    free(large_buffer);
                    continue;
                }

                // Normal case: prepend overflow
                memmove(chunk_buffer_ + overflow_size_, chunk_buffer_, chunk_read);
                memcpy(chunk_buffer_, overflow_buffer_, overflow_size_);
                chunk_read += overflow_size_;
                overflow_size_ = 0;
            }

            // Process blocks in this chunk using JS_RelocateMtag
            size_t processed = 0;
            while (processed < chunk_read) {
                uint8_t* block_start = chunk_buffer_ + processed;
                size_t available = chunk_read - processed;

                // Check if we have enough bytes for a block header
                if (available < 4) {
                    // Partial block - save to overflow and read more
                    overflow_size_ = available;
                    memcpy(overflow_buffer_, block_start, available);
                    ESP_LOGD(TAG, "Partial block header saved to overflow: %zu bytes", overflow_size_);
                    break;
                }

                // Get block size
                size_t block_size = JS_GetMemBlockSize(block_start);
                if (block_size > available) {
                    // Partial block - save to overflow and read more
                    overflow_size_ = available;
                    memcpy(overflow_buffer_, block_start, available);
                    ESP_LOGD(TAG, "Partial block saved to overflow: %zu bytes", overflow_size_);
                    break;
                }

                // Relocate block with ROM atom remapping
                if (JS_RelocateMtag(&reloc_state, block_start, block_size) != 0) {
                    ESP_LOGE(TAG, "JS_RelocateMtag failed for block");
                    temp_file.close();
                    JS_FreeContext(temp_ctx);
                    free(ctx_mem_buf);
                    return ESP_ERR_INVALID_STATE;
                }

                processed += block_size;
                reloc_state.data_offset += block_size;  // Update position in bytecode data
            }

            // Write relocated blocks to flash (excluding overflow)
            size_t write_size = processed;
            if (write_size > 0) {
                err = esp_partition_write(partition_, flash_write_offset,
                                          chunk_buffer_, write_size);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to write data to flash: 0x%x", err);
                    temp_file.close();
                    return err;
                }

                flash_write_offset += write_size;
                ESP_LOGD(TAG, "Wrote %zu bytes to flash (offset 0x%lx)",
                         write_size, flash_write_offset - write_size);
            }
        }
    }

    temp_file.close();

    // Handle any remaining overflow (shouldn't happen if file is well-formed)
    if (overflow_size_ > 0) {
        ESP_LOGW(TAG, "Trailing data in overflow buffer: %zu bytes", overflow_size_);

        // Write it anyway (might be padding)
        err = esp_partition_write(partition_, flash_write_offset,
                                  overflow_buffer_, overflow_size_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write overflow to flash: 0x%x", err);
            return err;
        }
        flash_write_offset += overflow_size_;
    }

    reloc_state_ = RelocationState::COMPLETE;
    ESP_LOGI(TAG, "Streaming relocation complete, wrote %lu bytes",
             flash_write_offset - partition_offset_);

    // Keep temp file for debugging (can download via /api/bytecode/download)
    // cleanupTempFile();
    ESP_LOGI(TAG, "Temp file kept for debugging: %s", temp_path_.c_str());

    // Recalculate CRC32 on the relocated bytecode in flash
    // Reuse chunk_buffer_ to avoid extra allocation
    ESP_LOGI(TAG, "Calculating CRC32 of relocated bytecode...");
    uint32_t relocated_crc = 0;
    uint32_t bytes_remaining = total_size_;
    uint32_t read_offset = partition_offset_;

    while (bytes_remaining > 0) {
        size_t chunk_size = (bytes_remaining > CHUNK_SIZE) ? CHUNK_SIZE : bytes_remaining;

        err = esp_partition_read(partition_, read_offset, chunk_buffer_, chunk_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read back for CRC32: 0x%x", err);
            return err;
        }

        relocated_crc = esp_crc32_le(relocated_crc, chunk_buffer_, chunk_size);

        bytes_remaining -= chunk_size;
        read_offset += chunk_size;
    }

    ESP_LOGI(TAG, "Relocated bytecode CRC32: 0x%lx (original was 0x%lx)",
             relocated_crc, crc32_);

    // Update registry with final bytecode CRC32
    uint32_t flash_addr = registry_.getPartitionAddress() + partition_offset_;
    if (!registry_.add(current_name_, partition_offset_, total_size_, relocated_crc, flash_addr)) {
        ESP_LOGE(TAG, "Failed to update registry");
        return ESP_ERR_INVALID_STATE;
    }

    if (!registry_.save()) {
        ESP_LOGE(TAG, "Failed to save registry");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Successfully wrote relocated bytecode for '%s'", current_name_);

    // Clean up QuickJS context
    JS_FreeContext(temp_ctx);
    free(ctx_mem_buf);

    // Clean up ROM translation table and clear global pointers
    if (rom_table_buf) {
        extern const ROMAtomEntry* g_rom_translation_table;
        extern uint16_t g_rom_translation_count;
        g_rom_translation_table = nullptr;
        g_rom_translation_count = 0;
        free(rom_table_buf);
        ESP_LOGI(TAG, "Cleaned up ROM translation table");
    }

    return ESP_OK;
}

esp_err_t BytecodeWriter::write(const char* name, const uint8_t* bytecode,
                                size_t size, bool final) {
    if (!partition_) {
        ESP_LOGE(TAG, "Partition not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!name || !bytecode || size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // First chunk - initialize write
    if (!writing_) {
        ESP_LOGI(TAG, "Starting bytecode upload: %s", name);

        // Validate bytecode header
        if (size < sizeof(JSBytecodeHeader)) {
            ESP_LOGE(TAG, "First chunk too small for bytecode header");
            return ESP_ERR_INVALID_ARG;
        }

        if (!JS_IsBytecode(bytecode, size)) {
            ESP_LOGE(TAG, "Invalid bytecode format");
            return ESP_ERR_INVALID_ARG;
        }

        const auto* header = reinterpret_cast<const JSBytecodeHeader*>(bytecode);

        // REQUIRED: Must be position-independent (base_addr == 0)
        if (header->base_addr != 0) {
            ESP_LOGE(TAG, "Only position-independent bytecode supported (base_addr must be 0)");
            ESP_LOGE(TAG, "Got base_addr: 0x%lx", (uint32_t)header->base_addr);
            ESP_LOGE(TAG, "Please compile with targetAddr: 0");
            return ESP_ERR_INVALID_ARG;
        }

        ESP_LOGI(TAG, "Position-independent bytecode detected ✓");

        // Store name
        strncpy(current_name_, name, sizeof(current_name_) - 1);
        current_name_[sizeof(current_name_) - 1] = '\0';

        // Get target address for relocation
        partition_offset_ = registry_.getNextOffset();
        relocation_target_addr_ = registry_.getVirtualBaseAddress() + partition_offset_;

        ESP_LOGI(TAG, "Will relocate to: 0x%lx", relocation_target_addr_);

        // Create temp file on LittleFS
        auto& fs = FreeButton::getBusAwareFileSystem();
        temp_path_ = String("/tmp_reloc_") + name;
        temp_file_ = fs.open(temp_path_.c_str(), FILE_WRITE);

        if (!temp_file_) {
            ESP_LOGE(TAG, "Failed to create temp file: %s", temp_path_.c_str());
            return ESP_ERR_NO_MEM;
        }

        ESP_LOGI(TAG, "Created temp file: %s", temp_path_.c_str());

        write_offset_ = 0;
        total_size_ = 0;
        crc32_ = 0;
        writing_ = true;
    }

    // Check size limit
    if (total_size_ + size > MAX_BYTECODE_SIZE) {
        ESP_LOGE(TAG, "Bytecode too large: %lu + %zu > %zu",
                 total_size_, size, MAX_BYTECODE_SIZE);
        cleanupTempFile();
        writing_ = false;
        return ESP_ERR_INVALID_SIZE;
    }

    // Stream to temp file (no relocation during upload)
    size_t written = temp_file_.write(bytecode, size);
    if (written != size) {
        ESP_LOGE(TAG, "Failed to write to temp file");
        cleanupTempFile();
        writing_ = false;
        return ESP_ERR_INVALID_STATE;
    }

    // Update tracking
    total_size_ += size;
    crc32_ = esp_crc32_le(crc32_, bytecode, size);
    ESP_LOGD(TAG, "Wrote %zu bytes to temp file (total: %lu)", size, total_size_);

    if (final) {
        temp_file_.close();
        ESP_LOGI(TAG, "Upload complete (%lu bytes), starting streaming relocation with ROM atom remapping",
                 total_size_);

        // Stream-relocate from temp file to flash (4KB chunks)
        // ROM atom remapping is now done during streaming
        esp_err_t err = streamRelocateToFlash();
        if (err != ESP_OK) {
            cleanupTempFile();
            writing_ = false;
            return err;
        }

        writing_ = false;
        current_name_[0] = '\0';
    }

    return ESP_OK;
}

esp_err_t BytecodeWriter::erase(const char* name) {
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }

    // Mark as inactive in registry
    if (!registry_.remove(name)) {
        ESP_LOGW(TAG, "Bytecode '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }

    // Save registry
    if (!registry_.save()) {
        ESP_LOGE(TAG, "Failed to save registry");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Marked bytecode '%s' as inactive", name);

    return ESP_OK;
}
