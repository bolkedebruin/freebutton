//
// Created by Bolke de Bruin on 31/08/2025.
//

#pragma once

#include "Adafruit_MCP23008.h"
#include "types.h"
#include <functional>
#include <memory>

namespace FreeButton {

class PushButton {
public:
    using ButtonCallback = std::function<void(int position, EventType event)>;
    using LongPressCallback = std::function<void(int position, int ticksRemaining)>;

    explicit PushButton(int id, int pin, std::shared_ptr<Adafruit_MCP23008> mcp);
    ~PushButton() = default;

    // Non-copyable, movable
    PushButton(const PushButton&) = delete;
    PushButton& operator=(const PushButton&) = delete;
    PushButton(PushButton&&) = default;
    PushButton& operator=(PushButton&&) = default;

    // Button state queries
    [[nodiscard]] bool clicked() const;
    [[nodiscard]] bool buttonDown() const; 
    [[nodiscard]] bool released() const;

    // Position information
    [[nodiscard]] int position() const { return id_ + 1; }  // 1-based
    [[nodiscard]] int row() const { return id_ / 2; }       // 0-based
    [[nodiscard]] bool isRight() const { return (id_ % 2) == 1; }

    // Update button state (call from button loop)
    void updateState(uint8_t gpioState);

    // Process events and trigger callbacks
    void processEvents();

    // Callback registration
    void onButtonEvent(ButtonCallback callback);
    void onLongPress(LongPressCallback callback);

    // Long press configuration
    void setLongPressDelay(uint8_t delay) { longPressDelay_ = delay; }
    void setLongPressRepeat(uint8_t repeat) { longPressRepeat_ = repeat; }

    // MCP access
    [[nodiscard]] std::shared_ptr<Adafruit_MCP23008> getMCP() const { return mcp_; }

private:
    static constexpr auto TAG = "PushButton";
    static constexpr uint8_t DEFAULT_LONG_PRESS_DELAY = 40;
    static constexpr uint8_t DEFAULT_LONG_PRESS_REPEAT = 25;

    const int id_;
    const int pin_;
    std::shared_ptr<Adafruit_MCP23008> mcp_;
    
    uint16_t history_;
    uint8_t longPressTicks_;
    uint8_t longPressDelay_;
    uint8_t longPressRepeat_;

    ButtonCallback buttonCallback_;
    LongPressCallback longPressCallback_;

    void handleClick();
    void handleRelease(); 
    void handleLongPress();
};

// Button Manager class to handle all buttons
class ButtonManager {
public:
    using GlobalButtonCallback = std::function<void(int position, EventType event)>;

    ButtonManager();
    ~ButtonManager() = default;

    // Non-copyable, movable
    ButtonManager(const ButtonManager&) = delete;
    ButtonManager& operator=(const ButtonManager&) = delete;
    ButtonManager(ButtonManager&&) = default;
    ButtonManager& operator=(ButtonManager&&) = default;

    // Button management
    void addButton(std::unique_ptr<PushButton> button);
    void removeButton(int position);
    
    // Get button by position (1-based)
    [[nodiscard]] PushButton* getButton(int position) const;
    
    // Start/stop button monitoring task
    void startMonitoring();
    void stopMonitoring();
    
    // Global callback for all button events
    void onGlobalButtonEvent(GlobalButtonCallback callback);

    // Configuration
    void setUpdateInterval(uint32_t intervalMs) { updateInterval_ = intervalMs; }

private:
    static constexpr const char* TAG = "ButtonManager";
    static constexpr uint32_t DEFAULT_UPDATE_INTERVAL = 20; // ms

    std::vector<std::unique_ptr<PushButton>> buttons_;
    
    GlobalButtonCallback globalCallback_;
    TaskHandle_t taskHandle_;
    SemaphoreHandle_t mcpMutex_;
    uint32_t updateInterval_;
    bool monitoring_;

    static void buttonTaskWrapper(void* parameter);
    void buttonTask() const;
    void updateAllButtons() const;
};

} // namespace FreeButton