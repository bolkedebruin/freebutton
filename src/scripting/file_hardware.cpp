//
// File System Hardware Abstraction Layer - Implementation
// C-compatible interface to C++ BusAwareFileSystem
//

#include "file_hardware.h"
#include <Arduino.h>
#include <esp_log.h>
#include "../drivers/BusAwareFileSystem.h"

static const char* TAG = "file_hardware";

extern "C" {

uint8_t* file_hw_load(const char* filename, int* plen, size_t max_size) {
    if (!filename) {
        ESP_LOGE(TAG, "NULL filename provided");
        return nullptr;
    }

    // Open file using BusAwareFileSystem
    auto& fs = FreeButton::getBusAwareFileSystem();
    FreeButton::BusAwareFile file = fs.open(filename, "r");

    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename);
        return nullptr;
    }

    // Get file size
    size_t file_size = file.size();
    if (file_size == 0) {
        ESP_LOGW(TAG, "File is empty: %s", filename);
        file.close();
        return nullptr;
    }

    // Apply size limit (if max_size is 0, use default based on file type detection)
    size_t size_limit = max_size;
    if (size_limit == 0) {
        // Use conservative default - will be checked after reading header
        size_limit = MAX_JS_BYTECODE_SIZE;
    }

    if (file_size > size_limit) {
        ESP_LOGE(TAG, "File too large: %s (%zu bytes, limit %zu bytes)",
                 filename, file_size, size_limit);
        file.close();
        return nullptr;
    }

    // Allocate buffer (+1 for null terminator)
    uint8_t* buffer = (uint8_t*)malloc(file_size + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for file: %s", file_size, filename);
        file.close();
        return nullptr;
    }

    // Read file contents
    size_t bytes_read = file.read(buffer, file_size);
    file.close();

    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Failed to read file (read %zu of %zu bytes): %s",
                 bytes_read, file_size, filename);
        free(buffer);
        return nullptr;
    }

    // Null-terminate for safety (in case it's a text file)
    buffer[file_size] = '\0';

    // Set output length if requested
    if (plen) {
        *plen = (int)file_size;
    }

    ESP_LOGI(TAG, "Successfully loaded file: %s (%zu bytes)", filename, file_size);
    return buffer;
}

} // extern "C"
