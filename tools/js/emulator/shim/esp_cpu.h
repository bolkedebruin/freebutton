// Emulator shim: the CPU cycle counter the firmware's render statistics read.
#pragma once
#include <stdint.h>
static inline uint32_t esp_cpu_get_cycle_count(void) { return 0; }
