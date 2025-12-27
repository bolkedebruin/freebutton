//
// Flash Memory-Mapped File Loading
// Zero-copy bytecode execution from flash partition
//

#ifndef FILE_HARDWARE_MMAP_H
#define FILE_HARDWARE_MMAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Memory-mapped file handle
typedef struct {
    const uint8_t* data;      // Pointer to mapped memory
    size_t size;              // Size of mapped region
    void* mmap_handle;        // ESP32 mmap handle (for unmapping)
} MappedFile;

// Map a bytecode file from flash partition to memory
// partition_label: Partition to map from (e.g., "js_code")
// offset: Offset within partition
// size: Size to map (must be 64KB aligned for ESP32)
// Returns mapped file handle, or NULL on error
MappedFile* file_hw_mmap(const char* partition_label, size_t offset, size_t size);

// Unmap a previously mapped file
void file_hw_munmap(MappedFile* mapped);

#ifdef __cplusplus
}
#endif

#endif // FILE_HARDWARE_MMAP_H
