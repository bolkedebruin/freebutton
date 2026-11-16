// Emulator shim: the firmware's random numbers (discovery pacing jitter).
// Fixed, so that runs are repeatable.
#pragma once
#include <stdint.h>
static inline uint32_t esp_random(void) { return 0; }
