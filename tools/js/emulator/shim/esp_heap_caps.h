// Emulator shim: the general heap. The virtual device has room (the host's
// "free heap" figure); nothing here models fragmentation.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "fbh.h"
#define MALLOC_CAP_INTERNAL 0x800
#define MALLOC_CAP_8BIT 0x4
#define MALLOC_CAP_DEFAULT 0x1000
static inline size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return fbh_free_heap(); }
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return fbh_free_heap(); }
static inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return fbh_free_heap(); }
