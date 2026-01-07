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
#include "scripting/BytecodeRegistry.h"

// Forward declaration - avoid include order issues with ESPAsyncWebServer
namespace Scripting {
    void cleanupWebSocketClients();
}

extern "C" {
    #include "scripting/file_hardware_mmap.h"
    #include <mquickjs.h>
    #include <esp_crc.h>
}

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

        // Load and execute user script from flash if it exists
        // Must load bytecode BEFORE executing any JavaScript to avoid RAM atom conflicts
        JSContext* ctx = scriptEngine.getContext();

        // Find bytecode in registry
        BytecodeEntry entry;
        if (bytecode_registry_find("script.jsc", &entry)) {
            ESP_LOGI(TAG, "Found user script in bytecode registry");

            // Memory map from flash
            MappedFile* mapped = file_hw_mmap("js_user", entry.offset, entry.size);
            if (mapped) {
                // Verify checksum
                uint32_t checksum = esp_crc32_le(0, mapped->data, mapped->size);
                if (checksum == entry.checksum) {
                    // Verify bytecode relocation before loading
                    auto hdr = reinterpret_cast<const JSBytecodeHeader *>(mapped->data);
                    uintptr_t expected_base = reinterpret_cast<uintptr_t>(hdr + 1);

                    ESP_LOGI(TAG, "Bytecode header: base_addr=0x%lx, expected=0x%lx",
                             (unsigned long)hdr->base_addr, (unsigned long)expected_base);

                    if (hdr->base_addr != expected_base) {
                        ESP_LOGE(TAG, "Bytecode relocation mismatch! Need to re-upload.");
                        file_hw_munmap(mapped);
                    } else {
                        // Load bytecode directly from flash (zero-copy, already relocated)
                        JSValue userFunc = JS_LoadBytecode(ctx, mapped->data);

                        if (!JS_IsException(userFunc)) {
                            ESP_LOGI(TAG, "User bytecode loaded successfully");
                            JSValue result = JS_Run(ctx, userFunc);

                            if (JS_IsException(result)) {
                                ESP_LOGE(TAG, "User script execution failed");
                                JSValue exception = JS_GetException(ctx);
                                JSCStringBuf buf;
                                const char* err = JS_ToCString(ctx, exception, &buf);
                                if (err) {
                                    ESP_LOGE(TAG, "Error: %s", err);
                                }

                                // Also try to get the stack trace
                                JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
                                if (!JS_IsUndefined(stack)) {
                                    JSCStringBuf stack_buf;
                                    const char* stack_str = JS_ToCString(ctx, stack, &stack_buf);
                                    if (stack_str) {
                                        ESP_LOGE(TAG, "Stack: %s", stack_str);
                                    }
                                }
                            } else {
                                ESP_LOGI(TAG, "User script executed successfully");

                                // Print the result
                                JSCStringBuf buf;
                                const char* result_str = JS_ToCString(ctx, result, &buf);
                                if (result_str) {
                                    ESP_LOGI(TAG, "Result: %s", result_str);
                                }
                            }
                        } else {
                            ESP_LOGE(TAG, "Failed to load bytecode");
                            JSValue exception = JS_GetException(ctx);
                            JSCStringBuf buf;
                            const char* err = JS_ToCString(ctx, exception, &buf);
                            if (err) {
                                ESP_LOGE(TAG, "Error: %s", err);
                            }
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "Bytecode checksum mismatch");
                }
                // Note: We keep mapped for zero-copy execution
            } else {
                ESP_LOGE(TAG, "Failed to memory map user script");
            }
        } else {
            ESP_LOGI(TAG, "No user script found");
            ESP_LOGI(TAG, "Upload script.jsc via web interface to run custom code");
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
    static unsigned long lastWsCleanupTime = 0;

    const unsigned long currentTime = millis();

    if (currentTime - lastSensorUpdateTime > SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdateTime = currentTime;
        sampleSensors();
    }

    // Clean up stale WebSocket clients every second
    if (currentTime - lastWsCleanupTime > 1000) {
        lastWsCleanupTime = currentTime;
        Scripting::cleanupWebSocketClients();
    }

    // Process JavaScript timers
    if (scriptEngine.isInitialized()) {
        int64_t delay_ms = scriptEngine.processTimers();
        // Timer processing is non-blocking, returns next timer delay or -1 if no timers
        (void)delay_ms; // We process timers every loop iteration
    }

    delay(10);
}