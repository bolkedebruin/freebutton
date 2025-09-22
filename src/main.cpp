#include <Arduino.h>
#include <map>
#include <Wire.h>

#include "config.h"
#include "pins.h"
#include "core/FileSystem.h"
#include "core/WebServer.h"
#include "core/WifiManager.h"
#include "core/HardwareInitializer.h"
#include "drivers/NeoPixel.h"
#include "mqtt/Broker.h"
#include "mqtt/Registry.h"
#include "mqtt/Component.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include "utils/helpers.h"
#include "drivers/Sensors.h"
#include "drivers/LvglDisplay.h"

using namespace FreeButton;

static const auto TAG = "main";

class MyComponent : public MqttSubscriber {
public:
    void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) override {
        printf("Received message on broker %d, topic: %.*s, data: %.*s\n",
               brokerId,
               static_cast<int>(topic.length()), topic.data(),
               static_cast<int>(data.length()), data.data());
    }

    [[nodiscard]] const std::string getComponentType() const override {
        return "MyComponent";
    }
};



const std::string deviceId(platform_create_id_string());

const std::map<std::string_view, std::string_view> deviceInfo = {
    {"name", "Button+ Base"},
    {"manufacturer", "ButtonPlus"},
    {"model", "Button+ " MACRO_TO_STRING(HW)},
    {"sw_version", MACRO_TO_STRING(FIRMWARE)},
};

NeoPixelController neopixels(NEOPIN);
auto& registry = MqttBrokerRegistry::instance();

// LVGL task for UI updates
void lvgl_task(void* pvParameter) {
    while (true) {
        FreeButton::getLvglDisplay().task();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

std::unique_ptr<MyComponent> globalComponent;

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

    neopixels.begin();
    neopixels.flash();

    // Initialize all hardware (I2C, sensors, displays, buttons)
    if (!initializeAllHardware()) {
        ESP_LOGE(TAG, "Hardware initialization failed!");
        return;
    }

    // Initialize LVGL display with progress bar
    if (FreeButton::initializeLvgl(FreeButton::getDisplayManager())) {
        FreeButton::getLvglDisplay().showProgressBar("Starting WiFi...");

        // Start LVGL task
        xTaskCreatePinnedToCore(lvgl_task, "lvgl", 4096, nullptr, 5, nullptr, 1);
        ESP_LOGI(TAG, "LVGL initialized successfully");
    } else {
        ESP_LOGE(TAG, "LVGL initialization failed!");
    }

    // Update progress
    FreeButton::getLvglDisplay().updateProgress(25, "WiFi starting...");

    setup_wifi_event_handler();
    start_wifi_manager();

    log_free_heap();
    FreeButton::getLvglDisplay().updateProgress(50, "Starting web server...");
    webserver_start();
    log_free_heap();

    FreeButton::getLvglDisplay().updateProgress(70, "Configuring MQTT...");

    auto broker = std::make_unique<MqttBroker>("homeserver", "mqtt://192.168.1.10");
    
    // Set up connection callback for HomeAssistant discovery
    broker->setOnConnectedCallback([](const uint8_t brokerId) {
        HomeAssistantDiscovery::onBrokerConnected(brokerId);
    });
    HomeAssistantDiscovery::setDeviceInfo({.identifiers = deviceId});

    registry.addBroker(std::move(broker));
    registry.connectAll();

    globalComponent = std::make_unique<MyComponent>();
    const uint8_t brokerId = MqttBrokerRegistry::getBrokerId("homeserver");
    globalComponent->addSubscription(brokerId, "test/topic", 1);

    HomeAssistantDiscovery::configureBroker(brokerId);
    HomeAssistantDiscovery::registerComponent(brokerId, globalComponent.get(), "sensor", "my_component_1", "My Component 1", "temperature");
    
    // Register sensors with Home Assistant Discovery
    registerSensorsWithDiscovery(brokerId);

    FreeButton::getLvglDisplay().updateProgress(90, "Finalizing setup...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Brief pause to show final progress

    FreeButton::getLvglDisplay().updateProgress(100, "Ready!");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Show completion

    FreeButton::getLvglDisplay().hideProgressBar();

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