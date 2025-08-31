//
// Created by Bolke de Bruin on 31/08/2025.
//

#pragma once

#include "../drivers/HardwareScanner.h"
#include "../drivers/LedButton.h"
#include "../drivers/Sensors.h"
#include "Adafruit_MCP23008.h"
#include "pins.h"
#include <memory>
#include <functional>
#include <esp_log.h>

namespace ButtonPlus {

class HardwareInitializer {
public:
    using InitCompleteCallback = std::function<void(bool success)>;
    
    HardwareInitializer();
    ~HardwareInitializer() = default;

    // Non-copyable, movable
    HardwareInitializer(const HardwareInitializer&) = delete;
    HardwareInitializer& operator=(const HardwareInitializer&) = delete;
    HardwareInitializer(HardwareInitializer&&) = default;
    HardwareInitializer& operator=(HardwareInitializer&&) = default;

    // Initialize all hardware at startup
    bool initializeHardware();
    
    // Get initialized components
    [[nodiscard]] HardwareScanner& getScanner() { return scanner_; }
    [[nodiscard]] ButtonManager& getButtonManager() { return buttonManager_; }
    
    // Configuration
    void onInitComplete(InitCompleteCallback callback);
    
    // Status queries  
    [[nodiscard]] bool isInitialized() const { return initialized_; }
    [[nodiscard]] int getTft320Bar() const { return tft320Bar_; }
    [[nodiscard]] bool hasButtonBars() const { return scanner_.hasButtonBars(); }

private:
    static constexpr const char* TAG = "HardwareInitializer";
    static constexpr int MAX_CONNECTORS = 4;

    HardwareScanner scanner_;
    ButtonManager buttonManager_;
    std::vector<std::shared_ptr<Adafruit_MCP23008>> mcps_;
    
    InitCompleteCallback initCallback_;
    bool initialized_;
    int tft320Bar_;  // -1 if none, connector number if found

    bool initializeI2C();
    bool initializeMCPs();
    int initializeButtons();
    bool initializeSensors();
    
    void onDeviceDetected(const I2CDevice& device);
    void onConnectorDetected(int connector, ConnectorType type);
    
    std::shared_ptr<Adafruit_MCP23008> getMCPForConnector(int connector);
};

// Global instance for easy access
extern std::unique_ptr<HardwareInitializer> g_hardwareInitializer;

// Convenience functions for main.cpp
bool initializeAllHardware();
HardwareInitializer& getHardwareInitializer();

} // namespace ButtonPlus