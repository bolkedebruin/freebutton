//
// LedButtonManager - Manages LedButtonControllers for all buttons
// Created by Bolke de Bruin on 31/08/2025.
//

#include "LedButtonManager.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include <esp_log.h>
#include <algorithm>

namespace ButtonPlus {

LedButtonManager::LedButtonManager(std::shared_ptr<NeoPixelController> neopixel, 
                                 uint8_t brokerId, 
                                 const std::string& deviceId)
    : neopixel_(std::move(neopixel))
    , brokerId_(brokerId)
    , deviceId_(deviceId) {
    
    if (!neopixel_) {
        ESP_LOGE(TAG, "Invalid NeoPixel controller");
        return;
    }
    
    ESP_LOGI(TAG, "Created LedButtonManager for device: %s, broker: %d", deviceId_.c_str(), brokerId_);
}

void LedButtonManager::addButton(std::shared_ptr<LedButton> button) {
    if (!button) {
        ESP_LOGE(TAG, "Cannot add null button");
        return;
    }
    
    const int position = button->position();
    
    // Check if button already exists
    if (findController(position) != ledControllers_.end()) {
        ESP_LOGW(TAG, "LED controller for button at position %d already exists", position);
        return;
    }
    
    // Create LED controller for this button
    auto ledController = std::make_unique<LedButtonController>(
        button, neopixel_, brokerId_, deviceId_);
    
    ESP_LOGI(TAG, "Adding LED controller for button at position %d", position);
    ledControllers_.push_back(std::move(ledController));
    
    // Sort by position for consistent ordering
    std::sort(ledControllers_.begin(), ledControllers_.end(),
              [](const auto& a, const auto& b) {
                  return a->getPosition() < b->getPosition();
              });
}

void LedButtonManager::removeButton(const int position) {
    auto it = findController(position);
    if (it != ledControllers_.end()) {
        ledControllers_.erase(it);
        ESP_LOGI(TAG, "Removed LED controller for button at position %d", position);
    } else {
        ESP_LOGW(TAG, "LED controller for button at position %d not found", position);
    }
}

LedButtonController* LedButtonManager::getLedController(const int position) const {
    auto it = findController(position);
    if (it != ledControllers_.end()) {
        return it->get();
    }
    return nullptr;
}

void LedButtonManager::registerAllWithHomeAssistant() const {
    if (!HomeAssistantDiscovery::isDiscoveryEnabled(brokerId_)) {
        ESP_LOGD(TAG, "Home Assistant discovery disabled for broker %d", brokerId_);
        return;
    }
    
    ESP_LOGI(TAG, "Registering %zu LED controllers with Home Assistant", ledControllers_.size());
    
    for (const auto& controller : ledControllers_) {
        controller->registerWithHomeAssistant();
    }
    
    ESP_LOGI(TAG, "All LED controllers registered with Home Assistant");
}

void LedButtonManager::setAllLedsState(const bool on) {
    ESP_LOGI(TAG, "Setting all LEDs state to: %s", on ? "ON" : "OFF");
    
    for (const auto& controller : ledControllers_) {
        controller->setLedState(on);
    }
}

void LedButtonManager::setAllLedsColor(const uint8_t red, const uint8_t green, const uint8_t blue) {
    ESP_LOGI(TAG, "Setting all LEDs color to RGB(%d,%d,%d)", red, green, blue);
    
    for (const auto& controller : ledControllers_) {
        controller->setLedColor(red, green, blue);
    }
}

void LedButtonManager::setAllLedsBrightness(const uint8_t brightness) {
    ESP_LOGI(TAG, "Setting all LEDs brightness to: %d", brightness);
    
    for (const auto& controller : ledControllers_) {
        controller->setLedBrightness(brightness);
    }
}

std::vector<std::unique_ptr<LedButtonController>>::iterator 
LedButtonManager::findController(const int position) {
    return std::find_if(ledControllers_.begin(), ledControllers_.end(),
                        [position](const auto& controller) {
                            return controller->getPosition() == position;
                        });
}

std::vector<std::unique_ptr<LedButtonController>>::const_iterator 
LedButtonManager::findController(const int position) const {
    return std::find_if(ledControllers_.begin(), ledControllers_.end(),
                        [position](const auto& controller) {
                            return controller->getPosition() == position;
                        });
}

void registerLedButtonsWithDiscovery(const uint8_t brokerId, LedButtonManager* manager) {
    if (!manager) {
        ESP_LOGE("LedButtonManager", "Cannot register null manager with discovery");
        return;
    }
    
    manager->registerAllWithHomeAssistant();
    ESP_LOGI("LedButtonManager", "Registered LED buttons with Home Assistant discovery");
}

} // namespace ButtonPlus