// Emulator shim: the general heap. Allocations are the wasm's malloc; the
// free figures are the host's "free heap" (nothing here models fragmentation).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "fbh.h"
#define MALLOC_CAP_INTERNAL 0x800
#define MALLOC_CAP_8BIT 0x4
#define MALLOC_CAP_DMA 0x8
#define MALLOC_CAP_DEFAULT 0x1000
static inline size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return fbh_free_heap(); }
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return fbh_free_heap(); }
static inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return fbh_free_heap(); }
static inline void* heap_caps_malloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
static inline void heap_caps_free(void* p) { free(p); }
