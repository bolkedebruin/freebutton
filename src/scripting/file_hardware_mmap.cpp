//
// Flash Memory-Mapped File Loading - Implementation
// Zero-copy bytecode execution from flash partition
//

#include "file_hardware_mmap.h"
#include <Arduino.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_flash.h>
#include <soc/soc_memory_layout.h>

static const char* TAG = "file_mmap";

extern "C" {

MappedFile* file_hw_mmap(const char* partition_label, size_t offset, size_t size) {
    if (!partition_label) {
        ESP_LOGE(TAG, "NULL partition label");
        return nullptr;
    }

    // Find the partition
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        partition_label
    );

    if (!partition) {
        ESP_LOGE(TAG, "Partition not found: %s", partition_label);
        return nullptr;
    }

    // Validate offset and size
    if (offset + size > partition->size) {
        ESP_LOGE(TAG, "Requested region exceeds partition size");
        return nullptr;
    }

    // Allocate MappedFile structure
    MappedFile* mapped = (MappedFile*)malloc(sizeof(MappedFile));
    if (!mapped) {
        ESP_LOGE(TAG, "Failed to allocate MappedFile");
        return nullptr;
    }

    // Memory map the partition region
    // Note: size must be multiple of 64KB for ESP32 MMU
    size_t aligned_size = (size + 0xFFFF) & ~0xFFFF;  // Round up to 64KB

    esp_err_t err = esp_partition_mmap(
        partition,
        offset,
        aligned_size,
        ESP_PARTITION_MMAP_DATA,  // Read-only mapping
        (const void**)&mapped->data,
        (esp_partition_mmap_handle_t*)&mapped->mmap_handle
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap partition: %s (error 0x%x)",
                 partition_label, err);
        free(mapped);
        return nullptr;
    }

    mapped->size = size;

    ESP_LOGI(TAG, "Mapped %zu bytes from partition '%s' at offset 0x%zx to address %p",
             size, partition_label, offset, mapped->data);

    // Log for stability testing across reboots
    ESP_LOGW(TAG, "VIRTUAL_ADDRESS_TEST: partition=%s offset=0x%zx virtual_addr=%p",
             partition_label, offset, mapped->data);

    return mapped;
}

void file_hw_munmap(MappedFile* mapped) {
    if (!mapped) return;

    if (mapped->mmap_handle) {
        esp_partition_munmap((esp_partition_mmap_handle_t)mapped->mmap_handle);
        ESP_LOGI(TAG, "Unmapped region at %p", mapped->data);
    }

    free(mapped);
}

} // extern "C"
