//
// LedButtonManager - Manages LedButtonControllers for all buttons
// Created by Bolke de Bruin on 31/08/2025.
//

#pragma once

#include "LedButtonController.h"
#include "LedButton.h"
#include "NeoPixel.h"
#include <vector>
#include <memory>

namespace FreeButton {

class LedButtonManager {
public:
    explicit LedButtonManager(std::shared_ptr<NeoPixelController> neopixel, 
                             uint8_t brokerId, 
                             const std::string& deviceId);
    ~LedButtonManager() = default;

    // Non-copyable, movable
    LedButtonManager(const LedButtonManager&) = delete;
    LedButtonManager& operator=(const LedButtonManager&) = delete;
    LedButtonManager(LedButtonManager&&) = default;
    LedButtonManager& operator=(LedButtonManager&&) = default;

    // Add a button with LED controller
    void addButton(std::shared_ptr<LedButton> button);
    
    // Remove a button
    void removeButton(int position);
    
    // Get LED controller for a specific button position
    [[nodiscard]] LedButtonController* getLedController(int position) const;
    
    // Register all LED controllers with Home Assistant
    void registerAllWithHomeAssistant() const;
    
    // Set LED state for all buttons
    void setAllLedsState(bool on);
    void setAllLedsColor(uint8_t red, uint8_t green, uint8_t blue);
    void setAllLedsBrightness(uint8_t brightness);
    
    // Get number of managed buttons
    [[nodiscard]] size_t getButtonCount() const { return ledControllers_.size(); }

private:
    static constexpr const char* TAG = "LedButtonManager";
    
    std::shared_ptr<NeoPixelController> neopixel_;
    uint8_t brokerId_;
    std::string deviceId_;
    
    std::vector<std::unique_ptr<LedButtonController>> ledControllers_;
    
    // Find controller by button position
    [[nodiscard]] std::vector<std::unique_ptr<LedButtonController>>::iterator findController(int position);
    [[nodiscard]] std::vector<std::unique_ptr<LedButtonController>>::const_iterator findController(int position) const;
};

// Convenience function to register LED button controllers with Home Assistant Discovery
void registerLedButtonsWithDiscovery(uint8_t brokerId, LedButtonManager* manager);

} // namespace FreeButton