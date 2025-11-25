//
// Created by Bolke de Bruin on 31/08/2025.
//

#include "PushButton.h"
#include "config.h"
#include <algorithm>

namespace FreeButton {

// PushButton Implementation
PushButton::PushButton(const int id, const int pin, std::shared_ptr<Adafruit_MCP23008> mcp)
    : id_(id)
    , pin_(pin)
    , mcp_(std::move(mcp))
    , history_(0xFFFF)  // Initially not pressed
    , longPressTicks_(0)
    , longPressDelay_(DEFAULT_LONG_PRESS_DELAY)
    , longPressRepeat_(DEFAULT_LONG_PRESS_REPEAT) {
    
    ESP_LOGD(TAG, "Created button %d on pin %d", id_, pin_);
}

bool PushButton::clicked() const {
    return (history_ & 0b0000000000000111) == 0b0000000000000110;
}

bool PushButton::buttonDown() const {
    return (history_ & 1) == 0;
}

bool PushButton::released() const {
    return (history_ & 0b000000000000011) == 1;
}

void PushButton::updateState(const uint8_t gpioState) {
    const uint8_t buttonState = (gpioState >> pin_) & 0x1;
    history_ = (history_ << 1) | buttonState;
}

void PushButton::processEvents() {
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

void PushButton::onButtonEvent(ButtonCallback callback) {
    buttonCallback_ = std::move(callback);
}

void PushButton::onLongPress(LongPressCallback callback) {
    longPressCallback_ = std::move(callback);
}

void PushButton::handleClick() {
    ESP_LOGI(TAG, "Button %d clicked", position());
    
    longPressTicks_ = longPressDelay_;
    
    if (buttonCallback_) {
        buttonCallback_(position(), EventType::CLICK);
    }
}

void PushButton::handleRelease() {
    ESP_LOGI(TAG, "Button %d released", position());
    
    longPressTicks_ = 0;
    
    if (buttonCallback_) {
        buttonCallback_(position(), EventType::RELEASE);
    }
}

void PushButton::handleLongPress() {
    ESP_LOGI(TAG, "Button %d long pressed", position());
    
    longPressTicks_ = longPressRepeat_;
    
    if (buttonCallback_) {
        buttonCallback_(position(), EventType::LONG_PRESS);
    }
}

} // namespace FreeButton