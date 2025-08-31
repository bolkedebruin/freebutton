//
// Created by Bolke de Bruin on 31/08/2025.
//

#include "LedButton.h"
#include "pins.h"
#include "config.h"
#include <algorithm>

namespace ButtonPlus {

// LedButton Implementation
LedButton::LedButton(const int id, const int pin, std::shared_ptr<Adafruit_MCP23008> mcp)
    : id_(id)
    , pin_(pin)
    , mcp_(std::move(mcp))
    , history_(0xFFFF)  // Initially not pressed
    , longPressTicks_(0)
    , longPressDelay_(DEFAULT_LONG_PRESS_DELAY)
    , longPressRepeat_(DEFAULT_LONG_PRESS_REPEAT) {
    
    ESP_LOGD(TAG, "Created button %d on pin %d", id_, pin_);
}

bool LedButton::clicked() const {
    return (history_ & 0b0000000000000111) == 0b0000000000000110;
}

bool LedButton::buttonDown() const {
    return (history_ & 1) == 0;
}

bool LedButton::released() const {
    return (history_ & 0b000000000000011) == 1;
}

void LedButton::updateState(const uint8_t gpioState) {
    const uint8_t buttonState = (gpioState >> pin_) & 0x1;
    history_ = (history_ << 1) | buttonState;
}

void LedButton::processEvents() {
    if (clicked()) {
        handleClick();
    }
    
    if (released()) {
        handleRelease();
    }
    
    if (buttonDown() && longPressTicks_ > 0) {
        if (--longPressTicks_ == 0) {
            handleLongPress();
        }
        
        // Notify long press callback with remaining ticks
        if (longPressCallback_) {
            longPressCallback_(position(), longPressTicks_);
        }
    }
}

void LedButton::onButtonEvent(ButtonCallback callback) {
    buttonCallback_ = std::move(callback);
}

void LedButton::onLongPress(LongPressCallback callback) {
    longPressCallback_ = std::move(callback);
}

void LedButton::setLedState(const bool on) const {
    if (mcp_) {
        // LED control implementation would go here
        // This depends on the specific LED pin configuration
        ESP_LOGD(TAG, "Button %d LED set to %s", position(), on ? "ON" : "OFF");
    }
}

void LedButton::setLedBrightness(const uint8_t brightness) const {
    if (mcp_) {
        // LED brightness control would go here
        ESP_LOGD(TAG, "Button %d LED brightness set to %d", position(), brightness);
    }
}

void LedButton::handleClick() {
    ESP_LOGI(TAG, "Button %d clicked", position());
    
    longPressTicks_ = longPressDelay_;
    
    if (buttonCallback_) {
        buttonCallback_(position(), EventType::CLICK);
    }
}

void LedButton::handleRelease() {
    ESP_LOGI(TAG, "Button %d released", position());
    
    longPressTicks_ = 0;
    
    if (buttonCallback_) {
        buttonCallback_(position(), EventType::RELEASE);
    }
}

void LedButton::handleLongPress() {
    ESP_LOGI(TAG, "Button %d long pressed", position());
    
    longPressTicks_ = longPressRepeat_;
    
    if (buttonCallback_) {
        buttonCallback_(position(), EventType::LONG_PRESS);
    }
}

// ButtonManager Implementation
ButtonManager::ButtonManager()
    : taskHandle_(nullptr)
    , mcpMutex_(xSemaphoreCreateMutex())
    , updateInterval_(DEFAULT_UPDATE_INTERVAL)
    , monitoring_(false) {
    
    buttons_.reserve(BUTTON_COUNT);
}

void ButtonManager::addButton(std::unique_ptr<LedButton> button) {
    if (!button) {
        ESP_LOGE(TAG, "Cannot add null button");
        return;
    }
    
    const int position = button->position();
    
    // Check if button already exists
    if (std::ranges::any_of(buttons_, [position](const auto& btn) {
        return btn->position() == position;
    })) {
        ESP_LOGW(TAG, "Button at position %d already exists", position);
        return;
    }
    
    ESP_LOGI(TAG, "Adding button at position %d", position);
    buttons_.push_back(std::move(button));
    
    // Sort by position for consistent ordering
    std::ranges::sort(buttons_, [](const auto& a, const auto& b) {
        return a->position() < b->position();
    });
}

void ButtonManager::removeButton(const int position) {
    buttons_.erase(
        std::ranges::remove_if(buttons_, [position](const auto& btn) {
            return btn->position() == position;
        }).begin(),
        buttons_.end()
    );
    
    ESP_LOGI(TAG, "Removed button at position %d", position);
}

LedButton* ButtonManager::getButton(const int position) const {
    if (const auto it = std::ranges::find_if(buttons_, [position](const auto& btn) {
        return btn->position() == position;
    }); it != buttons_.end()) {
        return it->get();
    }
    return nullptr;
}

void ButtonManager::startMonitoring() {
    if (monitoring_) {
        ESP_LOGW(TAG, "Button monitoring already started");
        return;
    }
    
    if (buttons_.empty()) {
        ESP_LOGW(TAG, "No buttons to monitor");
        return;
    }
    
    monitoring_ = true;
    
    const BaseType_t result = xTaskCreatePinnedToCore(
        buttonTaskWrapper,
        "ButtonManager",
        3072,
        this,
        1,  // Priority
        &taskHandle_,
        1   // Core 1
    );
    
    if (result == pdPASS) {
        ESP_LOGI(TAG, "Button monitoring started");
    } else {
        ESP_LOGE(TAG, "Failed to create button monitoring task");
        monitoring_ = false;
    }
}

void ButtonManager::stopMonitoring() {
    if (!monitoring_) {
        return;
    }
    
    monitoring_ = false;
    
    if (taskHandle_) {
        vTaskDelete(taskHandle_);
        taskHandle_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Button monitoring stopped");
}

void ButtonManager::onGlobalButtonEvent(GlobalButtonCallback callback) {
    globalCallback_ = std::move(callback);
}

void ButtonManager::buttonTaskWrapper(void* parameter) {
    auto* manager = static_cast<ButtonManager*>(parameter);
    manager->buttonTask();
}

void ButtonManager::buttonTask() {
    ESP_LOGI(TAG, "Button monitoring task started");
    
    while (monitoring_) {
        const TickType_t startTick = xTaskGetTickCount();
        
        updateAllButtons();
        
        // Process events for all buttons
        for (const auto& button : buttons_) {
            button->processEvents();
        }
        
        // Calculate remaining delay time
        const TickType_t elapsedTicks = xTaskGetTickCount() - startTick;
        const TickType_t delayTicks = pdMS_TO_TICKS(updateInterval_);
        
        if (elapsedTicks < delayTicks) {
            vTaskDelay(delayTicks - elapsedTicks);
        }
    }
    
    ESP_LOGI(TAG, "Button monitoring task stopped");
    vTaskDelete(nullptr);
}

void ButtonManager::updateAllButtons() const {
    // Group buttons by their shared MCP
    std::unordered_map<Adafruit_MCP23008*, std::vector<LedButton*>> mcpGroups;
    
    for (const auto& button : buttons_) {
        // Get MCP from button itself
        if (auto mcp = button->getMCP()) {
            mcpGroups[mcp.get()].push_back(button.get());
        }
    }
    
    // Update all buttons grouped by their MCP
    for (const auto& [mcp, buttonGroup] : mcpGroups) {
        if (pdTRUE == xSemaphoreTake(mcpMutex_, pdMS_TO_TICKS(20))) {
            const uint8_t gpioState = mcp->readGPIO();
            
            ESP_LOGD(TAG, "Read GPIO state 0x%02X from MCP", gpioState);
            
            // Update all buttons connected to this MCP
            for (auto* button : buttonGroup) {
                button->updateState(gpioState);
            }
            
            xSemaphoreGive(mcpMutex_);
        } else {
            ESP_LOGW(TAG, "Failed to acquire MCP mutex");
        }
    }
}

} // namespace ButtonPlus