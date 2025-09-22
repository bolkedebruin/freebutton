#include "FactoryButton.h"
#include <Arduino.h>
#include <esp_log.h>

namespace FreeButton {

static const char* TAG = "FactoryButton";

// Global instance
FactoryButton g_factoryButton;

FactoryButton::~FactoryButton() {
    stop();
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

bool FactoryButton::initialize(int pin) {
    pin_ = pin;

    // Initialize GPIO pin
    pinMode(pin_, INPUT_PULLUP);

    // Create mutex
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    ESP_LOGI(TAG, "Factory button initialized on pin %d", pin_);
    return true;
}

void FactoryButton::setCallback(const ButtonCallback& callback) {
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        callback_ = callback;
        xSemaphoreGive(mutex_);
    }
}

void FactoryButton::start() {
    if (flags_.isRunning || pin_ < 0) {
        return;
    }

    flags_.isRunning = true;

    xTaskCreatePinnedToCore(
        buttonTaskWrapper,
        "FactoryButtonTask",
        configMINIMAL_STACK_SIZE * 2,  // Reduced stack size
        this,
        1,  // Priority
        &taskHandle_,
        1   // Core 1
    );

    ESP_LOGI(TAG, "Factory button task started");
}

void FactoryButton::stop() {
    flags_.isRunning = false;

    if (taskHandle_) {
        vTaskDelete(taskHandle_);
        taskHandle_ = nullptr;
    }

    ESP_LOGI(TAG, "Factory button task stopped");
}

bool FactoryButton::isPressed() const {
    return digitalRead(pin_) == LOW;  // Active low
}

uint32_t FactoryButton::getPressedDuration() const {
    if (pressStartTime_ == 0) {
        return 0;
    }
    return millis() - pressStartTime_;
}

void FactoryButton::buttonTaskWrapper(void* param) noexcept {
    static_cast<FactoryButton*>(param)->buttonTask();
}

void FactoryButton::buttonTask() noexcept {
    uint32_t lastDebounceTime = 0;

    ESP_LOGI(TAG, "Button task started");

    while (flags_.isRunning) {
        uint32_t currentTime = millis();

        // Read current button state
        bool rawState = isPressed();

        // Debounce
        if (rawState != flags_.lastState) {
            lastDebounceTime = currentTime;
        }

        if ((currentTime - lastDebounceTime) > DEBOUNCE_TIME_MS) {
            if (rawState != flags_.currentState) {
                flags_.currentState = rawState;
                lastChangeTime_ = currentTime;

                if (flags_.currentState) {
                    // Button pressed
                    pressStartTime_ = currentTime;
                    flags_.longPressTriggered = false;
                    ESP_LOGD(TAG, "Button pressed");
                } else {
                    // Button released
                    uint32_t pressDuration = currentTime - pressStartTime_;
                    ESP_LOGD(TAG, "Button released after %lu ms", pressDuration);

                    const ButtonEvent event = (pressDuration >= LONG_PRESS_DURATION_MS)
                        ? ButtonEvent::LONG_PRESS_END
                        : (pressDuration > DEBOUNCE_TIME_MS)
                            ? ButtonEvent::SHORT_PRESS
                            : ButtonEvent::NONE;

                    pressStartTime_ = 0;

                    // Call callback
                    if (event != ButtonEvent::NONE && callback_ &&
                        xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
                        callback_(event);
                        xSemaphoreGive(mutex_);
                    }
                }
            }
        }

        // Check for long press start
        if (flags_.currentState && !flags_.longPressTriggered &&
            (currentTime - pressStartTime_) >= LONG_PRESS_DURATION_MS) {
            flags_.longPressTriggered = true;
            ESP_LOGD(TAG, "Long press detected");

            if (callback_ && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
                callback_(ButtonEvent::LONG_PRESS_START);
                xSemaphoreGive(mutex_);
            }
        }

        flags_.lastState = rawState;
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    }

    ESP_LOGI(TAG, "Button task ended");
}

// Convenience functions
bool initializeFactoryButton(const int pin) {
    return g_factoryButton.initialize(pin);
}

void setFactoryButtonCallback(const FactoryButton::ButtonCallback& callback) {
    g_factoryButton.setCallback(callback);
}

} // namespace FreeButton