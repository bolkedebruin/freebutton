#include "DeviceInfoManager.h"
#include "DeviceInfo.h"
#include "LvglDisplay.h"
#include "../ui/device_info_modal.h"
#include <esp_log.h>
#include <memory>
#include <lvgl.h>

namespace FreeButton {

static auto TAG = "DeviceInfo";

// Local state - only what's absolutely necessary
static bool isDisplaying = false;
static std::string* pendingDeviceInfoText = nullptr;
static bool shouldShowModal = false;
static bool shouldHideModal = false;
static lv_timer_t* modalCheckTimer = nullptr;

// Timer callback to handle modal operations from LVGL context
static void modalCheckTimerCallback(lv_timer_t* timer) {
    (void)timer; // Unused parameter

    if (shouldShowModal && pendingDeviceInfoText) {
        auto* screen = lv_scr_act();
        if (screen) {
            show_device_info_modal(screen, pendingDeviceInfoText->c_str());
            isDisplaying = true;
            ESP_LOGI(TAG, "Device info modal displayed");
        }

        delete pendingDeviceInfoText;
        pendingDeviceInfoText = nullptr;
        shouldShowModal = false;
    }

    if (shouldHideModal && isDisplaying) {
        hide_device_info_modal();
        isDisplaying = false;
        shouldHideModal = false;
        ESP_LOGI(TAG, "Device info modal hidden");
    }
}

// Create timer lazily when needed
static void ensureTimerExists() {
    if (!modalCheckTimer) {
        modalCheckTimer = lv_timer_create(modalCheckTimerCallback, 50, nullptr);
        ESP_LOGI(TAG, "Created modal check timer");
    }
}

void onDeviceInfoButtonEvent(ButtonEvent event) {
    ESP_LOGD(TAG, "Factory button event: %d", static_cast<int>(event));

    switch (event) {
        case ButtonEvent::LONG_PRESS_START: {
            ESP_LOGI(TAG, "Long press detected - showing device info");

            if (isDisplaying) {
                return; // Already displaying
            }

            // Get the active LVGL screen
            auto* screen = lv_scr_act();
            if (!screen) {
                ESP_LOGE(TAG, "LVGL screen not available");
                return;
            }

            // Create device info collector on heap to avoid stack overflow
            auto collector = std::make_unique<DeviceInfoCollector>();
            DeviceInfo info = collector->collectInfo();
            std::string deviceInfoText = collector->formatDeviceInfo(info);

            // Store device info text and flag for modal display
            if (pendingDeviceInfoText) {
                delete pendingDeviceInfoText;
            }
            pendingDeviceInfoText = new std::string(deviceInfoText);

            // Ensure timer exists before setting flag
            ensureTimerExists();
            shouldShowModal = true;
            break;
        }

        case ButtonEvent::LONG_PRESS_END:
            ESP_LOGI(TAG, "Long press ended - hiding device info");
            ensureTimerExists();
            shouldHideModal = true;
            break;

        case ButtonEvent::SHORT_PRESS:
            ESP_LOGD(TAG, "Short press detected - ignoring");
            break;

        default:
            break;
    }
}

bool initializeDeviceInfoButton(int factoryButtonPin) {
    ESP_LOGI(TAG, "Initializing device info button on pin %d", factoryButtonPin);

    // Initialize factory button
    if (!g_factoryButton.initialize(factoryButtonPin)) {
        ESP_LOGE(TAG, "Failed to initialize factory button");
        return false;
    }

    // Set button callback
    g_factoryButton.setCallback(onDeviceInfoButtonEvent);

    // Note: LVGL timer will be created on first use (after LVGL is initialized)

    ESP_LOGI(TAG, "Device info button initialized successfully");
    return true;
}

void startDeviceInfoButton() {
    ESP_LOGI(TAG, "Starting device info button");
    g_factoryButton.start();
}

void stopDeviceInfoButton() {
    ESP_LOGI(TAG, "Stopping device info button");
    g_factoryButton.stop();

    if (isDisplaying) {
        hide_device_info_modal();
        isDisplaying = false;
    }
}

} // namespace FreeButton