#include <Arduino.h>
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
#include "scripting/mqtt_binding.h"
#include "media/media_store.h"

// Forward declaration - avoid include order issues with ESPAsyncWebServer
namespace Scripting {
    void cleanupWebSocketClients();
    void processWebSocketRepl();
}

using namespace FreeButton;

static const auto TAG = "main";

const std::string deviceId(platform_create_id_string());

NeoPixelController neopixels(NEOPIN);
auto& registry = MqttBrokerRegistry::instance();
// JS heap, allocated once at engine init from the internal heap. 10 KB left
// ~1 KB free with btn_a00001's script (display items, keys, covers, volume),
// too little for the MQTT strings a callback allocates. JS is meant to own
// the device logic, so it gets 16 KB.
auto& scriptEngine = Scripting::ScriptEngine::getInstance(16 * 1024);

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
    esp_log_level_set("mqtt", ESP_LOG_INFO);
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

    // Map js_user before anything else memory-maps flash (the fonts in the
    // media partition, in initializeLvgl): bytecode is relocated for that
    // virtual address, which depends on the mapping order. After the
    // filesystem: instance() also loads the registry index from LittleFS.
    BytecodeRegistry::instance();

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
        UI::ProgressBar::Config cfg = {
            .blur_background = true,
        };
        progressBar = std::make_unique<UI::ProgressBar>(main_screen, cfg);
        progressBar->show("Starting...");
    } else {
        ESP_LOGE(TAG, "Main screen initialization failed!");
        exit(-1);
    }

    progressBar->update(10, "Mounting file system..");

    progressBar->update(20, "Loading configuration...");
    configManager.loadFromFile("/config.json");

    progressBar->update(30, "WiFi starting...");
    setup_wifi_event_handler();
    start_wifi_manager();

    progressBar->update(50, "Starting web server...");
    webserver_start();

    while (!is_wifi_connected()) {
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    progressBar->update(60, "Synchronizing time...");

    // Initialize and synchronize NTP time
    const std::string ntpServer = configManager.getNtpServer().empty() ? "pool.ntp.org" : configManager.getNtpServer();
    const std::string timezone = configManager.getTimezone().empty() ? "CET-1CEST,M3.5.0,M10.5.0/3" : configManager.getTimezone();

    NtpClient::initialize(ntpServer, timezone);
    NtpClient::syncTime();

    // Create the JavaScript engine (its heap is one ~10 KB allocation) BEFORE
    // MQTT starts. Allocated later, it raced the Home Assistant discovery
    // burst after connect, failed under heap pressure, and with
    // -fno-exceptions the failed new() aborted the boot. The user script
    // still runs at the same point as before, below.
    ESP_LOGI(TAG, "Initializing JavaScript engine...");
    const bool scriptEngineReady = scriptEngine.init();

    progressBar->update(65, "Starting MQTT brokers...");

    // Start brokers and configure Home Assistant auto-discovery
    configManager.startBrokers(deviceId);

    // Home Assistant discovery: the sources are registered once and generate
    // their entities per broker with "hassdiscovery" when published.
    auto& buttons = getHardwareInitializer().getButtonManager();
    HomeAssistantDiscovery::addSource(sensorDiscoverySource());
    HomeAssistantDiscovery::addSource(buttons);
    MqttBrokerRegistry::forEachBroker([&buttons](const uint8_t brokerId, const MqttBroker* broker) {
        if (broker && broker->isConfigured() && broker->hasAutoDiscovery()) {
            buttons.subscribeHomeAssistantCommands(brokerId);
        }
    });

    progressBar->update(75, "Starting scripting engine..");

    if (scriptEngineReady) {
        ESP_LOGI(TAG, "ScriptEngine initialized successfully");

        // The button task may call into the handlers this script registers
        // while it is still running; the engine lock keeps them out.
        const Scripting::EngineLock engineLock;

        // Bytecode relocated for the previous firmware build: rebased for
        // this one when the stdlib table is the same (no deploy needed).
        BytecodeRegistry::instance().rebaseStale();

        // Load and run the boot script (main.jsc, else script.jsc), then
        // build fb. The same sequence runs after a script restart
        // (ScriptEngine::restart()).
        scriptEngine.startUserScript();
    } else {
        ESP_LOGE(TAG, "Failed to initialize ScriptEngine");
    }

    progressBar->update(90, "Finalizing setup...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Brief pause to show final progress

    progressBar->update(100, "Ready!");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Show completion

    // Hide progress bar and show main application screen with DisplayItems
    progressBar->hide();

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

    // Media store upkeep (src/media): verify or erase one free page ahead of
    // uploads, or finish a directory rollover / compaction step. Every 250 ms
    // while work is pending, else every 10 s (an erase briefly stalls flash
    // reads, so it runs here rather than in an upload).
    static unsigned long lastMediaMaintainTime = 0;
    static bool mediaWorkPending = true;
    if (currentTime - lastMediaMaintainTime > (mediaWorkPending ? 250UL : 10000UL)) {
        lastMediaMaintainTime = currentTime;
        mediaWorkPending = media_maintain();
    }

    // Clean up stale WebSocket clients every second
    if (currentTime - lastWsCleanupTime > 1000) {
        lastWsCleanupTime = currentTime;
        Scripting::cleanupWebSocketClients();
    }

    // Home Assistant discovery: non-blocking, at most one config per pass,
    // only while the broker's outbox is drained and the heap has room.
    HomeAssistantDiscovery::poll();

    // JavaScript. Script code runs on this task (and, for button handlers,
    // on the button task), always under the engine lock. MQTT messages and
    // REPL requests are queued by their own tasks and delivered here.
    if (scriptEngine.isInitialized()) {
        // A new boot script was uploaded: restart the engine with it. First,
        // so the queued MQTT messages, REPL lines and timers below already
        // belong to the new script.
        scriptEngine.processRestartRequest();
        MqttBinding::processPending();
        Scripting::processWebSocketRepl();
        int64_t delay_ms = scriptEngine.processTimers();
        // Timer processing is non-blocking, returns next timer delay or -1 if no timers
        (void)delay_ms; // We process timers every loop iteration
    }

    delay(10);
}