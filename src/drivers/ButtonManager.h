//
// Created by Bolke de Bruin on 25/11/2025.
//

#pragma once

#include "PushButton.h"
#include "ButtonLed.h"
#include "types.h"
#include <memory>
#include <functional>
#include <vector>

namespace FreeButton {

// Button Manager class to handle all physical buttons and LEDs
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
    void registerButtonsWithHomeAssistant(uint8_t brokerId) const;

    // Get button by position (1-based)
    [[nodiscard]] PushButton* getButton(int position) const;

    // Start/stop button monitoring task
    void startMonitoring();
    void stopMonitoring();

    // Global callback for all button events
    void onGlobalButtonEvent(GlobalButtonCallback callback);

    // Configuration
    void setUpdateInterval(const uint32_t intervalMs) { updateInterval_ = intervalMs; }

    // LED management for all detected physical buttons
    void createLEDsForAllButtons(const std::string& deviceId, uint8_t brokerId = 0);
    void registerLEDsWithHomeAssistant() const;

    // Get LEDs for a specific button position
    std::vector<ButtonLed*> getLEDsForButton(int position) const;

private:
    static constexpr const char* TAG = "ButtonManager";
    static constexpr uint32_t DEFAULT_UPDATE_INTERVAL = 20; // ms

    std::vector<std::unique_ptr<PushButton>> buttons_;
    std::vector<std::unique_ptr<ButtonLed>> leds_;  // All physical button LEDs

    GlobalButtonCallback globalCallback_;
    TaskHandle_t taskHandle_;
    SemaphoreHandle_t mcpMutex_;
    uint32_t updateInterval_;
    bool monitoring_;

    static void buttonTaskWrapper(void* parameter);
    void buttonTask();
    void updateAllButtons() const;

    // LED creation helpers
    void createLEDsForButton(int position, const std::string& deviceId, uint8_t brokerId);
};

} // namespace FreeButton