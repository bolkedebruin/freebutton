#include <Arduino.h>
#include <map>
#include <Wire.h>

#include "config.h"
#include "pins.h"
#include "components/ConfigManager.h"
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
#include "scripting/ScriptEngine.h"

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
auto& scriptEngine = Scripting::ScriptEngine::getInstance(10 * 1024); // 10KB heap

// Global ConfigManager
freebutton::ConfigManager configManager;

// LVGL task for UI updates
void lvgl_task(void* pvParameter) {
    while (true) {
        getLvglDisplay().task();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    ESP_LOGI(TAG, "FreeButton Firmware: %s  Id: %s starting!", FIRMWARE, deviceId.c_str());

    HomeAssistantDiscovery::setDeviceInfo({deviceId, "ButtonPlus", MODEL, FIRMWARE, deviceId});

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
    configManager.loadFromFile("/config.json");

    // Update progress
    if (progressBar) {
        progressBar->update(40, "WiFi starting...");
    }

    setup_wifi_event_handler();
    start_wifi_manager();

    if (progressBar) {
        progressBar->update(60, "Starting web server...");
    }
    webserver_start();

    while (!is_wifi_connected()) {
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (progressBar) {
        progressBar->update(75, "Synchronizing time...");
    }

    // Initialize and synchronize NTP time
    const std::string ntpServer = configManager.getNtpServer().empty() ? "pool.ntp.org" : configManager.getNtpServer();
    const std::string timezone = configManager.getTimezone().empty() ? "CET-1CEST,M3.5.0,M10.5.0/3" : configManager.getTimezone();

    NtpClient::initialize(ntpServer, timezone);
    NtpClient::syncTime();

    if (progressBar) {
        progressBar->update(85, "Starting MQTT brokers...");
    }

    // Start brokers and configure Home Assistant auto-discovery
    configManager.startBrokers(deviceId);

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

    // Initialize JavaScript Engine (singleton)
    ESP_LOGI(TAG, "Initializing JavaScript engine...");

    if (scriptEngine.init()) {
        ESP_LOGI(TAG, "ScriptEngine initialized successfully");

        // Comprehensive test script showing all features
        const char* testScript = R"(
            // Basic console output
            console.log('JavaScript engine is working!');
            print('Testing print function');

            // Math operations
            var x = 42;
            var y = x * 2;
            console.log('42 * 2 = ' + y);

            // Date and time functions
            console.log('Date.now():', Date.now());
            console.log('performance.now():', performance.now());

            // Array and string methods
            var colors = ['red', 'green', 'blue'];
            console.log('Colors:', colors.join(', '));

            // LED control
            var numLeds = led.count();
            console.log('Number of LEDs:', numLeds);

            // Timers - schedule LED animations
            setTimeout(function() {
                console.log('Timer 1: Setting LED 0 to red');
                led.setColor(0, 255, 0, 0);
            }, 500);

            setTimeout(function() {
                console.log('Timer 2: Setting LED 0 to green');
                led.setColor(0, 0, 255, 0);
            }, 1500);

            setTimeout(function() {
                console.log('Timer 3: Setting LED 0 to blue');
                led.setColor(0, 0, 0, 255);
            }, 2500);

            button.onClick(8, function() {
                console.log('Button 8 clicked');
                led.setColor(2, 0, 255, 0);
                mqtt.publish(0, "test/button/8/clicked", "PRESS");
                button.setLabel(8, 'ON');
            });

            for (var i = 0; i < sensor.count(); i++) {
                console.log('Sensor ' + i + ' type ' + sensor.getType(i));
            }

            sensor.onChange(1, function(value) {
                console.log('Sensor 1 changed to ' + value);
                if (value > 25.0) {
                    led.setColor(1, 255, 0, 0); // red
                } else if (value > 23.0) {
                    led.setColor(1, 255, 165, 0); // orange
                } else {
                    led.setColor(1, 0, 0, 255); // blue
                }
            });

            console.log('Active MQTT brokers: ' + mqtt.getBrokerCount());
            console.log('Timers scheduled - will execute in main loop');
        )";

        auto result = scriptEngine.execute(testScript, "test.js");
        if (result.success) {
            ESP_LOGI(TAG, "JavaScript test executed successfully");
        } else {
            ESP_LOGE(TAG, "JavaScript test failed: %s", result.error.c_str());
        }
    } else {
        ESP_LOGE(TAG, "Failed to initialize ScriptEngine");
    }

    // Hide progress bar and show main application screen with DisplayItems
    if (progressBar) {
        progressBar->hide();
    }

    ESP_LOGI(TAG, "main UI loaded");
}

void loop() {
    static unsigned long lastSensorUpdateTime = 0;

    if (const unsigned long currentTime = millis(); currentTime - lastSensorUpdateTime > SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdateTime = currentTime;
        sampleSensors();
    }

    // Process JavaScript timers
    if (scriptEngine.isInitialized()) {
        int64_t delay_ms = scriptEngine.processTimers();
        // Timer processing is non-blocking, returns next timer delay or -1 if no timers
        (void)delay_ms; // We process timers every loop iteration
    }

    delay(10);
}