#include <Arduino.h>
#include <map>
#include <Wire.h>

#include "config.h"
#include "pins.h"
#include "core/FileSystem.h"
#include "core/WebServer.h"
#include "core/WifiManager.h"
#include "drivers/NeoPixel.h"
#include "mqtt/Broker.h"
#include "utils/helpers.h"

static const auto TAG = "main";

const std::string deviceId(platform_create_id_string());

const std::map<std::string_view, std::string_view> deviceInfo = {
    {"name", "Button+ Base"},
    {"manufacturer", "ButtonPlus"},
    {"model", "Button+ " MACRO_TO_STRING(HW)},
    {"sw_version", MACRO_TO_STRING(FIRMWARE)},
};

NeoPixelController neopixels(NEOPIN);

void log_free_heap() {
    ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
}

void setup() {
    ESP_LOGI(TAG, "FreeButton Firmware: %s  Id: %s starting!", FIRMWARE, deviceId.c_str());

    init_littlefs();

    // set log levels
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("broker", ESP_LOG_DEBUG);
    esp_log_level_set("displayitem", ESP_LOG_DEBUG);
    esp_log_level_set("mqtt", ESP_LOG_DEBUG);
    esp_log_level_set("item", ESP_LOG_DEBUG);
    esp_log_level_set("sensor", ESP_LOG_DEBUG);
    esp_log_level_set("config", ESP_LOG_DEBUG);
    esp_log_level_set("button", ESP_LOG_DEBUG);

    // Two wire init
    Wire.begin(CH1_SDA, CH1_SCL);
    Wire1.begin(BUTTON_SDA, BUTTON_SCL);

    neopixels.begin();
    neopixels.flash();

    setup_wifi_event_handler();
    start_wifi_manager();

    log_free_heap();
    webserver_start();
    log_free_heap();

    auto broker = MqttBroker(0, "mqtt://192.168.1.10");
    broker.start();
    broker.sendBirth();
    log_free_heap();
}

void loop() {
    static unsigned long lastSensorUpdateTime = 0;

    if (const unsigned long currentTime = millis(); currentTime - lastSensorUpdateTime > SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdateTime = currentTime;
        ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());

        // sampleSensors();
    }

    delay(10);
}