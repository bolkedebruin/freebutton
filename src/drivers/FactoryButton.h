#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace FreeButton {

enum class ButtonEvent {
    NONE,
    SHORT_PRESS,
    LONG_PRESS_START,
    LONG_PRESS_END,
    HOLD,  // every HOLD_TICK_MS after LONG_PRESS_START while still held; see getPressedDuration()
};

class FactoryButton {
public:
    static constexpr uint32_t LONG_PRESS_DURATION_MS = 2000;
    static constexpr uint32_t DEBOUNCE_TIME_MS = 50;
    static constexpr uint32_t TASK_DELAY_MS = 10;
    static constexpr uint32_t HOLD_TICK_MS = 500;

    using ButtonCallback = void(*)(ButtonEvent);

    FactoryButton() = default;
    ~FactoryButton();

    bool initialize(int pin);
    void setCallback(const ButtonCallback& callback);
    void start();
    void stop();

    bool isPressed() const;
    uint32_t getPressedDuration() const;

private:
    static void buttonTaskWrapper(void* param) noexcept;
    void buttonTask() noexcept;

    int pin_ = -1;
    ButtonCallback callback_;
    TaskHandle_t taskHandle_ = nullptr;

    struct {
        bool isRunning : 1;
        bool lastState : 1;
        bool currentState : 1;
        bool longPressTriggered : 1;
    } flags_ = {false, false, false, false};

    uint32_t pressStartTime_ = 0;
    uint32_t lastChangeTime_ = 0;
    uint32_t nextHoldTick_ = 0;

    SemaphoreHandle_t mutex_ = nullptr;
};

// Global instance for factory button
extern FactoryButton g_factoryButton;

// Convenience functions
bool initializeFactoryButton(int pin);
void setFactoryButtonCallback(const FactoryButton::ButtonCallback& callback);

} // namespace FreeButton