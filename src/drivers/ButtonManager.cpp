//
// Created by Bolke de Bruin on 25/11/2025.
//

#include "ButtonManager.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <algorithm>
#include <map>

#include "mqtt/HomeAssistantDiscovery.h"
#include "components/ConfigManager.h"
#include "scripting/button_hardware.h"

namespace FreeButton {

ButtonManager::ButtonManager()
    : taskHandle_(nullptr)
    , mcpMutex_(xSemaphoreCreateMutex())
    , updateInterval_(DEFAULT_UPDATE_INTERVAL)
    , monitoring_(false) {
}

void ButtonManager::addButton(std::unique_ptr<PushButton> button) {
    if (!button) {
        ESP_LOGW(TAG, "Attempting to add null button");
        return;
    }

    const int position = button->position();
    ESP_LOGI(TAG, "Adding push button at position %d", position);

    // Set up button event callback to forward to display components
    button->onButtonEvent([this](const int pos, const EventType event) {
        ESP_LOGI(TAG, "Button at position %d with event %d", pos, event);

        // Dispatch to JavaScript callbacks first
        switch (event) {
            case CLICK:
                button_hw_dispatch_click(pos);
                break;
            case LONG_PRESS:
                button_hw_dispatch_long_press(pos);
                break;
            case RELEASE:
                button_hw_dispatch_release(pos);
                break;
            default:
                break;
        }

        // Find and trigger corresponding display button
        auto* displayButton = configManager.getButtonByPosition(pos);
        if (displayButton) {
            ESP_LOGI(TAG, "Found display button for position %d, triggering event", pos);
            displayButton->handlePhysicalEvent(event);
        } else {
            ESP_LOGI(TAG, "No display button found for position %d", pos);
        }
    });

    buttons_.push_back(std::move(button));
}

void ButtonManager::removeButton(int position) {
    auto it = std::ranges::find_if(buttons_,
                                   [position](const auto& button) {
                                       return button->position() == position;
                                   });

    if (it != buttons_.end()) {
        ESP_LOGI(TAG, "Removing button at position %d", position);
        buttons_.erase(it);
    } else {
        ESP_LOGW(TAG, "Button at position %d not found for removal", position);
    }
}

PushButton* ButtonManager::getButton(int position) const {
    const auto it = std::ranges::find_if(buttons_,
                                   [position](const auto& button) {
                                       return button->position() == position;
                                   });

    return it != buttons_.end() ? it->get() : nullptr;
}

void ButtonManager::registerButtonsWithHomeAssistant(const uint8_t brokerId) const {
    for (const auto& button : buttons_) {
        const int pos = button->position();

        // Register click/short press trigger
        auto clickGenerator = [pos](uint8_t broker_id) -> HomeAssistantTriggerConfig {
            HomeAssistantTriggerConfig config;
            config.object_id = "button_" + std::to_string(pos) + "_click";
            config.type = "button_short_press";
            config.subtype = "button_" + std::to_string(pos);
            config.topic = std::string(PUBLICMQTTID) + "/" + deviceId + "/button_" + std::to_string(pos) + "/state";
            config.payload = R"({"event_type":"click"})";
            config.device = HomeAssistantDiscovery::getGlobalDeviceInfo();
            config.enabled = true;
            return config;
        };
        HomeAssistantDiscovery::registerTrigger(brokerId, std::move(clickGenerator));

        // Register long press trigger
        auto longPressGenerator = [pos](uint8_t broker_id) -> HomeAssistantTriggerConfig {
            HomeAssistantTriggerConfig config;
            config.object_id = "button_" + std::to_string(pos) + "_long";
            config.type = "button_long_press";
            config.subtype = "button_" + std::to_string(pos);
            config.topic = std::string(PUBLICMQTTID) + "/" + deviceId + "/button_" + std::to_string(pos) + "/state";
            config.payload = R"({"event_type":"longpress"})";
            config.device = HomeAssistantDiscovery::getGlobalDeviceInfo();
            config.enabled = true;
            return config;
        };
        HomeAssistantDiscovery::registerTrigger(brokerId, std::move(longPressGenerator));
    }
}

void ButtonManager::startMonitoring() {
    if (monitoring_) {
        ESP_LOGW(TAG, "Button monitoring already started");
        return;
    }

    ESP_LOGI(TAG, "Starting button monitoring task");

    monitoring_ = true;

    if (xTaskCreate(buttonTaskWrapper, "button_monitor", 4096, this, 5, &taskHandle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button monitoring task");
        monitoring_ = false;
        return;
    }
}

void ButtonManager::stopMonitoring() {
    if (!monitoring_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping button monitoring");
    monitoring_ = false;

    if (taskHandle_) {
        vTaskDelete(taskHandle_);
        taskHandle_ = nullptr;
    }
}

void ButtonManager::buttonTaskWrapper(void* parameter) {
    if (auto* manager = static_cast<ButtonManager*>(parameter)) {
        manager->buttonTask();
    }
    vTaskDelete(nullptr);
}

/* do NOT make this const */
void ButtonManager::buttonTask() {
    ESP_LOGI(TAG, "Button monitoring task started with %zu buttons", buttons_.size());

    while (monitoring_) {
        updateAllButtons();
        vTaskDelay(pdMS_TO_TICKS(updateInterval_));
    }

    ESP_LOGI(TAG, "Button monitoring task ended");
}

void ButtonManager::updateAllButtons() const {
    if (buttons_.empty()) {
        return;
    }

    // Take mutex to prevent concurrent MCP access
    if (xSemaphoreTake(mcpMutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take MCP mutex, skipping button update");
        return;
    }

    // Group buttons by MCP to minimize I2C traffic
    std::map<std::shared_ptr<Adafruit_MCP23008>, std::vector<PushButton*>> mcpButtons;

    for (const auto& button : buttons_) {
        auto mcp = button->getMCP();
        if (mcp) {
            mcpButtons[mcp].push_back(button.get());
        }
    }

    // Update all buttons grouped by MCP
    for (const auto& [mcp, buttonGroup] : mcpButtons) {
        if (mcp) {
            // Read GPIO state once per MCP
            const uint8_t gpioState = mcp->readGPIO();

            // Update all buttons on this MCP
            for (auto* button : buttonGroup) {
                button->updateState(gpioState);
                button->processEvents();
            }
        }
    }

    xSemaphoreGive(mcpMutex_);
}

void ButtonManager::createLEDsForAllButtons(const std::string& deviceId, uint8_t brokerId) {
    ESP_LOGI(TAG, "Creating LEDs for all detected physical buttons");

    for (const auto& button : buttons_) {
        int position = button->position();
        createLEDsForButton(position, deviceId, brokerId);
    }

    ESP_LOGI(TAG, "Created %zu LEDs total for %zu buttons", leds_.size(), buttons_.size());
}

void ButtonManager::createLEDsForButton(int position, const std::string& deviceId, uint8_t brokerId) {
    ESP_LOGD(TAG, "Creating LEDs for button at position %d", position);

    // Create front LED for this button position
    auto frontLed = std::make_unique<ButtonLed>(
        position, LedPosition::FRONT, 0, deviceId, brokerId);

    // Create wall LED for this button position
    auto wallLed = std::make_unique<ButtonLed>(
        position, LedPosition::WALL, 1, deviceId, brokerId);

    ESP_LOGD(TAG, "Created front and wall LEDs for button %d", position);

    leds_.push_back(std::move(frontLed));
    leds_.push_back(std::move(wallLed));
}

void ButtonManager::registerLEDsWithHomeAssistant() const {
    ESP_LOGI(TAG, "Registering %zu LEDs with Home Assistant", leds_.size());

    for (const auto& led : leds_) {
        led->registerWithHomeAssistant();
    }

    ESP_LOGI(TAG, "Home Assistant registration completed for all LEDs");
}

std::vector<ButtonLed*> ButtonManager::getLEDsForButton(const int position) const {
    std::vector<ButtonLed*> buttonLeds;

    for (const auto& led : leds_) {
        if (led->getButtonPosition() == position) {
            buttonLeds.push_back(led.get());
        }
    }

    return buttonLeds;
}

uint8_t ButtonManager::getBaseButtonPosition() const {
    uint8_t pos = 255;
    for (const auto& button : buttons_) {
        if (button->position() < pos) {
            pos = button->position();
        }
    }
    return pos;
}
} // namespace FreeButton