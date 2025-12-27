//
// File System Hardware Abstraction Layer - Header
// C-compatible interface to C++ BusAwareFileSystem
//

#ifndef FILE_HARDWARE_H
#define FILE_HARDWARE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum file size for JavaScript files (64KB - conservative limit for ESP32)
// Bytecode files can be larger since they're not parsed, just relocated
#define MAX_JS_SOURCE_SIZE (64 * 1024)
#define MAX_JS_BYTECODE_SIZE (128 * 1024)

// Load a file from LittleFS filesystem with size limits
// Returns pointer to allocated buffer containing file contents, or NULL on error
// Caller is responsible for freeing the returned buffer with free()
// If plen is not NULL, it will be set to the file size
// max_size: Maximum file size to load (0 = use default limits)
uint8_t* file_hw_load(const char* filename, int* plen, size_t max_size);

#ifdef __cplusplus
}
#endif

#endif // FILE_HARDWARE_H
