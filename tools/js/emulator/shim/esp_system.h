#pragma once
#include <stdint.h>
#include "fbh.h"
static inline uint32_t esp_get_free_heap_size(void) { return fbh_free_heap(); }
