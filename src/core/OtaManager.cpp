//
// Created by Claude Code on 24/12/2025.
// OTA Manager implementation
//

#include "OtaManager.h"
#include "ota_config.h"
#include "ui/ota_modal.h"
#include "drivers/LvglDisplay.h"
#include "mqtt/Registry.h"

#include <esp_log.h>
#include <esp_https_ota.h>
#include <esp_http_client.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#include <atomic>

static const char* TAG = "OtaManager";
static std::atomic<bool> ota_in_progress{false};

// HTTP event handler for OTA progress tracking
static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP header: %s = %s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // Could track progress here if content-length is available
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// OTA task function (runs in separate FreeRTOS task)
static void ota_task(void* pvParameter) {
    int firmware_id = (int)(intptr_t)pvParameter;
    ESP_LOGI(TAG, "OTA task started with firmware_id=%d", firmware_id);

    // Show OTA modal
    lv_obj_t* main_screen = FreeButton::getLvglDisplay().getMainScreen();
    if (main_screen) {
        show_ota_modal(main_screen, "Connecting to server...");
    }

    // Build API URL
    String url = OTA_API_BASE_URL;
    if (firmware_id > 0) {
        url += "/" + String(firmware_id);
    }
    ESP_LOGI(TAG, "OTA URL: %s", url.c_str());

    // Configure HTTP client
    esp_http_client_config_t http_config = {};
    http_config.url = url.c_str();
    http_config.cert_pem = OTA_CA_CERT;
    http_config.event_handler = ota_http_event_handler;
    http_config.keep_alive_enable = true;

    // Configure HTTPS OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    // Update modal
    update_ota_modal(10, "Downloading firmware...");

    // Perform OTA update
    esp_err_t ret = esp_https_ota(&ota_config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS OTA failed: %s", esp_err_to_name(ret));
        update_ota_modal(0, "Update failed!");
        delay(2000);
        hide_ota_modal();
        ota_in_progress.store(false, std::memory_order_release);
    } else {
        ESP_LOGI(TAG, "HTTPS OTA succeeded");
        update_ota_modal(100, "Update successful!");
        delay(500);
        ESP_LOGI(TAG, "Restarting...");
        esp_restart();
    }

    // Delete task
    vTaskDelete(NULL);
}

OtaResult ota_start_remote_update(int firmware_id) {
    ESP_LOGI(TAG, "Starting remote OTA update");

    // Check if already in progress
    if (ota_in_progress.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "OTA already in progress");
        return OtaResult::FAILED_TASK_CREATE;
    }

    // Set OTA in progress flag
    ota_in_progress.store(true, std::memory_order_release);

    // Disconnect all MQTT connections
    ESP_LOGI(TAG, "Disconnecting MQTT brokers");
    MqttBrokerRegistry::instance().disconnectAll();

    // Configure watchdog timeout (30 seconds for OTA)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = OTA_WATCHDOG_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic = false
    };
    esp_task_wdt_init(&wdt_config);

    // Create OTA task
    BaseType_t result = xTaskCreate(
        &ota_task,
        "ota_task",
        OTA_TASK_STACK_SIZE,
        (void*)(intptr_t)firmware_id,
        OTA_TASK_PRIORITY,
        nullptr
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        ota_in_progress.store(false, std::memory_order_release);
        return OtaResult::FAILED_TASK_CREATE;
    }

    ESP_LOGI(TAG, "OTA task created successfully");
    return OtaResult::SUCCESS;
}

bool ota_is_in_progress(void) {
    return ota_in_progress.load(std::memory_order_acquire);
}
