//
// Created by Bolke de Bruin on 25/11/2025.
//

#pragma once

#include "PushButton.h"
#include "ButtonLed.h"
#include "mqtt/Component.h"
#include <atomic>
#include <memory>
#include <vector>

namespace FreeButton {

// Receives Home Assistant `button` presses on buttonplus/<id>/button_<pos>/set
// (MQTT task) and hands them to the button monitoring task as pending bits.
class HaButtonCommandSubscriber final : public MqttSubscriber {
public:
    void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) override;
    const std::string getComponentType() const override { return "button_command"; }
    // Bit (pos-1) set = a press is pending for that position. Clears them.
    uint32_t takePending() { return pending_.exchange(0); }

private:
    std::atomic<uint32_t> pending_{0};
};

// Button Manager class to handle all physical buttons and LEDs
class ButtonManager {
public:
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
    // Register Home Assistant discovery (event, button, device triggers) for
    // every key position. Existence follows the PHYSICAL buttons; the config
    // only refines event_types / mirroring (read at publish time). Positions
    // without a button publish an empty retained payload, removing stale entities.
    void registerButtonsWithHomeAssistant(uint8_t brokerId) const;

    // Subscribe to the HA `button` command topic of every physical key on one
    // Home Assistant broker. Call after broker creation.
    void subscribeHomeAssistantCommands(uint8_t brokerId) const;

    // Get button by position (1-based)
    [[nodiscard]] PushButton* getButton(int position) const;

    // Start/stop button monitoring task
    void startMonitoring();
    void stopMonitoring();

    // Configuration
    void setUpdateInterval(const uint32_t intervalMs) { updateInterval_ = intervalMs; }

    // LED management for all detected physical buttons
    // connectorTypes[c]: 0=EMPTY, 1=BAR, 2=LARGE (per HardwareScanner::ConnectorType).
    // Only BAR connectors get LEDs; each bar owns a 4-LED block packed by bar order.
    void createLEDsForAllButtons(const std::string& deviceId, uint8_t brokerId,
                                 const uint8_t connectorTypes[4]);
    // Register light discovery for every LED on one Home Assistant broker.
    void registerLEDsWithHomeAssistant(uint8_t brokerId) const;

    // Get LEDs for a specific button position
    std::vector<ButtonLed*> getLEDsForButton(int position) const;

private:
    static constexpr const char* TAG = "ButtonManager";
    static constexpr uint32_t DEFAULT_UPDATE_INTERVAL = 20; // ms

    std::vector<std::unique_ptr<PushButton>> buttons_;
    std::vector<std::unique_ptr<ButtonLed>> leds_;  // All physical button LEDs
    // Heap-allocated: the broker keeps its address, so it must not move with ButtonManager.
    std::unique_ptr<HaButtonCommandSubscriber> commandSubscriber_{std::make_unique<HaButtonCommandSubscriber>()};

    TaskHandle_t taskHandle_;
    SemaphoreHandle_t mcpMutex_;
    uint32_t updateInterval_;
    bool mutable monitoring_;

    static void buttonTaskWrapper(void* parameter);
    void buttonTask();
    void updateAllButtons() const;
    void processHomeAssistantCommands() const;
    // Publish click/longpress JSON to buttonplus/<id>/button_<pos>/state on
    // every Home Assistant broker whose plan says the key's config doesn't
    // already (ButtonEventPlan option b).
    static void mirrorToCanonicalTopic(int pos, EventType event);

    // LED creation helpers
    void createLEDsForButton(int position, int frontIndex, int wallIndex,
                             const std::string& deviceId, uint8_t brokerId);

    // find lowest position of the button
    uint8_t getBaseButtonPosition() const;
};

} // namespace FreeButton