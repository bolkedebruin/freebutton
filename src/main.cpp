#include <Arduino.h>
#include <map>
#include <Wire.h>

#include "config.h"
#include "pins.h"
#include "components/ComponentManager.h"
#include "core/WebServer.h"
#include "core/WifiManager.h"
#include "core/HardwareInitializer.h"
#include "core/NtpClient.h"
#include "drivers/BusAwareFileSystem.h"
#include "drivers/NeoPixel.h"
#include "mqtt/Broker.h"
#include "mqtt/Registry.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include "utils/helpers.h"
#include "drivers/Sensors.h"
#include "drivers/LvglDisplay.h"
#include "ui/ProgressBar.h"

using namespace FreeButton;

static const auto TAG = "main";

const std::string deviceId(platform_create_id_string());

const std::map<std::string_view, std::string_view> deviceInfo = {
    {"name", "Button+ Base"},
    {"manufacturer", "ButtonPlus"},
    {"model", "Button+ " MACRO_TO_STRING(MODEL)},
    {"sw_version", MACRO_TO_STRING(FIRMWARE)},
};

NeoPixelController neopixels(NEOPIN);
auto& registry = MqttBrokerRegistry::instance();

// Global ComponentManager
freebutton::ComponentManager componentManager;

// LVGL task for UI updates
void lvgl_task(void* pvParameter) {
    while (true) {
        getLvglDisplay().task();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void log_free_heap() {
    ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
}

void setup() {
    ESP_LOGI(TAG, "FreeButton Firmware: %s  Id: %s starting!", FIRMWARE, deviceId.c_str());

    HomeAssistantDiscovery::setDeviceInfo({"test", "ButtonPlus", MODEL, FIRMWARE, deviceId});

    // set log levels
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("broker", ESP_LOG_DEBUG);
    esp_log_level_set("displayitem", ESP_LOG_DEBUG);
    esp_log_level_set("mqtt", ESP_LOG_DEBUG);
    esp_log_level_set("item", ESP_LOG_DEBUG);
    esp_log_level_set("sensor", ESP_LOG_DEBUG);
    esp_log_level_set("config", ESP_LOG_DEBUG);
    esp_log_level_set("button", ESP_LOG_DEBUG);

    neopixels.begin();
    neopixels.flash();

    // Initialize all hardware (I2C, sensors, displays, buttons)
    if (!initializeAllHardware()) {
        ESP_LOGE(TAG, "Hardware initialization failed!");
        return;
    }

    auto &displayManager = getDisplayManager();
    initBusAwareFileSystem(displayManager.getMainDisplay(), true, "", 10, "data");

    // Initialize LVGL display
    if (initializeLvgl(displayManager)) {
        // Start LVGL task
        xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, nullptr, 5, nullptr, 1);
        ESP_LOGI(TAG, "LVGL initialized successfully");
    } else {
        ESP_LOGE(TAG, "LVGL initialization failed!");
    }

    std::unique_ptr<UI::ProgressBar> progressBar;
    if (auto* main_screen = getLvglDisplay().getMainScreen(); main_screen) {
        progressBar = std::make_unique<UI::ProgressBar>(main_screen);
        progressBar->show("Starting...");
    } else {
        ESP_LOGE(TAG, "Main screen initialization failed!");
    }

    // Update progress
    if (progressBar) {
        progressBar->update(10, "Mounting file system..");
    }

    if (progressBar) {
        progressBar->update(20, "Loading configuration...");
    }
    componentManager.loadFromFile("/config.json");

    // Update progress
    if (progressBar) {
        progressBar->update(40, "WiFi starting...");
    }

    setup_wifi_event_handler();
    start_wifi_manager();

    log_free_heap();
    if (progressBar) {
        progressBar->update(60, "Starting web server...");
    }
    webserver_start();
    log_free_heap();

    while (!is_wifi_connected()) {
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (progressBar) {
        progressBar->update(75, "Synchronizing time...");
    }

    // Initialize and synchronize NTP time
    const std::string ntpServer = componentManager.getNtpServer().empty() ? "pool.ntp.org" : componentManager.getNtpServer();
    const std::string timezone = componentManager.getTimezone().empty() ? "CET-1CEST,M3.5.0,M10.5.0/3" : componentManager.getTimezone();

    NtpClient::initialize(ntpServer, timezone);
    NtpClient::syncTime();

    if (progressBar) {
        progressBar->update(85, "Starting MQTT brokers...");
    }

    // Start brokers and configure Home Assistant auto-discovery
    componentManager.startBrokers(deviceId);

    // Register sensors with Home Assistant Discovery (using first configured broker)
    MqttBrokerRegistry::forEachBroker([](const uint8_t brokerId, const MqttBroker* broker) {
        if (broker && broker->isConfigured() && broker->hasAutoDiscovery()) {
            registerSensorsWithDiscovery(brokerId);
        }
    });

    if (progressBar) {
        progressBar->update(90, "Finalizing setup...");
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // Brief pause to show final progress

    if (progressBar) {
        progressBar->update(100, "Ready!");
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Show completion

    // Hide progress bar and show main application screen with DisplayItems
    if (progressBar) {
        progressBar->hide();

        // Request creation of the Google Hello button on the main screen (where DisplayItems are)
        //getLvglDisplay().requestGoogleHelloButton();

        ESP_LOGI(TAG, "Progress bar hidden, main UI loaded");
    }

    log_free_heap();
}

void loop() {
    static unsigned long lastSensorUpdateTime = 0;

    if (const unsigned long currentTime = millis(); currentTime - lastSensorUpdateTime > SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdateTime = currentTime;
        //ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());

        sampleSensors();
    }

    delay(10);
}