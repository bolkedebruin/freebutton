//
// System Hardware Abstraction Layer - ESP32 Implementation
//

#include "system_binding.h"
#include <Arduino.h>
#include <esp_system.h>
#include "config.h"

extern "C" {

uint32_t system_hw_get_uptime(void) {
    return millis() / 1000;
}

uint32_t system_hw_get_free_heap(void) {
    return esp_get_free_heap_size();
}

const char* system_hw_get_version(void) {
    return FIRMWARE;
}

} // extern "C"
