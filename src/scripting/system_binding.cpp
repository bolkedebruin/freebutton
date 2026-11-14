//
// System Hardware Abstraction Layer - ESP32 Implementation
//

#include "system_binding.h"
#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include <string>

extern const std::string deviceId;   // main.cpp

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

const char* system_hw_get_id(void) {
    return deviceId.c_str();
}

const char* system_hw_get_model(void) {
    return MODEL;
}

size_t system_hw_get_name(char* buf, const size_t cap) {
    return HomeAssistantDiscovery::copyDeviceName(buf, cap);
}

} // extern "C"
