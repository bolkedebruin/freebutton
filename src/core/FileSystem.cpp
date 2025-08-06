//
// Created by Bolke de Bruin on 06/08/2025.
//
#include "LittleFS.h"

static auto TAG = "filesystem";

void init_littlefs(void) {
    // Initialize LittleFS
    if (!LittleFS.begin(true, "", 10, "data")) {
        ESP_LOGE(TAG, "LittleFS Mount Failed");
        return;
    }
    ESP_LOGI(TAG, "LittleFS Mounted");
}
